INCLUDES = $(DBUS_CFLAGS) $(TELEPATHY_CFLAGS) -I$(top_srcdir) \
	   -I$(top_srcdir)/src \
	   -I$(top_builddir)/src \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

if HAVE_SERVER

EXTRA_DIST = org.freedesktop.Telepathy.MissionControl5.service.in \
	     org.freedesktop.Telepathy.AccountManager.service.in
CLEANFILES = org.freedesktop.Telepathy.MissionControl5.service \
	     org.freedesktop.Telepathy.AccountManager.service

servicefiledir=$(prefix)/share/dbus-1/services
servicefile_DATA=org.freedesktop.Telepathy.MissionControl5.service \
		 org.freedesktop.Telepathy.AccountManager.service

%.service: %.service.in Makefile
	sed -e 's![@]libexecdir[@]!$(libexecdir)!' $< > $@

libexec_PROGRAMS = mission-control
mission_control_SOURCES = mc-server.c
mission_control_LDADD = $(top_builddir)/src/libmissioncontrol-server.la

endif
