mc_schemas = \
	im.telepathy.MissionControl.FromEmpathy.gschema.xml \
	$(NULL)

gsettings_SCHEMAS =

if ENABLE_CONN_SETTING
gsettings_SCHEMAS += $(mc_schemas)

# We build our own schema cache here for the benefit of the test suite
noinst_DATA = gschemas.compiled
gschemas.compiled: $(gsettings_SCHEMAS)
	$(AM_V_GEN)$(GLIB_COMPILE_SCHEMAS) --targetdir=$(builddir) $(srcdir)
endif

@GSETTINGS_RULES@

EXTRA_DIST = \
	$(mc_schemas) \
	$(NULL)

CLEANFILES = $(noinst_DATA)
