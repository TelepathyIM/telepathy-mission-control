# Copyright (C) 2010 Nokia Corporation
# Copyright (C) 2010 Collabora Ltd.
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

from servicetest import EventPattern, tp_name_prefix, call_async, assertEquals
from mctest import exec_test, create_fakecm_account
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "wjt@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    srv_name = 'fu-bar-42'
    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    # defaults to the empty string
    assertEquals(account_props.Get(cs.ACCOUNT, 'Service'), '');
    
    # set to a new value after creation
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Service', srv_name);
    q.expect_many(
        EventPattern('dbus-signal',
            path=account.object_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            args=[{'Service': srv_name}]),
        EventPattern('dbus-return', method='Set'),
        )
    assertEquals(account_props.Get(cs.ACCOUNT, 'Service'), srv_name)

    # set to an invalid value (no actual change should occur)

    # leading non-alphabetic (make sure _ isn't considered alphabetic)
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Service', '_fu-bar');
    q.expect_many(EventPattern('dbus-error', method='Set'))
    assertEquals(account_props.Get(cs.ACCOUNT, 'Service'), srv_name)

    # leading non-alphabetic
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Service', '.moose');
    q.expect_many(EventPattern('dbus-error', method='Set'))
    assertEquals(account_props.Get(cs.ACCOUNT, 'Service'), srv_name)

    # gregexes have an option to be lenient about trailing newlines:
    # this makes sure we haven't made that mistake
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Service', srv_name + '\n');
    q.expect_many(EventPattern('dbus-error', method='Set'))
    assertEquals(account_props.Get(cs.ACCOUNT, 'Service'), srv_name)

    # set to an empty string
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Service', '');
    q.expect_many(EventPattern('dbus-return', method='Set'))
    assertEquals(account_props.Get(cs.ACCOUNT, 'Service'), '')

    # test creation with a service
    account_manager = bus.get_object(cs.AM, cs.AM_PATH)
    am_iface = dbus.Interface(account_manager, cs.AM)

    service_prop = dbus.Dictionary({
        cs.ACCOUNT + '.Service': "moomin-troll",
        }, signature='sv')

    call_async(q, am_iface, 'CreateAccount',
            'fakecm',       # Connection_Manager
            'fakeprotocol', # Protocol
            'fakeaccount',  # Display_Name
            params,         # Parameters
            service_prop,   # Properties
            )

    ret = q.expect('dbus-return', method='CreateAccount')
    path = ret.value[0]
    account = bus.get_object(cs.tp_name_prefix + '.AccountManager', path)
    if_props = dbus.Interface(account, cs.PROPERTIES_IFACE)
    props = if_props.GetAll(cs.ACCOUNT)
    assertEquals(props.get('Service'), 'moomin-troll')

    # attempt creation with a bogus service
    service_prop = dbus.Dictionary({cs.ACCOUNT + '.Service': "1337"},
                                   signature='sv')

    call_async(q, am_iface, 'CreateAccount',
            'fakecm',       # Connection_Manager
            'fakeprotocol', # Protocol
            'fakeaccount',  # Display_Name
            params,         # Parameters
            service_prop,   # Properties
            )

    ret = q.expect('dbus-error', method='CreateAccount')

if __name__ == '__main__':
    exec_test(test, {})
