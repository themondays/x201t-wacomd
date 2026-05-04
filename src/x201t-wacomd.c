/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * x201t-wacomd - FreeBSD userspace bridge for ThinkPad X201 Tablet ISDv4 pen
 *
 * This daemon reads the internal serial Wacom ISDv4 digitizer and exposes it
 * through /dev/uinput and, optionally, the FreeBSD tty console mouse layer.
 */

#include <sys/consio.h>
#include <sys/ioctl.h>
#include <sys/mouse.h>
#include <sys/time.h>
#include <sys/types.h>

#include <dev/evdev/input.h>
#include <dev/evdev/uinput.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define DEFAULT_SERIAL_DEV "/dev/cuau2"
#define DEFAULT_UINPUT_DEV "/dev/uinput"
#define DEFAULT_CONSOLECTL_DEV "/dev/consolectl"
#define DEFAULT_BAUD 19200
#define DEFAULT_CONSOLE_SCALE 32
#define DEFAULT_CONSOLE_WIDTH 1280
#define DEFAULT_CONSOLE_HEIGHT 800
#define CONSOLE_MAX_DELTA 255

#define ISDV4_QUERY "*"
#define ISDV4_TOUCH_QUERY "%"
#define ISDV4_STOP "0"
#define ISDV4_SAMPLING "1"

#define ISDV4_PKGLEN_TPCPEN 9
#define ISDV4_PKGLEN_TPCCTL 11

#define HEADER_BIT 0x80
#define CONTROL_BIT 0x40
#define DATA_ID_MASK 0x3f
#define TOUCH_CONTROL_BIT 0x10

#define DEFAULT_X_MAX 26312
#define DEFAULT_Y_MAX 16520
#define DEFAULT_PRESSURE_MAX 255

struct tablet_ranges {
	int x_max;
	int y_max;
	int pressure_max;
	int version;
};

struct pen_packet {
	int proximity;
	int tip;
	int side;
	int eraser;
	int x;
	int y;
	int pressure;
};

enum console_mode {
	CONSOLE_MODE_ABSOLUTE,
	CONSOLE_MODE_RELATIVE
};

struct console_backend {
	int fd;
	int enabled;
	int extioctl;
	enum console_mode mode;
	int scale;
	int width;
	int height;
	int x_max;
	int y_max;
	int have_last;
	int last_x;
	int last_y;
	int rem_x;
	int rem_y;
	int last_buttons;
	int pointer_x;
	int pointer_y;
};

static volatile sig_atomic_t g_stop;
static int g_verbose;

static void
on_signal(int signo)
{
	(void)signo;
	g_stop = 1;
}

static void
usage(const char *argv0)
{
	fprintf(stderr,
	    "Usage: %s [-f] [-v] [-U] [-C] [-d serial_device] [-u uinput_device]\n"
	    "          [-c consolectl_device] [-b baud] [-M console_mode]\n"
	    "          [-S console_scale] [-W console_width] [-H console_height]\n"
	    "\n"
	    "Defaults:\n"
	    "  serial_device: %s\n"
	    "  uinput_device: %s\n"
	    "  consolectl:    %s\n"
	    "  baud:          %d\n"
	    "  console_mode:  absolute\n"
	    "  console_scale: %d\n"
	    "  console_size:  %dx%d\n"
	    "\n"
	    "Options:\n"
	    "  -C  also emit FreeBSD tty console mouse events through consolectl\n"
	    "  -M  console mode: absolute or relative\n"
	    "  -U  disable the uinput backend; useful for console-only installs\n",
	    argv0, DEFAULT_SERIAL_DEV, DEFAULT_UINPUT_DEV,
	    DEFAULT_CONSOLECTL_DEV, DEFAULT_BAUD, DEFAULT_CONSOLE_SCALE,
	    DEFAULT_CONSOLE_WIDTH, DEFAULT_CONSOLE_HEIGHT);
}

static int
set_serial_attr(int fd, int baud)
{
	struct termios t;
	speed_t speed;

	if (tcgetattr(fd, &t) == -1)
		memset(&t, 0, sizeof(t));

	t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
	    ICRNL | IXON);
	t.c_iflag |= IXOFF;
	t.c_oflag &= ~OPOST;
	t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	t.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
	t.c_cflag |= CS8 | CLOCAL | CREAD;
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 10;

	switch (baud) {
	case 19200:
		speed = B19200;
		break;
	case 38400:
		speed = B38400;
		break;
	default:
		fprintf(stderr, "unsupported baud rate: %d\n", baud);
		return -1;
	}

	cfsetispeed(&t, speed);
	cfsetospeed(&t, speed);

	if (tcsetattr(fd, TCSANOW, &t) == -1) {
		perror("tcsetattr");
		return -1;
	}

	return 0;
}

static int
write_all(int fd, const char *data)
{
	size_t done = 0;
	size_t len = strlen(data);

	while (done < len) {
		ssize_t n = write(fd, data + done, len - done);
		if (n == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			perror("write serial");
			return -1;
		}
		done += (size_t)n;
	}

	return 0;
}

static void
flush_serial(int fd)
{
	unsigned char buf[64];
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags == -1)
		return;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		return;
	while (read(fd, buf, sizeof(buf)) > 0)
		;
	(void)fcntl(fd, F_SETFL, flags);
}

static int
stop_tablet(int fd)
{
	if (g_verbose)
		fprintf(stderr, "serial: stop sampling\n");
	if (write_all(fd, ISDV4_STOP) == -1)
		return -1;
	usleep(250000);
	flush_serial(fd);
	return 0;
}

static int
start_tablet(int fd)
{
	if (g_verbose)
		fprintf(stderr, "serial: start sampling\n");
	return write_all(fd, ISDV4_SAMPLING);
}

static int
read_exact_packet(int fd, unsigned char *buf, int len, int timeout_ms)
{
	int got = 0;
	int attempts = 10;

	while (got < len && attempts-- > 0) {
		struct pollfd pfd;
		ssize_t n;

		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		if (poll(&pfd, 1, timeout_ms) <= 0)
			return -1;

		n = read(fd, buf + got, (size_t)(len - got));
		if (n == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			perror("read serial");
			return -1;
		}
		if (n == 0)
			continue;
		got += (int)n;
	}

	if (got != len)
		return -1;

	if (!(buf[0] & HEADER_BIT)) {
		int skip = 0;
		while (skip < len && !(buf[skip] & HEADER_BIT))
			skip++;
		if (skip == len)
			return -1;
		memmove(buf, buf + skip, (size_t)(len - skip));
		return read_exact_packet(fd, buf + (len - skip), skip, timeout_ms);
	}

	return 0;
}

static int
parse_query(const unsigned char *b, struct tablet_ranges *ranges)
{
	if (!(b[0] & HEADER_BIT) || !(b[0] & CONTROL_BIT))
		return -1;

	ranges->x_max = (b[1] << 9) | (b[2] << 2) | ((b[6] >> 5) & 0x3);
	ranges->y_max = (b[3] << 9) | (b[4] << 2) | ((b[6] >> 3) & 0x3);
	ranges->pressure_max = ((b[6] & 0x7) << 7) | b[5];
	ranges->version = (b[9] << 7) | b[10];
	return 0;
}

static int
query_tablet(int fd, struct tablet_ranges *ranges)
{
	unsigned char buf[ISDV4_PKGLEN_TPCCTL];

	memset(ranges, 0, sizeof(*ranges));

	if (stop_tablet(fd) == -1)
		return -1;
	if (write_all(fd, ISDV4_QUERY) == -1)
		return -1;
	if (read_exact_packet(fd, buf, ISDV4_PKGLEN_TPCCTL, 1000) == -1)
		return -1;
	if (parse_query(buf, ranges) == -1)
		return -1;

	if (g_verbose) {
		fprintf(stderr, "tablet: version=%d x_max=%d y_max=%d pressure_max=%d\n",
		    ranges->version, ranges->x_max, ranges->y_max,
		    ranges->pressure_max);
	}

	/* Touch query is intentionally skipped for this pen-only bridge. */
	return 0;
}

static int
parse_pen(const unsigned char *b, struct pen_packet *pkt)
{
	if (!(b[0] & HEADER_BIT) || (b[0] & TOUCH_CONTROL_BIT))
		return -1;

	pkt->proximity = (b[0] >> 5) & 0x1;
	pkt->tip = b[0] & 0x1;
	pkt->side = (b[0] >> 1) & 0x1;
	pkt->eraser = (b[0] >> 2) & 0x1;
	pkt->x = (b[1] << 9) | (b[2] << 2) | ((b[6] >> 5) & 0x3);
	pkt->y = (b[3] << 9) | (b[4] << 2) | ((b[6] >> 3) & 0x3);
	pkt->pressure = ((b[6] & 0x7) << 7) | b[5];
	return 0;
}

static int
uinput_ioctl(int fd, unsigned long req, int value, const char *name)
{
	if (ioctl(fd, req, value) == -1) {
		perror(name);
		return -1;
	}
	return 0;
}

static int
setup_abs(int fd, uint16_t code, int min, int max, int res)
{
	struct uinput_abs_setup abs_setup;

	memset(&abs_setup, 0, sizeof(abs_setup));
	abs_setup.code = code;
	abs_setup.absinfo.minimum = min;
	abs_setup.absinfo.maximum = max;
	abs_setup.absinfo.resolution = res;

	if (ioctl(fd, UI_ABS_SETUP, &abs_setup) == -1) {
		perror("UI_ABS_SETUP");
		return -1;
	}

	return 0;
}

static int
create_uinput_device(const char *path, const struct tablet_ranges *ranges)
{
	struct uinput_setup setup;
	int fd;

	fd = open(path, O_WRONLY | O_NONBLOCK);
	if (fd == -1) {
		perror(path);
		return -1;
	}

	if (uinput_ioctl(fd, UI_SET_EVBIT, EV_KEY, "UI_SET_EVBIT EV_KEY") == -1 ||
	    uinput_ioctl(fd, UI_SET_EVBIT, EV_ABS, "UI_SET_EVBIT EV_ABS") == -1 ||
	    uinput_ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_PEN, "UI_SET_KEYBIT BTN_TOOL_PEN") == -1 ||
	    uinput_ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_RUBBER, "UI_SET_KEYBIT BTN_TOOL_RUBBER") == -1 ||
	    uinput_ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH, "UI_SET_KEYBIT BTN_TOUCH") == -1 ||
	    uinput_ioctl(fd, UI_SET_KEYBIT, BTN_STYLUS, "UI_SET_KEYBIT BTN_STYLUS") == -1 ||
	    uinput_ioctl(fd, UI_SET_ABSBIT, ABS_X, "UI_SET_ABSBIT ABS_X") == -1 ||
	    uinput_ioctl(fd, UI_SET_ABSBIT, ABS_Y, "UI_SET_ABSBIT ABS_Y") == -1 ||
	    uinput_ioctl(fd, UI_SET_ABSBIT, ABS_PRESSURE, "UI_SET_ABSBIT ABS_PRESSURE") == -1 ||
	    uinput_ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT, "UI_SET_PROPBIT INPUT_PROP_DIRECT") == -1) {
		close(fd);
		return -1;
	}

	if (setup_abs(fd, ABS_X, 0, ranges->x_max, 100000) == -1 ||
	    setup_abs(fd, ABS_Y, 0, ranges->y_max, 100000) == -1 ||
	    setup_abs(fd, ABS_PRESSURE, 0, ranges->pressure_max, 0) == -1) {
		close(fd);
		return -1;
	}

	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_RS232;
	setup.id.vendor = 0x056a;
	setup.id.product = 0x0090;
	setup.id.version = (uint16_t)ranges->version;
	strlcpy(setup.name, "X201 Tablet ISDv4 Pen", sizeof(setup.name));

	if (ioctl(fd, UI_DEV_SETUP, &setup) == -1) {
		perror("UI_DEV_SETUP");
		close(fd);
		return -1;
	}
	if (ioctl(fd, UI_DEV_CREATE) == -1) {
		perror("UI_DEV_CREATE");
		close(fd);
		return -1;
	}

	sleep(1);
	return fd;
}

static int
emit_event(int fd, uint16_t type, uint16_t code, int32_t value)
{
	struct input_event ev;

	memset(&ev, 0, sizeof(ev));
	gettimeofday(&ev.time, NULL);
	ev.type = type;
	ev.code = code;
	ev.value = value;

	if (write(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
		perror("write uinput");
		return -1;
	}

	return 0;
}

static int
emit_pen(int fd, const struct pen_packet *pkt)
{
	int touch = pkt->proximity && (pkt->tip || pkt->eraser || pkt->pressure > 0);

	if (emit_event(fd, EV_KEY, BTN_TOOL_RUBBER, pkt->proximity && pkt->eraser) == -1 ||
	    emit_event(fd, EV_KEY, BTN_TOOL_PEN, pkt->proximity && !pkt->eraser) == -1 ||
	    emit_event(fd, EV_KEY, BTN_TOUCH, touch) == -1 ||
	    emit_event(fd, EV_KEY, BTN_STYLUS, pkt->proximity && pkt->side) == -1 ||
	    emit_event(fd, EV_ABS, ABS_X, pkt->x) == -1 ||
	    emit_event(fd, EV_ABS, ABS_Y, pkt->y) == -1 ||
	    emit_event(fd, EV_ABS, ABS_PRESSURE, touch ? pkt->pressure : 0) == -1 ||
	    emit_event(fd, EV_SYN, SYN_REPORT, 0) == -1)
		return -1;

	if (g_verbose) {
		fprintf(stderr, "pen: prox=%d tip=%d side=%d eraser=%d x=%d y=%d p=%d\n",
		    pkt->proximity, pkt->tip, pkt->side, pkt->eraser,
		    pkt->x, pkt->y, pkt->pressure);
	}

	return 0;
}

static const char *
console_mode_name(enum console_mode mode)
{
	return mode == CONSOLE_MODE_ABSOLUTE ? "absolute" : "relative";
}

static int
parse_console_mode(const char *value, enum console_mode *mode)
{
	if (strcmp(value, "absolute") == 0 || strcmp(value, "abs") == 0) {
		*mode = CONSOLE_MODE_ABSOLUTE;
		return 0;
	}
	if (strcmp(value, "relative") == 0 || strcmp(value, "rel") == 0) {
		*mode = CONSOLE_MODE_RELATIVE;
		return 0;
	}

	return -1;
}

static int
open_console_backend(const char *path, int scale, enum console_mode mode,
    int width, int height, const struct tablet_ranges *ranges,
    struct console_backend *console)
{
	mouse_info_t mouse;

	memset(console, 0, sizeof(*console));
	console->fd = -1;
	console->mode = mode;
	console->scale = scale > 0 ? scale : DEFAULT_CONSOLE_SCALE;
	console->width = width > 1 ? width : DEFAULT_CONSOLE_WIDTH;
	console->height = height > 1 ? height : DEFAULT_CONSOLE_HEIGHT;
	console->x_max = ranges->x_max > 0 ? ranges->x_max : DEFAULT_X_MAX;
	console->y_max = ranges->y_max > 0 ? ranges->y_max : DEFAULT_Y_MAX;
	console->pointer_x = console->width / 2;
	console->pointer_y = console->height / 2;

	console->fd = open(path, O_RDWR);
	if (console->fd == -1) {
		perror(path);
		return -1;
	}

	memset(&mouse, 0, sizeof(mouse));
	mouse.operation = MOUSE_MOTION_EVENT;
	console->extioctl = ioctl(console->fd, CONS_MOUSECTL, &mouse) == 0;
	console->enabled = 1;

	if (g_verbose) {
		fprintf(stderr,
		    "console: device=%s mode=%s ioctl=%s scale=%d size=%dx%d\n",
		    path, console_mode_name(console->mode),
		    console->extioctl ? "motion/button" : "action",
		    console->scale, console->width, console->height);
	}

	return 0;
}

static int
scaled_delta(int delta, int *remainder, int scale)
{
	long total;
	int out;

	total = (long)delta + *remainder;
	out = (int)(total / scale);
	*remainder = (int)(total - ((long)out * scale));
	return out;
}

static int
console_buttons(const struct pen_packet *pkt)
{
	int buttons = 0;

	if (!pkt->proximity)
		return 0;
	if (pkt->tip || pkt->pressure > 0)
		buttons |= MOUSE_BUTTON1DOWN;
	if (pkt->side)
		buttons |= MOUSE_BUTTON3DOWN;

	return buttons;
}

static int
clamp_int(int value, int min, int max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

static int
limited_delta(int value)
{
	return clamp_int(value, -CONSOLE_MAX_DELTA, CONSOLE_MAX_DELTA);
}

static int
send_console_motion(struct console_backend *console, int dx, int dy, int buttons)
{
	while (dx != 0 || dy != 0) {
		mouse_info_t mouse;
		int sx = limited_delta(dx);
		int sy = limited_delta(dy);

		memset(&mouse, 0, sizeof(mouse));
		mouse.operation = console->extioctl ?
		    MOUSE_MOTION_EVENT : MOUSE_ACTION;
		mouse.u.data.x = sx;
		mouse.u.data.y = sy;
		mouse.u.data.z = 0;
		mouse.u.data.buttons = buttons;
		if (ioctl(console->fd, CONS_MOUSECTL, &mouse) == -1) {
			perror(console->extioctl ?
			    "CONS_MOUSECTL MOUSE_MOTION_EVENT" :
			    "CONS_MOUSECTL MOUSE_ACTION");
			return -1;
		}

		console->pointer_x = clamp_int(console->pointer_x + sx, 0,
		    console->width - 1);
		console->pointer_y = clamp_int(console->pointer_y + sy, 0,
		    console->height - 1);
		dx -= sx;
		dy -= sy;
	}

	return 0;
}

static void
reset_console_pointer(struct console_backend *console)
{
	static const char tty_suffix[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < sizeof(tty_suffix) - 1; i++) {
		char path[sizeof("/dev/ttyvx")];
		mouse_info_t mouse;
		int fd;

		snprintf(path, sizeof(path), "/dev/ttyv%c", tty_suffix[i]);
		fd = open(path, O_RDWR | O_NONBLOCK);
		if (fd == -1)
			continue;

		memset(&mouse, 0, sizeof(mouse));
		mouse.operation = MOUSE_HIDE;
		(void)ioctl(fd, CONS_MOUSECTL, &mouse);
		memset(&mouse, 0, sizeof(mouse));
		mouse.operation = MOUSE_SHOW;
		(void)ioctl(fd, CONS_MOUSECTL, &mouse);
		close(fd);
	}

	console->pointer_x = console->width / 2;
	console->pointer_y = console->height / 2;
}

static int
send_console_button_events(struct console_backend *console, int buttons)
{
	int changed;
	int button;

	if (!console->extioctl) {
		mouse_info_t mouse;

		memset(&mouse, 0, sizeof(mouse));
		mouse.operation = MOUSE_ACTION;
		mouse.u.data.x = 0;
		mouse.u.data.y = 0;
		mouse.u.data.z = 0;
		mouse.u.data.buttons = buttons;
		if (ioctl(console->fd, CONS_MOUSECTL, &mouse) == -1) {
			perror("CONS_MOUSECTL MOUSE_ACTION");
			return -1;
		}
		return 0;
	}

	changed = console->last_buttons ^ buttons;
	button = MOUSE_BUTTON1DOWN;
	while (changed != 0) {
		mouse_info_t mouse;

		if ((changed & button) == 0) {
			button <<= 1;
			continue;
		}

		memset(&mouse, 0, sizeof(mouse));
		mouse.operation = MOUSE_BUTTON_EVENT;
		mouse.u.event.id = button;
		mouse.u.event.value = (buttons & button) ? 1 : 0;
		if (ioctl(console->fd, CONS_MOUSECTL, &mouse) == -1) {
			perror("CONS_MOUSECTL MOUSE_BUTTON_EVENT");
			return -1;
		}

		changed &= ~button;
		button <<= 1;
	}

	return 0;
}

static int
console_absolute_target(struct console_backend *console,
    const struct pen_packet *pkt, int *target_x, int *target_y)
{
	*target_x = (int)(((long)pkt->x * (console->width - 1)) /
	    console->x_max);
	*target_y = (int)(((long)pkt->y * (console->height - 1)) /
	    console->y_max);
	*target_x = clamp_int(*target_x, 0, console->width - 1);
	*target_y = clamp_int(*target_y, 0, console->height - 1);
	return 0;
}

static int
emit_console_absolute_pen(struct console_backend *console,
    const struct pen_packet *pkt, int buttons)
{
	int target_x;
	int target_y;
	int dx = 0;
	int dy = 0;

	if (pkt->proximity) {
		if (!console->have_last)
			reset_console_pointer(console);
		(void)console_absolute_target(console, pkt, &target_x, &target_y);
		dx = target_x - console->pointer_x;
		dy = target_y - console->pointer_y;
		if (send_console_motion(console, dx, dy,
		    console->last_buttons) == -1)
			return -1;
		console->have_last = 1;
		console->last_x = pkt->x;
		console->last_y = pkt->y;
	} else {
		console->have_last = 0;
	}

	if (buttons != console->last_buttons &&
	    send_console_button_events(console, buttons) == -1)
		return -1;

	if (g_verbose) {
		fprintf(stderr,
		    "console: mode=absolute target=%d,%d pos=%d,%d buttons=0x%x\n",
		    pkt->proximity ? target_x : -1, pkt->proximity ? target_y : -1,
		    console->pointer_x, console->pointer_y, buttons);
	}

	console->last_buttons = buttons;
	return 0;
}

static int
emit_console_relative_pen(struct console_backend *console,
    const struct pen_packet *pkt, int buttons)
{
	int dx = 0;
	int dy = 0;

	if (!pkt->proximity) {
		console->have_last = 0;
		console->rem_x = 0;
		console->rem_y = 0;
	} else if (!console->have_last) {
		console->last_x = pkt->x;
		console->last_y = pkt->y;
		console->have_last = 1;
	} else {
		dx = scaled_delta(pkt->x - console->last_x, &console->rem_x,
		    console->scale);
		dy = scaled_delta(pkt->y - console->last_y, &console->rem_y,
		    console->scale);
		console->last_x = pkt->x;
		console->last_y = pkt->y;
	}

	if ((dx != 0 || dy != 0) &&
	    send_console_motion(console, dx, dy, console->last_buttons) == -1)
		return -1;
	if (buttons != console->last_buttons &&
	    send_console_button_events(console, buttons) == -1)
		return -1;

	console->last_buttons = buttons;

	if (g_verbose) {
		fprintf(stderr, "console: mode=relative dx=%d dy=%d buttons=0x%x\n",
		    dx, dy, buttons);
	}

	return 0;
}

static int
emit_console_pen(struct console_backend *console, const struct pen_packet *pkt)
{
	int buttons;

	if (!console->enabled)
		return 0;

	buttons = console_buttons(pkt);

	if (console->mode == CONSOLE_MODE_ABSOLUTE)
		return emit_console_absolute_pen(console, pkt, buttons);
	return emit_console_relative_pen(console, pkt, buttons);
}

static int
event_loop(int serial_fd, int uinput_fd, struct console_backend *console)
{
	unsigned char buf[256];
	int len = 0;

	while (!g_stop) {
		struct pollfd pfd;
		ssize_t n;

		pfd.fd = serial_fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		if (poll(&pfd, 1, 500) <= 0)
			continue;

		n = read(serial_fd, buf + len, sizeof(buf) - (size_t)len);
		if (n == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			perror("read serial");
			return -1;
		}
		if (n <= 0)
			continue;

		len += (int)n;

		while (len >= ISDV4_PKGLEN_TPCPEN) {
			struct pen_packet pkt;
			int skip = 0;

			while (skip < len && !(buf[skip] & HEADER_BIT))
				skip++;
			if (skip > 0) {
				memmove(buf, buf + skip, (size_t)(len - skip));
				len -= skip;
			}
			if (len < ISDV4_PKGLEN_TPCPEN)
				break;

			if (buf[0] & CONTROL_BIT) {
				memmove(buf, buf + ISDV4_PKGLEN_TPCPEN,
				    (size_t)(len - ISDV4_PKGLEN_TPCPEN));
				len -= ISDV4_PKGLEN_TPCPEN;
				continue;
			}

			if (parse_pen(buf, &pkt) == 0) {
				if (uinput_fd != -1)
					(void)emit_pen(uinput_fd, &pkt);
				(void)emit_console_pen(console, &pkt);
			}

			memmove(buf, buf + ISDV4_PKGLEN_TPCPEN,
			    (size_t)(len - ISDV4_PKGLEN_TPCPEN));
			len -= ISDV4_PKGLEN_TPCPEN;
		}
	}

	return 0;
}

int
main(int argc, char **argv)
{
	const char *serial_dev = DEFAULT_SERIAL_DEV;
	const char *uinput_dev = DEFAULT_UINPUT_DEV;
	const char *consolectl_dev = DEFAULT_CONSOLECTL_DEV;
	struct tablet_ranges ranges;
	struct console_backend console;
	int serial_fd = -1;
	int uinput_fd = -1;
	int baud = DEFAULT_BAUD;
	enum console_mode console_mode = CONSOLE_MODE_ABSOLUTE;
	int console_scale = DEFAULT_CONSOLE_SCALE;
	int console_width = DEFAULT_CONSOLE_WIDTH;
	int console_height = DEFAULT_CONSOLE_HEIGHT;
	int console_enabled = 0;
	int uinput_enabled = 1;
	int foreground = 0;
	int ch;

	memset(&console, 0, sizeof(console));
	console.fd = -1;

	while ((ch = getopt(argc, argv, "b:c:Cd:fH:hM:S:u:UvW:")) != -1) {
		switch (ch) {
		case 'b':
			baud = atoi(optarg);
			break;
		case 'c':
			consolectl_dev = optarg;
			break;
		case 'C':
			console_enabled = 1;
			break;
		case 'd':
			serial_dev = optarg;
			break;
		case 'f':
			foreground = 1;
			break;
		case 'H':
			console_height = atoi(optarg);
			if (console_height <= 1) {
				fprintf(stderr, "invalid console height: %s\n", optarg);
				return 2;
			}
			break;
		case 'M':
			if (parse_console_mode(optarg, &console_mode) == -1) {
				fprintf(stderr, "invalid console mode: %s\n", optarg);
				return 2;
			}
			break;
		case 'S':
			console_scale = atoi(optarg);
			if (console_scale <= 0) {
				fprintf(stderr, "invalid console scale: %s\n", optarg);
				return 2;
			}
			break;
		case 'u':
			uinput_dev = optarg;
			break;
		case 'U':
			uinput_enabled = 0;
			break;
		case 'v':
			g_verbose = 1;
			break;
		case 'W':
			console_width = atoi(optarg);
			if (console_width <= 1) {
				fprintf(stderr, "invalid console width: %s\n", optarg);
				return 2;
			}
			break;
		case 'h':
		default:
			usage(argv[0]);
			return ch == 'h' ? 0 : 2;
		}
	}

	if (!uinput_enabled && !console_enabled) {
		fprintf(stderr, "at least one output backend must be enabled\n");
		usage(argv[0]);
		return 2;
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	serial_fd = open(serial_dev, O_RDWR | O_NOCTTY);
	if (serial_fd == -1) {
		perror(serial_dev);
		return 1;
	}
	if (set_serial_attr(serial_fd, baud) == -1)
		return 1;

	if (query_tablet(serial_fd, &ranges) == -1) {
		fprintf(stderr, "tablet query failed; using X201 fallback ranges\n");
		ranges.x_max = DEFAULT_X_MAX;
		ranges.y_max = DEFAULT_Y_MAX;
		ranges.pressure_max = DEFAULT_PRESSURE_MAX;
		ranges.version = 992;
	}

	if (uinput_enabled) {
		uinput_fd = create_uinput_device(uinput_dev, &ranges);
		if (uinput_fd == -1 && !console_enabled)
			return 1;
		if (uinput_fd == -1)
			fprintf(stderr, "uinput unavailable; continuing with console backend only\n");
	}

	if (console_enabled) {
		if (open_console_backend(consolectl_dev, console_scale,
		    console_mode, console_width, console_height, &ranges,
		    &console) == -1)
			return 1;
	}

	if (uinput_fd == -1 && !console.enabled) {
		fprintf(stderr, "no usable output backend\n");
		return 1;
	}

	if (!foreground && daemon(0, 0) == -1) {
		perror("daemon");
		return 1;
	}

	if (start_tablet(serial_fd) == -1)
		return 1;

	(void)event_loop(serial_fd, uinput_fd, &console);

	(void)stop_tablet(serial_fd);
	if (uinput_fd != -1) {
		(void)ioctl(uinput_fd, UI_DEV_DESTROY);
		close(uinput_fd);
	}
	if (console.fd != -1)
		close(console.fd);
	if (serial_fd != -1)
		close(serial_fd);

	return 0;
}
