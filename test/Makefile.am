INCLUDES = $(DBUS_CFLAGS) $(TELEPATHY_CFLAGS) -I$(top_srcdir) \
	   -I$(top_srcdir)/src \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

if HAVE_TESTS

SUBDIRS = twisted

noinst_PROGRAMS = \
    test-value-is-same

TESTS = $(noinst_PROGRAMS)

test_value_is_same_SOURCES = value-is-same.c
test_value_is_same_LDADD = $(top_builddir)/src/libmissioncontrol-server.la

endif
