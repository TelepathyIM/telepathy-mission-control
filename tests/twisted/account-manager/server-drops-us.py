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
        call_async, sync_dbus, TimeoutError
from mctest import exec_test, SimulatedConnection, create_fakecm_account,\
        SimulatedChannel
import constants as cs

params = dbus.Dictionary({"account": "someguy@example.com",
    "password": "secrecy"}, signature='sv')

def test(q, bus, mc):
    cm_name_ref = dbus.service.BusName(
            tp_name_prefix + '.ConnectionManager.fakecm', bus=bus)

    # Create an account
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    call_async(q, account, 'Set', cs.ACCOUNT, 'Enabled', False,
            dbus_interface=cs.PROPERTIES_IFACE)
    q.expect('dbus-return', method='Set')

    # Enable the account
    call_async(q, account, 'Set', cs.ACCOUNT, 'Enabled', True,
            dbus_interface=cs.PROPERTIES_IFACE)

    # Set online presence
    presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_BUSY), 'busy',
            'Fixing MC bugs'), signature='uss')
    call_async(q, account, 'Set', cs.ACCOUNT,
            'RequestedPresence', presence,
            dbus_interface=cs.PROPERTIES_IFACE)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=tp_name_prefix + '.ConnectionManager.fakecm',
            path=tp_path_prefix + '/ConnectionManager/fakecm',
            interface=tp_name_prefix + '.ConnectionManager',
            handled=False)

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', '_',
            'myself', has_presence=True)

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    # MC calls GetStatus (maybe) and then Connect

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)

    # Connect succeeds
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    conn = drop_and_expect_reconnect(q, bus, conn)
    conn = drop_and_expect_reconnect(q, bus, conn)
    conn = drop_and_expect_reconnect(q, bus, conn)

    forbidden = [EventPattern('dbus-method-call', method='RequestConnection')]
    q.forbid_events(forbidden)

    # Connection falls over for a miscellaneous reason
    conn.StatusChanged(cs.CONN_STATUS_DISCONNECTED,
            cs.CONN_STATUS_REASON_NETWORK_ERROR)
    # Right, that's it, I'm giving up...

    # This test can be considered to have succeeded if we don't
    # RequestConnection again before the test fails due to timeout.
    try:
        q.expect('the end of the world')
    except TimeoutError:
        return
    else:
        raise AssertionError('An impossible event happened')

def drop_and_expect_reconnect(q, bus, conn):
    # Connection falls over for a miscellaneous reason
    conn.StatusChanged(cs.CONN_STATUS_DISCONNECTED,
            cs.CONN_STATUS_REASON_NETWORK_ERROR)

    # MC reconnects

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=tp_name_prefix + '.ConnectionManager.fakecm',
            path=tp_path_prefix + '/ConnectionManager/fakecm',
            interface=tp_name_prefix + '.ConnectionManager',
            handled=False)

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', '_',
            'myself', has_presence=True)

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    # MC calls GetStatus (maybe) and then Connect

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)

    # Connect succeeds
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    return conn

if __name__ == '__main__':
    exec_test(test, {})
