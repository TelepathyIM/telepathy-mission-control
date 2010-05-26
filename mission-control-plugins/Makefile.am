INCLUDES = \
	$(TELEPATHY_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(GLIB_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DG_LOG_DOMAIN=\"mc-plugins\" \
	-DMCP_API_VERSION=@MCP_API_VERSION@ \
	-DMCP_ABI_VERSION=@MCP_ABI_VERSION@

AM_CFLAGS = $(ERROR_CFLAGS)

EXTRA_DIST = mcp-signals-marshal.list

lib_LTLIBRARIES = libmission-control-plugins.la

plugins_includedir = \
	$(includedir)/mission-control-@MCP_API_VERSION@/mission-control-plugins

plugins_include_HEADERS = \
	mission-control-plugins.h \
	account.h \
	account-storage.h \
	dispatch-operation.h \
	dispatch-operation-policy.h \
	loader.h \
	request.h \
	request-policy.h \
	mcp-signals-marshal.h

libmission_control_plugins_la_LDFLAGS = \
	-export-symbols-regex "^(mcp_)" \
	-version-info "@MCP_LT_CURRENT@":"@MCP_LT_REVISION@":"@MCP_LT_AGE@"

libmission_control_plugins_la_LIBADD = \
	$(TELEPATHY_LIBS) \
	$(DBUS_LIBS) \
	$(GLIB_LIBS)

nodist_libmission_control_plugins_la_SOURCES = \
	mcp-signals-marshal.c \
	mcp-signals-marshal.h

libmission_control_plugins_la_SOURCES = \
	debug-internal.h \
	account.c \
	account-storage.c \
	dispatch-operation.c \
	dispatch-operation-policy.c \
	implementation.h \
	loader.c \
	request.c \
	request-policy.c

BUILT_SOURCES = $(nodist_libmission_control_plugins_la_SOURCES)

CLEANFILES = $(BUILT_SOURCES)

%-signals-marshal.h: %-signals-marshal.list Makefile
	glib-genmarshal --header --prefix=_$(subst -,_,$(notdir $*))_marshal $< \
	  > $*-signals-marshal.h

%-signals-marshal.c: %-signals-marshal.list Makefile
	{ echo '#include "$(notdir $*)-signals-marshal.h"' && \
	  glib-genmarshal --body --prefix=_$(subst -,_,$(notdir $*))_marshal $<; } \
	    > $*-signals-marshal.c
