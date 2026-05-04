# X201 Tablet Wacom Driver Plan

The checked FreeBSD 14.3 setup needs a small userspace bridge. Existing
packages can query the serial ISDv4 tablet, but they do not create a usable
FreeBSD input device for Xorg.

## Target

Created `x201t-wacomd`:

- Input: `/dev/cuau2` at 19200 baud.
- Protocol: Wacom ISDv4 serial TabletPC, id `0x90`, pen-only.
- Desktop output: `/dev/uinput` evdev device.
- Optional console output: FreeBSD tty mouse events through `/dev/consolectl`.

## Desktop Output

The first working version emits an evdev tablet through `/dev/uinput`:

- `EV_KEY`
  - `BTN_TOOL_PEN`
  - `BTN_TOUCH`
  - `BTN_STYLUS`
  - `BTN_STYLUS2` if side buttons report cleanly
- `EV_ABS`
  - `ABS_X`, range `0..26312`
  - `ABS_Y`, range `0..16520`
  - `ABS_PRESSURE`, range `0..255`
- `EV_SYN`
  - `SYN_REPORT`

After the uinput device exists, Xorg sees it through `xf86-input-wacom` as
stylus and eraser pointer devices. A small Xorg `InputClass` override lowers
the pressure threshold for this old 0..255 pressure range.

## Console Output

FreeBSD text console mouse support goes through the console mouse ioctl
interface on `/dev/consolectl`. `moused` uses the same interface, but it does
not understand the X201 Tablet's ISDv4 protocol directly.

`x201t-wacomd` now has a second output backend:

- `-C` enables console output.
- `-c /dev/consolectl` selects the console control device.
- `-M absolute` maps the tablet area to the console framebuffer and jumps the
  tty pointer to the pen target.
- `-M relative` keeps the old relative-mouse behavior.
- `-S 32` controls raw-tablet-to-relative-pointer scaling in relative mode.
- `-E scroll` maps eraser-side vertical movement to mouse wheel/Z-axis events.
- `-Z 768` controls eraser scroll sensitivity; lower values scroll faster.
- `-W 1280 -H 800` set the console framebuffer dimensions used by absolute
  emulation.
- `-U` disables `/dev/uinput` for console-only installs.

FreeBSD `vt` does not process `MOUSE_MOVEABS` through `/dev/consolectl`; it
only processes relative events there. Absolute mode therefore tracks the
daemon's last known tty pointer position and emits bounded relative movement to
the tablet's absolute target. The pen tip maps to left button and the side
switch maps to right button.

When eraser mode is `scroll`, eraser proximity suppresses pointer movement and
button state, then converts vertical eraser motion into `mouse_data.z` wheel
events on `/dev/consolectl`. FreeBSD's console mouse stack accepts those
events, but plain `vt(4)` scrollback behavior depends on the active consumer;
some TUI programs or sysmouse/evdev consumers can use the wheel events, while
kernel scrollback may still require keyboard-style fallback support.

To avoid drift from Xorg or another console mouse, absolute mode recenters the
tty mouse cursor with `MOUSE_HIDE`/`MOUSE_SHOW` on `/dev/ttyv0` through
`/dev/ttyvf` at each new pen proximity, then sends the relative jump from that
known center to the tablet target.

## Next Probe

The serial tablet may need a reboot or power cycle after failed direct Xorg
initialization. After that, capture live pen packets:

```sh
sudo timeout 20 /usr/local/bin/isdv4-serial-debugger --verbose --baudrate 19200 /dev/cuau2
```

Move the pen across the panel during the capture. Save the output before writing
the decoder.

## Packaging Shape

Suggested files when implementing:

- `src/x201t-wacomd.c`
- `rc.d/x201t_wacomd`
- `scripts/install-x201t-wacomd.sh`
- `examples/xorg.conf.d/70-x201t-uinput-wacom.conf`

The rc.d service should start before the display manager if the uinput device
must be present at Xorg startup.
