TWISTED_TESTS =

TWISTED_BASIC_TESTS = \
	account-bad-cm.py \
	account-manager/auto-away.py \
	account-manager/create-with-properties.py \
	account-manager/nickname.py \
	account-manager/old-create-with-properties.py \
	account-manager/presence.py \
	account-manager/reconnect.py \
	account-manager/update-parameters.py \
	account-requests/cancel.py \
	account-requests/create-text.py \
	account-requests/delete-account-during-request.py \
	dispatcher/already-has-channel.py \
	dispatcher/already-has-obsolete.py \
	dispatcher/bypass-approval.py \
	dispatcher/cancel.py \
	dispatcher/capture-bundle.py \
	dispatcher/create-text.py \
	dispatcher/dispatch-obsolete.py \
	dispatcher/dispatch-rejected-by-plugin.py \
	dispatcher/dispatch-text.py \
	dispatcher/exploding-bundles.py \
	dispatcher/fdo-21034.py \
	dispatcher/lose-text.py \
	dispatcher/vanishing-client.py \
	do-nothing.py \
	test-account.py \
	test-connect.py

TWISTED_SEPARATE_TESTS = \
	account-manager/auto-connect.py \
	crash-recovery/crash-recovery.py \
	crash-recovery/client-crash-during-recovery.py \
	dispatcher/create-at-startup.py

# A demo dispatcher plugin
noinst_LTLIBRARIES = test-plugin.la
test_plugin_la_SOURCES = test-plugin.c
# these runes are necessary to make libtool build a dlopen()able shared
# library even though it's not going to be installed - the default for noinst
# libraries is static
test_plugin_la_LDFLAGS = -module -shared -avoid-version -rpath @abs_builddir@

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

BASIC_TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/test/twisted:@abs_top_builddir@/test/twisted \
	MC_DEBUG=2 \
	MC_TP_DEBUG=all \
	MC_FILTER_PLUGIN_DIR=@abs_top_builddir@/test/twisted/.libs \
	MC_CHANDLERS_DIR=@abs_top_builddir@/test/twisted/chandlers \
	MC_ACCOUNT_DIR=@abs_top_builddir@/test/twisted/accounts \
	XDG_DATA_HOME=@abs_top_builddir@/test/twisted \
	XDG_DATA_DIRS=@abs_top_srcdir@/test/twisted \
	XDG_CACHE_DIR=@abs_top_builddir@/test/twisted \
	MC_CLIENTS_DIR=@abs_top_builddir@/test/twisted/telepathy/clients \
	MC_MANAGER_DIR=@abs_top_builddir@/test/twisted/telepathy/managers \
	G_DEBUG=fatal_criticals

WITH_SESSION_BUS = \
	sh $(srcdir)/tools/with-session-bus.sh \
	--config-file=tools/tmp-session-bus.conf --

COMBINED_TESTS_ENVIRONMENT = \
	$(BASIC_TESTS_ENVIRONMENT) \
	$(TEST_PYTHON)

SEPARATE_TESTS_ENVIRONMENT = \
	$(BASIC_TESTS_ENVIRONMENT) \
	$(WITH_SESSION_BUS) \
	$(TEST_PYTHON)

if WANT_TWISTED_TESTS
check-local: check-twisted
endif

check-twisted:
	$(MAKE) -C tools
	rm -f tools/core
	rm -f tools/missioncontrol-*.log
	rm -f accounts/accounts.cfg
	rm -f accounts/.mc_connections
	env $(BASIC_TESTS_ENVIRONMENT) $(WITH_SESSION_BUS) $(MAKE) check-TESTS \
		TESTS='$$(TWISTED_BASIC_TESTS)' \
		TESTS_ENVIRONMENT='$$(COMBINED_TESTS_ENVIRONMENT)'
	$(MAKE) check-TESTS \
		TESTS='$$(TWISTED_SEPARATE_TESTS)' \
		TESTS_ENVIRONMENT='$$(SEPARATE_TESTS_ENVIRONMENT)'
	@if test -e tools/core; then\
		echo "Core dump exists: tools/core";\
		exit 1;\
	fi

EXTRA_DIST = \
	$(TWISTED_BASIC_TESTS) \
	$(TWISTED_SEPARATE_TESTS) \
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
	accounts/.mc_connections \
	mc-[1-9]*.log \
	*.pyc \
	*/*.pyc \
	with-session-bus-*.dbus-monitor-logs

check_misc_sources = $(TESTS)

SUBDIRS = tools
