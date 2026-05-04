#!/bin/sh
set -eu

SRC_DIR="$(dirname "$0")/../src"
RC_SRC="$(dirname "$0")/../rc.d/x201t_wacomd"
XORG_SRC="$(dirname "$0")/../examples/xorg.conf.d/70-x201t-uinput-wacom.conf"

if [ "$(uname -s)" != "FreeBSD" ]; then
        echo "This installer is intended for FreeBSD." >&2
        exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
        echo "Run as root: sudo $0" >&2
        exit 1
fi

make -C "$SRC_DIR" clean all
install -m 0755 "$SRC_DIR/x201t-wacomd" /usr/local/sbin/x201t-wacomd
install -m 0755 "$RC_SRC" /usr/local/etc/rc.d/x201t_wacomd
install -d -m 0755 /usr/local/etc/X11/xorg.conf.d
install -m 0644 "$XORG_SRC" /usr/local/etc/X11/xorg.conf.d/70-x201t-uinput-wacom.conf
sysrc x201t_wacomd_enable=YES
sysrc x201t_wacomd_device=/dev/cuau2
sysrc x201t_wacomd_baud=19200

echo "Installed x201t-wacomd."
echo "Start now with: service x201t_wacomd start"
echo "Restart SDDM after the uinput device appears: service sddm restart"
