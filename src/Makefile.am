INCLUDES = $(DBUS_CFLAGS) $(TELEPATHY_CFLAGS) \
	   -DMCD_DEFAULT_FILTER_PLUGIN_DIR=\"@pluginlibdir@\" \
	   -DDBUS_API_SUBJECT_TO_CHANGE \
	   -DMC_DISABLE_DEPRECATED -I$(top_srcdir)

mission_control_includedir = $(includedir)/mission-control
mission_control_include = \
	mcd-account.h \
	mcd-account-manager.h \
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

genincludedir = $(mission_control_includedir)/_gen
nodist_geninclude_HEADERS = \
	_gen/enums.h \
	_gen/interfaces.h \
	_gen/gtypes.h \
	_gen/svc-Account.h \
	_gen/svc-Account_Manager.h

nodist_libmissioncontrol_server_la_SOURCES = \
	_gen/gtypes-body.h \
	_gen/interfaces-body.h \
	_gen/register-dbus-glib-marshallers-body.h \
	_gen/signals-marshal.c \
	_gen/signals-marshal.h \
	_gen/signals-marshal.list \
	_gen/svc-Account.c \
	_gen/svc-Account_Manager.c


mission_control_include_HEADERS = \
	$(mission_control_include) \
	mcd-account-priv.h \
	mcd-enum-types.h

BUILT_SOURCES = \
	_gen/all.xml \
	mcd-service-gen.h \
	mcd-signals-marshal.h \
	mcd-signals-marshal.c \
	mcd-enum-types.c \
	mcd-enum-types.h \
	stamp-mcd-enum-types.h \
	$(nodist_libmissioncontrol_server_la_SOURCES) \
	$(nodist_geninclude_HEADERS)
 
CLEANFILES = $(BUILT_SOURCES)

lib_LTLIBRARIES = libmissioncontrol-server.la

libmissioncontrol_server_la_LIBADD = $(GCONF_LIBS) $(TELEPATHY_LIBS) \
	-ldl \
	$(top_builddir)/libmissioncontrol/libmissioncontrol-client.la

# we want to export symbols so that the plugins can see them
libmissioncontrol_server_la_LDFLAGS = -export-dynamic \
				      -version-info 4:0:3

libmissioncontrol_server_la_SOURCES = \
	mcd-account.c \
	mcd-account-manager.c \
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

# ---- telepathy-glib-style code generation ----

tools_dir = $(top_srcdir)/tools

_gen/%.xml: $(top_srcdir)/xml/%.xml $(wildcard $(top_srcdir)/xml/*.xml)
	$(mkdir_p) _gen
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

# Generated files which can be done for all "classes" at once

_gen/gtypes.h _gen/gtypes-body.h: _gen/all.xml \
	$(top_srcdir)/tools/glib-gtypes-generator.py
	$(PYTHON) $(top_srcdir)/tools/glib-gtypes-generator.py \
		$< _gen/gtypes mc

_gen/signals-marshal.list: _gen/all.xml \
	$(tools_dir)/glib-signals-marshal-gen.py
	$(PYTHON) $(tools_dir)/glib-signals-marshal-gen.py $< > $@

_gen/signals-marshal.h: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --header --prefix=_mc_ext_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --body --prefix=_mc_ext_marshal $< > $@

_gen/register-dbus-glib-marshallers-body.h: _gen/all.xml \
	$(tools_dir)/glib-client-marshaller-gen.py
	$(PYTHON) $(tools_dir)/glib-client-marshaller-gen.py $< \
		_mc_ext > $@

_gen/enums.h: _gen/all.xml $(tools_dir)/c-constants-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix mc \
		$(tools_dir)/c-constants-generator.xsl \
		$< > $@

_gen/interfaces.h: _gen/all.xml \
	$(tools_dir)/glib-interfaces-generator.xsl \
	$(tools_dir)/c-interfaces-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix mc \
		$(tools_dir)/glib-interfaces-generator.xsl \
		$< > $@

_gen/interfaces-body.h: _gen/all.xml \
	$(tools_dir)/glib-interfaces-body-generator.xsl \
	$(tools_dir)/c-interfaces-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix mc \
		$(tools_dir)/glib-interfaces-body-generator.xsl \
		$< > $@


# Generated files which must be generated per "class".
# (Currently the only "class" is nmc4, but the new API will need "classes"
# like account, account-manager, ...)

_gen/%.xml: $(top_srcdir)/xml/%.xml $(wildcard $(top_srcdir)/xml/*.xml)
	$(mkdir_p) _gen
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

_gen/cli-%-body.h _gen/cli-%.h: _gen/%.xml \
	$(tools_dir)/glib-client-gen.py Makefile.am
	$(PYTHON) $(tools_dir)/glib-client-gen.py \
		--group=`echo $* | tr x- x_` \
		--iface-quark-prefix=MC_IFACE_QUARK \
		$< Mc_Cli _gen/cli-$*

_gen/svc-%.c _gen/svc-%.h: _gen/%.xml \
	$(tools_dir)/glib-ginterface-gen.py Makefile.am
	$(PYTHON) $(tools_dir)/glib-ginterface-gen.py \
		--filename=_gen/svc-$* \
		--signal-marshal-prefix=_mc_ext \
		--include='<telepathy-glib/dbus.h>' \
		--include='"_gen/signals-marshal.h"' \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		$< Mc_Svc_
