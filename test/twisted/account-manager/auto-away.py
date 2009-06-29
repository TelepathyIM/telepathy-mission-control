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
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix
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

    # The account is initially valid but disabled
    assert not account.Get(cs.ACCOUNT, 'Enabled',
            dbus_interface=cs.PROPERTIES_IFACE)
    assert account.Get(cs.ACCOUNT, 'Valid',
            dbus_interface=cs.PROPERTIES_IFACE)

    # Enable the account
    account.Set(cs.ACCOUNT, 'Enabled', True,
            dbus_interface=cs.PROPERTIES_IFACE)
    q.expect('dbus-signal',
            path=account.object_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT)

    assert account.Get(cs.ACCOUNT, 'Enabled',
            dbus_interface=cs.PROPERTIES_IFACE)
    assert account.Get(cs.ACCOUNT, 'Valid',
            dbus_interface=cs.PROPERTIES_IFACE)

    # Check the requested presence is offline
    properties = account.GetAll(cs.ACCOUNT,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert properties is not None
    assert properties.get('RequestedPresence') == \
        dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_OFFLINE),
            'offline', '')), \
        properties.get('RequestedPresence')

    # Go online
    requested_presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_AVAILABLE),
        dbus.String(u'available'), dbus.String(u'staring at the sea')))
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
            'myself', has_presence=True)
    conn.statuses = dbus.Dictionary({
            'available': (cs.PRESENCE_TYPE_AVAILABLE, True, True),
            'away': (cs.PRESENCE_TYPE_AWAY, True, True),
            'busy': (cs.PRESENCE_TYPE_BUSY, True, True),
            'offline': (cs.PRESENCE_TYPE_OFFLINE, False, False),
        }, signature='s(ubb)')

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    # MC calls GetStatus (maybe) and then Connect

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)

    # Connect succeeds
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)
    conn.presence = dbus.Struct((cs.PRESENCE_TYPE_AVAILABLE, 'available', ''),
            signature='uss')

    # MC does some setup, including fetching the list of Channels

    get_statuses = q.expect('dbus-method-call',
            interface=cs.PROPERTIES_IFACE, method='Get',
            args=[cs.CONN_IFACE_SIMPLE_PRESENCE, 'Statuses'],
            path=conn.object_path, handled=True)

    call, signal = q.expect_many(
            EventPattern('dbus-method-call',
                path=conn.object_path,
                interface=cs.CONN_IFACE_SIMPLE_PRESENCE, method='SetPresence',
                args=['available', 'staring at the sea'],
                handled=True),
            EventPattern('dbus-signal',
                path=account.object_path,
                interface=cs.ACCOUNT, signal='AccountPropertyChanged',
                predicate = lambda e: 'CurrentPresence' in e.args[0]),
            )
    assert signal.args[0]['CurrentPresence'] == (cs.PRESENCE_TYPE_AVAILABLE,
        'available', '')

    e = q.expect('dbus-signal',
            path=account.object_path,
            interface=cs.ACCOUNT, signal='AccountPropertyChanged',
            predicate = lambda e: 'CurrentPresence' in e.args[0])
    assert e.args[0]['CurrentPresence'] == requested_presence

    # Check the requested presence is online
    properties = account.GetAll(cs.ACCOUNT,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert properties is not None
    assert properties.get('RequestedPresence') == requested_presence, \
        properties.get('RequestedPresence')

    # This is normally a C API, only exposed to D-Bus here for testing
    secret_debug_api = dbus.Interface(bus.get_object(cs.AM, "/"),
        'org.freedesktop.Telepathy.MissionControl5.RegressionTests')
    MCD_SYSTEM_IDLE = 32

    # Set the idle flag
    secret_debug_api.ChangeSystemFlags(dbus.UInt32(MCD_SYSTEM_IDLE),
            dbus.UInt32(0))

    e = q.expect('dbus-method-call',
            path=conn.object_path,
            interface=cs.CONN_IFACE_SIMPLE_PRESENCE, method='SetPresence',
            args=['away', ''],
            handled=True)

    # Unset the idle flag
    secret_debug_api.ChangeSystemFlags(dbus.UInt32(0),
            dbus.UInt32(MCD_SYSTEM_IDLE))

    # MC puts the account back online

    e = q.expect('dbus-method-call',
            path=conn.object_path,
            interface=cs.CONN_IFACE_SIMPLE_PRESENCE, method='SetPresence',
            args=['available', 'staring at the sea'],
            handled=True)

    # Go to a non-Available status
    requested_presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_BUSY),
        dbus.String(u'busy'), dbus.String(u'in the great below')))
    account.Set(cs.ACCOUNT,
            'RequestedPresence', requested_presence,
            dbus_interface=cs.PROPERTIES_IFACE)
    e = q.expect('dbus-method-call',
            path=conn.object_path,
            interface=cs.CONN_IFACE_SIMPLE_PRESENCE, method='SetPresence',
            args=['busy', 'in the great below'],
            handled=True)

    forbidden = [EventPattern('dbus-method-call',
            path=conn.object_path,
            interface=cs.CONN_IFACE_SIMPLE_PRESENCE, method='SetPresence')]
    q.forbid_events(forbidden)

    # Set the idle flag
    secret_debug_api.ChangeSystemFlags(dbus.UInt32(MCD_SYSTEM_IDLE),
            dbus.UInt32(0))

    # MC does not put the account away

    # Unset the idle flag
    secret_debug_api.ChangeSystemFlags(dbus.UInt32(0),
            dbus.UInt32(MCD_SYSTEM_IDLE))

    # MC does not put the account back online

    q.unforbid_events(forbidden)

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
