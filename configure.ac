dnl Set the version number to e.g. 5.x.y immediately before a release.
dnl Set the version number to e.g. 5.x.y+ immediately after (this will
dnl enable -Werror).
AC_INIT([telepathy-mission-control], [5.16.5])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR(Makefile.am)
AM_INIT_AUTOMAKE([1.9 tar-ustar -Wno-portability])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])
AC_CONFIG_HEADERS(config.h)

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AM_PROG_MKDIR_P

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
MCP_API_VERSION=5.5
AC_SUBST([MCP_API_VERSION])

# If ABI has been removed or changed since last release
#     increment current
#     set age and revision to 0
# else if ABI has been added since last release
#     increment age and current
#     set revision to 0
# else if library source has changed since last release
#     increment revision
MCP_LT_CURRENT=5
MCP_LT_REVISION=1
MCP_LT_AGE=5
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
   deprecated-declarations \
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

aegis_enabled="no"
AC_MSG_CHECKING([whether to build with Aegis (libcreds) support])
AC_ARG_ENABLE(aegis,
              AC_HELP_STRING([--enable-aegis],
                             [Aegis security framework support]),
              [
                AC_CHECK_LIB(creds, creds_init,
                             [
                               AC_DEFINE([ENABLE_AEGIS], [1],
                                         [Define if Aegis is supported])
                               AC_SUBST([AEGIS_LIBS], [-lcreds])
                             ],
                             [ AC_MSG_ERROR([Aegis: libcreds not found]) ])
                aegis_enabled="yes"
              ],
              [
                aegis_enabled="no"
                AC_MSG_RESULT(no)
                AC_DEFINE([ENABLE_AEGIS], [0], [Define if Aegis is supported])
              ])

AM_CONDITIONAL(ENABLE_AEGIS, [test x$aegis_enabled = xyes])

PKG_PROG_PKG_CONFIG()

dnl libaccounts-glib SSO
libaccounts_sso_enabled="no"
AC_MSG_CHECKING(whether to build with libaccounts-glib SSO suport)

AC_ARG_ENABLE([libaccounts-sso],
              AC_HELP_STRING([--enable-libaccounts-sso],
                             [build with SSO suport. default=no]),
              [ AC_MSG_RESULT(${enableval})
                libaccounts_sso_enabled="${enableval}" ],
              [ AC_MSG_RESULT(no) ] )

AS_IF([ test "x$libaccounts_sso_enabled" = xauto ],
      [ PKG_CHECK_EXISTS([libaccounts-glib],
                         [libaccounts_sso_enabled=yes],
                         [libaccounts_sso_enabled=no]) ])

dnl set up the shell/make variable, the #define and check the package version
AS_IF([ test "x$libaccounts_sso_enabled" = xyes ],
      [ ENABLE_LIBACCOUNTS_SSO=yes
        AC_DEFINE(ENABLE_LIBACCOUNTS_SSO, [1],
                  [Define if libaccounts SSO support is required])
        PKG_CHECK_MODULES([LIBACCOUNTS_SSO], [libaccounts-glib >= 0.26]) ],
      [ AC_DEFINE(ENABLE_LIBACCOUNTS_SSO, [0]) ])

dnl export the CFLAGS and LDFLAGS equivalents determined by PKG_CHECK_MODULES
AC_SUBST(LIBACCOUNTS_SSO_CFLAGS)
AC_SUBST(LIBACCOUNTS_SSO_LIBS)

dnl the automake conditional
AM_CONDITIONAL(ENABLE_LIBACCOUNTS_SSO, [test x$libaccounts_sso_enabled = xyes])
dnl /libaccounts-glib SSO

AC_ARG_WITH([accounts-glib-hidden-service-type],
    [AS_HELP_STRING([--with-accounts-glib-hidden-service-type],
      [mark accounts with the provided service type as hidden @<:@default=no@:>@])],
    [],
    [with_accounts_glib_hidden_service_type=no])
AS_IF([ test "x$with_accounts_glib_hidden_service_type" != xno ],
    [AC_DEFINE_UNQUOTED(ACCOUNTS_GLIB_HIDDEN_SERVICE_TYPE,
        ["$with_accounts_glib_hidden_service_type"],
        [Defined to the non-IM service type for hidden accounts, if any])])

AM_CONDITIONAL(ENABLE_LIBACCOUNTS_GLIB_HIDDEN,
    [test x$with_accounts_glib_hidden_service_type != xno])

PKG_CHECK_MODULES(DBUS, [dbus-1 >= 0.95, dbus-glib-1 >= 0.82])
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES([TELEPATHY], [telepathy-glib >= 0.20.0])
AC_DEFINE([TP_VERSION_MIN_REQUIRED], [TP_VERSION_0_18],
  [Ignore post-0.18 deprecations])
AC_DEFINE([TP_VERSION_MAX_ALLOWED], [TP_VERSION_0_20],
  [Prevent post-0.20 APIs])
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

dnl Check for MCE, a Maemo service used to determine when the device is idle.
PKG_CHECK_MODULES([MCE], mce >= 1.5, [HAVE_MCE=yes], [HAVE_MCE=no])
AM_CONDITIONAL([HAVE_MCE], [test x"$HAVE_MCE" = xyes])
if test x"$HAVE_MCE" = xyes; then
  AC_DEFINE([HAVE_MCE], [1], [Define if mce is available])
fi

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

# -----------------------------------------------------------
# Suspend/resume tracking goop
# -----------------------------------------------------------

AC_ARG_ENABLE([upower],
    [AS_HELP_STRING([--enable-upower],
        [monitor device suspending and resuming using upower-glib @<:@default=auto@:>@])],
    [],
    [enable_upower=auto])

if test "x$enable_upower" != xno; then
    PKG_CHECK_MODULES([UPOWER_GLIB], [upower-glib < 0.99],
        [AC_DEFINE([HAVE_UPOWER], [1], [Define to use upower-glib])
         have_upower=yes
        ],
        [if test "x$enable_upower" == xyes; then
            AC_MSG_ERROR([$UPOWER_GLIB_PKG_ERRORS])
         else
            have_upower=no
         fi
        ])
else
    have_upower=no
fi

AC_SUBST([UPOWER_GLIB_CFLAGS])
AC_SUBST([UPOWER_GLIB_LIBS])

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
xml/Makefile \
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
        ConnMan integration..........:  ${have_connman}
        Connectivity GSetting........:  ${enable_conn_setting}
        Suspend tracking with UPower.:  ${have_upower}
        Aegis........................:  ${aegis_enabled}
        libaccounts-glib backend.....:  ${libaccounts_sso_enabled}
        Hidden accounts-glib accounts:  ${with_accounts_glib_hidden_service_type}
        Nokia Mode Control Entity....:  ${HAVE_MCE}
"
