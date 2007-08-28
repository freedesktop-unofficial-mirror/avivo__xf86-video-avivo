#  Copyright 2005 Adam Jackson.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Process this file with autoconf to produce a configure script

AC_PREREQ(2.57)
AC_INIT([xf86-video-avivo],
        0.0.999,
        [http://home.fooishbar.org/wiki/Avivo],
        xf86-video-avivo)

AC_CONFIG_SRCDIR([Makefile.am])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_AUX_DIR(.)

AM_INIT_AUTOMAKE([dist-bzip2])

AM_MAINTAINER_MODE

# Checks for programs.
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_PROG_CC

AH_TOP([#include "xorg-server.h"])

AC_ARG_WITH(xorg-module-dir, [  --with-xorg-module-dir=DIR ],
                             [ moduledir="$withval" ],
                             [ moduledir="$libdir/xorg/modules" ])
AC_SUBST(moduledir)


# Checks for extensions
m4_pattern_forbid([XORG_DRIVER_CHECK_EXT])dnl
XORG_DRIVER_CHECK_EXT(DPMSExtension, xextproto)

# Checks for pkg-config packages
PKG_CHECK_MODULES(XORG, xorg-server >= 1.2.99.901 xproto $REQUIRED_MODULES)
sdkdir=$(pkg-config --variable=sdkdir xorg-server)

PKG_CHECK_MODULES([PCIACCESS], [pciaccess >= 0.8.0])
save_CFLAGS=$CFLAGS
CFLAGS=$XORG_CFLAGS
AC_CHECK_DECL(XSERVER_LIBPCIACCESS,
             [XSERVER_LIBPCIACCESS=yes],[XSERVER_LIBPCIACCESS=no],
             [#include "xorg-server.h"])
CFLAGS=$save_CFLAGS
if test x$XSERVER_LIBPCIACCESS = xyes ; then
    AC_DEFINE(PCIACCESS, 1, [Define to 1 if libpciaccess is available])
    AC_SUBST([PCIACCESS_CFLAGS])
    AC_SUBST([PCIACCESS_LIBS])
fi

CFLAGS="$CFLAGS $XORG_CFLAGS -Wall"
INCLUDES="$XORG_INCS -I${sdkdir} "'-I$(top_srcdir)/include'
AC_SUBST([CFLAGS])
AC_SUBST([INCLUDES])

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC

DRIVER_NAME=avivo
AC_SUBST([DRIVER_NAME])

m4_pattern_forbid([XORG_MANPAGE_SECTIONS])dnl
XORG_MANPAGE_SECTIONS
m4_pattern_forbid([XORG_RELEASE_VERSION])dnl
XORG_RELEASE_VERSION

AC_OUTPUT([
	Makefile
	include/Makefile
	xorg/Makefile
	avivotool/Makefile
])
