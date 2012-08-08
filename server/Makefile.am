AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src \
	$(DBUS_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(TELEPATHY_CFLAGS) \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

AM_CFLAGS = $(ERROR_CFLAGS)

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
	$(AM_V_GEN)sed \
		-e 's![@]libexecdir[@]!$(libexecdir)!' \
		-e 's![@]bindir[@]!$(bindir)!' \
		$< > $@

man8_MANS = mission-control-5.8

%.8: %.8.in Makefile
	$(AM_V_GEN)sed -e 's![@]libexecdir[@]!$(libexecdir)!' $< > $@

libexec_PROGRAMS = mission-control-5
mission_control_5_SOURCES = mc-server.c
mission_control_5_LDADD = \
	$(GLIB_LIBS) \
	$(TELEPATHY_LIBS) \
	$(DBUS_LIBS) \
	$(top_builddir)/src/libmcd-convenience.la \
	$(top_builddir)/mission-control-plugins/libmission-control-plugins.la

Android.mk: Makefile.am
	androgenizer -:PROJECT telepathy-mission-control \
	 -:SHARED libmission-control-5 -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(mission_control_5_SOURCES) \
	 -:CFLAGS $(DEFAULT_INCLUDES) $(DEFS) $(CFLAGS) $(AM_CFLAGS) \
	 -:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) $(INCLUDES) \
	  -DBUILD_AS_ANDROID_SERVICE \
	 -:LDFLAGS $(mission_control_5_LDADD) \
	 -:LIBFILTER_STATIC mcd-convenience mission-control-plugins \
	  mcclient \
	> $@
