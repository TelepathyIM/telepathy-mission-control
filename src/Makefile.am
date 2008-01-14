INCLUDES = $(GCONF_CFLAGS) $(TELEPATHY_CFLAGS) \
	   -DMCD_DEFAULT_FILTER_PLUGIN_DIR=\"@pluginlibdir@\" \
	   -DDBUS_API_SUBJECT_TO_CHANGE \
	   -DMC_DISABLE_DEPRECATED -I$(top_srcdir)

mission_control_includedir = $(includedir)/mission-control
mission_control_include = \
	mcd-debug.h \
	mcd-mission.h \
	mcd-operation.h \
	mcd-master.h \
	mcd-controller.h \
	mcd-manager.h \
	mcd-connection.h \
	mcd-presence-frame.h \
	mcd-channel.h \
	mcd-proxy.h \
	mcd-dispatcher.h \
	mcd-dispatcher-context.h \
	mcd-service.h \
	mcd-chan-handler.h \
	mcd-provisioning.h \
	mcd-provisioning-factory.h

mission_control_include_HEADERS = \
	$(mission_control_include) \
	mcd-enum-types.h

BUILT_SOURCES = \
	mcd-service-gen.h \
	mcd-signals-marshal.h \
	mcd-signals-marshal.c \
	mcd-enum-types.c \
	mcd-enum-types.h \
	stamp-mcd-enum-types.h
 
CLEANFILES = $(BUILT_SOURCES)

lib_LTLIBRARIES = libmissioncontrol-server.la

libmissioncontrol_server_la_LIBADD = $(GCONF_LIBS) $(TELEPATHY_LIBS) \
	$(top_builddir)/libmissioncontrol/libmissioncontrol-client.la

# we want to export symbols so that the plugins can see them
libmissioncontrol_server_la_LDFLAGS = -export-dynamic \
				      -version-info 3:6:2

libmissioncontrol_server_la_SOURCES = \
	mcd-debug.c \
	mcd-enum-types.c \
	mcd-signals-marshal.c \
	mcd-mission.c \
	mcd-operation.c \
	mcd-controller.c \
	mcd-master.c \
	mcd-manager.c \
	mcd-connection.c \
	mcd-presence-frame.c \
	mcd-dispatcher.c \
	mcd-channel.c \
	mcd-service.c \
	mcd-proxy.c \
	mcd-chan-handler.c \
	mcd-provisioning.c \
	mcd-provisioning-factory.c 

# bin_PROGRAMS = mission-control
# mission_control_LDFLAGS = -export-dynamic
# 
# mission_control_LDADD = $(GCONF_LIBS) $(TELEPATHY_LIBS) \
# 		       	$(top_builddir)/libmissioncontrol/libmissioncontrol-config.la \
# 		       	libmissioncontrol-server.la
#
# mission_control_SOURCES = \
# 	mcd-main.c

%-signals-marshal.h: %-signals-marshal.list Makefile
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.h

%-signals-marshal.c: %-signals-marshal.list Makefile
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.c

mcd-enum-types.h: stamp-mcd-enum-types.h
	@true
stamp-mcd-enum-types.h: Makefile $(mission_control_include) mcd-enum-types.c
	( cd $(srcdir) && glib-mkenums \
		--fhead "#ifndef __MCD_ENUM_TYPES_H__\n#define __MCD_ENUM_TYPES_H__\n\n#include \"mcd-mission.h\"\n#include \"mcd-channel.h\"\n#include \"mcd-provisioning.h\"\n\nG_BEGIN_DECLS\n" \
		--fprod "/* enumerations from \"@filename@\" */\n" \
		--vhead "GType @enum_name@_get_type (void) G_GNUC_CONST;\n#define MCD_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n" \
		--ftail "G_END_DECLS\n\n#endif /* __MCD_ENUM_TYPES_H__ */" \
	    $(mission_control_include) ) >> xgen-geth \
	&& (cmp -s xgen-geth mcd-enum-types.h || cp xgen-geth mcd-enum-types.h ) \
	&& rm -f xgen-geth \
	&& echo timestamp > $(@F)

mcd-enum-types.c: Makefile
	( cd $(srcdir) && glib-mkenums \
		--fhead "#include \"mcd-enum-types.h\"\n#define g_intern_static_string(s) (s)\n" \
		--fprod "\n/* enumerations from \"@filename@\" */" \
		--ftail "\n#define __MCD_ENUM_TYPES_C__\n" \
		--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {" \
		--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
		--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (g_intern_static_string (\"@EnumName@\"), values);\n  }\n  return etype;\n}\n" \
	    $(mission_control_include) ) > xgen-getc \
	&& cp xgen-getc mcd-enum-types.c  \
	&& rm -f xgen-getc


mcd-service-gen.h: $(top_builddir)/xml/MissionControl-gen.xml
	dbus-binding-tool --mode=glib-server --prefix=mcd_service $< > $@

EXTRA_DIST = mcd-signals-marshal.list stamp-mcd-enum-types.h
