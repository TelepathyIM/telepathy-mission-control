SUBDIRS = tools xml libmcclient libmissioncontrol src doc server test

if HAVE_EXAMPLES
SUBDIRS += examples
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmcclient.pc libmissioncontrol.pc mission-control.pc

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc
