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

EXTRA_DIST = im.telepathy1.MissionControl6.service.in \
	     im.telepathy1.AccountManager.service.in \
	     mission-control-6.8.in
CLEANFILES = im.telepathy1.MissionControl6.service \
	     im.telepathy1.AccountManager.service \
	     mission-control-6.8

servicefiledir=$(prefix)/share/dbus-1/services
servicefile_DATA= \
	im.telepathy1.MissionControl6.service \
	im.telepathy1.AccountManager.service \
	$(NULL)

%.service: %.service.in Makefile
	$(AM_V_GEN)sed \
		-e 's![@]libexecdir[@]!$(libexecdir)!' \
		-e 's![@]bindir[@]!$(bindir)!' \
		$< > $@

man8_MANS = mission-control-6.8

%.8: %.8.in Makefile
	$(AM_V_GEN)sed -e 's![@]libexecdir[@]!$(libexecdir)!' $< > $@

libexec_PROGRAMS = mission-control-6
mission_control_6_SOURCES = mc-server.c
mission_control_6_LDADD = \
	$(GLIB_LIBS) \
	$(TELEPATHY_LIBS) \
	$(DBUS_LIBS) \
	$(top_builddir)/src/libmcd-convenience.la \
	$(top_builddir)/mission-control-plugins/libmission-control-plugins.la

Android.mk: Makefile.am
	androgenizer -:PROJECT telepathy-mission-control \
	 -:SHARED libmission-control-6 -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(mission_control_6_SOURCES) \
	 -:CFLAGS $(DEFAULT_INCLUDES) $(DEFS) $(CFLAGS) $(AM_CFLAGS) \
	 -:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) \
	  -DBUILD_AS_ANDROID_SERVICE \
	 -:LDFLAGS $(mission_control_6_LDADD) \
	 -:LIBFILTER_STATIC mcd-convenience mission-control-plugins \
	  mcclient \
	> $@
