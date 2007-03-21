XSLTFLAGS = --nonet --novalid
DROP_NAMESPACE = sed -e 's@xmlns:mc="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
SPECS = MissionControl.xml

SPECS_GEN = ${SPECS:.xml=-gen.xml}

all-local: $(SPECS_GEN)

%-gen.xml: %.xml $(top_srcdir)/tools/spec-to-introspect.xsl
	$(XSLTPROC) $(XSLTFLAGS) $(top_srcdir)/tools/spec-to-introspect.xsl $< | $(DROP_NAMESPACE) > $@

#modified/%.xml: %.xml $(top_srcdir)/tools/add-client-c-symbol.xsl
#	@mkdir -p modified
#	$(XSLTPROC) $(XSLTFLAGS) --stringparam basename $* \
#		$(top_srcdir)/tools/add-client-c-symbol.xsl $< > $@

clean-local:
	rm -f $(SPECS_GEN)

EXTRA_DIST = mcd-dbus-services.xml $(SPECS)

