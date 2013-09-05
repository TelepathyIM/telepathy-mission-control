# Test for default account storage backend.
#
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
        call_async, assertEquals
from mctest import (
    exec_test, create_fakecm_account, get_fakecm_account, connect_to_mc,
    keyfile_read, tell_mc_to_die, resuscitate_mc
    )
import constants as cs

# This doesn't escape its parameters before passing them to the shell,
# so be careful.
def account_store(op, backend, key=None, value=None,
        account='fakecm/fakeprotocol/dontdivert_40example_2ecom0'):
    cmd = [ '../account-store', op, backend, account ]
    if key:
        cmd.append(key)
        if value:
            cmd.append(value)

    lines = os.popen(' '.join(cmd)).read()
    ret = []
    for line in lines.split('\n'):
        if line.startswith('** '):
            continue

        if line:
            ret.append(line)

    if len(ret) > 0:
        return ret[0]
    else:
        return None

def test(q, bus, mc):
    ctl_dir = os.environ['MC_ACCOUNT_DIR']
    old_key_file_name = os.path.join(ctl_dir, 'accounts.cfg')
    new_key_file_name = os.path.join(os.environ['XDG_DATA_HOME'],
            'telepathy', 'mission-control', 'accounts.cfg')
    group = 'fakecm/fakeprotocol/dontdivert_40example_2ecom0'

    account_manager, properties, interfaces = connect_to_mc(q, bus, mc)

    assert properties.get('ValidAccounts') == [], \
        properties.get('ValidAccounts')
    assert properties.get('InvalidAccounts') == [], \
        properties.get('InvalidAccounts')

    params = dbus.Dictionary({"account": "dontdivert@example.com",
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

    # Alter some miscellaneous r/w properties

    account_props.Set(cs.ACCOUNT, 'DisplayName', 'Work account')
    account_props.Set(cs.ACCOUNT, 'Icon', 'im-jabber')
    account_props.Set(cs.ACCOUNT, 'Nickname', 'Joe Bloggs')

    tell_mc_to_die(q, bus)

    # .. let's check the keyfile
    assert not os.path.exists(old_key_file_name)
    kf = keyfile_read(new_key_file_name)
    assert group in kf, kf
    assert kf[group]['manager'] == 'fakecm'
    assert kf[group]['protocol'] == 'fakeprotocol'
    assert kf[group]['param-account'] == params['account'], kf
    assert kf[group]['DisplayName'] == 'Work account', kf
    assert kf[group]['Icon'] == 'im-jabber', kf
    assert kf[group]['Nickname'] == 'Joe Bloggs', kf

    # This works wherever the password is stored
    pwd = account_store('get', 'default', 'param-password')
    assert pwd == params['password'], pwd

    # We no longer use gnome-keyring, so the password is stored as clear-text.
    assert kf[group]['param-password'] == params['password'], kf

    # Reactivate MC
    account_manager, properties, interfaces = resuscitate_mc(q, bus, mc)
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
    assert not os.path.exists(old_key_file_name)
    kf = keyfile_read(new_key_file_name)
    assert group not in kf, kf

    # Tell MC to die, again
    tell_mc_to_die(q, bus)

    low_prio_key_file_name = os.path.join(
            os.environ['XDG_DATA_DIRS'].split(':')[0],
            'telepathy', 'mission-control', 'accounts.cfg')
    os.makedirs(os.path.dirname(low_prio_key_file_name), 0700)

    # This is deliberately a lower-priority location
    os.remove(new_key_file_name)
    open(low_prio_key_file_name, 'w').write(
r"""# Telepathy accounts
[%s]
manager=fakecm
protocol=fakeprotocol
param-account=dontdivert@example.com
param-password=password_in_keyfile
DisplayName=New and improved account
AutomaticPresence=2;available;;
""" % group)

    account_manager, properties, interfaces = resuscitate_mc(q, bus, mc)
    account = get_fakecm_account(bus, mc, account_path)
    account_iface = dbus.Interface(account, cs.ACCOUNT)

    # Files in lower-priority XDG locations aren't copied until something
    # actually changes, and they aren't deleted.
    assert not os.path.exists(new_key_file_name)
    assert os.path.exists(low_prio_key_file_name)

    # Delete the password (only), like Empathy 3.0-3.4 do when migrating
    account_iface.UpdateParameters({}, ['password'])
    q.expect('dbus-signal',
            path=account_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            predicate=(lambda e:
                'Parameters' in e.args[0]),
            )

    # Tell MC to die yet again
    tell_mc_to_die(q, bus)

    # Check the account has copied (not moved! XDG_DATA_DIRS are,
    # conceptually, read-only) from the old to the new name
    assert not os.path.exists(old_key_file_name)
    assert os.path.exists(low_prio_key_file_name)
    kf = keyfile_read(new_key_file_name)
    assert 'param-password' not in kf[group]
    pwd = account_store('get', 'default', 'param-password')
    assertEquals(None, pwd)

    # Write out an account configuration in the old keyfile, to test
    # migration
    os.remove(new_key_file_name)
    os.remove(low_prio_key_file_name)
    open(old_key_file_name, 'w').write(
r"""# Telepathy accounts
[%s]
manager=fakecm
protocol=fakeprotocol
param-account=dontdivert@example.com
DisplayName=Ye olde account
AutomaticPresence=2;available;;
""" % group)

    account_manager, properties, interfaces = resuscitate_mc(q, bus, mc)
    account = get_fakecm_account(bus, mc, account_path)
    account_iface = dbus.Interface(account, cs.ACCOUNT)

    # This time it *does* get moved (really copied+deleted) automatically
    # during MC startup
    assert not os.path.exists(old_key_file_name)
    assert not os.path.exists(low_prio_key_file_name)
    kf = keyfile_read(new_key_file_name)
    assert 'param-password' not in kf[group]
    assertEquals('Ye olde account', kf[group]['DisplayName'])

if __name__ == '__main__':
    ctl_dir = os.environ['MC_ACCOUNT_DIR']
    try:
        os.mkdir(ctl_dir, 0700)
    except OSError:
        pass
    exec_test(test, {}, timeout=10, use_fake_accounts_service=False)
