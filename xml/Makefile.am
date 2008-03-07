tools_dir = $(top_srcdir)/tools

XSLTFLAGS = --nonet --novalid
DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'

SPECS = MissionControl.xml \
	Account_Manager.xml \
	Account.xml

SPECS_GEN = ${SPECS:.xml=-gen.xml}

all-local: $(SPECS_GEN)

%-gen.xml: %.xml $(tools_dir)/spec-to-introspect.xsl
	$(XSLTPROC) $(XSLTFLAGS) $(tools_dir)/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

clean-local:
	rm -f $(SPECS_GEN)

EXTRA_DIST = \
	all.xml \
	nmc4.xml \
	$(SPECS)
