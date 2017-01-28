dnl Set the version number to e.g. 5.x.y immediately before a release.
dnl Set the version number to e.g. 5.x.y+ immediately after (this will
dnl enable -Werror).
AC_INIT([telepathy-mission-control], [5.16.99+])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR(Makefile.am)
AM_INIT_AUTOMAKE([1.9 tar-ustar -Wno-portability subdir-objects])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])
AC_CONFIG_HEADERS(config.h)

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_MKDIR_P

AC_HEADER_STDC
AC_CHECK_HEADERS([sys/stat.h sys/types.h sysexits.h])
AC_CHECK_FUNCS([umask])

case "$PACKAGE_VERSION" in
  *+)
    mc_is_release=no
    ;;
  *)
    mc_is_release=yes
    ;;
esac

# If API has been removed or changed since last release, change MCP_API (which
# is part of the directory name under /usr/include) to the version that
# changed it
MCP_API_VERSION=5.18
AC_SUBST([MCP_API_VERSION])

# If ABI has been removed or changed since last release
#     increment current
#     set age and revision to 0
# else if ABI has been added since last release
#     increment age and current
#     set revision to 0
# else if library source has changed since last release
#     increment revision
MCP_LT_CURRENT=6
MCP_LT_REVISION=0
MCP_LT_AGE=0
AC_SUBST([MCP_LT_CURRENT])
AC_SUBST([MCP_LT_REVISION])
AC_SUBST([MCP_LT_AGE])
# The ABI version appears in the name of the plugin directory
MCP_ABI_VERSION=`expr ${MCP_LT_CURRENT} - ${MCP_LT_AGE}`
AC_SUBST([MCP_ABI_VERSION])

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

XSLTPROC=
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
    AC_MSG_ERROR([xsltproc (from the libxslt source package) is required])
fi

AM_PATH_PYTHON([2.6])
AC_MSG_CHECKING([for enough Python for the tests])
mc_want_twisted_tests=no
if $PYTHON -c "import twisted.internet.reactor, dbus.mainloop.glib"\
    >/dev/null 2>&1; then
    mc_want_twisted_tests=yes
fi
AC_MSG_RESULT([$mc_want_twisted_tests])
AM_CONDITIONAL([WANT_TWISTED_TESTS], test yes = "$mc_want_twisted_tests")

AC_ARG_ENABLE(debug,            [  --disable-debug               compile without debug code],[enable_debug=${enableval}], enable_debug=yes )
AS_IF([test "x$enable_debug" = "xyes"],
    [AC_DEFINE([ENABLE_DEBUG], [1], [Define to compile in debug messages])])

AC_ARG_ENABLE(cast-checks,      [  --disable-cast-checks         compile with GLIB cast checks disabled],[cchecks=${enableval}],cchecks=yes)
AS_IF([test "x$cchecks" = "xno"],
    [AC_DEFINE([G_DISABLE_CAST_CHECKS], [1],
        [Define to disable checked casts (not recommended)])])

AC_ARG_ENABLE(asserts,      [  --disable-asserts        compile with GLIB assertions disabled],[asserts=${enableval}],asserts=yes)
AS_IF([test "x$asserts" = "xno"],
    [AC_DEFINE([G_DISABLE_ASSERTS], [1],
        [Define to disable assertions (not recommended)])])

AC_ARG_ENABLE(checks,       [  --disable-checks         compile with GLIB checks disabled],[checks=${enableval}],checks=yes)
AS_IF([test "x$checks" = "xno"],
    [AC_DEFINE([G_DISABLE_CHECKS], [1],
        [Define to disable checks (not recommended)])])

AC_ARG_ENABLE(coverage,       [  --enable-coverage         compile with coverage info],[enable_coverage=${enableval}],enable_coverage=no)
if test "x$enable_coverage" = "xyes"; then
       CFLAGS="$CFLAGS -g -fprofile-arcs -ftest-coverage"
fi

AC_ARG_ENABLE([installed-tests],
  AC_HELP_STRING([--enable-installed-tests],
    [make tests installable]),
    [installed_tests=$enableval], [installed_tests=no])

if test x$installed_tests = xyes; then
  AC_DEFINE(ENABLE_INSTALLED_TESTS, [],
      [Make tests installable])
fi
AM_CONDITIONAL([ENABLE_INSTALLED_TESTS], [test "x$installed_tests" = xyes])

mctestsdir=${libdir}/telepathy-mission-control-tests
AC_SUBST(mctestsdir)

AC_ARG_WITH([accounts_dir], [AS_HELP_STRING([--with-accounts-dir=<path>],[Directory in which accounts were previously stored])])
if test -z "$with_accounts_dir" ; then
    ACCOUNTS_DIR="~/.mission-control/accounts"
else
    ACCOUNTS_DIR=$with_accounts_dir
fi
AC_SUBST(ACCOUNTS_DIR)
AC_DEFINE_UNQUOTED([ACCOUNTS_DIR], ["$ACCOUNTS_DIR"], [Directory in which accounts were previously stored])


AC_ARG_WITH(accounts_cache_dir, AS_HELP_STRING([--with-accounts-cache-dir=<path>],[Directory for account/connection mapping for crash recovery]))
if test -z "$with_accounts_cache_dir" ; then
    ACCOUNTS_CACHE_DIR=""
else
    ACCOUNTS_CACHE_DIR=$with_accounts_cache_dir
fi
AC_SUBST(ACCOUNTS_CACHE_DIR)
AC_DEFINE_UNQUOTED(ACCOUNTS_CACHE_DIR,"$ACCOUNTS_CACHE_DIR", [Directory for account/connection mapping for crash recovery])

PKG_PROG_PKG_CONFIG()

PKG_CHECK_MODULES(DBUS, [dbus-1 >= 0.95, dbus-glib-1 >= 0.82])
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES([TELEPATHY], [telepathy-glib >= 0.23.1])
AC_DEFINE([TP_VERSION_MIN_REQUIRED], [TP_VERSION_0_24],
  [Ignore post-0.24 deprecations])
AC_DEFINE([TP_VERSION_MAX_ALLOWED], [TP_VERSION_0_24],
  [Prevent post-0.24 APIs])
AC_DEFINE([TP_SEAL_ENABLE], [], [Define to hide deprecated struct fields])
AC_DEFINE([TP_DISABLE_SINGLE_INCLUDE], [], [Avoid individual headers])

PKG_CHECK_MODULES([GLIB],
	[glib-2.0 >= 2.46, gobject-2.0, gmodule-no-export-2.0, gio-2.0])

PKG_CHECK_MODULES([GIO_UNIX], [gio-unix-2.0],
  [
  AC_DEFINE(HAVE_GIO_UNIX, [], [Define if GIO-Unix is available])
  GLIB_CFLAGS="$GLIB_CFLAGS $GIO_UNIX_CFLAGS"
  GLIB_LIBS="$GLIB_LIBS $GIO_UNIX_LIBS"
  ],
  [])

AC_DEFINE([GLIB_VERSION_MIN_REQUIRED], [GLIB_VERSION_2_46], [Ignore post 2.46 deprecations])
AC_DEFINE([GLIB_VERSION_MAX_ALLOWED], [GLIB_VERSION_2_46], [Prevent post 2.46 APIs])

# -----------------------------------------------------------
# Connectivity integration
# -----------------------------------------------------------
AC_ARG_WITH(connectivity,
              AS_HELP_STRING([--with-connectivity=@<:@nm/auto/no@:>@],
                             [build with connectivity support]), ,
                             with_connectivity=auto)

# --with-connectivity=connman always used to turn off NM support,
# so treat it as "no" now that ConnMan doesn't need anything special.
if test "x$with_connectivity" = "xno" || test "x$with_connectivity" = "xconnman"; then
   have_nm=no
else
   PKG_CHECK_MODULES(NETWORK_MANAGER,
   [
      libnm
   ], have_nm="yes", have_nm="no")

   if test "x$have_nm" = "xyes"; then
      AC_DEFINE(HAVE_NM, 1, [Define if you have libnm-glib])
   fi
fi

if test "x$with_connectivity" = "xnm" -a "x$have_nm" != "xyes"; then
   AC_MSG_ERROR([Couldn't find Network Manager dependencies:

$NETWORK_MANAGER_PKG_ERRORS])
fi

AM_CONDITIONAL(HAVE_NM, test "x$have_nm" = "xyes")

AC_ARG_ENABLE([conn-setting],
    [AS_HELP_STRING([--enable-conn-setting],
         [use a GSetting to control automatic connectivity @<:@default=yes@:>@])],
    [],
    [enable_conn_setting=yes])
if test x"$enable_conn_setting" = xyes; then
  AC_DEFINE([ENABLE_CONN_SETTING], [1], [Define to use the "use-conn" GSetting])
fi
AM_CONDITIONAL([ENABLE_CONN_SETTING], [test x"$enable_conn_setting" = xyes])

dnl ***************************************************************************
dnl Check for marshal and enum generators
dnl ***************************************************************************
GLIB_GENMARSHAL="`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`"
AC_SUBST(GLIB_GENMARSHAL)
GLIB_MKENUMS="`$PKG_CONFIG --variable=glib_mkenums glib-2.0`"
AC_SUBST(GLIB_MKENUMS)

GTK_DOC_CHECK([1.17],[--flavour no-tmpl])
GLIB_GSETTINGS

pluginlibdir=$libdir/mission-control
AC_SUBST(pluginlibdir)

AC_OUTPUT([
Makefile \
data/Makefile \
doc/Makefile \
doc/reference/Makefile \
doc/reference/mission-control-plugins/Makefile \
m4/Makefile \
mission-control-plugins.pc \
mission-control-plugins-uninstalled.pc \
mission-control-plugins/Makefile \
plugins/Makefile \
server/Makefile \
src/Makefile \
tests/Makefile \
tests/twisted/Makefile \
tests/twisted/tools/Makefile \
tools/Makefile \
util/Makefile \
])

echo "
Configure summary:

        Compiler.....................:  ${CC}
        Compiler Flags...............:  ${CFLAGS} ${ERROR_CFLAGS}
        Linker Flags.................:  ${LDFLAGS}
        Prefix.......................:  ${prefix}
        Enable debug.................:  ${enable_debug}
        Python tests.................:  ${mc_want_twisted_tests}
        Install unit tests...........:  ${installed_tests}

    Options:
        Account storage directory....:  ${ACCOUNTS_DIR}
        Crash recovery directory.....:  ${ACCOUNTS_CACHE_DIR:-\$XDG_CACHE_HOME}

    Features:
        Plugin API documentation.....:  ${enable_gtk_doc}
        Network Manager integration..:  ${have_nm}
        Connectivity GSetting........:  ${enable_conn_setting}
"
