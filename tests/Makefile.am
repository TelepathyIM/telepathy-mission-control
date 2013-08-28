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

SUBDIRS = . twisted

TEST_EXECUTABLES = \
	test-keyfile \
	test-value-is-same \
	$(NULL)

NON_TEST_EXECUTABLES = account-store tease-the-minotaur

noinst_PROGRAMS = $(TEST_EXECUTABLES) $(NON_TEST_EXECUTABLES)

TESTS = $(TEST_EXECUTABLES)

# This test needs linking against the convenience library, so it can use
# MC internals.
test_value_is_same_SOURCES = value-is-same.c
test_value_is_same_LDADD = $(top_builddir)/src/libmcd-convenience.la

test_keyfile_SOURCES = keyfile.c
test_keyfile_LDADD = $(top_builddir)/src/libmcd-convenience.la

tease_the_minotaur_SOURCES = tease-the-minotaur.c
tease_the_minotaur_LDADD = $(top_builddir)/src/libmcd-convenience.la

account_store_LDADD = $(GLIB_LIBS)
account_store_SOURCES = \
	account-store.c \
	account-store-default.c \
	account-store-default.h

if ENABLE_LIBACCOUNTS_SSO
account_store_SOURCES += account-store-libaccounts.c account-store-libaccounts.h
account_store_LDADD += $(LIBACCOUNTS_SSO_LIBS)
INCLUDES += $(LIBACCOUNTS_SSO_CFLAGS)
endif
