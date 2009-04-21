tools_dir = $(top_srcdir)/tools

XSLTFLAGS = --nonet --novalid
DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
DROP_TPTYPE = sed -e 's@tp:type="[^"]*"@@g'

SPECS = \
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
	Channel_Dispatcher.xml \
	Channel_Dispatcher_Interface_Operation_List.xml \
	Channel_Request.xml \
	Client.xml \
	Client_Approver.xml \
	Client_Handler.xml \
	Client_Interface_Requests.xml \
	Client_Observer.xml \
        Connection_Interface_Contact_Capabilities.xml


SPECS_GEN = ${SPECS:%.xml=_gen/introspect-%.xml}

all-local: $(SPECS_GEN)

_gen/introspect-%.xml: %.xml $(tools_dir)/spec-to-introspect.xsl
	$(mkdir_p) _gen
	$(XSLTPROC) $(XSLTFLAGS) $(tools_dir)/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

clean-local:
	rm -f $(SPECS_GEN)

EXTRA_DIST = \
	all.xml \
	nmc5.xml \
	generic-types.xml \
	telepathy-types.xml \
	$(SPECS)
