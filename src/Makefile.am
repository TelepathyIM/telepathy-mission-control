INCLUDES = \
	   $(TELEPATHY_CFLAGS) \
	   $(DBUS_CFLAGS) \
	   $(GLIB_CFLAGS) \
	   -DG_LOG_DOMAIN=\"mcd\" \
	   -DMCD_PLUGIN_LOADER_DIR=\"@libdir@/mission-control-plugins.@MCP_ABI_VERSION@\" \
	   -DMC_DISABLE_DEPRECATED -I$(top_srcdir)

AM_CFLAGS = $(ERROR_CFLAGS)

if ENABLE_GNOME_KEYRING
INCLUDES += $(GNOME_KEYRING_CFLAGS)
endif

mc_headers = \
	mcd-account.h \
	mcd-account-compat.h \
	mcd-account-conditions.h \
	mcd-account-manager.h \
	mcd-account-manager-default.h \
	mcd-debug.h \
	mcd-mission.h \
	mcd-operation.h \
	mcd-master.h \
	mcd-controller.h \
	mcd-manager.h \
	mcd-connection.h \
	mcd-connection-plugin.h \
	mcd-connection-service-points.h \
	mcd-plugin.h \
	mcd-channel.h \
	mcd-proxy.h \
	mcd-dispatcher.h \
	mcd-dispatcher-context.h \
	mcd-service.h \
	mcd-transport.h \
	mcd-provisioning.h \
	mcd-provisioning-factory.h \
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
	_gen/cli-Connection_Interface_Contact_Capabilities_Draft1.h \
	_gen/cli-Connection_Interface_Contact_Capabilities.h \
	_gen/cli-Connection_Interface_Power_Saving.h \
	_gen/enums.h \
	_gen/interfaces.h \
	_gen/svc-Account_Interface_Addressing.h \
	_gen/svc-Account_Interface_ChannelRequests.h \
	_gen/svc-Account_Interface_Compat.h \
	_gen/svc-Account_Interface_Conditions.h \
	_gen/svc-Account_Interface_External_Password_Storage.h \
	_gen/svc-Account_Interface_Hidden.h \
	_gen/svc-Account_Interface_Stats.h \
	_gen/svc-Account_Manager_Interface_Hidden.h \
	_gen/svc-Account_Manager_Interface_Query.h \
	_gen/svc-dispatcher.h \
	_gen/svc-request.h

nodist_libmcd_convenience_la_SOURCES = \
	_gen/cli-Connection_Manager_Interface_Account_Storage-body.h \
	_gen/cli-Connection_Interface_Contact_Capabilities_Draft1-body.h \
	_gen/cli-Connection_Interface_Contact_Capabilities-body.h \
	_gen/cli-Connection_Interface_Power_Saving-body.h \
	_gen/register-dbus-glib-marshallers-body.h \
	_gen/signals-marshal.c \
	_gen/signals-marshal.h \
	_gen/signals-marshal.list \
	_gen/svc-Account_Interface_Addressing.c \
	_gen/svc-Account_Interface_ChannelRequests.c \
	_gen/svc-Account_Interface_Compat.c \
	_gen/svc-Account_Interface_Conditions.c \
	_gen/svc-Account_Interface_External_Password_Storage.c \
	_gen/svc-Account_Interface_Hidden.c \
	_gen/svc-Account_Interface_Stats.c \
	_gen/svc-Account_Manager_Interface_Query.c \
	_gen/svc-Account_Manager_Interface_Hidden.c \
	_gen/svc-dispatcher.c \
	_gen/svc-request.c \
	mcd-enum-types.c \
	mcd-enum-types.h \
	mcd-signals-marshal.c \
	mcd-signals-marshal.h \
	$(mc_gen_headers)

BUILT_SOURCES = \
	_gen/mcd.xml \
	stamp-mcd-enum-types.h \
	$(nodist_libmcd_convenience_la_SOURCES)

CLEANFILES = $(BUILT_SOURCES)

libmcd_convenience_la_LIBADD = \
	$(top_builddir)/libmcclient/libmcclient.la \
	$(top_builddir)/mission-control-plugins/libmission-control-plugins.la \
	$(TELEPATHY_LIBS) \
	$(DBUS_LIBS) \
	$(GLIB_LIBS)

if ENABLE_GNOME_KEYRING
libmcd_convenience_la_LIBADD += $(GNOME_KEYRING_LIBS)
endif

if ENABLE_LIBACCOUNTS_SSO
libmcd_convenience_la_LIBADD += $(LIBACCOUNTS_SSO_LIBS)
INCLUDES += $(LIBACCOUNTS_SSO_CFLAGS)
endif

noinst_LTLIBRARIES = libmcd-convenience.la

if ENABLE_MCD_PLUGINS
# When this older plugin API is enabled, libmissioncontrol-server.la is a
# shared library that wraps the convenience library
lib_LTLIBRARIES = libmissioncontrol-server.la
libmissioncontrol_server_la_SOURCES =
libmissioncontrol_server_la_LIBADD = libmcd-convenience.la

libmissioncontrol_server_la_LDFLAGS = \
	-export-symbols-regex '^((mc_)|(mcd_)|(mission_control_))'

if ENABLE_MC_SERVER_SO6
# the redundant quoting here is to prevent the libtool command line from
# looking like an error message in an oddly named file
libmissioncontrol_server_la_LDFLAGS += -version-info "10":"0":"4"
else
libmissioncontrol_server_la_LDFLAGS += -release $(VERSION)
endif

mission_control_includedir = $(includedir)/mission-control
mission_control_include_HEADERS = $(mc_headers)

genincludedir = $(mission_control_includedir)/_gen
nodist_geninclude_HEADERS = $(mc_gen_headers)

INCLUDES += \
	-DMCD_DEFAULT_FILTER_PLUGIN_DIR=\"@pluginlibdir@\"
else # ! ENABLE_MCD_PLUGINS
# When this older plugin API is not enabled, libmissioncontrol-server.la is
# just a copy of the convenience library
noinst_LTLIBRARIES += libmissioncontrol-server.la
libmissioncontrol_server_la_SOURCES =
libmissioncontrol_server_la_LIBADD = libmcd-convenience.la
endif # ! ENABLE_MCD_PLUGINS

libmcd_convenience_la_SOURCES = \
	mcd-account.c \
	mcd-account-addressing.h \
	mcd-account-compat.c \
	mcd-account-conditions.c \
	mcd-account-config.h \
	mcd-account-connection.c \
	mcd-account-requests.c \
	mcd-account-stats.c \
	mcd-account-addressing.c \
	mcd-account-manager.c \
	mcd-account-manager-priv.h \
	mcd-account-manager-query.c \
	mcd-account-manager-default.c \
	mcd-account-priv.h \
	mcd-client.c \
	mcd-client-priv.h \
	channel-utils.c \
	channel-utils.h \
	client-registry.c \
	client-registry.h \
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
	mcd-controller.c \
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
	mcd-proxy.c \
	mcd-transport.c \
	mcd-provisioning.c \
	mcd-provisioning-factory.c \
	mcd-storage.c \
	mcd-storage.h \
	mcd-storage-priv.h \
	plugin-dispatch-operation.c \
	plugin-dispatch-operation.h \
	plugin-loader.c \
	plugin-loader.h \
	plugin-request.c \
	plugin-request.h \
	plugin-account.c \
	plugin-account.h \
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

%-signals-marshal.h: %-signals-marshal.list Makefile
	glib-genmarshal --header --prefix=_$(subst -,_,$*)_marshal $< > $*-signals-marshal.h

%-signals-marshal.c: %-signals-marshal.list Makefile
	{ echo '#include "$*-signals-marshal.h"' && \
	glib-genmarshal --body --prefix=_$(subst -,_,$*)_marshal $<; } \
	> $*-signals-marshal.c

mcd-enum-types.h: stamp-mcd-enum-types.h
	@true
stamp-mcd-enum-types.h: Makefile $(mc_headers) mcd-enum-types.c
	( cd $(srcdir) && glib-mkenums \
		--fhead "#ifndef __MCD_ENUM_TYPES_H__\n#define __MCD_ENUM_TYPES_H__\n\n#include \"mcd-mission.h\"\n#include \"mcd-channel.h\"\n#include \"mcd-provisioning.h\"\n#include \"mcd-transport.h\"\n\nG_BEGIN_DECLS\n" \
		--fprod "/* enumerations from \"@filename@\" */\n" \
		--vhead "GType @enum_name@_get_type (void) G_GNUC_CONST;\n#define MCD_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n" \
		--ftail "G_END_DECLS\n\n#endif /* __MCD_ENUM_TYPES_H__ */" \
	    $(mc_headers) ) >> xgen-geth \
	&& (cmp -s xgen-geth mcd-enum-types.h || cp xgen-geth mcd-enum-types.h ) \
	&& rm -f xgen-geth \
	&& echo timestamp > $(@F)

mcd-enum-types.c: Makefile $(mc_headers)
	( cd $(srcdir) && glib-mkenums \
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
	client.xml \
	dispatcher.xml \
	dispatch-operation.xml \
	mcd-signals-marshal.list \
	request.xml \
	stamp-mcd-enum-types.h

# ---- telepathy-glib-style code generation ----

tools_dir = $(top_srcdir)/tools

_gen/mcd.xml: mcd.xml $(wildcard $(top_srcdir)/xml/*.xml)
	$(mkdir_p) _gen
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

_gen/%.xml: $(top_srcdir)/xml/%.xml $(wildcard $(top_srcdir)/xml/*.xml)
	$(mkdir_p) _gen
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

# Generated files which can be done for all "classes" at once

_gen/signals-marshal.list: _gen/mcd.xml \
	$(tools_dir)/glib-signals-marshal-gen.py
	$(PYTHON) $(tools_dir)/glib-signals-marshal-gen.py $< > $@

_gen/signals-marshal.h: _gen/signals-marshal.list Makefile.am
	$(GLIB_GENMARSHAL) --header --prefix=_mcd_ext_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list Makefile.am
	{ echo '#include "_gen/signals-marshal.h"' && \
	$(GLIB_GENMARSHAL) --body --prefix=_mcd_ext_marshal $<; } \
	> $@

_gen/register-dbus-glib-marshallers-body.h: _gen/mcd.xml \
	$(tools_dir)/glib-client-marshaller-gen.py Makefile.am
	$(PYTHON) $(tools_dir)/glib-client-marshaller-gen.py $< \
		_mcd_ext > $@

_gen/enums.h: _gen/mcd.xml $(tools_dir)/c-constants-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix mc \
		$(tools_dir)/c-constants-generator.xsl \
		$< > $@

_gen/interfaces.h: _gen/mcd.xml \
	$(tools_dir)/glib-interfaces-generator.xsl \
	$(tools_dir)/c-interfaces-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix mc \
		$(tools_dir)/glib-interfaces-generator.xsl \
		$< > $@

# Generated files which must be generated per "class".
# (Currently the only "class" is nmc4, but the new API will need "classes"
# like account, account-manager, ...)

_gen/%.xml: $(top_srcdir)/xml/%.xml $(wildcard $(top_srcdir)/xml/*.xml) Makefile.am
	$(mkdir_p) _gen
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

_gen/%.xml: %.xml $(wildcard $(top_srcdir)/xml/*.xml) Makefile.am
	$(mkdir_p) _gen
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

_gen/cli-%-body.h _gen/cli-%.h: _gen/%.xml \
	$(tools_dir)/glib-client-gen.py Makefile.am
	$(PYTHON) $(tools_dir)/glib-client-gen.py \
		--group=`echo $* | tr x- x_` \
		--iface-quark-prefix=MC_IFACE_QUARK \
		--tp-proxy-api=0.7.6 \
		$< Mc_Cli _gen/cli-$*

_gen/svc-%.c _gen/svc-%.h: _gen/%.xml \
	$(tools_dir)/glib-ginterface-gen.py Makefile.am
	$(PYTHON) $(tools_dir)/glib-ginterface-gen.py \
		--filename=_gen/svc-$* \
		--signal-marshal-prefix=_mcd_ext \
		--include='<telepathy-glib/dbus.h>' \
		--include='"_gen/signals-marshal.h"' \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		--allow-unstable \
		$< Mc_Svc_
