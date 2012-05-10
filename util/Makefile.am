AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(DBUS_CFLAGS) \
	$(LIBFFI_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(TELEPATHY_CFLAGS) \
	$(NULL)

LDADD = \
	$(LIBFFI_LIBS) \
	$(GLIB_LIBS) \
	$(DBUS_LIBS) \
	$(TELEPATHY_LIBS) \
	$(NULL)

AM_CFLAGS = $(ERROR_CFLAGS)

bin_PROGRAMS = mc-tool mc-wait-for-name

mc_tool_SOURCES = mc-tool.c

mc_wait_for_name_SOURCES = wait-for-name.c

dist_man1_MANS = mc-tool.1

man1_MANS = mc-wait-for-name.1

EXTRA_DIST = \
    $(man1_MANS:%=%.in)
CLEANFILES = $(man1_MANS)

mc-wait-for-name.1: mc-wait-for-name.1.in Makefile
	$(AM_V_GEN)sed -e 's![@]bindir[@]!@bindir@!g' < $< > $@

Android.mk: Makefile.am
	androgenizer -:PROJECT telepathy_mission_controll \
	 -:EXECUTABLE mc-tool -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(mc_tool_SOURCES) \
	 -:CFLAGS $(DEFS) $(CFLAGS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	  $(AM_CFLAGS) \
	 -:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) \
	 -:LDFLAGS $(LDADD) \
	> $@
