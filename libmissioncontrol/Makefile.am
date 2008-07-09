INCLUDES = \
	$(DBUS_CFLAGS) \
	$(TELEPATHY_CFLAGS) \
	$(LIBTELEPATHY_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DMC_DISABLE_DEPRECATED \
	-DLIBDIR="@libdir@" -DLIBVERSION="0"

BUILT_SOURCES = \
	_gen/all.xml \
	_gen/nmc4.xml \
	mission-control-signals-marshal.c \
	mission-control-signals-marshal.h \
	mc-enum-types.c \
	mc-enum-types.h \
	mc-signals-marshal.c \
	mc-signals-marshal.h \
	mc-client-lib-gen.h \
	$(nodist_libmissioncontrol_client_la_SOURCES) \
	$(nodist_geninclude_HEADERS)

CLEANFILES = $(BUILT_SOURCES) stamp-mc-enum-types.h

lib_LTLIBRARIES = libmissioncontrol-client.la

libmissioncontrol_client_la_CFLAGS = $(GLIB_CFLAGS) $(GCONF_CFLAGS)
libmissioncontrol_client_la_SOURCES = \
	cli-nmc4.c \
	dbus-api.c \
        mc.c\
        mc-manager.c \
        mc-manager-priv.h \
        mc-protocol.c \
        mc-protocol-priv.h \
        mc-profile.c \
        mc-account.c \
        mc-account-proxy.c \
        mc-account-priv.h \
        mc-account-monitor.c \
	mc-account-monitor-priv.h \
        mc-account-manager-proxy.c \
	mc-signals-marshal.c \
	mc-enum-types.c \
	mission-control-signals-marshal.c \
	mission-control.c

libmissioncontrol_client_includedir = $(includedir)/libmissioncontrol
libmissioncontrol_client_include_HEADERS = \
	cli-nmc4.h \
	dbus-api.h \
        mc.h \
        mc-account.h \
        mc-account-proxy.h \
        mc-account-monitor.h \
        mc-account-manager-proxy.h \
	mc-enum-types.h \
        mc-profile.h \
        mc-protocol.h \
        mc-manager.h \
        mc-remap.h \
	mc-signals-marshal.h \
	mission-control-signals-marshal.h \
	mission-control.h \
	svc-nmc4.h

genincludedir = $(libmissioncontrol_client_includedir)/_gen
nodist_geninclude_HEADERS = \
	_gen/cli-Account.h \
	_gen/cli-Account_Interface_Avatar.h \
	_gen/cli-Account_Manager.h \
	_gen/cli-nmc4.h \
	_gen/enums.h \
	_gen/interfaces.h \
	_gen/gtypes.h \
	_gen/svc-nmc4.h

nodist_libmissioncontrol_client_la_SOURCES = \
	_gen/cli-Account-body.h \
	_gen/cli-Account_Interface_Avatar-body.h \
	_gen/cli-Account_Manager-body.h \
	_gen/cli-nmc4-body.h \
	_gen/gtypes-body.h \
	_gen/interfaces-body.h \
	_gen/register-dbus-glib-marshallers-body.h \
	_gen/signals-marshal.c \
	_gen/signals-marshal.h \
	_gen/signals-marshal.list \
	_gen/svc-nmc4.c

libmissioncontrol_client_la_LIBADD = $(GCONF_LIBS) $(GLIB_LIBS) $(DBUS_LIBS) \
				     $(TELEPATHY_LIBS) $(LIBTELEPATHY_LIBS)

libmissioncontrol_client_la_LDFLAGS = $(common_ldflags) \
				      -export-symbols-regex "^(mc_)|(omc_)|(mission_control_)" \
				      -version-info 5:0:0

mission_control_include = \
	mission-control-signals-marshal.h \
	mission-control.h

mc-client-lib-gen.h: $(top_builddir)/xml/MissionControl-gen.xml
	dbus-binding-tool --prefix=mc-client-lib --mode=glib-client $< > $@

%-marshal.h: %-marshal.list Makefile.am
	echo "#include <libmissioncontrol/mc-remap.h>" > $*-marshal.h
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< >> $*-marshal.h

%-marshal.c: %-marshal.list Makefile.am
	echo "#include <libmissioncontrol/mc-remap.h>" > $*-marshal.c
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< >> $*-marshal.c

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
mc_account_LDADD = libmissioncontrol-client.la
noinst_PROGRAMS = test
test_SOURCES = test.c
test_LDADD = libmissioncontrol-client.la

EXTRA_DIST = \
	$(libmissioncontrol_client_include_DATA) \
	mission-control-signals-marshal.list \
	mc-signals-marshal.list

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
	$(tools_dir)/glib-blocking-client-gen.py Makefile.am
	$(PYTHON) $(tools_dir)/glib-blocking-client-gen.py \
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
