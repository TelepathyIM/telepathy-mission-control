TWISTED_TESTS =

TWISTED_BASIC_TESTS = \
	account/addressing.py \
	account-manager/account-basics.py \
	account-manager/bad-cm.py \
	account-manager/avatar.py \
	account-manager/create-auto-connect.py \
	account-manager/create-twice.py \
	account-manager/create-with-properties.py \
	account-manager/enable.py \
	account-manager/enable-auto-connect.py \
	account-manager/nickname.py \
	account-manager/service.py \
	account-manager/param-types.py \
	account-manager/presence.py \
	account-manager/reconnect.py \
	account-manager/recover-from-disconnect.py \
	account-manager/request-online.py \
	account-manager/update-parameters.py \
	account-requests/cancel.py \
	account-requests/create-text.py \
	account-requests/delete-account-during-request.py \
	capabilities/draft-1.py \
	capabilities/contact-caps.py \
	capabilities/legacy-caps.py \
	dispatcher/approver-fails.py \
	dispatcher/already-has-channel.py \
	dispatcher/already-has-obsolete.py \
	dispatcher/bypass-approval.py \
	dispatcher/bypass-observers.py \
	dispatcher/cancel.py \
	dispatcher/capture-bundle.py \
	dispatcher/connect-for-request.py \
	dispatcher/create-handler-fails.py \
	dispatcher/create-hints.py \
	dispatcher/create-no-preferred-handler.py \
	dispatcher/create-delayed-by-mini-plugin.py \
	dispatcher/create-rejected-by-mini-plugin.py \
	dispatcher/create-text.py \
	dispatcher/created-behind-our-back.py \
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
	dispatcher/request-disabled-account.py \
	dispatcher/respawn-observers.py \
	dispatcher/respawn-activatable-observers.py \
	dispatcher/undispatchable.py \
	dispatcher/vanishing-client.py

if ENABLE_MCD_PLUGINS
TWISTED_BASIC_TESTS += \
	dispatcher/create-delayed-by-plugin.py \
	dispatcher/dispatch-delayed-by-plugin.py \
	dispatcher/dispatch-rejected-by-plugin.py
endif

# Tests that aren't to be run if we're running the real (installed) MC,
# because they rely on special behaviour of mc-debug-server.
#
# For simplicity, these are also separate tests: at least
# account-storage/*.py need their own instances.
TWISTED_SPECIAL_BUILD_TESTS = \
	account-manager/hidden.py \
	account-manager/auto-away.py \
	account-storage/default-keyring-storage.py \
	account-storage/diverted-storage.py

TWISTED_SLOW_TESTS = \
	account-manager/server-drops-us.py

TWISTED_SEPARATE_TESTS = \
	account-manager/auto-connect.py \
	account-manager/avatar-persist.py \
	account-manager/avatar-refresh.py \
	account-manager/device-idle.py \
	account-manager/make-valid.py \
	crash-recovery/crash-recovery.py \
	dispatcher/create-at-startup.py

TWISTED_SEPARATE_TESTS += $(TWISTED_SPECIAL_BUILD_TESTS)

if HAVE_MCE
HAVE_MCE_PYBOOL = True
else
HAVE_MCE_PYBOOL = False
endif

config.py: Makefile
	$(AM_V_GEN) { \
		echo "HAVE_MCE = $(HAVE_MCE_PYBOOL)"; \
	} > $@

BUILT_SOURCES = config.py

AM_CFLAGS = $(ERROR_CFLAGS)

# A demo dispatcher plugin (new, minimal API)
noinst_LTLIBRARIES = mcp-plugin.la mcp-account-diversion.la mcp-dbus-caller-permission.la
mcp_plugin_la_SOURCES = mcp-plugin.c
# these runes are necessary to make libtool build a dlopen()able shared
# library even though it's not going to be installed - the default for noinst
# libraries is static
mcp_plugin_la_LDFLAGS = -module -shared -avoid-version -rpath @abs_builddir@

mcp_account_diversion_la_SOURCES = mcp-account-diversion.c
mcp_account_diversion_la_LDFLAGS = $(mcp_plugin_la_LDFLAGS)

mcp_dbus_caller_permission_la_SOURCES = mcp-dbus-caller-permission.c
mcp_dbus_caller_permission_la_LDFLAGS = $(mcp_plugin_la_LDFLAGS)

if ENABLE_MCD_PLUGINS
# A demo dispatcher plugin (old API, with access to MCD internals)
noinst_LTLIBRARIES += test-plugin.la
test_plugin_la_SOURCES = test-plugin.c
test_plugin_la_LDFLAGS = -module -shared -avoid-version -rpath @abs_builddir@
endif

# A debug version of the normal MC executable, which exits cleanly on
# disconnection from D-Bus (so gcov info gets written out)
noinst_PROGRAMS = mc-debug-server
mc_debug_server_SOURCES = mc-debug-server.c
mc_debug_server_LDADD = \
	$(top_builddir)/src/libmissioncontrol-server.la \
	$(top_builddir)/src/libmcd-convenience.la

INCLUDES = \
	-I$(top_srcdir) -I$(top_builddir) \
	-I$(top_srcdir)/src -I$(top_builddir)/src \
	$(DBUS_CFLAGS) \
	$(TELEPATHY_CFLAGS) \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

if ENABLE_GNOME_KEYRING
INCLUDES += $(GNOME_KEYRING_CFLAGS)
endif

TESTS =

TMPSUFFIX = foo

BASIC_TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/tests/twisted:@abs_top_builddir@/tests/twisted \
	MC_DEBUG=all \
	MC_FILTER_PLUGIN_DIR=@abs_top_builddir@/tests/twisted/.libs \
	MC_CHANDLERS_DIR=@abs_top_srcdir@/tests/twisted/chandlers \
	MC_ACCOUNT_DIR=@abs_top_builddir@/tests/twisted/tmp-$(TMPSUFFIX) \
	XDG_CONFIG_HOME=@abs_top_builddir@/tests/twisted/tmp-$(TMPSUFFIX) \
	XDG_CONFIG_DIRS=@abs_top_srcdir@/tests/twisted \
	XDG_DATA_HOME=@abs_top_builddir@/tests/twisted/tmp-$(TMPSUFFIX) \
	XDG_DATA_DIRS=@abs_top_srcdir@/tests/twisted \
	XDG_CACHE_DIR=@abs_top_builddir@/tests/twisted/tmp-$(TMPSUFFIX) \
	XDG_CACHE_HOME=@abs_top_builddir@/tests/twisted/tmp-$(TMPSUFFIX) \
	MC_CLIENTS_DIR=@abs_top_srcdir@/tests/twisted/telepathy/clients \
	MC_MANAGER_DIR=@abs_top_srcdir@/tests/twisted/telepathy/managers \
	G_DEBUG=fatal_criticals

if ENABLE_GNOME_KEYRING
BASIC_TESTS_ENVIRONMENT += MC_TEST_GNOME_KEYRING=1
endif

if ENABLE_LIBACCOUNTS_SSO
BASIC_TESTS_ENVIRONMENT += AG_DEBUG=all \
	ACCOUNTS=@abs_top_builddir@/tests/twisted/tmp-$(TMPSUFFIX) \
	AG_SERVICES=@abs_top_builddir@/tests/twisted/tmp-$(TMPSUFFIX)
endif

WITH_SESSION_BUS = \
	sh $(srcdir)/tools/with-session-bus.sh \
	--config-file=tools/tmp-session-bus.conf --

COMBINED_TESTS_ENVIRONMENT = \
	$(BASIC_TESTS_ENVIRONMENT) \
	$(PYTHON)

SEPARATE_TESTS_ENVIRONMENT = \
	$(BASIC_TESTS_ENVIRONMENT) \
	$(WITH_SESSION_BUS) \
	$(PYTHON)

if WANT_TWISTED_TESTS
check-local: check-twisted

if HAVE_SERVER

installcheck-local: installcheck-twisted
installcheck-twisted:
	$(MAKE) check-twisted \
		TWISTED_SPECIAL_BUILD_TESTS= \
		MC_EXECUTABLE=@libexecdir@/mission-control-5

endif # HAVE_SERVER

endif # WANT_TWISTED_TESTS

check-twisted:
	$(MAKE) -C tools
	rm -f tools/core
	rm -f tools/missioncontrol-*.log
	mkdir tmp-$$$$ && { \
		$(MAKE) check-combined TMPSUFFIX=$$$$; \
		e=$$?; \
		rm -rf tmp-$$$$; \
	exit $$e; }
	for TESTS in $(TWISTED_SEPARATE_TESTS); do export TESTS; ( \
	rm -rf tmp-$$$$ && mkdir tmp-$$$$ && { \
		e=0; \
		$(MAKE) check-separate \
			TMPSUFFIX=$$$$ \
			TESTS=$$TESTS \
		|| e=$$?; \
		rm -rf tmp-$$$$; \
	}; exit $$e ) || exit $$?; done
	@if test -e tools/core; then\
		echo "Core dump exists: tools/core";\
		exit 1;\
	fi

check-torture:
	mkdir tmp-$$$$ && { \
		env $(BASIC_TESTS_ENVIRONMENT) $(WITH_SESSION_BUS) \
			$(MAKE) _check-torture TMPSUFFIX=$$$$; \
		e=$$?; \
		rm -rf tmp-$$$$; \
	exit $$e; }

TORTURE_REPEATS = 100

_check-torture:
	for i in `seq 1 $(TORTURE_REPEATS)`; do \
		$(MAKE) check-TESTS \
			TESTS='$$(TWISTED_BASIC_TESTS) $$(TWISTED_SLOW_TESTS)'\
			TESTS_ENVIRONMENT='$$(COMBINED_TESTS_ENVIRONMENT)'; \
		e=$$?; \
		test z$$e = z0 || break; \
	done

CHECK_TWISTED_SLOW =

check-combined:
	if test x$(CHECK_TWISTED_SLOW) = x; then \
		extra_tests= ; \
	else \
		extra_tests=' $$(TWISTED_SLOW_TESTS)'; \
	fi; \
	env $(BASIC_TESTS_ENVIRONMENT) $(WITH_SESSION_BUS) \
	$(MAKE) check-TESTS \
		TESTS='$$(TWISTED_BASIC_TESTS)'"$${extra_tests}" \
		TESTS_ENVIRONMENT='$$(COMBINED_TESTS_ENVIRONMENT)'

check-separate:
	echo "Running $(TESTS) in tmp-$(TMPSUFFIX)"
	$(MAKE) check-TESTS \
		TESTS_ENVIRONMENT='$$(SEPARATE_TESTS_ENVIRONMENT)'

EXTRA_DIST = \
	$(TWISTED_BASIC_TESTS) \
	$(TWISTED_SEPARATE_TESTS) \
	accounts/README \
	chandlers/README \
	telepathy/clients/README \
	telepathy/clients/AbiWord.client \
	telepathy/clients/Logger.client \
	telepathy/managers/fakecm.manager \
	telepathy/managers/onewitheverything.manager \
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
	with-session-bus-*.dbus-monitor-logs \
	config.py

check_misc_sources = $(TESTS)

SUBDIRS = tools
