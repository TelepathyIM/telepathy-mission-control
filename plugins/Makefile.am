INCLUDES = \
	$(TELEPATHY_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(GLIB_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DLIBDIR="@libdir@" \
	-DLIBVERSION="@MCP_ABI_VERSION@"

noinst_LTLIBRARIES =

if ENABLE_AEGIS

noinst_LTLIBRARIES += libmcp-aegis.la
libmcp_aegis_la_SOURCES = mcp-dbus-aegis-acl.c
libmcp_aegis_la_LIBADD = \
	$(top_builddir)/mission-control-plugins/libmission-control-plugins.la \
	$(TELEPATHY_LIBS) \
	$(DBUS_LIBS) \
	$(AEGIS_LIBS)

endif
