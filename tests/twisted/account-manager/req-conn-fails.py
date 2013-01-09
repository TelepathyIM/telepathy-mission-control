# vim: set fileencoding=utf-8 :
#
# Copyright © 2011-2012 Collabora Ltd.
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
    params = dbus.Dictionary(
        {"account": "someguy@example.com",
         "password": "secrecy",
        }, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

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

    q.dbus_raise(e.message, cs.NOT_IMPLEMENTED, "CM is broken")

    # MC should report the connection dying.
    e = q.expect('dbus-signal', signal='AccountPropertyChanged',
        predicate=lambda e: 'ConnectionError' in e.args[0])
    changed, = e.args
    assertEquals('/', changed['Connection'])
    assertEquals(cs.CONN_STATUS_DISCONNECTED, changed['ConnectionStatus'])
    assertEquals(cs.CONN_STATUS_REASON_NONE, changed['ConnectionStatusReason'])
    assertEquals(cs.NOT_IMPLEMENTED, changed['ConnectionError'])

if __name__ == '__main__':
    exec_test(test)
