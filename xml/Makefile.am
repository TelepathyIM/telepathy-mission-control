tools_dir = $(top_srcdir)/tools

XSLTFLAGS = --nonet --novalid
DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
DROP_TPTYPE = sed -e 's@tp:type="[^"]*"@@g'

SPECS = MissionControl.xml \
	Account_Manager.xml \
	Account_Manager_Interface_Creation.xml \
	Account_Manager_Interface_Query.xml \
	Account.xml \
	Account_Interface_Avatar.xml \
	Account_Interface_ChannelRequests.xml \
	Account_Interface_Compat.xml \
	Account_Interface_Conditions.xml \
	Account_Interface_Stats.xml \
	Channel_Dispatch_Operation.xml \
	Client.xml \
	Client_Approver.xml \
	Client_Handler.xml \
	Client_Observer.xml \
        Connection_Interface_Contact_Capabilities.xml


SPECS_GEN = ${SPECS:.xml=-gen.xml}

all-local: $(SPECS_GEN)

MissionControl-gen.xml: MissionControl.xml $(tools_dir)/spec-to-introspect.xsl
	$(XSLTPROC) $(XSLTFLAGS) $(tools_dir)/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) | $(DROP_TPTYPE) > $@

%-gen.xml: %.xml $(tools_dir)/spec-to-introspect.xsl
	$(XSLTPROC) $(XSLTFLAGS) $(tools_dir)/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

clean-local:
	rm -f $(SPECS_GEN)

EXTRA_DIST = \
	all.xml \
	nmc4.xml \
	nmc5.xml \
	generic-types.xml \
	telepathy-types.xml \
	$(SPECS)
