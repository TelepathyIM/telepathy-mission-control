INCLUDES = $(DBUS_CFLAGS) $(TELEPATHY_CFLAGS) -I$(top_srcdir) \
	   -I$(top_srcdir)/src \
	   -I$(top_builddir)/src \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

AM_CFLAGS = $(ERROR_CFLAGS)

if HAVE_SERVER

EXTRA_DIST = org.freedesktop.Telepathy.MissionControl5.service.in \
	     org.freedesktop.Telepathy.AccountManager.service.in \
	     mission-control-5.8.in
CLEANFILES = org.freedesktop.Telepathy.MissionControl5.service \
	     org.freedesktop.Telepathy.AccountManager.service \
	     mission-control-5.8

servicefiledir=$(prefix)/share/dbus-1/services
servicefile_DATA=org.freedesktop.Telepathy.MissionControl5.service \
		 org.freedesktop.Telepathy.AccountManager.service

%.service: %.service.in Makefile
	sed -e 's![@]libexecdir[@]!$(libexecdir)!' $< > $@

man8_MANS = mission-control-5.8

%.8: %.8.in Makefile
	sed -e 's![@]libexecdir[@]!$(libexecdir)!' $< > $@

libexec_PROGRAMS = mission-control-5
mission_control_5_SOURCES = mc-server.c
mission_control_5_LDADD = $(top_builddir)/src/libmissioncontrol-server.la

endif
