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
from mctest import exec_test, SimulatedConnection, create_fakecm_account,\
        SimulatedChannel
import constants as cs

def test(q, bus, mc):
    cm_name_ref = dbus.service.BusName(
            tp_name_prefix + '.ConnectionManager.fakecm', bus=bus)

    # Create an account
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    # Events that indicate that Reconnect might have done something
    looks_like_reconnection = [
            EventPattern('dbus-method-call', method='RequestConnection'),
            EventPattern('dbus-method-call', method='Disconnect'),
            ]

    q.forbid_events(looks_like_reconnection)

    # While we want to be online but the account is disabled, Reconnect is a
    # no-op. Set Enabled to False explicitly, so we're less reliant on initial
    # state.

    call_async(q, account, 'Set', cs.ACCOUNT, 'Enabled', False,
            dbus_interface=cs.PROPERTIES_IFACE)
    q.expect('dbus-return', method='Set')

    requested_presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_AVAILABLE),
        dbus.String(u'available'), dbus.String(u'')))
    call_async(q, account, 'Set', cs.ACCOUNT,
            'RequestedPresence', requested_presence,
            dbus_interface=cs.PROPERTIES_IFACE)
    q.expect('dbus-return', method='Set')

    call_async(q, account, 'Reconnect', dbus_interface=cs.ACCOUNT)
    q.expect('dbus-return', method='Reconnect')

    sync_dbus(bus, q, account)

    # While we want to be offline but the account is enabled, Reconnect is
    # still a no-op.
    requested_presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_OFFLINE),
        dbus.String(u'offline'), dbus.String(u'')))
    call_async(q, account, 'Set', cs.ACCOUNT,
            'RequestedPresence', requested_presence,
            dbus_interface=cs.PROPERTIES_IFACE)
    q.expect_many(
            EventPattern('dbus-return', method='Set'),
            EventPattern('dbus-signal',
                path=account.object_path,
                signal='AccountPropertyChanged',
                interface=cs.ACCOUNT),
            )

    # Enable the account
    call_async(q, account, 'Set', cs.ACCOUNT, 'Enabled', True,
            dbus_interface=cs.PROPERTIES_IFACE)
    q.expect_many(
            EventPattern('dbus-return', method='Set'),
            EventPattern('dbus-signal',
                path=account.object_path,
                signal='AccountPropertyChanged',
                interface=cs.ACCOUNT),
            )

    call_async(q, account, 'Reconnect', dbus_interface=cs.ACCOUNT)
    q.expect('dbus-return', method='Reconnect')

    sync_dbus(bus, q, account)

    # Actually go online now

    q.unforbid_events(looks_like_reconnection)

    requested_presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_AVAILABLE),
        dbus.String(u'brb'), dbus.String(u'Be back soon!')))
    account.Set(cs.ACCOUNT,
            'RequestedPresence', requested_presence,
            dbus_interface=cs.PROPERTIES_IFACE)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=tp_name_prefix + '.ConnectionManager.fakecm',
            path=tp_path_prefix + '/ConnectionManager/fakecm',
            interface=tp_name_prefix + '.ConnectionManager',
            handled=False)

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', '_',
            'myself')

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    # MC does some setup, including fetching the list of Channels

    q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CONN_IFACE_REQUESTS],
                path=conn.object_path, handled=True),
            )

    # MC calls GetStatus (maybe) and then Connect

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)

    # Connect succeeds
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    # Assert that the NormalizedName is harvested from the Connection at some
    # point
    while 1:
        e = q.expect('dbus-signal',
                interface=cs.ACCOUNT, signal='AccountPropertyChanged',
                path=account.object_path)
        if 'NormalizedName' in e.args[0]:
            assert e.args[0]['NormalizedName'] == 'myself', e.args
            break

    # Check the requested presence is online
    properties = account.GetAll(cs.ACCOUNT,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert properties is not None
    assert properties.get('RequestedPresence') == requested_presence, \
        properties.get('RequestedPresence')

    # Reconnect
    account.Reconnect(dbus_interface=cs.ACCOUNT)

    q.expect('dbus-method-call', method='Disconnect',
            path=conn.object_path, handled=True)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=tp_name_prefix + '.ConnectionManager.fakecm',
            path=tp_path_prefix + '/ConnectionManager/fakecm',
            interface=tp_name_prefix + '.ConnectionManager',
            handled=False)
    # The object path needs to be different from the first simulated
    # connection which we made above, because the object isn't removed
    # from this bus and it's actually hard to do so because it's not
    # really on a bus, it's on the queue. So let's just change the
    # object path and it's fine.
    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', 'second',
            'myself')
    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CONN_IFACE_REQUESTS],
                path=conn.object_path, handled=True),
            )

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    # Put the account offline
    requested_presence = (dbus.UInt32(cs.PRESENCE_TYPE_OFFLINE), 'offline', '')
    account.Set(cs.ACCOUNT,
            'RequestedPresence', requested_presence,
            dbus_interface=cs.PROPERTIES_IFACE)

    # In response, MC tells us to Disconnect, and we do
    q.expect('dbus-method-call', method='Disconnect',
            path=conn.object_path, handled=True)

    properties = account.GetAll(cs.ACCOUNT, dbus_interface=cs.PROPERTIES_IFACE)
    assert properties['Connection'] == '/'
    assert properties['ConnectionStatus'] == cs.CONN_STATUS_DISCONNECTED
    assert properties['CurrentPresence'] == requested_presence
    assert properties['RequestedPresence'] == requested_presence

if __name__ == '__main__':
    exec_test(test, {})
