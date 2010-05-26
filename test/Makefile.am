INCLUDES = \
	$(DBUS_CFLAGS) \
	$(TELEPATHY_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

AM_CFLAGS = $(ERROR_CFLAGS)

if ENABLE_GNOME_KEYRING
INCLUDES += $(GNOME_KEYRING_CFLAGS)
endif

if HAVE_TESTS

SUBDIRS = twisted

noinst_PROGRAMS = \
    test-value-is-same

TESTS = $(noinst_PROGRAMS)

# This test needs linking against the convenience library, so it can use
# MC internals.
test_value_is_same_SOURCES = value-is-same.c
test_value_is_same_LDADD = $(top_builddir)/src/libmcd-convenience.la

endif
