"""Regression test for dispatching an incoming Text channel.
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    text_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    # Throughout this entire test, we should never be asked to approve or
    # handle a channel.
    forbidden = [
            EventPattern('dbus-method-call', method='AddDispatchOperation'),
            EventPattern('dbus-method-call', method='HandleChannels'),
            # FIXME: currently we're never asked to ObserveChannels either -
            # is that right?
            #
            # EventPattern('dbus-method-call', method='ObserveChannels'),
            ]
    q.forbid_events(forbidden)

    # Two clients want to observe, approve and handle channels
    empathy = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)
    kopete = SimulatedClient(q, bus, 'Kopete',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [empathy, kopete])

    # subscribe to the OperationList interface (MC assumes that until this
    # property has been retrieved once, nobody cares)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

    # This ID is special-cased by the test-plugin plugin, which rejects
    # channels to or from it
    channel_properties = dbus.Dictionary(text_fixed_properties,
            signature='sv')
    channel_properties[cs.CHANNEL + '.TargetID'] = 'rick.astley@example.com'
    channel_properties[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'rick.astley@example.com')
    channel_properties[cs.CHANNEL + '.InitiatorID'] = 'rick.astley@example.com'
    channel_properties[cs.CHANNEL + '.InitiatorHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'rick.astley@example.com')
    channel_properties[cs.CHANNEL + '.Requested'] = False
    channel_properties[cs.CHANNEL + '.Interfaces'] = dbus.Array(signature='s')

    chan = SimulatedChannel(conn, channel_properties)
    chan.announce()

    # A channel dispatch operation is created

    e = q.expect('dbus-signal',
            path=cs.CD_PATH,
            interface=cs.CD_IFACE_OP_LIST,
            signal='NewDispatchOperation')

    cdo_path = e.args[0]
    cdo_properties = e.args[1]

    assert cdo_properties[cs.CDO + '.Account'] == account.object_path
    assert cdo_properties[cs.CDO + '.Connection'] == conn.object_path
    assert cs.CDO + '.Interfaces' in cdo_properties

    handlers = cdo_properties[cs.CDO + '.PossibleHandlers'][:]
    handlers.sort()
    assert handlers == [cs.tp_name_prefix + '.Client.Empathy',
            cs.tp_name_prefix + '.Client.Kopete'], handlers

    # The plugin realises we've been rickrolled, and responds
    q.expect('dbus-method-call',
            path=chan.object_path,
            interface=cs.CHANNEL, method='Close', args=[],
            handled=True)

    e = q.expect('dbus-signal',
            path=cdo_path, interface=cs.CDO, signal='ChannelLost')
    assert e.args[0] == chan.object_path

    q.expect_many(
            EventPattern('dbus-signal', path=cdo_path,
                interface=cs.CDO, signal='Finished'),
            EventPattern('dbus-signal', path=cs.CD_PATH,
                interface=cs.CD_IFACE_OP_LIST,
                signal='DispatchOperationFinished',
                args=[cdo_path]),
            )

if __name__ == '__main__':
    exec_test(test, {})
