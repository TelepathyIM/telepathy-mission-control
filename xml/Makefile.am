tools_dir = $(top_srcdir)/tools

XSLTFLAGS = --nonet --novalid
DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
DROP_TPTYPE = sed -e 's@tp:type="[^"]*"@@g'

SPECS = \
	$(NULL)


SPECS_GEN = ${SPECS:%.xml=_gen/introspect-%.xml}

all-local: $(SPECS_GEN)

_gen/introspect-%.xml: %.xml $(tools_dir)/spec-to-introspect.xsl
	$(AM_V_at)$(mkdir_p) _gen
	$(AM_V_GEN)$(XSLTPROC) $(XSLTFLAGS) $(tools_dir)/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

clean-local:
	rm -f $(SPECS_GEN)

EXTRA_DIST = \
	all.xml \
	nmc5.xml \
	generic-types.xml \
	telepathy-types.xml \
	$(SPECS)
