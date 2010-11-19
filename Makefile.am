ACLOCAL_AMFLAGS = -I m4

SUBDIRS = \
    m4 \
    tools \
    xml \
    mission-control-plugins \
    plugins \
    libmcclient \
    src \
    server \
    util \
    test \
    doc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = mission-control-plugins.pc

if ENABLE_MCD_PLUGINS
pkgconfig_DATA += mission-control.pc libmcclient.pc
endif

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-gtk-doc \
	--disable-gnome-keyring \
	--disable-mcd-plugins

maintainer-upload-release: _maintainer-upload-release-local
_maintainer-upload-release-local: _maintainer-upload-release-check
	rsync -rvzPp --chmod=Dg+s,ug+rwX,o=rX doc/reference/mission-control-plugins/html/ \
	telepathy.freedesktop.org:/srv/telepathy.freedesktop.org/www/doc/mission-control-plugins/

include tools/lcov.am
include tools/telepathy.am

CHANGELOG_RANGE=telepathy-mission-control-5.4.0..
