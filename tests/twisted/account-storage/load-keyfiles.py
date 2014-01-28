# Test for "stringified GVariant per account" storage backend introduced in
# Mission Control 5.16, when loading pre-prepared files
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

import errno
import os
import os.path

import dbus

from servicetest import (
    assertEquals, assertContains, assertDoesNotContain,
    )
from mctest import (
    MC, exec_test, get_fakecm_account, connect_to_mc,
    SimulatedConnectionManager,
    )
from storage_helper import (
    account_store,
    )
import constants as cs

def test(q, bus, mc):
    simulated_cm = SimulatedConnectionManager(q, bus)

    ctl_dir = os.environ['MC_ACCOUNT_DIR']
    old_key_file_name = os.path.join(ctl_dir, 'accounts.cfg')
    newer_key_file_name = os.path.join(os.environ['XDG_DATA_HOME'],
            'telepathy', 'mission-control', 'accounts.cfg')

    # We do several scenarios in one MC run, to speed up testing a bit.
    scenarios = ('low', 'priority', 'masked', 'migration', 'absentcm')

    variant_file_names = {}
    low_prio_variant_file_names = {}
    account_paths = {}
    tails = {}

    for s in scenarios:
        variant_file_names[s] = os.path.join(os.environ['XDG_DATA_HOME'],
                'telepathy', 'mission-control',
                'fakecm-fakeprotocol-dontdivert%s_40example_2ecom0.account'
                    % s)
        tails[s] = ('fakecm/fakeprotocol/dontdivert%s_40example_2ecom0' % s)
        account_paths[s] = cs.ACCOUNT_PATH_PREFIX + tails[s]
        low_prio_variant_file_names[s] = os.path.join(
                os.environ['XDG_DATA_DIRS'].split(':')[0],
                'telepathy', 'mission-control',
                'fakecm-fakeprotocol-dontdivert%s_40example_2ecom0.account' %
                    s)

        try:
            os.makedirs(os.path.dirname(variant_file_names[s]), 0700)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise

        try:
            os.makedirs(os.path.dirname(low_prio_variant_file_names[s]), 0700)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise

    # This is deliberately a lower-priority location
    open(low_prio_variant_file_names['low'], 'w').write(
"""{
'manager': <'fakecm'>,
'protocol': <'fakeprotocol'>,
'DisplayName': <'Account in a low-priority location'>,
'AutomaticPresence': <(uint32 2, 'available', '')>,
'Parameters': <{
    'account': <'dontdivertlow@example.com'>,
    'password': <'password_in_variant_file'>,
    'snakes': <uint32 42>
    }>
}
""")

    # This is in a lower-priority location and we don't know the
    # parameters' types yet
    open(low_prio_variant_file_names['migration'], 'w').write(
"""{
'manager': <'fakecm'>,
'protocol': <'fakeprotocol'>,
'DisplayName': <'Account in a low-priority location with KeyFileParameters'>,
'AutomaticPresence': <(uint32 2, 'available', '')>,
'KeyFileParameters': <{
    'account': 'dontdivertmigration@example.com',
    'password': 'password_in_variant_file',
    'snakes': '42'
    }>
}
""")

    # This is in a lower-priority location, and we don't know the
    # parameters' types, and we can't learn them by asking the CM
    # because it isn't installed
    open(low_prio_variant_file_names['absentcm'], 'w').write(
"""{
'manager': <'absentcm'>,
'protocol': <'absentprotocol'>,
'DisplayName': <'Account in a low-priority location with absent CM'>,
'AutomaticPresence': <(uint32 2, 'available', '')>,
'KeyFileParameters': <{
    'account': 'dontdivertabsentcm@example.com',
    'password': 'hello',
    'snakes': '42'
    }>
}
""")

    # This version of this account will be used
    open(variant_file_names['priority'], 'w').write("""{
'manager': <'fakecm'>,
'protocol': <'fakeprotocol'>,
'DisplayName': <'Visible'>,
'AutomaticPresence': <(uint32 2, 'available', '')>,
'KeyFileParameters': <{'account': 'dontdivertpriority@example.com',
    'password': 'password_in_variant_file',
    'snakes': '42'
    }>
}
""")
    # This one won't, because it's "masked" by the higher-priority one
    open(low_prio_variant_file_names['priority'], 'w').write("""{
'manager': <'fakecm'>,
'protocol': <'fakeprotocol'>,
'DisplayName': <'Hidden'>,
'Nickname': <'Hidden'>,
'AutomaticPresence': <(uint32 2, 'available', '')>,
'KeyFileParameters': <{'account': 'dontdivertpriority@example.com',
    'password': 'password_in_variant_file',
    'snakes': '42'
    }>
}
""")

    # This empty file is considered to "mask" the lower-priority one
    open(variant_file_names['masked'], 'w').write('')
    open(low_prio_variant_file_names['masked'], 'w').write("""{
'manager': <'fakecm'>,
'protocol': <'fakeprotocol'>,
'AutomaticPresence': <(uint32 2, 'available', '')>,
'KeyFileParameters': <{'account': 'dontdivert@example.com',
    'password': 'password_in_variant_file',
    'snakes': '42'
    }>
}
""")

    mc = MC(q, bus)
    account_manager, properties, interfaces = connect_to_mc(q, bus, mc)

    for s in scenarios:
        if s == 'masked':
            assertDoesNotContain(account_paths[s], properties['ValidAccounts'])
            assertDoesNotContain(account_paths[s], properties['InvalidAccounts'])
        elif s == 'absentcm':
            assertContains(account_paths[s], properties['InvalidAccounts'])
            assertDoesNotContain(account_paths[s], properties['ValidAccounts'])
        else:
            assertContains(account_paths[s], properties['ValidAccounts'])
            assertDoesNotContain(account_paths[s], properties['InvalidAccounts'])

    accounts = {}
    account_ifaces = {}

    for s in scenarios:
        if s != 'masked':
            accounts[s] = get_fakecm_account(bus, mc, account_paths[s])
            account_ifaces[s] = dbus.Interface(accounts[s], cs.ACCOUNT)

        if s not in ('masked', 'absentcm'):
            # We can't get untyped parameters if we don't know what types
            # the CM gives them.
            assertEquals(42, accounts[s].Properties.Get(cs.ACCOUNT,
                'Parameters')['snakes'])
            assertEquals(dbus.UInt32,
                    type(accounts[s].Properties.Get(cs.ACCOUNT,
                        'Parameters')['snakes']))

        # Files in lower-priority XDG locations aren't copied until something
        # actually changes, and they aren't deleted.

        if s == 'low':
            assert os.path.exists(low_prio_variant_file_names[s])

    # Delete the password (only), like Empathy 3.0-3.4 do when migrating.
    # This results in the higher-priority file being written out.
    account_ifaces['low'].UpdateParameters({}, ['password'])
    q.expect('dbus-signal',
            path=account_paths['low'],
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            predicate=(lambda e:
                'Parameters' in e.args[0]),
            )
    # Check the account has copied (not moved! XDG_DATA_DIRS are,
    # conceptually, read-only) 'low' from the old to the new name
    assert not os.path.exists(old_key_file_name)
    assert not os.path.exists(newer_key_file_name)
    assert os.path.exists(low_prio_variant_file_names['low'])
    assert os.path.exists(variant_file_names['low'])

    # test that priority works
    assertContains(account_paths["priority"], properties['ValidAccounts'])
    assertEquals('',
            accounts['priority'].Properties.Get(cs.ACCOUNT, 'Nickname'))
    assertEquals('Visible',
            accounts['priority'].Properties.Get(cs.ACCOUNT, 'DisplayName'))

    # test what happens when we delete an account that has a lower-priority
    # "other self": it becomes masked
    assert accounts['priority'].Remove() is None
    assert not os.path.exists(old_key_file_name)
    assert not os.path.exists(newer_key_file_name)
    assert os.path.exists(low_prio_variant_file_names['priority'])
    assert os.path.exists(variant_file_names['priority'])
    assert open(variant_file_names['priority'], 'r').read() == ''
    assertContains('password_in_variant_file',
            open(low_prio_variant_file_names['priority'], 'r').read())

    # The masked account is still masked
    assert open(variant_file_names['masked'], 'r').read() == ''

    # Teach the one that knows its CM that the 'password' is a string.
    # This results in the higher-priority file being written out.
    account_ifaces['migration'].UpdateParameters({'password': 'hello'}, [])
    q.expect('dbus-signal',
            path=account_paths['migration'],
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            predicate=(lambda e:
                'Parameters' in e.args[0]),
            )
    # Check the account has copied (not moved! XDG_DATA_DIRS are,
    # conceptually, read-only) 'migration' from the old to the new name
    assert not os.path.exists(old_key_file_name)
    assert not os.path.exists(newer_key_file_name)
    assert os.path.exists(low_prio_variant_file_names['migration'])
    assert os.path.exists(variant_file_names['migration'])
    assertEquals("'hello'", account_store('get', 'variant-file',
        'param-password', account=tails['migration']))
    # Parameters whose types are still unknown are copied too, but their
    # types are still unknown
    assertEquals("keyfile-escaped '42'", account_store('get', 'variant-file',
        'param-snakes', account=tails['migration']))

    # 'absentcm' is still only in the low-priority location: we can't
    # known the types of its parameters
    assert not os.path.exists(old_key_file_name)
    assert not os.path.exists(newer_key_file_name)
    assert os.path.exists(low_prio_variant_file_names['absentcm'])
    assert not os.path.exists(variant_file_names['absentcm'])

if __name__ == '__main__':
    exec_test(test, {}, preload_mc=False, use_fake_accounts_service=False)
