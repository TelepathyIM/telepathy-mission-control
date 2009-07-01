INCLUDES = \
	$(TELEPATHY_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(GLIB_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DLIBDIR="@libdir@" -DLIBVERSION="0" \
	-DG_LOG_DOMAIN=\"mc-client\"

BUILT_SOURCES = \
	_gen/libmcclient.xml \
	mc-signals-marshal.c \
	mc-signals-marshal.h \
	$(nodist_libmcclient_la_SOURCES) \
	$(nodist_geninclude_HEADERS)

CLEANFILES = $(BUILT_SOURCES)

lib_LTLIBRARIES = libmcclient.la

libmcclient_la_CFLAGS = $(GLIB_CFLAGS)
libmcclient_la_SOURCES = \
	dbus-api.c \
	mc-account.c \
	mc-account-avatar.c \
	mc-account-compat.c \
	mc-account-conditions.c \
	mc-account-manager.c \
	mc-account-request.c \
	mc-account-stats.c \
	mc-dispatch-operation.c \
	mc-errors.c \
	mc-profile.c \
	mc-signals-marshal.c

libmcclient_includedir = $(includedir)/libmcclient
libmcclient_include_HEADERS = \
	dbus-api.h \
	mc-account.h \
	mc-enums.h \
	mc-errors.h \
	mc-gtypes.h \
	mc-interfaces.h \
	mc-profile.h \
	mc-account-manager.h \
	mc-dispatch-operation.h \
	mc-signals-marshal.h \
	mc-svc-client.h

noinst_HEADERS = \
	mc-account-priv.h

genincludedir = $(libmcclient_includedir)/_gen
nodist_geninclude_HEADERS = \
	_gen/mc-quark.h \
	_gen/cli-account.h \
	_gen/cli-account-manager.h \
	_gen/cli-dispatch-operation.h \
	_gen/svc-client.h \
	_gen/enums.h \
	_gen/interfaces.h \
	_gen/gtypes.h

nodist_libmcclient_la_SOURCES = \
	_gen/mc-quark.c \
	_gen/cli-account-body.h \
	_gen/cli-account-manager-body.h \
	_gen/cli-dispatch-operation-body.h \
	_gen/svc-client.c \
	_gen/gtypes-body.h \
	_gen/interfaces-body.h \
	_gen/register-dbus-glib-marshallers-body.h \
	_gen/signals-marshal.c \
	_gen/signals-marshal.h \
	_gen/signals-marshal.list

libmcclient_la_LIBADD = \
	$(TELEPATHY_LIBS) \
	$(DBUS_LIBS) \
	$(GLIB_LIBS)

libmcclient_la_LDFLAGS = \
	-export-symbols-regex "^(mc_)|(mission_control_)"

if ENABLE_MC_CLIENT_SO6
# the redundant quoting here is to prevent the libtool command line from
# looking like an error message in an oddly named file
libmcclient_la_LDFLAGS += -version-info "10":"0":"4"
else
libmcclient_la_LDFLAGS += -release $(VERSION)
endif

%-marshal.h: %-marshal.list Makefile.am
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.h

%-marshal.c: %-marshal.list Makefile.am
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.c


EXTRA_DIST = \
	$(libmcclient_include_DATA) \
	libmcclient.xml \
	account.xml \
	account-manager.xml \
	client.xml \
	dispatch-operation.xml \
	mc-quark.list \
	mc-signals-marshal.list

# ---- telepathy-glib-style code generation ----

tools_dir = $(top_srcdir)/tools

_gen/%-quark.c _gen/%-quark.h: %-quark.list \
	$(tools_dir)/gquark-gen.py Makefile.am
	$(mkdir_p) _gen
	$(PYTHON) $(tools_dir)/gquark-gen.py \
		--quark-prefix=MC_QUARK \
		$< $*_quark _gen/$*-quark

_gen/libmcclient.xml: libmcclient.xml $(wildcard $(top_srcdir)/xml/*.xml)
	$(mkdir_p) _gen
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

# Generated files which can be done for all "classes" at once

_gen/gtypes.h _gen/gtypes-body.h: _gen/libmcclient.xml \
	$(top_srcdir)/tools/glib-gtypes-generator.py
	$(PYTHON) $(top_srcdir)/tools/glib-gtypes-generator.py \
		$< _gen/gtypes mc

_gen/signals-marshal.list: _gen/libmcclient.xml \
	$(tools_dir)/glib-signals-marshal-gen.py
	$(PYTHON) $(tools_dir)/glib-signals-marshal-gen.py $< > $@

_gen/signals-marshal.h: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --header --prefix=_mc_ext_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --body --prefix=_mc_ext_marshal $< > $@

_gen/register-dbus-glib-marshallers-body.h: _gen/libmcclient.xml \
	$(tools_dir)/glib-client-marshaller-gen.py
	$(PYTHON) $(tools_dir)/glib-client-marshaller-gen.py $< \
		_mc_ext > $@

_gen/enums.h: _gen/libmcclient.xml $(tools_dir)/c-constants-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix mc \
		$(tools_dir)/c-constants-generator.xsl \
		$< > $@

_gen/interfaces.h: _gen/libmcclient.xml \
	$(tools_dir)/glib-interfaces-generator.xsl \
	$(tools_dir)/c-interfaces-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix mc \
		$(tools_dir)/glib-interfaces-generator.xsl \
		$< > $@

_gen/interfaces-body.h: _gen/libmcclient.xml \
	$(tools_dir)/glib-interfaces-body-generator.xsl \
	$(tools_dir)/c-interfaces-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix mc \
		$(tools_dir)/glib-interfaces-body-generator.xsl \
		$< > $@


# Generated files which must be generated per "class".
# (Currently the only "class" is nmc4, but the new API will need "classes"
# like account, account-manager, ...)

_gen/%.xml: %.xml $(wildcard $(top_srcdir)/xml/*.xml)
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
		--signal-marshal-prefix=_mc_ext \
		--include='<telepathy-glib/dbus.h>' \
		--include='"_gen/signals-marshal.h"' \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		--allow-unstable \
		$< Mc_Svc_
