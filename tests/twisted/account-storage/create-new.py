# Test for "stringified GVariant per account" storage backend introduced in
# Mission Control 5.16, when creating a new account stored in this default
# backend
#
# Copyright (C) 2009-2010 Nokia Corporation
# Copyright (C) 2009-2014 Collabora Ltd.
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

from servicetest import (
    EventPattern, assertEquals,
    )
from mctest import (
    exec_test, create_fakecm_account, connect_to_mc,
    )
from storage_helper import (account_store)
import constants as cs

def test(q, bus, mc):
    ctl_dir = os.environ['MC_ACCOUNT_DIR']
    old_key_file_name = os.path.join(ctl_dir, 'accounts.cfg')
    newer_key_file_name = os.path.join(os.environ['XDG_DATA_HOME'],
            'telepathy', 'mission-control', 'accounts.cfg')
    new_variant_file_name = os.path.join(os.environ['XDG_DATA_HOME'],
            'telepathy', 'mission-control',
            'fakecm-fakeprotocol-dontdivert_40example_2ecom0.account')

    account_manager, properties, interfaces = connect_to_mc(q, bus, mc)

    assert properties.get('ValidAccounts') == [], \
        properties.get('ValidAccounts')
    assert properties.get('InvalidAccounts') == [], \
        properties.get('InvalidAccounts')

    params = dbus.Dictionary({"account": "dontdivert@example.com",
        "password": "secrecy",
        "snakes": dbus.UInt32(23)}, signature='sv')
    (simulated_cm, account) = create_fakecm_account(q, bus, mc, params)

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

    # Alter some miscellaneous r/w properties

    account_props.Set(cs.ACCOUNT, 'DisplayName', 'Work account')
    account_props.Set(cs.ACCOUNT, 'Icon', 'im-jabber')
    account_props.Set(cs.ACCOUNT, 'Nickname', 'Joe Bloggs')
    account_props.Set(cs.ACCOUNT, 'ConnectAutomatically', True)
    account_props.Set(cs.ACCOUNT, 'AutomaticPresence',
            (dbus.UInt32(cs.PRESENCE_EXTENDED_AWAY), 'xa',
                'never online'))

    # .. let's check the keyfile
    assert not os.path.exists(old_key_file_name)
    assert not os.path.exists(newer_key_file_name)
    assert os.path.exists(new_variant_file_name)
    assert 'Joe Bloggs' in open(new_variant_file_name).read()
    assertEquals("'fakecm'", account_store('get', 'variant-file', 'manager'))
    assertEquals("'fakeprotocol'", account_store('get', 'variant-file',
        'protocol'))
    assertEquals("'Work account'", account_store('get', 'variant-file',
        'DisplayName'))
    assertEquals("'im-jabber'", account_store('get', 'variant-file',
        'Icon'))
    assertEquals("'Joe Bloggs'", account_store('get', 'variant-file',
        'Nickname'))
    assertEquals('true', account_store('get', 'variant-file',
        'ConnectAutomatically'))
    assertEquals("(uint32 4, 'xa', 'never online')",
            account_store('get', 'variant-file', 'AutomaticPresence'))
    assertEquals("'dontdivert@example.com'",
            account_store('get', 'variant-file', 'param-account'))
    assertEquals("uint32 23",
            account_store('get', 'variant-file', 'param-snakes'))
    assertEquals("'secrecy'",
            account_store('get', 'variant-file', 'param-password'))

    assertEquals({'password': 'secrecy', 'account': 'dontdivert@example.com',
        'snakes': 23}, account.Properties.Get(cs.ACCOUNT, 'Parameters'))

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
    assert not os.path.exists(old_key_file_name)
    assert not os.path.exists(newer_key_file_name)
    assert not os.path.exists(new_variant_file_name)

if __name__ == '__main__':
    exec_test(test, {}, timeout=10, use_fake_accounts_service=False)
