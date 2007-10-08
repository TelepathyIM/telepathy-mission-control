AC_INIT(telepathy-mission-control, 4.47)
AC_CONFIG_SRCDIR(Makefile.am)
AM_INIT_AUTOMAKE([tar-ustar])
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LIBTOOL

AC_HEADER_STDC

AM_CFLAGS="$CFLAGS -Wall -fno-strict-aliasing -ansi -DDMALLOC"

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


AC_ARG_WITH(chandlers_dir, AS_HELP_STRING([--with-chandlers-dir=<path>],[Directory for channel handlers]))
if test -z "$with_chandlers_dir" ; then
    CHANDLERS_DIR="telepathy/managers"
else
    CHANDLERS_DIR=$with_chandlers_dir
fi
AC_SUBST(CHANDLERS_DIR)
AC_DEFINE_UNQUOTED(CHANDLERS_DIR,"$CHANDLERS_DIR", [Directory for channel handlers])

test_enabled="no"
AC_MSG_CHECKING(whether to build tests)
AC_ARG_ENABLE(tests,
	[  --enable-tests           build tests. default=no],
	[
		AC_MSG_RESULT(${enableval})
		test_enabled="${enableval}"
	],
	[
		AC_MSG_RESULT(no)
		test_enabled="no"
	]
)
AM_CONDITIONAL(HAVE_TESTS, [test x$test_enabled = xyes])

server_enabled="yes"
AC_MSG_CHECKING(whether to build the sample server)
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

PKG_CHECK_MODULES(DBUS, [dbus-1 >= 0.51, dbus-glib-1 >= 0.51], have_dbus=yes, have_dbus=no)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(TELEPATHY, libtelepathy >= 0.0.54)
AC_SUBST(TELEPATHY_LIBS)
AC_SUBST(TELEPATHY_CFLAGS)

GCONF_REQUIRED_VERSION=2.0.0
PKG_CHECK_MODULES(GCONF, gconf-2.0 >= $GCONF_REQUIRED_VERSION)
AC_SUBST(GCONF_CFLAGS)
AC_SUBST(GCONF_LIBS)

PKG_CHECK_MODULES(GLIB, glib-2.0)
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB_CFLAGS)

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
mission-control.pc \
libmissioncontrol.pc \
src/Makefile \
xml/Makefile \
doc/Makefile \
test/Makefile \
tools/Makefile \
doc/reference/Makefile \
doc/reference/libmissioncontrol/Makefile \
doc/reference/libmissioncontrol-server/Makefile \
libmissioncontrol/Makefile \
server/Makefile \
server/org.freedesktop.Telepathy.MissionControl.service
])
