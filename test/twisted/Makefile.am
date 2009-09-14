TWISTED_TESTS =

TWISTED_BASIC_TESTS = \
	account-manager/account-basics.py \
	account-manager/bad-cm.py \
	account-manager/avatar.py \
	account-manager/create-auto-connect.py \
	account-manager/create-with-properties.py \
	account-manager/enable.py \
	account-manager/enable-auto-connect.py \
	account-manager/nickname.py \
	account-manager/old-create-with-properties.py \
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
	dispatcher/cancel.py \
	dispatcher/capture-bundle.py \
	dispatcher/connect-for-request.py \
	dispatcher/create-handler-fails.py \
	dispatcher/create-no-preferred-handler.py \
	dispatcher/create-text.py \
	dispatcher/dispatch-activatable.py \
	dispatcher/dispatch-obsolete.py \
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
	dispatcher/undispatchable.py \
	dispatcher/vanishing-client.py

if ENABLE_PLUGINS
TWISTED_BASIC_TESTS += \
	dispatcher/dispatch-rejected-by-plugin.py
endif

# Tests that aren't to be run if we're running the real (installed) MC,
# because they rely on special behaviour of mc-debug-server
TWISTED_SPECIAL_BUILD_TESTS = \
	account-manager/auto-away.py \
	dispatcher/create-lowmem.py

TWISTED_BASIC_TESTS += $(TWISTED_SPECIAL_BUILD_TESTS)

TWISTED_SEPARATE_TESTS = \
	account-manager/auto-connect.py \
	account-manager/avatar-persist.py \
	account-manager/avatar-refresh.py \
	account-manager/make-valid.py \
	crash-recovery/crash-recovery.py \
	dispatcher/create-at-startup.py

if ENABLE_PLUGINS
# A demo dispatcher plugin
noinst_LTLIBRARIES = test-plugin.la
test_plugin_la_SOURCES = test-plugin.c
# these runes are necessary to make libtool build a dlopen()able shared
# library even though it's not going to be installed - the default for noinst
# libraries is static
test_plugin_la_LDFLAGS = -module -shared -avoid-version -rpath @abs_builddir@
endif

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

if ENABLE_GNOME_KEYRING
INCLUDES += $(GNOME_KEYRING_CFLAGS)
endif

TESTS =

TMPSUFFIX = foo

BASIC_TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/test/twisted:@abs_top_builddir@/test/twisted \
	MC_DEBUG=all \
	MC_FILTER_PLUGIN_DIR=@abs_top_builddir@/test/twisted/.libs \
	MC_CHANDLERS_DIR=@abs_top_srcdir@/test/twisted/chandlers \
	MC_ACCOUNT_DIR=@abs_top_builddir@/test/twisted/tmp-$(TMPSUFFIX) \
	XDG_CONFIG_HOME=@abs_top_builddir@/test/twisted/tmp-$(TMPSUFFIX) \
	XDG_CONFIG_DIRS=@abs_top_srcdir@/test/twisted \
	XDG_DATA_HOME=@abs_top_builddir@/test/twisted/tmp-$(TMPSUFFIX) \
	XDG_DATA_DIRS=@abs_top_srcdir@/test/twisted \
	XDG_CACHE_DIR=@abs_top_builddir@/test/twisted/tmp-$(TMPSUFFIX) \
	MC_CLIENTS_DIR=@abs_top_srcdir@/test/twisted/telepathy/clients \
	MC_MANAGER_DIR=@abs_top_srcdir@/test/twisted/telepathy/managers \
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
			TESTS='$$(TWISTED_BASIC_TESTS)' \
			TESTS_ENVIRONMENT='$$(COMBINED_TESTS_ENVIRONMENT)'; \
		e=$$?; \
		test z$$e = z0 || break; \
	done

check-combined:
	env $(BASIC_TESTS_ENVIRONMENT) $(WITH_SESSION_BUS) \
	$(MAKE) check-TESTS \
		TESTS='$$(TWISTED_BASIC_TESTS)' \
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
	with-session-bus-*.dbus-monitor-logs

check_misc_sources = $(TESTS)

SUBDIRS = tools
