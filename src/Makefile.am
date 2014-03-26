AM_CPPFLAGS = \
	   $(TELEPATHY_CFLAGS) \
	   $(DBUS_CFLAGS) \
	   $(GLIB_CFLAGS) \
	   $(NETWORK_MANAGER_CFLAGS) \
	   -DG_LOG_DOMAIN=\"mcd\" \
	   -DMCD_PLUGIN_LOADER_DIR=\"@libdir@/mission-control-plugins-6-@MCP_ABI_VERSION@\" \
	   -DMC_DISABLE_DEPRECATED -I$(top_srcdir)

AM_CFLAGS = $(ERROR_CFLAGS)

mc_headers = \
	mcd-account.h \
	mcd-account-manager.h \
	mcd-account-manager-default.h \
	mcd-debug.h \
	mcd-mission.h \
	mcd-operation.h \
	mcd-master.h \
	mcd-manager.h \
	mcd-connection.h \
	mcd-connection-service-points.h \
	mcd-channel.h \
	mcd-dispatcher.h \
	mcd-service.h \
	mcd-storage.h

nodist_libmcd_convenience_la_SOURCES = \
	mcd-enum-types.c \
	mcd-enum-types.h \
	$(NULL)

BUILT_SOURCES = \
	stamp-mcd-enum-types.h \
	$(nodist_libmcd_convenience_la_SOURCES)

CLEANFILES = \
	$(BUILT_SOURCES) \
	$(NULL)

libmcd_convenience_la_LIBADD = \
	$(top_builddir)/mission-control-plugins/libmission-control-plugins-6.la \
	$(TELEPATHY_LIBS) \
	$(DBUS_LIBS) \
	$(GLIB_LIBS) \
	$(NETWORK_MANAGER_LIBS) \
	$(NULL)

noinst_LTLIBRARIES = libmcd-convenience.la

libmcd_convenience_la_SOURCES = \
	mcd-account.c \
	mcd-account-addressing.h \
	mcd-account-config.h \
	mcd-account-requests.c \
	mcd-account-addressing.c \
	mcd-account-manager.c \
	mcd-account-manager-priv.h \
	mcd-account-manager-default.c \
	mcd-account-priv.h \
	mcd-client.c \
	mcd-client-priv.h \
	channel-utils.c \
	channel-utils.h \
	client-registry.c \
	client-registry.h \
	connectivity-monitor.c \
	connectivity-monitor.h \
	mcd-dbusprop.c \
	mcd-dbusprop.h \
	mcd-debug.c \
	mcd-dispatch-operation.c \
	mcd-dispatch-operation-priv.h \
	mcd-handler-map.c \
	mcd-handler-map-priv.h \
	mcd-misc.c \
	mcd-misc.h \
	mcd-mission.c \
	mcd-mission-priv.h \
	mcd-operation.c \
	mcd-master.c \
	mcd-master-priv.h \
	mcd-manager.c \
	mcd-manager-priv.h \
	mcd-connection.c \
	mcd-connection-service-points.c \
	mcd-connection-priv.h \
	mcd-dispatcher.c \
	mcd-dispatcher-priv.h \
	mcd-channel.c \
	mcd-channel-priv.h \
	mcd-service.c \
	mcd-slacker.c \
	mcd-slacker.h \
	mcd-storage.c \
	mcd-storage.h \
	plugin-dispatch-operation.c \
	plugin-dispatch-operation.h \
	plugin-loader.c \
	plugin-loader.h \
	plugin-request.c \
	plugin-request.h \
	request.c \
	request.h \
	sp_timestamp.h \
	$(mc_headers)

mcd-enum-types.h: stamp-mcd-enum-types.h
	$(AM_V_GEN)true
stamp-mcd-enum-types.h: Makefile $(mc_headers) mcd-enum-types.c
	$(AM_V_GEN)( cd $(srcdir) && glib-mkenums \
		--fhead "#ifndef __MCD_ENUM_TYPES_H__\n#define __MCD_ENUM_TYPES_H__\n\n#include \"mcd-mission.h\"\n#include \"mcd-channel.h\"\n\nG_BEGIN_DECLS\n" \
		--fprod "/* enumerations from \"@filename@\" */\n" \
		--vhead "GType @enum_name@_get_type (void) G_GNUC_CONST;\n#define MCD_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n" \
		--ftail "G_END_DECLS\n\n#endif /* __MCD_ENUM_TYPES_H__ */" \
	    $(mc_headers) ) >> xgen-geth \
	&& (cmp -s xgen-geth mcd-enum-types.h || cp xgen-geth mcd-enum-types.h ) \
	&& rm -f xgen-geth \
	&& echo timestamp > $(@F)

mcd-enum-types.c: Makefile $(mc_headers)
	$(AM_V_GEN)( cd $(srcdir) && glib-mkenums \
		--fhead "#include \"mcd-enum-types.h\"\n#define g_intern_static_string(s) (s)\n" \
		--fprod "\n/* enumerations from \"@filename@\" */" \
		--ftail "\n#define __MCD_ENUM_TYPES_C__\n" \
		--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {" \
		--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
		--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (g_intern_static_string (\"@EnumName@\"), values);\n  }\n  return etype;\n}\n" \
	    $(mc_headers) ) > xgen-getc \
	&& cp xgen-getc mcd-enum-types.c  \
	&& rm -f xgen-getc

EXTRA_DIST = \
	stamp-mcd-enum-types.h

Android.mk: Makefile.am $(nodist_libmcd_convenience_la_SOURCES)
	androgenizer  -:PROJECT telepathy-mission-control \
	 -:STATIC libmcd-convenience \
	 -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libmcd_convenience_la_SOURCES) $(nodist_libmcd_convenience_la_SOURCES) \
	 -:CFLAGS $(DEFAULT_INCLUDES) $(DEFS) $(CFLAGS) $(AM_CFLAGS) \
	 -:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) \
	 -:LDFLAGS $(libmcd_convenience_la_LIBADD) \
	> $@

include ../tools/header-checks.am

check-local: header-decl-macro-check
