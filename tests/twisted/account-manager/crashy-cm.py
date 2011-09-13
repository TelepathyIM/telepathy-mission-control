# vim: set fileencoding=utf-8 :
#
# crashy-cm: tests that MC sets at least moderately-useful error information on
#            accounts when they're disconnected due to the CM crashing.
#
# Copyright © 2011 Collabora Ltd.
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
from servicetest import (
    tp_name_prefix, tp_path_prefix, assertEquals,
)
from mctest import (
    exec_test, create_fakecm_account, SimulatedConnection,
)
import constants as cs

def test(q, bus, mc):
    cm_bus = dbus.bus.BusConnection()
    cm_bus.set_exit_on_disconnect(False)   # we'll disconnect later
    q.attach_to_bus(cm_bus)

    params = dbus.Dictionary(
        {"account": "someguy@example.com",
         "password": "secrecy",
        }, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params,
        cm_bus=cm_bus)

    account.Properties.Set(cs.ACCOUNT, 'Enabled', True)

    # Set online presence
    presence = dbus.Struct(
        (dbus.UInt32(cs.PRESENCE_TYPE_BUSY), 'busy', 'Fixing MC bugs'),
        signature='uss')
    account.Properties.Set(cs.ACCOUNT, 'RequestedPresence', presence)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=tp_name_prefix + '.ConnectionManager.fakecm',
            path=tp_path_prefix + '/ConnectionManager/fakecm',
            interface=tp_name_prefix + '.ConnectionManager',
            handled=False)

    conn = SimulatedConnection(q, cm_bus, 'fakecm', 'fakeprotocol', '_',
            'myself', has_presence=True)

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)

    # Connect succeeds
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    # CM crashes
    conn.release_name()
    del cm_name_ref
    cm_bus.flush()
    cm_bus.close()

    # MC should report the connection dying.
    e = q.expect('dbus-signal', signal='AccountPropertyChanged',
        predicate=lambda e: 'ConnectionError' in e.args[0])
    changed, = e.args
    assertEquals('/', changed['Connection'])
    assertEquals(cs.CONN_STATUS_DISCONNECTED, changed['ConnectionStatus'])
    # In the absence of a better code, None will have to do.
    assertEquals(cs.CONN_STATUS_REASON_NONE, changed['ConnectionStatusReason'])
    # And NoReply will do as “it crashed”.
    assertEquals(cs.DBUS_ERROR_NO_REPLY, changed['ConnectionError'])

if __name__ == '__main__':
    exec_test(test)
