# Copyright (C) 2009-2010 Nokia Corporation
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

import time
import os
import os.path
import signal

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, create_fakecm_account, get_account_manager, \
    get_fakecm_account, make_mc, connect_to_mc, keyfile_read
import constants as cs

def test(q, bus, mc):
    empty_key_file_name = os.path.join(os.environ['MC_ACCOUNT_DIR'], 'accounts.cfg')

    key_file_name = os.path.join(os.getenv('XDG_CACHE_HOME'),
        'mcp-test-diverted-account-plugin.conf')
    group = 'fakecm/fakeprotocol/someguy_40example_2ecom0'

    account_manager, properties, interfaces = connect_to_mc(q, bus, mc)

    assert properties.get('ValidAccounts') == [], \
        properties.get('ValidAccounts')
    assert properties.get('InvalidAccounts') == [], \
        properties.get('InvalidAccounts')

    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_path = account.__dbus_object_path__

    # Check the account is correctly created
    properties = account_manager.GetAll(cs.AM,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert properties is not None
    assert properties.get('ValidAccounts') == [account_path], properties
    account_path = properties['ValidAccounts'][0]
    assert isinstance(account_path, dbus.ObjectPath), repr(account_path)
    assert properties.get('InvalidAccounts') == [], properties

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)
    nokia_compat = dbus.Interface(account, cs.ACCOUNT_IFACE_NOKIA_COMPAT)

    # Alter some miscellaneous r/w properties

    account_props.Set(cs.ACCOUNT, 'DisplayName', 'Work account')
    account_props.Set(cs.ACCOUNT, 'Icon', 'im-jabber')
    account_props.Set(cs.ACCOUNT, 'Nickname', 'Joe Bloggs')
    nokia_compat.SetHasBeenOnline()
    account_props.Set(cs.ACCOUNT_IFACE_NOKIA_COMPAT,
        'SecondaryVCardFields', ['x-badger', 'x-mushroom'])

    secret_debug_api = dbus.Interface(bus.get_object(cs.AM, "/"),
        'org.freedesktop.Telepathy.MissionControl5.RegressionTests')
    secret_debug_api.Abort()

    # Make sure MC exits
    q.expect('dbus-signal', signal='NameOwnerChanged',
        predicate=(lambda e:
            e.args[0] == 'org.freedesktop.Telepathy.AccountManager' and
            e.args[2] == ''))

    # .. let's check the diverted keyfile
    kf = keyfile_read(key_file_name)
    assert group in kf, kf
    assert kf[group]['manager'] == 'fakecm'
    assert kf[group]['protocol'] == 'fakeprotocol'
    assert kf[group]['param-account'] == params['account'], kf
    assert kf[group]['param-password'] == params['password'], kf
    assert kf[group]['DisplayName'] == 'Work account', kf
    assert kf[group]['Icon'] == 'im-jabber', kf
    assert kf[group]['Nickname'] == 'Joe Bloggs', kf
    assert kf[group]['HasBeenOnline'] == 'true', kf
    assert kf[group]['SecondaryVCardFields'] == 'x-badger;x-mushroom;', kf

    # default keyfile should be empty
    ekf = keyfile_read(empty_key_file_name)
    assert ekf == { None: {} }, ekf

    # Reactivate MC
    bus.get_object(cs.MC, "/")

    # Wait until it's up
    q.expect('dbus-signal', signal='NameOwnerChanged',
        predicate=(lambda e:
            e.args[0] == 'org.freedesktop.Telepathy.AccountManager' and
            e.args[2] != ''))

    mc = make_mc(bus, q.append)
    account_manager, properties, interfaces = connect_to_mc(q, bus, mc)
    account = get_fakecm_account(bus, mc, account_path)

    account_iface = dbus.Interface(account, cs.ACCOUNT)

    # Delete the account
    assert account_iface.Remove() is None
    account_event, account_manager_event = q.expect_many(
        EventPattern('dbus-signal',
            path=account_path,
            signal='Removed',
            interface=cs.ACCOUNT,
            args=[]
            ),
        EventPattern('dbus-signal',
            path=cs.AM_PATH,
            signal='AccountRemoved',
            interface=cs.AM,
            args=[account_path]
            ),
        )

    # Check the account is correctly deleted
    kf = keyfile_read(key_file_name)
    assert group not in kf, kf

if __name__ == '__main__':
    exec_test(test, {}, timeout=10)
