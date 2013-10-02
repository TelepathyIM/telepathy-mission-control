# Tests that don't need their own MC instance, although in practice
# we currently run them in their own MC instance anyway.
TWISTED_BASIC_TESTS = \
	account-manager/account-basics.py \
	account-manager/avatar.py \
	account-manager/backend-makes-changes.py \
	account-manager/bad-cm.py \
	account-manager/crashy-cm.py \
	account-manager/create-auto-connect.py \
	account-manager/create-twice.py \
	account-manager/create-with-properties.py \
	account-manager/enable-auto-connect.py \
	account-manager/enable.py \
	account-manager/irc.py \
	account-manager/nickname.py \
	account-manager/param-types.py \
	account-manager/presence.py \
	account-manager/reconnect.py \
	account-manager/recover-from-disconnect.py \
	account-manager/req-conn-fails.py \
	account-manager/request-online.py \
	account-manager/service.py \
	account-manager/update-parameters.py \
	account-requests/cancel.py \
	account-requests/create-text.py \
	account-requests/delete-account-during-request.py \
	account/addressing.py \
	capabilities/contact-caps.py \
	dispatcher/already-has-channel.py \
	dispatcher/already-has-obsolete.py \
	dispatcher/approver-fails.py \
	dispatcher/bypass-approval.py \
	dispatcher/bypass-observers.py \
	dispatcher/cancel.py \
	dispatcher/capture-bundle.py \
	dispatcher/cdo-claim.py \
	dispatcher/connect-for-request.py \
	dispatcher/create-delayed-by-mini-plugin.py \
	dispatcher/create-handler-fails.py \
	dispatcher/create-hints.py \
	dispatcher/create-no-preferred-handler.py \
	dispatcher/create-rejected-by-mini-plugin.py \
	dispatcher/create-text.py \
	dispatcher/created-behind-our-back.py \
	dispatcher/delay-approvers.py \
	dispatcher/delay-then-call-handle-with.py \
	dispatcher/delay-then-dont-call-approvers.py \
	dispatcher/dispatch-activatable.py \
	dispatcher/dispatch-before-connected.py \
	dispatcher/dispatch-delayed-by-mini-plugin.py \
	dispatcher/dispatch-obsolete.py \
	dispatcher/dispatch-rejected-by-mini-plugin.py \
	dispatcher/dispatch-text.py \
	dispatcher/ensure-and-redispatch.py \
	dispatcher/ensure-is-approval.py \
	dispatcher/ensure-rapidly.py \
	dispatcher/exploding-bundles.py \
	dispatcher/fdo-21034.py \
	dispatcher/handle-channels-fails.py \
	dispatcher/lose-text.py \
	dispatcher/recover-from-disconnect.py \
	dispatcher/redispatch-channels.py \
	dispatcher/request-disabled-account.py \
	dispatcher/respawn-activatable-observers.py \
	dispatcher/respawn-observers.py \
	dispatcher/some-delay-approvers.py \
	dispatcher/undispatchable.py \
	dispatcher/vanishing-client.py \
	$(NULL)

# Tests that aren't to be run if we're running the real (installed) MC,
# because they rely on special behaviour of mc-debug-server.
#
# For simplicity, these are also separate tests: at least
# account-storage/*.py need their own instances.
TWISTED_SPECIAL_BUILD_TESTS = \
	account-manager/connectivity.py \
	account-manager/hidden.py \
	account-storage/default-keyring-storage.py \
	account-storage/diverted-storage.py

# Tests that are usually too slow to run.
TWISTED_SLOW_TESTS = \
	account-manager/server-drops-us.py

# Tests that need their own MC instance.
TWISTED_SEPARATE_TESTS = \
	account-manager/auto-connect.py \
	account-manager/avatar-refresh.py \
	account-manager/device-idle.py \
	account-manager/make-valid.py \
	crash-recovery/crash-recovery.py \
	dispatcher/create-at-startup.py

# All the tests that are run by "make check"
TWISTED_TESTS = \
	$(TWISTED_BASIC_TESTS) \
	$(TWISTED_SEPARATE_TESTS) \
	$(TWISTED_SPECIAL_BUILD_TESTS) \
	$(NULL)

# other files used by the twisted tests, but are not tests and are not built
# source
TWISTED_OTHER_FILES = \
	constants.py \
	fakeaccountsservice.py \
	fakeclient.py \
	fakecm.py \
	fakeconnectivity.py \
	mctest.py \
	servicetest.py \
	telepathy/clients/README \
	telepathy/clients/AbiWord.client \
	telepathy/clients/Logger.client \
	telepathy/managers/fakecm.manager \
	telepathy/managers/onewitheverything.manager \
	telepathy/managers/README \
	$(NULL)

# This deliberately doesn't include the TWISTED_SPECIAL_BUILD_TESTS
# since it's testing the real binary, not the "debug" binary.
mc-twisted-tests.list:
	$(AM_V_GEN)echo $(TWISTED_BASIC_TESTS) $(TWISTED_SEPARATE_TESTS) > $@

run-test.sh: run-test.sh.in Makefile
	$(AM_V_GEN)sed \
		-e "s|[@]mctestsdir[@]|@mctestsdir@|g" \
		-e "s|[@]TEST_PYTHON[@]|$(PYTHON)|g" \
		$< > $@
	@chmod +x $@

if ENABLE_INSTALLED_TESTS
# Install files in each directory. They could be tests, pristine data files,
# scripts or built source
twistedtestsdir = @mctestsdir@/twisted
nobase_nodist_twistedtests_SCRIPTS = \
	run-test.sh \
	$(NULL)
nobase_dist_twistedtests_DATA = \
	$(TWISTED_BASIC_TESTS) \
	$(TWISTED_SEPARATE_TESTS) \
	$(TWISTED_OTHER_FILES) \
	$(NULL)
nobase_nodist_twistedtests_DATA = \
	config.py \
	mc-twisted-tests.list \
	$(NULL)
endif


if HAVE_MCE
HAVE_MCE_PYBOOL = True
else
HAVE_MCE_PYBOOL = False
endif

if HAVE_NM
HAVE_NM_PYBOOL = True
else
HAVE_NM_PYBOOL = False
endif

config.py: Makefile
	$(AM_V_GEN) { \
		echo "HAVE_MCE = $(HAVE_MCE_PYBOOL)"; \
		echo "HAVE_NM = $(HAVE_NM_PYBOOL)"; \
	} > $@

BUILT_SOURCES = \
	config.py \
	mc-twisted-tests.list \
	run-test.sh \
	$(NULL)

AM_CFLAGS = $(ERROR_CFLAGS)

plugins_list = \
	mcp-plugin.la \
	mcp-account-diversion.la \
	mcp-dbus-caller-permission.la \
	libgiofakenetworkmonitor.la \
	$(NULL)

if ENABLE_INSTALLED_TESTS

noinst_LTLIBRARIES = \
	$(NULL)
testplugindir = @mctestsdir@/twisted/plugins
testplugin_LTLIBRARIES = \
	$(plugins_list) \
	$(NULL)

else

# A demo dispatcher plugin (new, minimal API)
noinst_LTLIBRARIES = $(plugins_list)

endif

mcp_plugin_la_SOURCES = \
	dbus-account-plugin.c \
	dbus-account-plugin.h \
	mcp-plugin.c \
	$(NULL)
# these runes are necessary to make libtool build a dlopen()able shared
# library even though it's not going to be installed - the default for noinst
# libraries is static
mcp_plugin_la_LDFLAGS = -module -shared -avoid-version -rpath @abs_builddir@

libgiofakenetworkmonitor_la_SOURCES = \
	fake-network-monitor.c \
	$(NULL)
libgiofakenetworkmonitor_la_LDFLAGS = $(mcp_plugin_la_LDFLAGS)

mcp_account_diversion_la_SOURCES = mcp-account-diversion.c
mcp_account_diversion_la_LDFLAGS = $(mcp_plugin_la_LDFLAGS)

mcp_dbus_caller_permission_la_SOURCES = mcp-dbus-caller-permission.c
mcp_dbus_caller_permission_la_LDFLAGS = $(mcp_plugin_la_LDFLAGS)

# A debug version of the normal MC executable, which exits cleanly on
# disconnection from D-Bus (so gcov info gets written out)
noinst_PROGRAMS = mc-debug-server
mc_debug_server_SOURCES = mc-debug-server.c
mc_debug_server_LDADD = \
	$(top_builddir)/src/libmcd-convenience.la

INCLUDES = \
	-I$(top_srcdir) -I$(top_builddir) \
	-I$(top_srcdir)/src -I$(top_builddir)/src \
	$(DBUS_CFLAGS) \
	$(TELEPATHY_CFLAGS) \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

TESTS =

if WANT_TWISTED_TESTS
check-local: check-twisted

installcheck-local: installcheck-twisted
installcheck-twisted:
	$(MAKE) check-twisted \
		TWISTED_SPECIAL_BUILD_TESTS= \
		MC_EXECUTABLE=$(DESTDIR)@libexecdir@/mission-control-5

endif # WANT_TWISTED_TESTS

CHECK_TWISTED_SLOW =

check-twisted: $(BUILT_SOURCES)
	$(MAKE) -C tools
	rm -f core
	failed=0; \
	if test x$(CHECK_TWISTED_SLOW) = x; then \
		extra_tests= ; \
	else \
		extra_tests=' $$(TWISTED_SLOW_TESTS)'; \
	fi; \
	MC_TEST_UNINSTALLED=1 \
	  MC_ABS_TOP_SRCDIR=@abs_top_srcdir@ \
	  MC_ABS_TOP_BUILDDIR=@abs_top_builddir@ \
	  sh run-test.sh "${TWISTED_TESTS}${extra_tests}"
	if test -e core; then\
		echo "Core dump exists: core";\
		exit 1;\
	fi

EXTRA_DIST = \
	$(TWISTED_BASIC_TESTS) \
	$(TWISTED_SEPARATE_TESTS) \
	$(TWISTED_SLOW_TESTS) \
	$(TWISTED_SPECIAL_BUILD_TESTS) \
	$(TWISTED_OTHER_FILES) \
	accounts/README \
	run-test.sh.in \
	$(NULL)

CLEANFILES = \
	accounts/accounts.cfg \
	accounts/.mc_connections \
	mc-[1-9]*.log \
	missioncontrol.log \
	*.pyc \
	*/*.pyc \
	with-session-bus-*.dbus-monitor-logs \
	$(BUILT_SOURCES) \
	$(NULL)

check_misc_sources = $(TESTS)

SUBDIRS = tools
