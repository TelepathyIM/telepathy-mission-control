AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(DBUS_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(TELEPATHY_CFLAGS) 

LDADD = \
	$(GLIB_LIBS) \
	$(DBUS_LIBS) \
	$(TELEPATHY_LIBS)

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
