SUBDIRS = reference
SPECS = $(top_srcdir)/xml/all.xml
SPECS_DOC = mc-dbus-iface.html

all-local: $(SPECS_DOC)

$(SPECS_DOC): $(top_srcdir)/xml/all.xml $(wildcard $(top_srcdir)/xml/*.xml) $(top_srcdir)/tools/doc-generator.xsl
	$(XSLTPROC) --xinclude --novalid \
		--param allow-undefined-interfaces "true()" \
		$(top_srcdir)/tools/doc-generator.xsl $< > $@

clean-local:
	rm -f $(SPECS_DOC)

