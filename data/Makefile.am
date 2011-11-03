gsettings_SCHEMAS = \
	im.telepathy.MissionControl.FromEmpathy.gschema.xml \
	$(NULL)
@GSETTINGS_RULES@

# We build our own schema cache here for the benefit of the test suite
noinst_DATA = gschemas.compiled
gschemas.compiled: $(gsettings_SCHEMAS)
	$(AM_V_GEN)$(GLIB_COMPILE_SCHEMAS) --targetdir=$(builddir) $(srcdir)

EXTRA_DIST = \
	$(gsettings_SCHEMAS) \
	$(NULL)

CLEANFILES = $(noinst_DATA)
