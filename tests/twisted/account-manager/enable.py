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
        call_async
from mctest import exec_test, create_fakecm_account
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "smcv@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'RequestedPresence',
            (dbus.UInt32(cs.PRESENCE_TYPE_OFFLINE), 'offline', ''))
    q.expect('dbus-return', method='Set')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Enabled', False)
    q.expect('dbus-return', method='Set')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'ConnectAutomatically',
            False)
    q.expect('dbus-return', method='Set')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'RequestedPresence',
            (dbus.UInt32(cs.PRESENCE_TYPE_BUSY), 'busy', 'Testing Enabled'))
    q.expect('dbus-return', method='Set')

    # Go online by setting Enabled
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Enabled', True)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=cs.tp_name_prefix + '.ConnectionManager.fakecm',
            path=cs.tp_path_prefix + '/ConnectionManager/fakecm',
            interface=cs.tp_name_prefix + '.ConnectionManager',
            handled=False)

if __name__ == '__main__':
    exec_test(test, {})
