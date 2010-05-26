ACLOCAL_AMFLAGS = -I m4

SUBDIRS = \
    m4 \
    tools \
    xml \
    mission-control-plugins \
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
pkgconfig_DATA = libmcclient.pc mission-control-plugins.pc

if ENABLE_MCD_PLUGINS
pkgconfig_DATA += mission-control.pc
endif

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-gtk-doc \
	--disable-gnome-keyring \
	--disable-mcd-plugins

include tools/lcov.am
include tools/telepathy.am
