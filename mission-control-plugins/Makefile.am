INCLUDES = \
	$(TELEPATHY_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(GLIB_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DG_LOG_DOMAIN=\"mc-plugins\" \
	-DMCP_API_VERSION=@MCP_API_VERSION@ \
	-DMCP_ABI_VERSION=@MCP_ABI_VERSION@

lib_LTLIBRARIES = libmission-control-plugins.la

plugins_includedir = \
	$(includedir)/mission-control-@MCP_API_VERSION@/mission-control-plugins

plugins_include_HEADERS = \
	mission-control-plugins.h \
	dispatch-operation.h \
	dispatch-operation-policy.h

libmission_control_plugins_la_LDFLAGS = \
	-export-symbols-regex "^(mcp_)" \
	-version-info "@MCP_LT_CURRENT@":"@MCP_LT_REVISION@":"@MCP_LT_AGE@"

libmission_control_plugins_la_LIBADD = \
	$(TELEPATHY_LIBS) \
	$(DBUS_LIBS) \
	$(GLIB_LIBS)

libmission_control_plugins_la_SOURCES = \
	dispatch-operation.c \
	dispatch-operation-policy.c \
	implementation.h
