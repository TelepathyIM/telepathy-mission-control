# Copyright (C) 2009 Nokia Corporation
# Copyright (C) 2009-2010 Collabora Ltd.
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
from mctest import exec_test, create_fakecm_account, get_account_manager
import constants as cs

def test(q, bus, mc):
    account_manager = get_account_manager(bus)
    account_manager_iface = dbus.Interface(account_manager, cs.AM)

    # fd.o #25684: creating similarly-named accounts in very quick succession
    # used to fail

    params = dbus.Dictionary({"account": "create-twice",
        "password": "secrecy"}, signature='sv')

    cm_name_ref = dbus.service.BusName(cs.tp_name_prefix +
            '.ConnectionManager.fakecm', bus=bus)
    account_manager = bus.get_object(cs.AM, cs.AM_PATH)
    am_iface = dbus.Interface(account_manager, cs.AM)

    call_async(q, am_iface, 'CreateAccount',
            'fakecm',
            'fakeprotocol',
            'fakeaccount',
            params,
            {})
    call_async(q, am_iface, 'CreateAccount',
            'fakecm',
            'fakeprotocol',
            'fakeaccount',
            params,
            {})

    ret1 = q.expect('dbus-return', method='CreateAccount')
    ret2 = q.expect('dbus-return', method='CreateAccount')

    path1 = ret1.value[0]
    path2 = ret2.value[0]
    assert path1 != path2

if __name__ == '__main__':
    exec_test(test, {})
