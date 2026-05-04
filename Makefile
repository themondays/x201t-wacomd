CC?=	cc
CFLAGS?=	-O2
PREFIX?=	/usr/local

all: x201t-wacomd

x201t-wacomd: src/x201t-wacomd.c
	$(CC) $(CFLAGS) -Wall -Wextra -Wpedantic -o $@ src/x201t-wacomd.c

install: x201t-wacomd
	install -d $(DESTDIR)$(PREFIX)/sbin
	install -m 555 x201t-wacomd $(DESTDIR)$(PREFIX)/sbin/x201t-wacomd
	install -d $(DESTDIR)$(PREFIX)/etc/rc.d
	install -m 555 rc.d/x201t_wacomd $(DESTDIR)$(PREFIX)/etc/rc.d/x201t_wacomd

clean:
	rm -f x201t-wacomd

.PHONY: all install clean
