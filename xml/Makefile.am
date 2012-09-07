tools_dir = $(top_srcdir)/tools

XSLTFLAGS = --nonet --novalid
DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
DROP_TPTYPE = sed -e 's@tp:type="[^"]*"@@g'

SPECS = \
	Account_Manager_Interface_Hidden.xml \
	Account_Interface_Conditions.xml \
	Account_Interface_External_Password_Storage.xml \
	Account_Interface_Hidden.xml \
	Connection_Manager_Interface_Account_Storage.xml \
	Channel_Dispatcher_Interface_Messages_DRAFT.xml


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
