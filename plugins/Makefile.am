INCLUDES = \
	$(TELEPATHY_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(GLIB_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DLIBDIR="@libdir@" \
	-DLIBVERSION="@MCP_ABI_VERSION@"

EXTRA_DIST = 

instdir = @libdir@/mission-control-plugins.@MCP_ABI_VERSION@

plugin_LDFLAGS = -module -shared -avoid-version -rpath @abs_builddir@

inst_LTLIBRARIES = 

if ENABLE_AEGIS

inst_LTLIBRARIES += mcp-dbus-aegis-acl.la

mcp_dbus_aegis_acl_la_SOURCES = mcp-dbus-aegis-acl.c
mcp_dbus_aegis_acl_la_LDFLAGS = \
	$(plugin_LDFLAGS) \
	$(TELEPATHY_LIBS) \
	$(DBUS_LIBS) \
	$(AEGIS_LIBS)

endif
