INCLUDES = $(GCONF_CFLAGS) $(TELEPATHY_CFLAGS) -I$(top_srcdir)

bin_PROGRAMS = mc-account-convert mc-tool mc-wait-for-name
mc_account_convert_SOURCES = mc-account-convert.c
mc_account_convert_LDADD = $(GCONF_LIBS) $(TELEPATHY_LIBS) $(top_builddir)/libmcclient/libmcclient.la

mc_tool_SOURCES = mc-tool.c
mc_tool_LDADD = $(GCONF_LIBS) $(TELEPATHY_LIBS) $(top_builddir)/libmcclient/libmcclient.la

mc_wait_for_name_SOURCES = wait-for-name.c
mc_wait_for_name_LDADD = $(TELEPATHY_LIBS)
