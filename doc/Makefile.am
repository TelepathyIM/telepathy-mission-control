SUBDIRS = reference
SPECS = $(top_builddir)/xml/mcd-dbus-services.xml
SPECS_DOC = mc-dbus-iface.html

all-local: $(SPECS_DOC)

$(SPECS_DOC): $(SPECS) $(top_builddir)/xml/MissionControl.xml $(top_srcdir)/tools/doc-generator.xsl
	$(XSLTPROC) --xinclude --novalid $(top_srcdir)/tools/doc-generator.xsl $< > $@

clean-local:
	rm -f $(SPECS_DOC)

