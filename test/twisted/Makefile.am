TWISTED_TESTS =

TWISTED_BASIC_TESTS = \
	account-bad-cm.py \
	account-requests/cancel.py \
	account-requests/create-text.py \
	account-requests/delete-account-during-request.py \
	dispatcher/already-has-channel.py \
	dispatcher/cancel.py \
	dispatcher/create-text.py \
	dispatcher/dispatch-text.py \
	do-nothing.py \
	test-account.py \
	test-connect.py

# A debug version of the normal MC executable, which exits cleanly on
# disconnection from D-Bus (so gcov info gets written out)
noinst_PROGRAMS = mc-debug-server
mc_debug_server_SOURCES = mc-debug-server.c
mc_debug_server_LDADD = $(top_builddir)/src/libmissioncontrol-server.la

INCLUDES = \
	-I$(top_srcdir) -I$(top_builddir) \
	-I$(top_srcdir)/src -I$(top_builddir)/src \
	$(DBUS_CFLAGS) \
	$(TELEPATHY_CFLAGS) \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

TESTS =

TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/test/twisted:@abs_top_builddir@/test/twisted

if WANT_TWISTED_TESTS
  TWISTED_TESTS += $(TWISTED_BASIC_TESTS)
endif

check-local: check-twisted

check-twisted:
	$(MAKE) -C tools
	rm -f tools/core
	sh $(srcdir)/tools/with-session-bus.sh --config-file=tools/tmp-session-bus.conf -- $(MAKE) check-TESTS \
		TESTS="$(TWISTED_TESTS)" \
		TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) $(TEST_PYTHON)"
	@if test -e tools/core; then\
		echo "Core dump exists: tools/core";\
		exit 1;\
	fi

EXTRA_DIST = \
	$(TWISTED_BASIC_TESTS) \
	telepathy/clients/README \
	telepathy/managers/fakecm.manager \
	telepathy/managers/README \
	constants.py \
        fakeclient.py \
        fakecm.py \
	mctest.py \
	servicetest.py

CLEANFILES = \
	accounts/accounts.cfg \
	mc-[1-9]*.log \
	*.pyc \
	*/*.pyc \
	with-session-bus-*.dbus-monitor-logs

check_misc_sources = $(TESTS)

SUBDIRS = tools
