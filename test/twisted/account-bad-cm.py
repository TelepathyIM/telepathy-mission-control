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

"""Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=20880
"""

import dbus

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from fakecm import start_fake_connection_manager
from mctest import exec_test
import constants as cs

FakeCM_bus_name = "com.example.FakeCM"
ConnectionManager_object_path = "/com/example/FakeCM/ConnectionManager"


def test(q, bus, mc):
    # Get the AccountManager interface
    account_manager = bus.get_object(cs.AM, cs.AM_PATH)
    account_manager_iface = dbus.Interface(account_manager, cs.AM)

    # Create an account with a bad Connection_Manager - it should fail

    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    call_async(q, account_manager_iface, 'CreateAccount',
            'nonexistent_cm', # Connection_Manager
            'fakeprotocol', # Protocol
            'fakeaccount', #Display_Name
            params, # Parameters
            {}, # Properties
            )
    q.expect('dbus-error', method='CreateAccount')

    # Create an account with a bad Protocol - it should fail

    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    call_async(q, account_manager_iface, 'CreateAccount',
            'fakecm', # Connection_Manager
            'nonexistent-protocol', # Protocol
            'fakeaccount', #Display_Name
            params, # Parameters
            {}, # Properties
            )
    q.expect('dbus-error', method='CreateAccount')

    # Create an account with incomplete Parameters - it should fail

    params = dbus.Dictionary({"account": "someguy@example.com"},
            signature='sv')
    call_async(q, account_manager_iface, 'CreateAccount',
            'fakecm', # Connection_Manager
            'fakeprotocol', # Protocol
            'fakeaccount', #Display_Name
            params, # Parameters
            {}, # Properties
            )
    q.expect('dbus-error', method='CreateAccount')

if __name__ == '__main__':
    exec_test(test, {})
