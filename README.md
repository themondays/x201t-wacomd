# x201t-wacomd

FreeBSD userspace Wacom ISDv4 bridge for the Lenovo ThinkPad X201 Tablet.

The X201 Tablet's internal pen digitizer is not a USB Wacom tablet. It is a
serial ISDv4 TabletPC device, commonly exposed as `/dev/cuau2` at 19200 baud.
`x201t-wacomd` reads that serial stream and exposes the pen through:

- `/dev/uinput` for Xorg / `xf86-input-wacom`.
- `/dev/consolectl` for FreeBSD `vt(4)` console pointer support.

This is X201 Tablet-focused software. Other serial ISDv4 TabletPC devices may
need different defaults or packet handling.

## Features

- Queries the serial ISDv4 tablet for coordinate and pressure ranges.
- Emits an evdev tablet through `/dev/uinput`.
- Supports stylus tip, pressure, proximity, and side button.
- Optional tty console pointer backend through `/dev/consolectl`.
- Console absolute mode maps the tablet area to a configured framebuffer size.
- Console-only mode can run without Xorg/uinput.
- FreeBSD rc.d service.
- Local FreeBSD port under `ports/x11-drivers/x201t-wacomd`.

## Build

On FreeBSD:

```sh
make
sudo make install
```

Manual test:

```sh
sudo ./x201t-wacomd -f -v -d /dev/cuau2 -b 19200
```

Console-only test:

```sh
sudo ./x201t-wacomd -f -v -U -C -M absolute -W 1280 -H 800
```

## FreeBSD Port

Build the included port inside a ports tree:

```sh
sudo cp -R ports/x11-drivers/x201t-wacomd /usr/ports/x11-drivers/
cd /usr/ports/x11-drivers/x201t-wacomd
sudo make clean stage check-plist package
```

Install without Xorg dependency:

```sh
sudo make OPTIONS_UNSET=XORG install clean
```

Enable on an X201 Tablet:

```sh
sudo sysrc x201t_wacomd_enable=YES
sudo sysrc x201t_wacomd_device=/dev/cuau2
sudo sysrc x201t_wacomd_baud=19200
sudo service x201t_wacomd start
```

Enable tty console pointer support:

```sh
sudo sysrc x201t_wacomd_console_enable=YES
sudo sysrc x201t_wacomd_console_mode=absolute
sudo sysrc x201t_wacomd_console_width=1280
sudo sysrc x201t_wacomd_console_height=800
sudo service x201t_wacomd restart
```

Console-only setup:

```sh
sudo sysrc x201t_wacomd_uinput_enable=NO
sudo sysrc x201t_wacomd_console_enable=YES
sudo service x201t_wacomd restart
```

## Notes

FreeBSD runs every executable script in `/usr/local/etc/rc.d`. Do not leave
executable backup scripts in that directory; old service copies can start at
boot and hold the serial tablet device open.

Service status checks should be run as root because the pidfile is root-owned:

```sh
sudo service x201t_wacomd status
```

## License

BSD-2-Clause. See [LICENSE](LICENSE).
