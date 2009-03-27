ACLOCAL_AMFLAGS = -I m4

SUBDIRS = m4 tools xml libmcclient src server util

if HAVE_LEGACY
SUBDIRS += libmissioncontrol
endif

if HAVE_EXAMPLES
SUBDIRS += examples
endif

SUBDIRS += doc test

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmcclient.pc mission-control.pc
if HAVE_LEGACY
pkgconfig_DATA += libmissioncontrol.pc
endif

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

include tools/lcov.am
