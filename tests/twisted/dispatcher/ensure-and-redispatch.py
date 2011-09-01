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

    # We have more than one Client "head" on the same unique name, to test
    # fd.o #24645 - we want the same "head" to be reinvoked if at all possible

    # This one is first in alphabetical order, is discovered first, and can
    # handle all channels, but is not the one we want
    empathy_worse_match = SimulatedClient(q, bus, 'A.Temporary.Handler',
            observe=[], approve=[],
            handle=[[]], bypass_approval=False)

    # This is the one we actually want
    empathy = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # this one is a closer match, but not the preferred handler
    closer_match = dbus.Dictionary(text_fixed_properties)
    closer_match[cs.CHANNEL + '.TargetID'] = 'juliet'
    empathy_better_match = SimulatedClient(q, bus, 'Empathy.BetterMatch',
            observe=[], approve=[],
            handle=[closer_match], bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [empathy_worse_match, empathy,
        empathy_better_match])

    channel = test_channel_creation(q, bus, account, empathy, conn)

    # After the channel has been dispatched, a handler that would normally
    # be a closer match turns up. Regardless, we should not redispatch to it.
    # For the better client to be treated as if it's in a different process,
    # it needs its own D-Bus connection.
    closer_match = dbus.Dictionary(text_fixed_properties)
    closer_match[cs.CHANNEL + '.TargetID'] = 'juliet'
    closer_match[cs.CHANNEL + '.InitiatorID'] = conn.self_ident
    better_bus = dbus.bus.BusConnection()
    q.attach_to_bus(better_bus)
    better = SimulatedClient(q, better_bus, 'BetterMatch',
            observe=[], approve=[],
            handle=[closer_match], bypass_approval=False)
    expect_client_setup(q, [better])

    test_channel_redispatch(q, bus, account, empathy, conn, channel)
    test_channel_redispatch(q, bus, account, empathy, conn, channel,
            ungrateful_handler=True)
    empathy.release_name()
    empathy_better_match.release_name()
    empathy_worse_match.release_name()
    test_channel_redispatch(q, bus, account, empathy, conn, channel,
            client_gone=True)
    channel.close()

def test_channel_creation(q, bus, account, client, conn):
    user_action_time = dbus.Int64(1238582606)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)

    # chat UI calls ChannelDispatcher.EnsureChannel
    request = dbus.Dictionary({
            cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
            cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
            cs.CHANNEL + '.TargetID': 'juliet',
            }, signature='sv')
    call_async(q, cd, 'EnsureChannel',
            account.object_path, request, user_action_time, client.bus_name,
            dbus_interface=cs.CD)
    ret = q.expect('dbus-return', method='EnsureChannel')
    request_path = ret.value[0]

    # chat UI connects to signals and calls ChannelRequest.Proceed()

    cr = bus.get_object(cs.AM, request_path)
    request_props = cr.GetAll(cs.CR, dbus_interface=cs.PROPERTIES_IFACE)
    assert request_props['Account'] == account.object_path
    assert request_props['Requests'] == [request]
    assert request_props['UserActionTime'] == user_action_time
    assert request_props['PreferredHandler'] == client.bus_name
    assert request_props['Interfaces'] == []

    cr.Proceed(dbus_interface=cs.CR)

    cm_request_call, add_request_call = q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_REQUESTS,
                method='EnsureChannel',
                path=conn.object_path, args=[request], handled=False),
            EventPattern('dbus-method-call', handled=False,
                interface=cs.CLIENT_IFACE_REQUESTS,
                method='AddRequest', path=client.object_path),
            )

    assert add_request_call.args[0] == request_path
    request_props = add_request_call.args[1]
    assert request_props[cs.CR + '.Account'] == account.object_path
    assert request_props[cs.CR + '.Requests'] == [request]
    assert request_props[cs.CR + '.UserActionTime'] == user_action_time
    assert request_props[cs.CR + '.PreferredHandler'] == client.bus_name
    assert request_props[cs.CR + '.Interfaces'] == []

    q.dbus_return(add_request_call.message, signature='')

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

    # this order of events is guaranteed by telepathy-spec (since 0.17.14)
    q.dbus_return(cm_request_call.message, True, # <- Yours
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
    assert e.args[4] == [request_path], e.args
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == channel.object_path, channels
    assert channels[0][1] == channel_immutable, channels

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
    assert e.args[3] == [request_path], e.args
    assert e.args[4] == user_action_time
    assert isinstance(e.args[5], dict)
    assert len(e.args) == 6

    # Handler accepts the Channels
    q.dbus_return(e.message, signature='')

    # CR emits Succeeded
    q.expect('dbus-signal', path=request_path,
                interface=cs.CR, signal='Succeeded')

    return channel

def test_channel_redispatch(q, bus, account, client, conn, channel,
        ungrateful_handler=False, client_gone=False):

    user_action_time = dbus.Int64(1244444444)

    forbidden = [
            # Because we create no new channels, nothing should be observed.
            EventPattern('dbus-method-call', method='ObserveChannels'),
            # Even though there is a better handler on a different unique
            # name, the channels must not be re-dispatched to it.
            EventPattern('dbus-method-call', method='HandleChannels',
                predicate=lambda e: e.path != client.object_path),
            # If the handler rejects the re-handle call, the channel must not
            # be closed.
            EventPattern('dbus-method-call', method='Close'),
            ]

    if client_gone:
        # There's nothing to call these methods on any more.
        forbidden.append(EventPattern('dbus-method-call',
            method='HandleChannels'))
        forbidden.append(EventPattern('dbus-method-call',
            method='AddRequest'))

    q.forbid_events(forbidden)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)

    # UI calls ChannelDispatcher.EnsureChannel again
    request = dbus.Dictionary({
            cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
            cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
            cs.CHANNEL + '.TargetID': 'juliet',
            }, signature='sv')
    call_async(q, cd, 'EnsureChannel',
            account.object_path, request, user_action_time, client.bus_name,
            dbus_interface=cs.CD)
    ret = q.expect('dbus-return', method='EnsureChannel')
    request_path = ret.value[0]

    # UI connects to signals and calls ChannelRequest.Proceed()

    cr = bus.get_object(cs.AM, request_path)
    request_props = cr.GetAll(cs.CR, dbus_interface=cs.PROPERTIES_IFACE)
    assert request_props['Account'] == account.object_path
    assert request_props['Requests'] == [request]
    assert request_props['UserActionTime'] == user_action_time
    assert request_props['PreferredHandler'] == client.bus_name
    assert request_props['Interfaces'] == []

    cr.Proceed(dbus_interface=cs.CR)

    cm_request_pattern = EventPattern('dbus-method-call',
        interface=cs.CONN_IFACE_REQUESTS,
        method='EnsureChannel',
        path=conn.object_path, args=[request], handled=False)

    if client_gone:
        (cm_request_call,) = q.expect_many(cm_request_pattern)
        add_request_call = None
    else:
        (cm_request_call, add_request_call) = q.expect_many(
                cm_request_pattern,
                EventPattern('dbus-method-call', handled=False,
                    interface=cs.CLIENT_IFACE_REQUESTS,
                    method='AddRequest', path=client.object_path),
                )

    if add_request_call is not None:
        assert add_request_call.args[0] == request_path
        request_props = add_request_call.args[1]
        assert request_props[cs.CR + '.Account'] == account.object_path
        assert request_props[cs.CR + '.Requests'] == [request]
        assert request_props[cs.CR + '.UserActionTime'] == user_action_time
        assert request_props[cs.CR + '.PreferredHandler'] == client.bus_name
        assert request_props[cs.CR + '.Interfaces'] == []

        q.dbus_return(add_request_call.message, signature='')

    # Time passes. The same channel is returned.
    q.dbus_return(cm_request_call.message, False, # <- Yours
            channel.object_path, channel.immutable, signature='boa{sv}')

    if not client_gone:
        # Handler is re-invoked. This HandleChannels call is only said to
        # satisfy the new request, because the earlier request has already
        # been satisfied.
        e = q.expect('dbus-method-call',
                path=client.object_path,
                interface=cs.HANDLER, method='HandleChannels',
                handled=False)
        assert e.args[0] == account.object_path, e.args
        assert e.args[1] == conn.object_path, e.args
        channels = e.args[2]
        assert len(channels) == 1, channels
        assert channels[0][0] == channel.object_path, channels
        assert channels[0][1] == channel.immutable, channels
        assert e.args[3] == [request_path], e.args
        assert e.args[4] == user_action_time
        assert isinstance(e.args[5], dict)
        assertContains('request-properties', e.args[5])
        assertContains(request_path, e.args[5]['request-properties'])
        assertLength(1, e.args[5]['request-properties'])
        assertEquals(request_props,
                e.args[5]['request-properties'][request_path])
        assert len(e.args) == 6

        if ungrateful_handler:
            q.dbus_raise(e.message, cs.INVALID_ARGUMENT,
                    'I am very strict in my misunderstanding of telepathy-spec')
        else:
            # Handler accepts the Channels
            q.dbus_return(e.message, signature='')

    # CR emits Succeeded
    q.expect('dbus-signal', path=request_path,
                interface=cs.CR, signal='Succeeded')

    q.unforbid_events(forbidden)

if __name__ == '__main__':
    exec_test(test, {})
