TWISTED_TESTS =

TWISTED_BASIC_TESTS = \
        test-account.py \
        test-basic.py

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
	mctest.py \
	servicetest.py

CLEANFILES = mc-[1-9]*.log *.pyc */*.pyc

check_misc_sources = $(TESTS)

SUBDIRS = tools
