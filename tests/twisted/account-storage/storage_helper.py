# Helper code for former default account storage backends
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
    assertEquals, assertContains, assertLength,
    )
from mctest import (
    exec_test, get_fakecm_account, connect_to_mc,
    MC, SimulatedConnectionManager,
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

def test_keyfile(q, bus, mc, how_old='5.12'):
    simulated_cm = SimulatedConnectionManager(q, bus)

    if how_old == '5.12':
        # This is not actually ~/.mission-control, but it uses the same
        # code paths.
        dot_mission_control = os.environ['MC_ACCOUNT_DIR']
        old_key_file_name = os.path.join(dot_mission_control, 'accounts.cfg')

        os.makedirs(dot_mission_control +
            '/fakecm/fakeprotocol/dontdivert1_40example_2ecom0')
        avatar_bin = open(dot_mission_control +
            '/fakecm/fakeprotocol/dontdivert1_40example_2ecom0/avatar.bin',
            'w')
        avatar_bin.write('hello, world')
        avatar_bin.close()
    elif how_old == '5.14':
        # Same format, different location.
        old_key_file_name = os.path.join(os.environ['XDG_DATA_HOME'],
                'telepathy', 'mission-control', 'accounts.cfg')

        # exercise override of an avatar in XDG_DATA_DIRS
        avatar_dir = (os.environ['XDG_DATA_DIRS'].split(':')[0] +
            '/telepathy/mission-control')
        os.makedirs(avatar_dir)
        avatar_bin = open(avatar_dir +
            '/fakecm-fakeprotocol-dontdivert1_40example_2ecom0.avatar',
            'w')
        avatar_bin.write('hello, world')
        avatar_bin.close()
    else:
        raise AssertionError('Unsupported value for how_old')

    a1_new_variant_file_name = os.path.join(os.environ['XDG_DATA_HOME'],
            'telepathy-1', 'mission-control',
            'fakecm-fakeprotocol-dontdivert1_40example_2ecom0.account')
    a1_tail = 'fakecm/fakeprotocol/dontdivert1_40example_2ecom0'

    a2_new_variant_file_name = os.path.join(os.environ['XDG_DATA_HOME'],
            'telepathy-1', 'mission-control',
            'fakecm-fakeprotocol-dontdivert2_40example_2ecom0.account')
    a2_tail = 'fakecm/fakeprotocol/dontdivert2_40example_2ecom0'

    try:
        os.makedirs(os.path.dirname(old_key_file_name), 0700)
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise

    open(old_key_file_name, 'w').write(
r"""# Telepathy accounts
[%s]
manager=fakecm
protocol=fakeprotocol
param-account=dontdivert1@example.com
param-password=1
DisplayName=First among equals
AutomaticPresence=2;available;;
AvatarMime=text/plain
avatar_token=hello, world

[%s]
manager=fakecm
protocol=fakeprotocol
param-account=dontdivert2@example.com
param-password=2
DisplayName=Second to none
AutomaticPresence=2;available;;
""" % (a1_tail, a2_tail))

    mc = MC(q, bus)
    account_manager, properties, interfaces = connect_to_mc(q, bus, mc)

    # During MC startup, it moved the old keyfile's contents into
    # variant-based files, and deleted the old keyfile.
    assert not os.path.exists(old_key_file_name)
    assert os.path.exists(a1_new_variant_file_name)
    assert os.path.exists(a2_new_variant_file_name)
    assertEquals("'First among equals'",
            account_store('get', 'variant-file', 'DisplayName',
                account=a1_tail))
    assertEquals("'Second to none'",
            account_store('get', 'variant-file', 'DisplayName',
                account=a2_tail))
    # Because the CM is installed, we can work out the right types
    # for the parameters, too.
    assertEquals("'dontdivert1@example.com'",
            account_store('get', 'variant-file', 'param-account',
                account=a1_tail))
    assertEquals("'dontdivert2@example.com'",
            account_store('get', 'variant-file', 'param-account',
                account=a2_tail))

    # Also, MC has both accounts in memory...
    assertContains(cs.ACCOUNT_PATH_PREFIX + a1_tail,
            properties['UsableAccounts'])
    account = get_fakecm_account(bus, mc, cs.ACCOUNT_PATH_PREFIX + a1_tail)
    assertEquals('dontdivert1@example.com',
            account.Properties.Get(cs.ACCOUNT, 'Parameters')['account'])
    assertEquals('First among equals',
            account.Properties.Get(cs.ACCOUNT, 'DisplayName'))
    assertEquals((dbus.ByteArray('hello, world'), 'text/plain'),
            account.Properties.Get(cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
                byte_arrays=True))

    assertContains(cs.ACCOUNT_PATH_PREFIX + a2_tail,
            properties['UsableAccounts'])
    account = get_fakecm_account(bus, mc, cs.ACCOUNT_PATH_PREFIX + a2_tail)
    assertEquals('dontdivert2@example.com',
            account.Properties.Get(cs.ACCOUNT, 'Parameters')['account'])
    assertEquals('Second to none',
            account.Properties.Get(cs.ACCOUNT, 'DisplayName'))

    # ... and no other accounts.
    assertLength(2, properties['UsableAccounts'])
