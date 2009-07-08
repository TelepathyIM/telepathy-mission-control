INCLUDES = $(TELEPATHY_CFLAGS) -I$(top_srcdir)

bin_PROGRAMS = mc-tool mc-wait-for-name

mc_tool_SOURCES = mc-tool.c
mc_tool_LDADD = $(TELEPATHY_LIBS) $(top_builddir)/libmcclient/libmcclient.la

mc_wait_for_name_SOURCES = wait-for-name.c
mc_wait_for_name_LDADD = $(TELEPATHY_LIBS)

dist_man1_MANS = mc-tool.1

man1_MANS = mc-wait-for-name.1

EXTRA_DIST = $(man1_MANS:%=%.in)
CLEANFILES = $(man1_MANS)

mc-wait-for-name.1: mc-wait-for-name.1.in Makefile
	sed -e 's![@]bindir[@]!@bindir@!g' < $< > $@

if HAVE_GCONF

bin_PROGRAMS += mc-account-convert
mc_account_convert_CPPFLAGS = $(GCONF_CFLAGS)
mc_account_convert_SOURCES = mc-account-convert.c
mc_account_convert_LDADD = $(GCONF_LIBS) $(TELEPATHY_LIBS) $(top_builddir)/libmcclient/libmcclient.la

dist_man1_MANS += mc-account-convert.1

endif
