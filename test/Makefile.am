INCLUDES = $(DBUS_CFLAGS) $(TELEPATHY_CFLAGS) -I$(top_srcdir) \
	   -I$(top_srcdir)/src \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

if HAVE_TESTS

bin_PROGRAMS = mc-client
mc_client_SOURCES = mc-client.c
mc_client_LDADD = $(top_srcdir)/libmissioncontrol/libmissioncontrol.la

endif
