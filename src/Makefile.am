INCLUDES = \
	   $(TELEPATHY_CFLAGS) \
	   $(DBUS_CFLAGS) \
	   $(GLIB_CFLAGS) \
	   $(NETWORK_MANAGER_CFLAGS) \
	   $(CONNMAN_CFLAGS) \
	   $(UPOWER_GLIB_CFLAGS) \
	   -DG_LOG_DOMAIN=\"mcd\" \
	   -DMCD_PLUGIN_LOADER_DIR=\"@libdir@/mission-control-plugins.@MCP_ABI_VERSION@\" \
	   -DMC_DISABLE_DEPRECATED -I$(top_srcdir)

AM_CFLAGS = $(ERROR_CFLAGS)

mc_headers = \
	mcd-account.h \
	mcd-account-conditions.h \
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

if ENABLE_LIBACCOUNTS_SSO
mc_headers += \
	mcd-account-manager-sso.h \
	$(NULL)

if ENABLE_LIBACCOUNTS_GLIB_HIDDEN
mc_headers += \
	mcd-storage-ag-hidden.h \
	$(NULL)
endif

endif

mc_gen_headers = \
	_gen/cli-Connection_Manager_Interface_Account_Storage.h \
	_gen/enums.h \
	_gen/gtypes.h \
	_gen/interfaces.h \
	_gen/svc-Account_Interface_Conditions.h \
	_gen/svc-Account_Interface_External_Password_Storage.h \
	_gen/svc-Account_Interface_Hidden.h \
	_gen/svc-Account_Manager_Interface_Hidden.h \
	_gen/svc-dispatcher.h

nodist_libmcd_convenience_la_SOURCES = \
	_gen/cli-Connection_Manager_Interface_Account_Storage-body.h \
	_gen/gtypes-body.h \
	_gen/interfaces-body.h \
	_gen/register-dbus-glib-marshallers-body.h \
	_gen/svc-Account_Interface_Conditions.c \
	_gen/svc-Account_Interface_External_Password_Storage.c \
	_gen/svc-Account_Interface_Hidden.c \
	_gen/svc-Account_Manager_Interface_Hidden.c \
	_gen/svc-dispatcher.c \
	mcd-enum-types.c \
	mcd-enum-types.h \
	$(mc_gen_headers)

BUILT_SOURCES = \
	_gen/mcd.xml \
	stamp-mcd-enum-types.h \
	$(nodist_libmcd_convenience_la_SOURCES)

CLEANFILES = \
	$(BUILT_SOURCES) \
	_gen/cli-Connection_Manager_Interface_Account_Storage-gtk-doc.h \
	_gen/svc-dispatcher-gtk-doc.h \
	_gen/svc-Account_Interface_Hidden-gtk-doc.h \
	_gen/svc-Account_Interface_External_Password_Storage-gtk-doc.h \
	_gen/svc-Account_Interface_Conditions-gtk-doc.h \
	_gen/svc-Account_Manager_Interface_Hidden-gtk-doc.h \
	_gen/gtypes-gtk-doc.h \
	$(NULL)

libmcd_convenience_la_LIBADD = \
	$(top_builddir)/mission-control-plugins/libmission-control-plugins.la \
	$(TELEPATHY_LIBS) \
	$(DBUS_LIBS) \
	$(GLIB_LIBS) \
	$(NETWORK_MANAGER_LIBS) \
	$(CONNMAN_LIBS) \
	$(UPOWER_GLIB_LIBS) \
	$(NULL)

if ENABLE_LIBACCOUNTS_SSO
libmcd_convenience_la_LIBADD += $(LIBACCOUNTS_SSO_LIBS)
INCLUDES += $(LIBACCOUNTS_SSO_CFLAGS)
endif

if ENABLE_AEGIS
libmcd_convenience_la_LIBADD += $(top_builddir)/plugins/libmcp-aegis.la
endif

noinst_LTLIBRARIES = libmcd-convenience.la

libmcd_convenience_la_SOURCES = \
	mcd-account.c \
	mcd-account-addressing.h \
	mcd-account-conditions.c \
	mcd-account-config.h \
	mcd-account-connection.c \
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
	gtypes.c \
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

if ENABLE_LIBACCOUNTS_SSO
libmcd_convenience_la_SOURCES += \
	mcd-account-manager-sso.c \
	$(NULL)

if ENABLE_LIBACCOUNTS_GLIB_HIDDEN
libmcd_convenience_la_SOURCES += \
	mcd-storage-ag-hidden.c \
	$(NULL)
endif

endif

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
	mcd.xml \
	dispatcher.xml \
	stamp-mcd-enum-types.h

Android.mk: Makefile.am $(nodist_libmcd_convenience_la_SOURCES)
	androgenizer  -:PROJECT telepathy-mission-control \
	 -:STATIC libmcd-convenience \
	 -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libmcd_convenience_la_SOURCES) $(nodist_libmcd_convenience_la_SOURCES) \
	 -:CFLAGS $(DEFAULT_INCLUDES) $(INCLUDES) $(DEFS) $(CFLAGS) $(AM_CFLAGS) \
	 -:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) \
	 -:LDFLAGS $(libmcd_convenience_la_LIBADD) \
	> $@

# ---- telepathy-glib-style code generation ----

tools_dir = $(top_srcdir)/tools

_gen/mcd.xml: mcd.xml $(wildcard $(top_srcdir)/xml/*.xml)
	$(AM_V_at)$(mkdir_p) _gen
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

_gen/%.xml: $(top_srcdir)/xml/%.xml $(wildcard $(top_srcdir)/xml/*.xml)
	$(AM_V_at)$(mkdir_p) _gen
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

# Generated files which can be done for all "classes" at once

_gen/register-dbus-glib-marshallers-body.h: _gen/mcd.xml \
	$(tools_dir)/glib-client-marshaller-gen.py Makefile.am
	$(AM_V_GEN)$(PYTHON) $(tools_dir)/glib-client-marshaller-gen.py $< \
		_mcd_ext > $@

_gen/enums.h: _gen/mcd.xml $(tools_dir)/c-constants-generator.xsl
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix mc \
		$(tools_dir)/c-constants-generator.xsl \
		$< > $@

_gen/interfaces.h: _gen/mcd.xml \
	$(tools_dir)/glib-interfaces-generator.xsl \
	$(tools_dir)/c-interfaces-generator.xsl
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix mc \
		$(tools_dir)/glib-interfaces-generator.xsl \
		$< > $@

_gen/interfaces-body.h: _gen/mcd.xml \
	$(tools_dir)/glib-interfaces-body-generator.xsl \
	$(tools_dir)/c-interfaces-generator.xsl
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix mc \
		$(tools_dir)/glib-interfaces-body-generator.xsl \
		$< > $@

_gen/gtypes.h: _gen/gtypes-gtk-doc.h
	@: # generated as a side-effect

_gen/gtypes-body.h: _gen/gtypes-gtk-doc.h
	@: # generated as a side-effect

_gen/gtypes-gtk-doc.h: _gen/mcd.xml \
	$(top_srcdir)/tools/glib-gtypes-generator.py
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/tools/glib-gtypes-generator.py \
		$< _gen/gtypes mc



# Generated files which must be generated per "class".
# (Currently the only "class" is nmc4, but the new API will need "classes"
# like account, account-manager, ...)

_gen/%.xml: $(top_srcdir)/xml/%.xml $(wildcard $(top_srcdir)/xml/*.xml) Makefile.am
	$(AM_V_at)$(mkdir_p) _gen
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

_gen/%.xml: %.xml $(wildcard $(top_srcdir)/xml/*.xml) Makefile.am
	$(AM_V_at)$(mkdir_p) _gen
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

_gen/cli-%-body.h: _gen/cli-%-gtk-doc.h
	@: # generated as a side-effect

_gen/cli-%.h: _gen/cli-%-gtk-doc.h
	@: # generated as a side-effect

_gen/cli-%-gtk-doc.h: _gen/%.xml \
	$(tools_dir)/glib-client-gen.py Makefile.am
	$(AM_V_GEN)$(PYTHON) $(tools_dir)/glib-client-gen.py \
		--group=`echo $* | tr x- x_` \
		--iface-quark-prefix=MC_IFACE_QUARK \
		--tp-proxy-api=0.7.6 \
		$< Mc_Cli _gen/cli-$*

_gen/svc-%.c: _gen/svc-%-gtk-doc.h
	@: # generated as a side-effect

_gen/svc-%.h: _gen/svc-%-gtk-doc.h
	@: # generated as a side-effect

_gen/svc-%-gtk-doc.h: _gen/%.xml \
	$(tools_dir)/glib-ginterface-gen.py Makefile.am
	$(AM_V_GEN)$(PYTHON) $(tools_dir)/glib-ginterface-gen.py \
		--filename=_gen/svc-$* \
		--signal-marshal-prefix=_mcd_ext \
		--include='<telepathy-glib/telepathy-glib.h>' \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		--allow-unstable \
		$< Mc_Svc_

include ../tools/header-checks.am

check-local: header-decl-macro-check
