dnl Set the version number to e.g. 5.0.beta42 immediately before a release.
dnl Set the version number to e.g. 5.0.beta42+ immediately after (this will
dnl enable -Werror).
AC_INIT(telepathy-mission-control, 5.3.1)

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR(Makefile.am)
AM_INIT_AUTOMAKE([1.9 tar-ustar -Wno-portability])
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AM_PROG_MKDIR_P

AC_HEADER_STDC
AC_CHECK_HEADERS([sys/stat.h sys/types.h])
AC_CHECK_FUNCS([umask])

case "$PACKAGE_VERSION" in
  *+)
    mc_is_release=no
    ;;
  *)
    mc_is_release=yes
    ;;
esac

# Recommended CFLAGS for pedantic checking, from telepathy-glib

TP_COMPILER_WARNINGS([ERROR_CFLAGS],
  [test "x$mc_is_release" = xno],   dnl <- Disable -Werror in releases
  [all \
   extra \
   declaration-after-statement \
   shadow \
   strict-prototypes \
   missing-prototypes \
   missing-declarations \
   sign-compare \
   nested-externs \
   pointer-arith \
   format-security \
   init-self \
   ],
  [missing-field-initializers \
   unused-parameter])
AC_SUBST([ERROR_CFLAGS])

AM_CFLAGS="$CFLAGS -fno-strict-aliasing -ansi -DDMALLOC $ERROR_CFLAGS"
AC_SUBST([AM_CFLAGS])

define([EXPAND_VARIABLE],
[$2=[$]$1
while true; do
    case "[$]$2" in
    *\[$]* ) eval "$2=[$]$2" ;;
    *) break ;;
    esac
done])

EXPAND_VARIABLE(datadir,datadir_expanded)

XSLTPROC=
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
    AC_MSG_ERROR([xsltproc (from the libxslt source package) is required])
fi

AM_PATH_PYTHON([2.5])
AC_MSG_CHECKING([for enough Python for the tests])
mc_want_twisted_tests=no
if $PYTHON -c "import twisted.internet.reactor, dbus.mainloop.glib"\
    >/dev/null 2>&1; then
    mc_want_twisted_tests=yes
fi
AC_MSG_RESULT([$mc_want_twisted_tests])
AM_CONDITIONAL([WANT_TWISTED_TESTS], test yes = "$mc_want_twisted_tests")

AC_ARG_ENABLE(debug,            [  --disable-debug               compile without debug code],[enable_debug=${enableval}], enable_debug=yes )
if test "x$enable_debug" = "xyes"; then
	CFLAGS="$CFLAGS -DENABLE_DEBUG"
fi

AC_ARG_ENABLE(cast-checks,      [  --disable-cast-checks         compile with GLIB cast checks disabled],[cchecks=${enableval}],cchecks=yes)
if test "x$cchecks" = "xno"; then
	CFLAGS="$CFLAGS -DG_DISABLE_CAST_CHECKS"
fi

AC_ARG_ENABLE(asserts,      [  --disable-asserts        compile with GLIB assertions disabled],[asserts=${enableval}],asserts=yes)
if test "x$asserts" = "xno"; then
	CFLAGS="$CFLAGS -DG_DISABLE_ASSERTS"
fi

AC_ARG_ENABLE(checks,       [  --disable-checks         compile with GLIB checks disabled],[checks=${enableval}],checks=yes)
if test "x$checks" = "xno"; then
	CFLAGS="$CFLAGS -DG_DISABLE_CHECKS"
fi

AC_ARG_ENABLE(coverage,       [  --enable-coverage         compile with coverage info],[enable_coverage=${enableval}],enable_coverage=no)
if test "x$enable_coverage" = "xyes"; then
       CFLAGS="$CFLAGS -g -fprofile-arcs -ftest-coverage"
fi

AC_ARG_WITH(profiles_dir, AS_HELP_STRING([--with-profiles-dir=<path>],[Directory for storing profiles]))
if test -z "$with_profiles_dir" ; then
    PROFILES_DIR="mission-control/profiles"
else
    PROFILES_DIR=$with_profiles_dir
fi
AC_SUBST(PROFILES_DIR)
AC_DEFINE_UNQUOTED(PROFILES_DIR,"$PROFILES_DIR", [Directory for storing profiles])


AC_ARG_WITH(managers_dir, AS_HELP_STRING([--with-managers-dir=<path>],[Directory for storing managers]))
if test -z "$with_managers_dir" ; then
    MANAGERS_DIR="telepathy/managers"
else
    MANAGERS_DIR=$with_managers_dir
fi
AC_SUBST(MANAGERS_DIR)
AC_DEFINE_UNQUOTED(MANAGERS_DIR,"$MANAGERS_DIR", [Directory for storing managers])


AC_ARG_WITH(accounts_dir, AS_HELP_STRING([--with-accounts-dir=<path>],[Directory for storing accounts]))
if test -z "$with_accounts_dir" ; then
    ACCOUNTS_DIR="~/.mission-control/accounts"
else
    ACCOUNTS_DIR=$with_accounts_dir
fi
AC_SUBST(ACCOUNTS_DIR)
AC_DEFINE_UNQUOTED(ACCOUNTS_DIR,"$ACCOUNTS_DIR", [Directory for storing accounts])


AC_ARG_WITH(accounts_cache_dir, AS_HELP_STRING([--with-accounts-cache-dir=<path>],[Directory for account/connection mapping for crash recovery]))
if test -z "$with_accounts_cache_dir" ; then
    ACCOUNTS_CACHE_DIR=""
else
    ACCOUNTS_CACHE_DIR=$with_accounts_cache_dir
fi
AC_SUBST(ACCOUNTS_CACHE_DIR)
AC_DEFINE_UNQUOTED(ACCOUNTS_CACHE_DIR,"$ACCOUNTS_CACHE_DIR", [Directory for account/connection mapping for crash recovery])


AC_ARG_WITH(chandlers_dir, AS_HELP_STRING([--with-chandlers-dir=<path>],[Directory for channel handlers]))
if test -z "$with_chandlers_dir" ; then
    CHANDLERS_DIR="telepathy/managers"
else
    CHANDLERS_DIR=$with_chandlers_dir
fi
AC_SUBST(CHANDLERS_DIR)
AC_DEFINE_UNQUOTED(CHANDLERS_DIR,"$CHANDLERS_DIR", [Directory for channel handlers])

test_enabled="yes"
AC_MSG_CHECKING(whether to build tests)
AC_ARG_ENABLE(tests,
	[  --disable-tests           don't build tests. default: enable them],
	[
		AC_MSG_RESULT(${enableval})
		test_enabled="${enableval}"
	],
	[
		AC_MSG_RESULT([yes (default)])
		test_enabled="yes"
	]
)
AM_CONDITIONAL(HAVE_TESTS, [test x$test_enabled = xyes])

examples_enabled="yes"
AC_MSG_CHECKING(whether to build examples)
AC_ARG_ENABLE(examples,
	[  --disable-examples        don't build examples. default: build them],
	[
		AC_MSG_RESULT(${enableval})
		examples_enabled="${enableval}"
	],
	[
		AC_MSG_RESULT([yes (default)])
		examples_enabled="yes"
	]
)
AM_CONDITIONAL(HAVE_EXAMPLES, [test x$examples_enabled = xyes])

server_enabled="yes"
AC_MSG_CHECKING(whether to build the daemon)
AC_ARG_ENABLE(server,
	[  --enable-server         build server. default=yes],
	[
		AC_MSG_RESULT(${enableval})
		server_enabled="${enableval}"
	],
	[
		AC_MSG_RESULT(yes)
		server_enabled="yes"
	]
)
AM_CONDITIONAL(HAVE_SERVER, [test x$server_enabled = xyes])

ENABLE_MC_CLIENT_SO6="no"
AC_MSG_CHECKING(whether to pretend libmcclient is stable)
AC_ARG_ENABLE(mcclient-so6,
	[  --enable-mcclient-so6    pretend libmcclient is ABI-stable],
	[
		AC_MSG_RESULT(${enableval})
		ENABLE_MC_CLIENT_SO6="${enableval}"
	],
	[
		AC_MSG_RESULT(no)
		ENABLE_MC_CLIENT_SO6="no"
	]
)
AM_CONDITIONAL(ENABLE_MC_CLIENT_SO6, [test "x$ENABLE_MC_CLIENT_SO6" = xyes])

ENABLE_MC_SERVER_SO6="no"
AC_MSG_CHECKING(whether to pretend libmissioncontrol-server is stable)
AC_ARG_ENABLE(mcserver-so6,
	[  --enable-mcserver-so6    pretend libmissioncontrol-server is ABI-stable],
	[
		AC_MSG_RESULT(${enableval})
		ENABLE_MC_SERVER_SO6="${enableval}"
	],
	[
		AC_MSG_RESULT(no)
		ENABLE_MC_SERVER_SO6="no"
	]
)
AM_CONDITIONAL(ENABLE_MC_SERVER_SO6, [test "x$ENABLE_MC_SERVER_SO6" = xyes])

plugins_enabled="no"
AC_MSG_CHECKING(whether to support plugins)
AC_ARG_ENABLE(plugins,
	[  --enable-plugins          support plugins. default=no],
	[
		AC_MSG_RESULT(${enableval})
		plugins_enabled="${enableval}"
	],
	[
		AC_MSG_RESULT(no)
		plugins_enabled="no"
	]
)
AM_CONDITIONAL(ENABLE_PLUGINS, [test x$plugins_enabled = xyes])
if test "x$plugins_enabled" = xyes
then
  AC_DEFINE([ENABLE_PLUGINS], [1], [Define if plugins should be enabled])
fi

keyring_enabled="no"
AC_MSG_CHECKING(whether to build with gnome-keyring support)
AC_ARG_ENABLE(gnome-keyring,
        [ --enable-gnome-keyring    build with gnome-keyring support. default=no],
        [
                AC_MSG_RESULT(${enableval})
                keyring_enabled="${enableval}"
        ],
        [
                AC_MSG_RESULT(no)
                keyring_enabled="no"
        ]
)

PKG_PROG_PKG_CONFIG()

if test "x$keyring_enabled" = xauto
then
    PKG_CHECK_EXISTS([gnome-keyring-1], [keyring_enabled=yes],
                     [keyring_enabled=no])
fi

AM_CONDITIONAL(ENABLE_GNOME_KEYRING, [test x$keyring_enabled = xyes])

if test "x$keyring_enabled" = xyes
then
  PKG_CHECK_MODULES([GNOME_KEYRING], [gnome-keyring-1])
  AC_SUBST([GNOME_KEYRING_LIBS])
  AC_SUBST([GNOME_KEYRING_CFLAGS])
  AC_DEFINE([ENABLE_GNOME_KEYRING], [1], [Define whether gnome-keyring support is enabled])
else
  AC_DEFINE([ENABLE_GNOME_KEYRING], [0], [Define whether gnome-keyring support is enabled])
fi

PKG_CHECK_MODULES(DBUS, [dbus-1 >= 0.51, dbus-glib-1 >= 0.51], have_dbus=yes, have_dbus=no)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(TELEPATHY, telepathy-glib >= 0.7.37)
AC_SUBST(TELEPATHY_LIBS)
AC_SUBST(TELEPATHY_CFLAGS)

GCONF_REQUIRED_VERSION=2.0.0
PKG_CHECK_MODULES(GCONF, gconf-2.0 >= $GCONF_REQUIRED_VERSION,
		  [HAVE_GCONF=yes], [HAVE_GCONF=no])
AM_CONDITIONAL(HAVE_GCONF, [test x$HAVE_GCONF = xyes])
AC_SUBST(GCONF_CFLAGS)
AC_SUBST(GCONF_LIBS)

PKG_CHECK_MODULES(GLIB, glib-2.0 gobject-2.0)
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB_CFLAGS)

if test "x$plugins_enabled" = xyes
then
  PKG_CHECK_MODULES(GMODULE, gmodule-no-export-2.0)
  AC_SUBST(GMODULE_LIBS)
  AC_SUBST(GMODULE_CFLAGS)
fi

dnl docs/Makefile.am needs to know whether it's an out of tree build
dnl (srcdir != builddir)
AM_CONDITIONAL([OUT_OF_TREE_BUILD], [test "z$ac_srcdir" != z.])

dnl ***************************************************************************
dnl Check for marshal and enum generators
dnl ***************************************************************************
GLIB_GENMARSHAL="`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`"
AC_SUBST(GLIB_GENMARSHAL)
GLIB_MKENUMS="`$PKG_CONFIG --variable=glib_mkenums glib-2.0`"
AC_SUBST(GLIB_MKENUMS)

GTK_DOC_CHECK([1.3])

pluginlibdir=$libdir/mission-control
AC_SUBST(pluginlibdir)

AC_OUTPUT([
Makefile \
doc/Makefile \
doc/reference/Makefile \
doc/reference/libmcclient/Makefile \
doc/reference/libmissioncontrol-server/Makefile \
examples/Makefile \
libmcclient.pc \
libmcclient/Makefile \
m4/Makefile \
mission-control.pc \
server/Makefile \
src/Makefile \
test/Makefile \
test/twisted/Makefile \
test/twisted/tools/Makefile \
tools/Makefile \
util/Makefile \
xml/Makefile \
])
