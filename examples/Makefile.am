INCLUDES = $(TELEPATHY_CFLAGS) -I$(top_srcdir)
AM_CFLAGS = $(ERROR_CFLAGS)

noinst_PROGRAMS = mc-example mc-example-2 mc-example-3
mc_example_SOURCES = mc-example.c
mc_example_LDADD = $(TELEPATHY_LIBS) $(top_builddir)/libmcclient/libmcclient.la

mc_example_2_SOURCES = mc-example-2.c
mc_example_2_LDADD = $(TELEPATHY_LIBS) $(top_builddir)/libmcclient/libmcclient.la

mc_example_3_SOURCES = mc-example-3.c
mc_example_3_LDADD = $(TELEPATHY_LIBS) $(top_builddir)/libmcclient/libmcclient.la

