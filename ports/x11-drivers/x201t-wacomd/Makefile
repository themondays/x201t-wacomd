PORTNAME=	x201t-wacomd
DISTVERSION=	0.1.1
CATEGORIES=	x11-drivers
MASTER_SITES=
DISTFILES=

MAINTAINER=	jd@themondays.ca
COMMENT=	Userspace Wacom ISDv4 bridge for Lenovo ThinkPad X201 Tablet
WWW=		https://github.com/themondays/x201t-wacomd

LICENSE=	BSD2CLAUSE

USES=		uidfix
USE_RC_SUBR=	x201t_wacomd
SUB_FILES=	pkg-message

OPTIONS_DEFINE=	XORG
OPTIONS_DEFAULT=	XORG
OPTIONS_SUB=	yes

XORG_DESC=	Install Xorg Wacom driver dependency and Xorg sample config
XORG_RUN_DEPENDS=	${LOCALBASE}/lib/xorg/modules/input/wacom_drv.so:x11-drivers/xf86-input-wacom

WRKSRC=		${WRKDIR}/${PORTNAME}-${DISTVERSION}

do-extract:
	${MKDIR} ${WRKSRC}
	${CP} ${FILESDIR}/x201t-wacomd.c ${WRKSRC}/

do-build:
	${CC} ${CFLAGS} ${CPPFLAGS} -Wall -Wextra -Wpedantic \
		-o ${WRKSRC}/x201t-wacomd ${WRKSRC}/x201t-wacomd.c

do-install:
	${INSTALL_PROGRAM} ${WRKSRC}/x201t-wacomd \
		${STAGEDIR}${PREFIX}/sbin/x201t-wacomd

do-install-XORG-on:
	${MKDIR} ${STAGEDIR}${PREFIX}/etc/X11/xorg.conf.d
	${INSTALL_DATA} ${FILESDIR}/70-x201t-uinput-wacom.conf.sample \
		${STAGEDIR}${PREFIX}/etc/X11/xorg.conf.d/

.include <bsd.port.mk>
