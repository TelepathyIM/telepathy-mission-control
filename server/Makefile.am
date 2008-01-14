INCLUDES = $(DBUS_CFLAGS) $(TELEPATHY_CFLAGS) -I$(top_srcdir) \
	   -I$(top_srcdir)/src \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

if HAVE_SERVER

servicefiledir=$(prefix)/share/dbus-1/services
servicefile_DATA=org.freedesktop.Telepathy.MissionControl.service

libexec_PROGRAMS = mission-control
mission_control_SOURCES = mc-server.c
mission_control_LDADD = $(top_builddir)/src/libmissioncontrol-server.la \
			$(top_builddir)/libmissioncontrol/libmissioncontrol-client.la

endif
