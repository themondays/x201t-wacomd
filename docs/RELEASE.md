# Release Checklist

Recommended repository:

```text
themondays/x201t-wacomd
```

Recommended description:

```text
FreeBSD userspace Wacom ISDv4 bridge for ThinkPad X201 Tablet pen input
```

Recommended first tag:

```text
v0.1.0
```

## Before Publishing

1. Confirm source and port source match:
   `diff -u src/x201t-wacomd.c ports/x11-drivers/x201t-wacomd/files/x201t-wacomd.c`.
2. Confirm rc.d scripts parse with `sh -n`.
3. Build on FreeBSD.
4. Build the FreeBSD port with default options and with `OPTIONS_UNSET=XORG`.
5. Confirm no packages, work directories, local configs, or machine state files
   are tracked.

## Validation

```sh
make clean all
./x201t-wacomd -h
sh -n rc.d/x201t_wacomd
sh -n ports/x11-drivers/x201t-wacomd/files/x201t_wacomd.in
diff -u src/x201t-wacomd.c ports/x11-drivers/x201t-wacomd/files/x201t-wacomd.c
```

On FreeBSD:

```sh
cd ports/x11-drivers/x201t-wacomd
sudo make clean stage check-plist package
sudo make OPTIONS_UNSET=XORG clean stage check-plist package
```

## GitHub Release Notes

Title:

```text
v0.1.0: FreeBSD X201 Tablet Wacom bridge
```

Body:

```text
Initial standalone release of x201t-wacomd.

x201t-wacomd is a FreeBSD userspace bridge for the Lenovo ThinkPad X201 Tablet
internal serial Wacom ISDv4 pen digitizer. It reads the tablet serial stream
and exposes pen input through /dev/uinput for Xorg/xf86-input-wacom and,
optionally, through /dev/consolectl for FreeBSD vt(4) console pointer support.

The repository includes the daemon source, rc.d service, Xorg sample config,
testing notes, and a local FreeBSD port under ports/x11-drivers/x201t-wacomd.
```
