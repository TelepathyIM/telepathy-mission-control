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
import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, sync_dbus
from mctest import exec_test, create_fakecm_account, SimulatedConnection, \
        SimulatedChannel
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "smcv@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    # Ensure that it's enabled but has offline RP and doesn't connect
    # automatically

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'RequestedPresence',
            (dbus.UInt32(cs.PRESENCE_TYPE_OFFLINE), 'offline', ''))
    q.expect('dbus-return', method='Set')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'AutomaticPresence',
            (dbus.UInt32(cs.PRESENCE_TYPE_BUSY), 'busy',
                'Testing automatic presence'))
    q.expect('dbus-return', method='Set')
    q.expect('dbus-signal', signal='AccountPropertyChanged',
            predicate=lambda e:
                e.args[0].get('AutomaticPresence', (None, None, None))[1]
                    == 'busy')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'ConnectAutomatically',
            False)
    q.expect('dbus-return', method='Set')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Enabled', True)
    q.expect('dbus-return', method='Set')
    q.expect('dbus-signal', signal='AccountPropertyChanged',
            predicate=lambda e: e.args[0].get('Enabled'))

    # Requesting a channel will put us online

    user_action_time = dbus.Int64(1238582606)

    cd = bus.get_object(cs.CD, cs.CD_PATH)

    request = dbus.Dictionary({
            cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
            cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
            cs.CHANNEL + '.TargetID': 'juliet',
            }, signature='sv')
    call_async(q, cd, 'CreateChannel',
            account.object_path, request, user_action_time, "",
            dbus_interface=cs.CD)
    ret = q.expect('dbus-return', method='CreateChannel')
    request_path = ret.value[0]

    cr = bus.get_object(cs.AM, request_path)
    request_props = cr.GetAll(cs.CR, dbus_interface=cs.PROPERTIES_IFACE)
    assert request_props['Account'] == account.object_path
    assert request_props['Requests'] == [request]
    assert request_props['UserActionTime'] == user_action_time
    assert request_props['PreferredHandler'] == ""
    assert request_props['Interfaces'] == []

    # make sure RequestConnection doesn't get called until we Proceed
    events = [EventPattern('dbus-method-call', method='RequestConnection')]
    q.forbid_events(events)

    sync_dbus(bus, q, mc)

    q.unforbid_events(events)

    cr.Proceed(dbus_interface=cs.CR)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=cs.tp_name_prefix + '.ConnectionManager.fakecm',
            path=cs.tp_path_prefix + '/ConnectionManager/fakecm',
            interface=cs.tp_name_prefix + '.ConnectionManager',
            handled=False)

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', '_',
            'myself', has_requests=True, has_presence=True)

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)
    conn.presence = dbus.Struct((cs.PRESENCE_TYPE_AVAILABLE, 'available', ''),
            signature='uss')

    _, cm_request_call = q.expect_many(
            EventPattern('dbus-method-call', path=conn.object_path,
                interface=cs.CONN_IFACE_SIMPLE_PRESENCE, method='SetPresence',
                args=['busy', 'Testing automatic presence'], handled=True),
            EventPattern('dbus-method-call', path=conn.object_path,
                interface=cs.CONN_IFACE_REQUESTS, method='CreateChannel',
                args=[request], handled=False),
            )

    q.dbus_emit(conn.object_path, cs.CONN_IFACE_SIMPLE_PRESENCE,
            'PresencesChanged',
            {conn.self_handle: (dbus.UInt32(cs.PRESENCE_TYPE_BUSY), 'busy',
                'Testing automatic presence')},
            signature='a{u(uss)}')

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
    q.dbus_return(cm_request_call.message,
            channel.object_path, channel.immutable, signature='oa{sv}')
    channel.announce()

    # there's no handler, so it gets shot down

    q.expect('dbus-method-call', path=channel.object_path, method='Close',
            handled=True)

if __name__ == '__main__':
    exec_test(test, {})
