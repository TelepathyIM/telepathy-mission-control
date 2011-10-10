if ENABLE_INSTALLED_TESTS
twistedtestsdir = @mctestsdir@/twisted/tools
nobase_nodist_twistedtests_SCRIPTS = \
	run-mc.sh \
	fake-startup.sh \
	$(NULL)
nobase_dist_twistedtests_SCRIPTS = \
	with-session-bus.sh \
	$(NULL)
nobase_nodist_twistedtests_DATA = \
	$(installed_conf_files) \
	$(installed_service_files) \
	$(NULL)
endif

exec-with-log.sh: exec-with-log.sh.in Makefile
	$(AM_V_GEN)sed -e "s|[@]abs_top_srcdir[@]|@abs_top_srcdir@|g" \
		-e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" \
		$< > $@
	$(AM_V_at)chmod +x $@

run-mc.sh: run-mc.sh.in Makefile
	$(AM_V_GEN)sed \
		-e "s|[@]mctestsdir[@]|@mctestsdir@|g" \
		-e "s|[@]libexecdir[@]|$(libexecdir)|g" \
		$< > $@
	@chmod +x $@

servicedir-uninstalled/%.conf: servicedir-uninstalled/%.conf.in Makefile
	$(AM_V_at)$(mkdir_p) servicedir-uninstalled
	$(AM_V_GEN)sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

servicedir-installed/%.conf: servicedir-installed/%.conf.in Makefile
	$(AM_V_at)$(mkdir_p) servicedir-installed
	$(AM_V_GEN)sed -e "s|[@]mctestsdir[@]|@mctestsdir@|g" $< > $@

# We don't use the full filename for the .in because > 99 character filenames
# in tarballs are non-portable (and automake 1.8 doesn't let us build
# non-archaic tarballs)
servicedir-uninstalled/org.freedesktop.Telepathy.%.service: servicedir-uninstalled/%.service.in Makefile
	$(AM_V_at)$(mkdir_p) servicedir-uninstalled
	$(AM_V_GEN)sed \
		-e "s|[@]abs_top_srcdir[@]|@abs_top_srcdir@|g" \
		-e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" \
		$< > $@

servicedir-installed/org.freedesktop.Telepathy.%.service: servicedir-installed/%.service.in Makefile
	$(AM_V_at)$(mkdir_p) servicedir-installed
	$(AM_V_GEN)sed \
		-e "s|[@]mctestsdir[@]|@mctestsdir@|g" \
		$< > $@

# D-Bus service file for testing
uninstalled_service_in_files = \
	servicedir-uninstalled/MissionControl5.service.in \
	servicedir-uninstalled/Client.AbiWord.service.in \
	servicedir-uninstalled/Client.Logger.service.in
uninstalled_service_files = $(patsubst servicedir-uninstalled/%.in,servicedir-uninstalled/org.freedesktop.Telepathy.%, $(uninstalled_service_in_files))
installed_service_in_files = \
	servicedir-installed/MissionControl5.service.in \
	servicedir-installed/Client.AbiWord.service.in \
	servicedir-installed/Client.Logger.service.in
installed_service_files = $(patsubst servicedir-installed/%.in,servicedir-installed/org.freedesktop.Telepathy.%, $(installed_service_in_files))

# D-Bus config file for testing
uninstalled_conf_in_files = servicedir-uninstalled/tmp-session-bus.conf.in
uninstalled_conf_files = $(uninstalled_conf_in_files:.conf.in=.conf)
installed_conf_in_files = servicedir-installed/tmp-session-bus.conf.in
installed_conf_files = $(installed_conf_in_files:.conf.in=.conf)

BUILT_SOURCES = \
	$(uninstalled_service_files) $(uninstalled_conf_files) \
	$(installed_service_files) $(installed_conf_files) \
	exec-with-log.sh

EXTRA_DIST = \
	$(uninstalled_service_in_files) \
	$(uninstalled_conf_in_files) \
	$(installed_service_in_files) \
	$(installed_conf_in_files) \
	exec-with-log.sh.in \
	fake-startup.sh \
	valgrind.supp \
	with-session-bus.sh \
	run-mc.sh.in \
	$(NULL)

CLEANFILES = \
    $(BUILT_SOURCES) \
    missioncontrol.log \
    missioncontrol-*.log
