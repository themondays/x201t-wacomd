# Testing

Use these checks on a FreeBSD ThinkPad X201 Tablet.

## Probe

Confirm the serial tablet responds:

```sh
sudo timeout 8 /usr/local/bin/isdv4-serial-inputattach \
  --verbose --baudrate 19200 /dev/cuau2
```

Expected pen values on the known X201 Tablet class:

```text
TABLET: version: 992
TABLET: x max: 26312 y max 16520
TABLET: pressure max: 255
```

Finger touch may not be reported on every X201 Tablet panel.

## Build

```sh
make clean all
./x201t-wacomd -h
```

## Foreground Desktop Backend

```sh
sudo ./x201t-wacomd -f -v -d /dev/cuau2 -b 19200
```

In another terminal:

```sh
xinput list
xsetwacom --list devices
```

## Foreground Console Backend

```sh
sudo vidcontrol -m on < /dev/ttyv0
sudo ./x201t-wacomd -f -v -U -C -M absolute -W 1280 -H 800
```

The pen should move the FreeBSD tty mouse pointer. Tip press maps to left
button; side switch maps to right button.

## Port

```sh
cd ports/x11-drivers/x201t-wacomd
sudo make clean stage check-plist package
sudo make OPTIONS_UNSET=XORG clean stage check-plist package
```

## rc.d

```sh
sudo sh -n /usr/local/etc/rc.d/x201t_wacomd
sudo service x201t_wacomd onestatus
```

Do not leave executable backup files in `/usr/local/etc/rc.d`.
