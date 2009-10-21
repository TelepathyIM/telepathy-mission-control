ACLOCAL_AMFLAGS = -I m4

SUBDIRS = \
    m4 \
    tools \
    xml \
    libmcclient \
    src \
    server \
    util \
    test \
    doc

if HAVE_EXAMPLES
SUBDIRS += examples
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmcclient.pc

if ENABLE_PLUGINS
pkgconfig_DATA += mission-control.pc
endif

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc --disable-gnome-keyring

include tools/lcov.am
include tools/telepathy.am
