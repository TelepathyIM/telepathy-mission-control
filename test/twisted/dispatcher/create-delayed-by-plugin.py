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
"""Regression test for dispatching a requested Text channel, with a plugin that
delays dispatching and asks for permission.
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

def request_channel_expect_query(q, bus, account, conn, client):
    # This target is special-cased in test-plugin.c
    target = 'policy@example.com'
    request_properties = dbus.Dictionary(text_fixed_properties,
            signature='sv')
    request_properties[cs.CHANNEL + '.TargetID'] = target

    cd = bus.get_object(cs.CD, cs.CD_PATH)

    user_action_time = dbus.Int64(1238582606)

    call_async(q, cd, 'CreateChannel',
            account.object_path, request_properties, user_action_time,
            client.bus_name, dbus_interface=cs.CD)
    ret = q.expect('dbus-return', method='CreateChannel')
    request_path = ret.value[0]

    cr = bus.get_object(cs.AM, request_path)
    cr.Proceed(dbus_interface=cs.CR)

    cm_request_call = q.expect('dbus-method-call',
            interface=cs.CONN_IFACE_REQUESTS, method='CreateChannel',
            path=conn.object_path, args=[request_properties], handled=False)

    # A channel is returned eventually

    channel_properties = dbus.Dictionary(text_fixed_properties,
            signature='sv')
    channel_properties[cs.CHANNEL + '.TargetID'] = target
    channel_properties[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, target)
    channel_properties[cs.CHANNEL + '.InitiatorID'] = conn.self_ident
    channel_properties[cs.CHANNEL + '.InitiatorHandle'] = conn.self_handle
    channel_properties[cs.CHANNEL + '.Requested'] = True
    channel_properties[cs.CHANNEL + '.Interfaces'] = \
            dbus.Array([cs.CHANNEL_IFACE_GROUP,
                ],signature='s')

    chan = SimulatedChannel(conn, channel_properties, group=True)
    q.dbus_return(cm_request_call.message,
            chan.object_path, chan.immutable, signature='oa{sv}')
    chan.announce()

    # What does the policy service think?
    e = q.expect('dbus-method-call', path='/com/example/Policy',
            interface='com.example.Policy', method='RequestPermission')

    # Think about it for a bit
    sync_dbus(bus, q, account)

    # Let the test code decide how to reply
    return e, chan, cr

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    policy_bus_name_ref = dbus.service.BusName('com.example.Policy', bus)

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

    e, chan, cr = request_channel_expect_query(q, bus, account, conn, empathy)

    # No.
    q.dbus_raise(e.message, 'com.example.Errors.No', 'Denied!')

    # The plugin responds
    e = q.expect('dbus-method-call',
            path=chan.object_path,
            interface=cs.CHANNEL_IFACE_GROUP,
            # this error message is from the plugin
            method='RemoveMembersWithReason', args=[[conn.self_handle],
                "Computer says no", cs.GROUP_REASON_PERMISSION_DENIED],
            handled=False)
    q.dbus_return(e.message, signature='')
    chan.close()

    # Try again
    e, chan, cr = request_channel_expect_query(q, bus, account, conn, kopete)

    # Yes.
    q.dbus_return(e.message, signature='')

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
    q.dbus_return(k.message, signature='')
    q.dbus_return(e.message, signature='')

    for _ in ('Kopete', 'Empathy'):
        e = q.expect('dbus-method-call',
                interface=cs.HANDLER, method='HandleChannels', handled=False)
        q.dbus_raise(e.message, cs.INVALID_ARGUMENT, 'Never mind')

    q.expect_many(
            EventPattern('dbus-signal', path=cr.object_path,
                interface=cs.CR, signal='Failed'),
            EventPattern('dbus-method-call', path=chan.object_path,
                interface=cs.CHANNEL, method='Close', handled=True),
            )

if __name__ == '__main__':
    exec_test(test, {})
