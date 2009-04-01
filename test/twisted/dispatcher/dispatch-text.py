"""Regression test for dispatching an incoming Text channel.
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel
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

    # Two clients want to observe, dispatch and handle channels
    empathy = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)
    kopete = SimulatedClient(q, bus, 'Kopete',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # wait for MC to download the properties
    q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.CLIENT, 'Interfaces'],
                path=empathy.object_path),
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.APPROVER, 'ApproverChannelFilter'],
                path=empathy.object_path),
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.HANDLER, 'HandlerChannelFilter'],
                path=empathy.object_path),
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.OBSERVER, 'ObserverChannelFilter'],
                path=empathy.object_path),

            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.CLIENT, 'Interfaces'],
                path=kopete.object_path),
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.APPROVER, 'ApproverChannelFilter'],
                path=kopete.object_path),
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.HANDLER, 'HandlerChannelFilter'],
                path=kopete.object_path),
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='Get',
                args=[cs.OBSERVER, 'ObserverChannelFilter'],
                path=kopete.object_path),
            )

    channel_properties = dbus.Dictionary(text_fixed_properties,
            signature='sv')
    channel_properties[cs.CHANNEL + '.TargetID'] = 'juliet'
    channel_properties[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'juliet')
    channel_properties[cs.CHANNEL + '.InitiatorID'] = 'juliet'
    channel_properties[cs.CHANNEL + '.InitiatorHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'juliet')
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

    handlers = cdo_properties[cs.CDO + '.PossibleHandlers'][:]
    handlers.sort()
    # FIXME: not true
    #assert handlers == [cs.tp_name_prefix + '.Client.Empathy',
    #        cs.tp_name_prefix + '.Client.Kopete'], handlers

    assert cdo_properties[cs.CDO + '.Channels'] == [(chan.object_path,
        channel_properties)]

    cd = bus.get_object(cs.CD_BUS_NAME, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)

    assert cs.CD_IFACE_OP_LIST in cd_props.Get(cs.CD, 'Interfaces')
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') ==\
            [(cdo_path, cdo_properties)]

    cdo = bus.get_object(cs.CD_BUS_NAME, cdo_path)
    cdo_iface = dbus.Interface(cdo, cs.CDO)

    # Both Observers are told about the new channel

    e, k = q.expect_many(
            EventPattern('dbus-method-call',
                path=empathy.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False),
            EventPattern('dbus-method-call',
                path=kopete.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False),
            )
    assert e.args[0] == account.object_path, e.args
    assert e.args[1] == conn.object_path, e.args
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == chan.object_path, channels
    assert channels[0][1] == channel_properties, channels

    assert k.args == e.args

    # Both Observers indicate that they are ready to proceed
    q.dbus_return(k.message, signature='')
    q.dbus_return(e.message, signature='')

    # The Approvers are next

    e, k = q.expect_many(
            EventPattern('dbus-method-call',
                path=empathy.object_path,
                interface=cs.APPROVER, method='AddDispatchOperation',
                handled=False),
            EventPattern('dbus-method-call',
                path=kopete.object_path,
                interface=cs.APPROVER, method='AddDispatchOperation',
                handled=False),
            )

    assert e.args == [cdo_path, cdo_properties]
    assert k.args == [cdo_path, cdo_properties]

    q.dbus_return(e.message, signature='')
    q.dbus_return(k.message, signature='')

    # Both Approvers now have a flashing icon or something, trying to get the
    # user's attention

    # The user responds to Empathy first
    call_async(q, cdo_iface, 'HandleWith',
            cs.tp_name_prefix + '.Client.Empathy')

    # Empathy is asked to handle the channels
    e = q.expect('dbus-method-call',
            path=empathy.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    # Empathy accepts the channels
    q.dbus_return(e.message, signature='')

    # FIXME: this shouldn't happen until after HandleChannels has succeeded,
    # but MC currently does this as soon as HandleWith is called (fd.o #21003)
    #q.expect('dbus-signal', path=cdo_path, signal='Finished')
    #q.expect('dbus-signal', path=cs.CD_PATH,
    #    signal='DispatchOperationFinished', args=[cdo_path])

    # HandleWith succeeds
    q.expect('dbus-return', method='HandleWith')

if __name__ == '__main__':
    exec_test(test, {})
