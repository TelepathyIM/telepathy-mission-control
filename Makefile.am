ACLOCAL_AMFLAGS = -I m4

SUBDIRS = m4 tools xml libmcclient src server util

if HAVE_EXAMPLES
SUBDIRS += examples
endif

SUBDIRS += doc test

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmcclient.pc

if ENABLE_PLUGINS
pkgconfig_DATA += mission-control.pc
endif

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

include tools/lcov.am
include tools/telepathy.am
