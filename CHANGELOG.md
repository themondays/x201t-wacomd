# Changelog

## v0.1.1 - 2026-05-05

Added:

- Tty console eraser-scroll mode through `/dev/consolectl` mouse Z-axis
  events.
- rc.conf knobs for console eraser mode and scroll sensitivity.

## v0.1.0 - 2026-05-05

Initial standalone GitHub release.

Included:

- FreeBSD userspace ISDv4 serial tablet bridge.
- `/dev/uinput` backend for Xorg / `xf86-input-wacom`.
- Optional `/dev/consolectl` backend for FreeBSD `vt(4)` pointer support.
- Absolute and relative console modes.
- rc.d service.
- Xorg InputClass sample.
- Local FreeBSD port under `ports/x11-drivers/x201t-wacomd`.
