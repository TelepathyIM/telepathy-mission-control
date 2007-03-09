INCLUDES = $(DBUS_CFLAGS) $(TELEPATHY_CFLAGS) -I$(top_srcdir) \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

BUILT_SOURCES = \
	mission-control-signals-marshal.c \
	mission-control-signals-marshal.h \
	mc-enum-types.c \
	mc-enum-types.h \
	mc-client-lib-gen.h

CLEANFILES = $(BUILT_SOURCES)

lib_LTLIBRARIES = libmissioncontrol-config.la libmissioncontrol.la

libmissioncontrol_config_la_CFLAGS = $(GLIB_CFLAGS) $(GCONF_CFLAGS)
libmissioncontrol_config_la_SOURCES = \
        mc.c\
        mc-manager.c \
        mc-manager-priv.h \
        mc-protocol.c \
        mc-protocol-priv.h \
        mc-profile.c \
        mc-account.c \
        mc-account-priv.h \
        mc-account-monitor.c \
	mc-account-monitor-priv.h \
	mc-enum-types.c

libmissioncontrol_config_includedir = $(includedir)/libmissioncontrol
libmissioncontrol_config_include_DATA = \
        mc.h \
        mc-account.h \
        mc-account-monitor.h \
        mc-profile.h \
        mc-protocol.h \
        mc-manager.h

libmissioncontrol_config_la_LIBADD = $(GCONF_LIBS) $(GLIB_LIBS) $(DBUS_LIBS)

libmissioncontrol_config_la_LDFLAGS = $(common_ldflags) -export-symbols-regex "^mc_"

libmissioncontrol_la_LIBADD = $(DBUS_LIBS) $(TELEPATHY_LIBS) \
	libmissioncontrol-config.la

libmissioncontrol_la_SOURCES = \
	mission-control-signals-marshal.c \
	mission-control.c

mission_control_include = mission-control.h

mc-client-lib-gen.h: ../xml/mcd-dbus-services.xml
	dbus-binding-tool --prefix=mc-client-lib --mode=glib-client $< > $@

%-marshal.h: %-marshal.list Makefile.am
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.h

%-marshal.c: %-marshal.list Makefile.am
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.c

mc-enum-types.h: stamp-mc-enum-types.h
	@true
stamp-mc-enum-types.h: Makefile $(mission_control_include) mc-enum-types.c
	( cd $(srcdir) && glib-mkenums \
		--fhead "#ifndef __MC_ENUM_TYPES_H__\n#define __MC_ENUM_TYPES_H__\n\n#include \"mission-control.h\"\n\nG_BEGIN_DECLS\n" \
		--fprod "/* enumerations from \"@filename@\" */\n" \
		--vhead "GType @enum_name@_get_type (void) G_GNUC_CONST;\n#define MC_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n" \
		--ftail "G_END_DECLS\n\n#endif /* __MC_ENUM_TYPES_H__ */" \
	    $(mission_control_include) ) >> xgen-geth \
	&& (cmp -s xgen-geth mc-enum-types.h || cp xgen-geth mc-enum-types.h ) \
	&& rm -f xgen-geth \
	&& echo timestamp > $(@F)

mc-enum-types.c: Makefile $(mission_control_include)
	( cd $(srcdir) && glib-mkenums \
		--fhead "#include \"mc-enum-types.h\"\n#define g_intern_static_string(s) (s)\n" \
		--fprod "\n/* enumerations from \"@filename@\" */" \
		--ftail "\n#define __MC_ENUM_TYPES_C__\n" \
		--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {" \
		--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
		--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (g_intern_static_string (\"@EnumName@\"), values);\n  }\n  return etype;\n}\n" \
	    $(mission_control_include) ) > xgen-getc \
	&& cp xgen-getc mc-enum-types.c  \
	&& rm -f xgen-getc

bin_PROGRAMS = mc-account
mc_account_SOURCES = mc-account-cli.c
mc_account_LDADD = libmissioncontrol-config.la
noinst_PROGRAMS = test
test_SOURCES = test.c
test_LDADD = libmissioncontrol-config.la

missioncontrolincludeinstdir=$(includedir)/libmissioncontrol
missioncontrolincludeinst_DATA = \
	mission-control-signals-marshal.h \
	mc-client-lib-gen.h \
	mission-control.h

EXTRA_DIST = \
	$(missioncontrolincludeinst_DATA) \
	$(libmissioncontrol_config_include_DATA)
