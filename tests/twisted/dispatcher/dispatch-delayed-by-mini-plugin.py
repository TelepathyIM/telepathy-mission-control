# Copyright (C) 2009 Nokia Corporation
# Copyright (C) 2009 Collabora Ltd.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
# 02110-1301 USA

import dbus
"""Regression test for dispatching an incoming Text channel.
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, sync_dbus
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

text_fixed_properties = dbus.Dictionary({
    cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
    cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
    }, signature='sv')

def signal_channel_expect_query(q, bus, account, conn, empathy, kopete):
    # This target is special-cased in test-plugin.c
    target = 'policy@example.net'
    channel_properties = dbus.Dictionary(text_fixed_properties,
            signature='sv')
    channel_properties[cs.CHANNEL + '.TargetID'] = target
    channel_properties[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, target)
    channel_properties[cs.CHANNEL + '.InitiatorID'] = target
    channel_properties[cs.CHANNEL + '.InitiatorHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, target)
    channel_properties[cs.CHANNEL + '.Requested'] = False
    channel_properties[cs.CHANNEL + '.Interfaces'] = \
            dbus.Array([cs.CHANNEL_IFACE_DESTROYABLE, cs.CHANNEL_IFACE_GROUP,
                ],signature='s')

    chan = SimulatedChannel(conn, channel_properties, group=True)
    chan.announce()

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

    # What does the policy service think?
    permission = q.expect('dbus-method-call', path='/com/example/Policy',
            interface='com.example.Policy', method='RequestPermission')

    # Think about it for a bit
    sync_dbus(bus, q, account)

    # Both Observers indicate that they are ready to proceed
    q.dbus_return(k.message, signature='')
    q.dbus_return(e.message, signature='')

    # Let the test code decide how to reply
    return permission, chan, cdo_path

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    policy_bus_name_ref = dbus.service.BusName('com.example.Policy', bus)

    # For the beginning of this test, we should never be asked to handle
    # a channel.
    forbidden = [
            EventPattern('dbus-method-call', method='HandleChannels'),
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

    e, chan, cdo_path = signal_channel_expect_query(q, bus, account, conn,
            empathy, kopete)

    # No.
    q.dbus_raise(e.message, 'com.example.Errors.No', 'Denied!')

    # The plugin responds
    _, _, e = q.expect_many(
            EventPattern('dbus-signal', path=cdo_path,
                interface=cs.CDO, signal='Finished'),
            EventPattern('dbus-signal', path=cs.CD_PATH,
                interface=cs.CD_IFACE_OP_LIST,
                signal='DispatchOperationFinished',
                args=[cdo_path]),
            EventPattern('dbus-method-call',
                path=chan.object_path,
                interface=cs.CHANNEL_IFACE_GROUP,
                # this error message is from the plugin
                method='RemoveMembersWithReason', args=[[conn.self_handle],
                    "Computer says no", cs.GROUP_REASON_PERMISSION_DENIED],
                handled=False),
            )
    q.dbus_return(e.message, signature='')
    chan.close()

    # Try again
    e, chan, cdo_path = signal_channel_expect_query(q, bus, account, conn,
            empathy, kopete)

    # Yes.
    q.dbus_return(e.message, signature='')

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
    q.dbus_return(e.message, signature='')
    q.dbus_return(k.message, signature='')

    empathy_cdo = bus.get_object(cs.CD, cdo_path)
    empathy_cdo_iface = dbus.Interface(empathy_cdo, cs.CDO)
    call_async(q, empathy_cdo_iface, 'Claim')

    check_handler = q.expect('dbus-method-call', path='/com/example/Policy',
            interface='com.example.Policy', method='CheckHandler')
    q.dbus_raise(check_handler.message, 'com.example.Errors.No',
            "That handler doesn't have enough options")
    q.expect('dbus-error', method='Claim', name=cs.PERMISSION_DENIED)

    kopete_cdo = bus.get_object(cs.CD, cdo_path)
    kopete_cdo_iface = dbus.Interface(kopete_cdo, cs.CDO)
    call_async(q, kopete_cdo_iface, 'Claim')

    check_handler = q.expect('dbus-method-call', path='/com/example/Policy',
            interface='com.example.Policy', method='CheckHandler')
    q.dbus_return(check_handler.message, signature='')

    q.expect_many(
            EventPattern('dbus-signal', path=cdo_path, signal='Finished'),
            EventPattern('dbus-signal', path=cs.CD_PATH,
                signal='DispatchOperationFinished', args=[cdo_path]),
            EventPattern('dbus-return', method='Claim'),
            )

    sync_dbus(bus, q, mc)

    # Try again; this time we'll reject a selected handler
    e, chan, cdo_path = signal_channel_expect_query(q, bus, account, conn,
            empathy, kopete)

    # The request is fine, continue...
    q.dbus_return(e.message, signature='')

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
    q.dbus_return(e.message, signature='')
    q.dbus_return(k.message, signature='')

    kopete_cdo = bus.get_object(cs.CD, cdo_path)
    kopete_cdo_iface = dbus.Interface(kopete_cdo, cs.CDO)
    call_async(q, kopete_cdo_iface, 'HandleWith',
            cs.tp_name_prefix + '.Client.Kopete')

    check_handler = q.expect('dbus-method-call', path='/com/example/Policy',
            interface='com.example.Policy', method='CheckHandler')
    q.dbus_raise(check_handler.message, 'com.example.Errors.No',
            'That handler is not good enough')
    q.expect('dbus-error', method='HandleWith', name=cs.PERMISSION_DENIED)

    # well, let's try *something*... Kopete has been marked as failed,
    # so this will try Empathy
    call_async(q, kopete_cdo_iface, 'HandleWith', '')

    check_handler = q.expect('dbus-method-call', path='/com/example/Policy',
            interface='com.example.Policy', method='CheckHandler')
    q.dbus_raise(check_handler.message, 'com.example.Errors.No',
            'That handler is no good either')

    # Oops... we ran out of handlers
    _, _, _, e = q.expect_many(
            EventPattern('dbus-error', method='HandleWith',
                name=cs.PERMISSION_DENIED),
            EventPattern('dbus-signal', path=cdo_path,
                interface=cs.CDO, signal='Finished'),
            EventPattern('dbus-signal', path=cs.CD_PATH,
                interface=cs.CD_IFACE_OP_LIST,
                signal='DispatchOperationFinished',
                args=[cdo_path]),
            EventPattern('dbus-method-call',
                path=chan.object_path,
                interface=cs.CHANNEL_IFACE_DESTROYABLE,
                method='Destroy',
                handled=False),
            )
    q.dbus_return(e.message, signature='')
    chan.close()

    sync_dbus(bus, q, mc)

    # From now on we no longer want to forbid HandleChannels, but we do want
    # to forbid AddDispatchOperation
    q.unforbid_events(forbidden)
    forbidden = [
            EventPattern('dbus-method-call', method='AddDispatchOperation'),
            ]
    q.forbid_events(forbidden)

    # Try yet again
    policy_request, chan, cdo_path = signal_channel_expect_query(q, bus,
            account, conn, empathy, kopete)

    # Before the policy service replies, someone requests the same channel

    user_action_time = dbus.Int64(1238582606)
    call_async(q, cd, 'EnsureChannel',
            account.object_path, chan.immutable, user_action_time,
            kopete.bus_name, dbus_interface=cs.CD)
    ret, add_request_call = q.expect_many(
            EventPattern('dbus-return', method='EnsureChannel'),
            EventPattern('dbus-method-call', handled=False,
                interface=cs.CLIENT_IFACE_REQUESTS,
                method='AddRequest', path=kopete.object_path),
            )
    request_path = ret.value[0]

    cr = bus.get_object(cs.CD, request_path)
    cr.Proceed(dbus_interface=cs.CR)

    cm_request_call = q.expect('dbus-method-call',
            interface=cs.CONN_IFACE_REQUESTS,
            method='EnsureChannel',
            path=conn.object_path, args=[chan.immutable], handled=False)

    q.dbus_return(add_request_call.message, signature='')

    # Time passes. The CM returns the existing channel, and the policy
    # service gets round to replying

    q.dbus_return(cm_request_call.message, False,
            chan.object_path, chan.immutable, signature='boa{sv}')

    q.dbus_return(policy_request.message, signature='')

    # Now we want to pass the channel to the selected handler.
    # What does the policy service think about that?
    check_handler = q.expect('dbus-method-call', path='/com/example/Policy',
            interface='com.example.Policy', method='CheckHandler')

    # Yeah, we're OK with that.
    q.dbus_return(check_handler.message, signature='')

    e = q.expect('dbus-method-call',
            path=kopete.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)
    assert e.args[0] == account.object_path, e.args
    assert e.args[1] == conn.object_path, e.args
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == chan.object_path, channels
    assert channels[0][1] == chan.immutable, channels
    assert e.args[3] == [request_path], e.args
    assert e.args[4] == user_action_time, (e.args[4], user_action_time)
    assert isinstance(e.args[5], dict)
    assert len(e.args) == 6

    # Handler accepts the Channels
    q.dbus_return(e.message, signature='')

    q.expect_many(
            EventPattern('dbus-signal', interface=cs.CDO, signal='Finished'),
            EventPattern('dbus-signal', interface=cs.CD_IFACE_OP_LIST,
                signal='DispatchOperationFinished'),
            )

    sync_dbus(bus, q, mc)

if __name__ == '__main__':
    exec_test(test, {})
