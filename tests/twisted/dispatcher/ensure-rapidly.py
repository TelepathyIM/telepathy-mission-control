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

"""Feature test ensuring that MC deals correctly with EnsureChannel returning
a channel that has already been dispatched to a handler.
"""

import dbus
import dbus.service

from servicetest import (EventPattern, tp_name_prefix, tp_path_prefix,
        call_async, assertContains, assertLength, assertEquals)
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

    client = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [client])

    channel = test_channel_creation(q, bus, account, client, conn,
            yours_first=True, swap_requests=False)
    channel.close()

    channel = test_channel_creation(q, bus, account, client, conn,
            yours_first=True, swap_requests=True)
    channel.close()

    channel = test_channel_creation(q, bus, account, client, conn,
            yours_first=False, swap_requests=False)
    channel.close()

    channel = test_channel_creation(q, bus, account, client, conn,
            yours_first=False, swap_requests=True)
    channel.close()

def test_channel_creation(q, bus, account, client, conn,
        yours_first=True, swap_requests=False):
    user_action_time1 = dbus.Int64(1238582606)
    user_action_time2 = dbus.Int64(1244444444)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)

    # chat UI calls ChannelDispatcher.EnsureChannel
    request = dbus.Dictionary({
            cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
            cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
            cs.CHANNEL + '.TargetID': 'juliet',
            }, signature='sv')
    call_async(q, cd, 'EnsureChannel',
            account.object_path, request, user_action_time1, client.bus_name,
            dbus_interface=cs.CD)
    ret = q.expect('dbus-return', method='EnsureChannel')
    request_path = ret.value[0]

    # chat UI connects to signals and calls ChannelRequest.Proceed()

    cr1 = bus.get_object(cs.AM, request_path)
    request_props = cr1.GetAll(cs.CR, dbus_interface=cs.PROPERTIES_IFACE)
    assert request_props['Account'] == account.object_path
    assert request_props['Requests'] == [request]
    assert request_props['UserActionTime'] == user_action_time1
    assert request_props['PreferredHandler'] == client.bus_name
    assert request_props['Interfaces'] == []

    cr1.Proceed(dbus_interface=cs.CR)

    cm_request_call1, add_request_call1 = q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_REQUESTS,
                method='EnsureChannel',
                path=conn.object_path, args=[request], handled=False),
            EventPattern('dbus-method-call', handled=False,
                interface=cs.CLIENT_IFACE_REQUESTS,
                method='AddRequest', path=client.object_path),
            )

    # Before the first request has succeeded, the user gets impatient and
    # the UI re-requests.
    call_async(q, cd, 'EnsureChannel',
            account.object_path, request, user_action_time2, client.bus_name,
            dbus_interface=cs.CD)
    ret = q.expect('dbus-return', method='EnsureChannel')
    request_path = ret.value[0]
    cr2 = bus.get_object(cs.AM, request_path)

    request_props = cr2.GetAll(cs.CR, dbus_interface=cs.PROPERTIES_IFACE)
    assert request_props['Account'] == account.object_path
    assert request_props['Requests'] == [request]
    assert request_props['UserActionTime'] == user_action_time2
    assert request_props['PreferredHandler'] == client.bus_name
    assert request_props['Interfaces'] == []

    cr2.Proceed(dbus_interface=cs.CR)

    cm_request_call2, add_request_call2 = q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_REQUESTS,
                method='EnsureChannel',
                path=conn.object_path, args=[request], handled=False),
            EventPattern('dbus-method-call', handled=False,
                interface=cs.CLIENT_IFACE_REQUESTS,
                method='AddRequest', path=client.object_path),
            )

    assert add_request_call1.args[0] == cr1.object_path
    request_props1 = add_request_call1.args[1]
    assert request_props1[cs.CR + '.Account'] == account.object_path
    assert request_props1[cs.CR + '.Requests'] == [request]
    assert request_props1[cs.CR + '.UserActionTime'] == user_action_time1
    assert request_props1[cs.CR + '.PreferredHandler'] == client.bus_name
    assert request_props1[cs.CR + '.Interfaces'] == []

    assert add_request_call2.args[0] == cr2.object_path
    request_props2 = add_request_call2.args[1]
    assert request_props2[cs.CR + '.Account'] == account.object_path
    assert request_props2[cs.CR + '.Requests'] == [request]
    assert request_props2[cs.CR + '.UserActionTime'] == user_action_time2
    assert request_props2[cs.CR + '.PreferredHandler'] == client.bus_name
    assert request_props2[cs.CR + '.Interfaces'] == []

    q.dbus_return(add_request_call1.message, signature='')
    q.dbus_return(add_request_call2.message, signature='')

    # Time passes. A channel is returned.

    channel_immutable = dbus.Dictionary(request)
    channel_immutable[cs.CHANNEL + '.InitiatorID'] = conn.self_ident
    channel_immutable[cs.CHANNEL + '.InitiatorHandle'] = conn.self_handle
    channel_immutable[cs.CHANNEL + '.Requested'] = True
    channel_immutable[cs.CHANNEL + '.Interfaces'] = \
        dbus.Array([], signature='s')
    channel_immutable[cs.CHANNEL + '.TargetHandle'] = \
        conn.ensure_handle(cs.HT_CONTACT, 'juliet')
    channel = SimulatedChannel(conn, channel_immutable)

    # Having announce() (i.e. NewChannels) come last is guaranteed by
    # telepathy-spec (since 0.17.14). There is no other ordering guarantee.

    if swap_requests:
        m2, m1 = cm_request_call1.message, cm_request_call2.message
    else:
        m1, m2 = cm_request_call1.message, cm_request_call2.message

    q.dbus_return(m1, yours_first,
            channel.object_path, channel.immutable, signature='boa{sv}')
    q.dbus_return(m2, not yours_first,
            channel.object_path, channel.immutable, signature='boa{sv}')

    channel.announce()

    # Observer should get told, processing waits for it
    e = q.expect('dbus-method-call',
            path=client.object_path,
            interface=cs.OBSERVER, method='ObserveChannels',
            handled=False)
    assert e.args[0] == account.object_path, e.args
    assert e.args[1] == conn.object_path, e.args
    assert e.args[3] == '/', e.args         # no dispatch operation
    assert sorted(e.args[4]) == sorted([cr1.object_path,
        cr2.object_path]), e.args
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == channel.object_path, channels
    assert channels[0][1] == channel.immutable, channels

    # Observer says "OK, go"
    q.dbus_return(e.message, signature='')

    # Handler is next
    e = q.expect('dbus-method-call',
            path=client.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)
    assert e.args[0] == account.object_path, e.args
    assert e.args[1] == conn.object_path, e.args
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == channel.object_path, channels
    assert channels[0][1] == channel_immutable, channels
    assert sorted(e.args[3]) == sorted([cr1.object_path,
        cr2.object_path]), e.args
    assert e.args[4] == user_action_time2, (e.args[4], user_action_time2)
    assert isinstance(e.args[5], dict)
    assertContains('request-properties', e.args[5])
    assertContains(cr1.object_path, e.args[5]['request-properties'])
    assertContains(cr2.object_path, e.args[5]['request-properties'])
    assertLength(2, e.args[5]['request-properties'])
    assertEquals(request_props1,
            e.args[5]['request-properties'][cr1.object_path])
    assertEquals(request_props2,
            e.args[5]['request-properties'][cr2.object_path])
    assert len(e.args) == 6

    # Handler accepts the Channels
    q.dbus_return(e.message, signature='')

    # CR emits Succeeded
    q.expect('dbus-signal', path=request_path,
                interface=cs.CR, signal='Succeeded')

    return channel

if __name__ == '__main__':
    exec_test(test, {})
