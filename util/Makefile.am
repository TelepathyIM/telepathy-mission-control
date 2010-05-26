INCLUDES = $(TELEPATHY_CFLAGS) $(GLIB_CFLAGS) -I$(top_srcdir)
AM_CFLAGS = $(ERROR_CFLAGS)

bin_PROGRAMS = mc-tool mc-wait-for-name

mc_tool_SOURCES = mc-tool.c
mc_tool_LDADD = $(TELEPATHY_LIBS) $(top_builddir)/libmcclient/libmcclient.la

mc_wait_for_name_SOURCES = wait-for-name.c
mc_wait_for_name_LDADD = $(TELEPATHY_LIBS) $(GLIB_LIBS)

dist_man1_MANS = mc-tool.1

man1_MANS = mc-wait-for-name.1

EXTRA_DIST = \
    $(man1_MANS:%=%.in)
CLEANFILES = $(man1_MANS)

mc-wait-for-name.1: mc-wait-for-name.1.in Makefile
	sed -e 's![@]bindir[@]!@bindir@!g' < $< > $@
