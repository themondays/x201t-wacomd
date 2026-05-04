#!/bin/sh
set -eu

SERIAL_DEV="${SERIAL_DEV:-/dev/cuau2}"
BAUD_RATE="${BAUD_RATE:-19200}"
TARGET_USER="${TARGET_USER:-${SUDO_USER:-}}"
CONFIG_SRC="$(dirname "$0")/../examples/xorg.conf.d/60-x201t-wacom-direct-serial.conf"
CONFIG_DIR="/usr/local/etc/X11/xorg.conf.d"
CONFIG_DST="${CONFIG_DIR}/60-x201t-wacom.conf"

APPLY=0
PROBE=0
INSTALL_DIRECT_XORG=0

usage() {
        cat <<EOF
Usage:
  $0 --probe [--device /dev/cuau2] [--baudrate 19200]
  $0 --apply --user USER [--device /dev/cuau2] [--baudrate 19200]
  $0 --install-direct-xorg --user USER [--device /dev/cuau2] [--baudrate 19200]

Environment overrides:
  SERIAL_DEV=/dev/cuau2
  BAUD_RATE=19200
  TARGET_USER=your-login

This installs prerequisites for ThinkPad X201 Tablet Wacom investigation.
Direct Xorg serial config is experimental and failed on the checked machine.
The script does not restart SDDM or reboot.
EOF
}

while [ "$#" -gt 0 ]; do
        case "$1" in
                --apply)
                        APPLY=1
                        ;;
                --install-direct-xorg)
                        APPLY=1
                        INSTALL_DIRECT_XORG=1
                        ;;
                --probe)
                        PROBE=1
                        ;;
                --user)
                        if [ "$#" -lt 2 ]; then
                                echo "--user requires a value" >&2
                                exit 2
                        fi
                        TARGET_USER="$2"
                        shift
                        ;;
                --device)
                        if [ "$#" -lt 2 ]; then
                                echo "--device requires a value" >&2
                                exit 2
                        fi
                        SERIAL_DEV="$2"
                        shift
                        ;;
                --baudrate)
                        if [ "$#" -lt 2 ]; then
                                echo "--baudrate requires a value" >&2
                                exit 2
                        fi
                        BAUD_RATE="$2"
                        shift
                        ;;
                -h|--help)
                        usage
                        exit 0
                        ;;
                *)
                        echo "Unknown argument: $1" >&2
                        usage >&2
                        exit 2
                        ;;
        esac
        shift
done

need_root() {
        if [ "$(id -u)" -ne 0 ]; then
                echo "Run as root, for example: sudo $0 --apply --user ${TARGET_USER}" >&2
                exit 1
        fi
}

check_freebsd() {
        if [ "$(uname -s)" != "FreeBSD" ]; then
                echo "This script is intended for FreeBSD." >&2
                exit 1
        fi
}

probe_tablet() {
        if [ ! -c "$SERIAL_DEV" ]; then
                echo "Serial device not found: ${SERIAL_DEV}" >&2
                return 1
        fi

        if ! command -v /usr/local/bin/isdv4-serial-inputattach >/dev/null 2>&1; then
                echo "Missing /usr/local/bin/isdv4-serial-inputattach; install xf86-input-wacom first." >&2
                return 1
        fi

        echo "Probing ${SERIAL_DEV} at ${BAUD_RATE} baud..."
        echo "If TABLET geometry appears, the serial digitizer answered."
        echo "A later FreeBSD line-discipline error is expected for this probe."
        timeout 8 /usr/local/bin/isdv4-serial-inputattach --verbose --baudrate "$BAUD_RATE" "$SERIAL_DEV" || true
}

install_packages() {
        pkg install -y xorg xinput xf86-input-wacom libwacom
}

install_config() {
        if [ ! -f "$CONFIG_SRC" ]; then
                echo "Missing config template: ${CONFIG_SRC}" >&2
                exit 1
        fi

        install -d -m 0755 "$CONFIG_DIR"

        if [ -f "$CONFIG_DST" ]; then
                backup="${CONFIG_DST}.bak.$(date +%Y%m%d-%H%M%S)"
                cp -p "$CONFIG_DST" "$backup"
                echo "Backed up existing config to ${backup}"
        fi

        sed \
                -e "s#/dev/cuau2#${SERIAL_DEV}#g" \
                -e "s#Option \"BaudRate\" \"19200\"#Option \"BaudRate\" \"${BAUD_RATE}\"#g" \
                "$CONFIG_SRC" > "${CONFIG_DST}.tmp"
        install -m 0644 "${CONFIG_DST}.tmp" "$CONFIG_DST"
        rm -f "${CONFIG_DST}.tmp"
        echo "Installed ${CONFIG_DST}"
}

allow_serial_access() {
        if [ -z "$TARGET_USER" ]; then
                echo "Set --user USER or TARGET_USER before changing group membership." >&2
                exit 2
        fi
        if getent group dialer >/dev/null 2>&1; then
                pw groupmod dialer -m "$TARGET_USER"
                echo "Added ${TARGET_USER} to dialer group."
        else
                echo "Group dialer does not exist; serial permissions need manual review." >&2
        fi
}

check_freebsd

if [ "$PROBE" -eq 1 ]; then
        probe_tablet
fi

if [ "$APPLY" -eq 1 ]; then
        need_root
        install_packages
        allow_serial_access
        if [ "$INSTALL_DIRECT_XORG" -eq 1 ]; then
                install_config
                echo "Installed experimental direct Xorg config."
        else
                echo "Skipped direct Xorg config; it failed on the checked machine."
                echo "Use --install-direct-xorg only for controlled retesting."
        fi
        echo
        echo "Next steps:"
        echo "  1. Log out and back in, or reboot, so group membership is refreshed."
        echo "  2. Build/install an ISDv4-to-uinput bridge before expecting pen input."
        echo "  3. Verify in X11 after that: xinput list; xsetwacom --list devices"
elif [ "$PROBE" -eq 0 ]; then
        usage
fi
