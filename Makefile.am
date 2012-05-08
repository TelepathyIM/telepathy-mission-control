ACLOCAL_AMFLAGS = -I m4

CHECK_FOR_UNRELEASED := \
    $(srcdir)/NEWS \
    $(wildcard $(srcdir)/src/*.[ch]) \
    $(wildcard $(libmmclient)/src/*.[ch]) \
    $(NULL)

SUBDIRS = \
    data \
    m4 \
    tools \
    xml \
    mission-control-plugins \
    plugins \
    src \
    server \
    util \
    tests \
    doc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = mission-control-plugins.pc

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-gtk-doc \
	--disable-gnome-keyring \
	$(NULL)

maintainer-upload-release: _maintainer-upload-release-local
_maintainer-upload-release-local: _maintainer-upload-release-check
	rsync -rvzPp --chmod=Dg+s,ug+rwX,o=rX doc/reference/mission-control-plugins/html/ \
	telepathy.freedesktop.org:/srv/telepathy.freedesktop.org/www/doc/mission-control-plugins/

include tools/lcov.am
include tools/telepathy.am

CHANGELOG_RANGE=telepathy-mission-control-5.4.0..
