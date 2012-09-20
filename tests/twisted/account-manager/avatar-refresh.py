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
"""Feature test for signing in and setting an avatar, on CMs like Salut where
the avatar must be reset every time you sign in.
"""

import os

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, assertEquals
from mctest import exec_test, SimulatedConnection, create_fakecm_account, MC
import constants as cs

cm_name_ref = dbus.service.BusName(
        cs.tp_name_prefix + '.ConnectionManager.fakecm', bus=dbus.SessionBus())

account_id = 'fakecm/fakeprotocol/jc_2edenton_40unatco_2eint'

def preseed():

    accounts_dir = os.environ['MC_ACCOUNT_DIR']

    try:
        os.mkdir(accounts_dir, 0700)
    except OSError:
        pass

    accounts_cfg = open(accounts_dir + '/accounts.cfg', 'w')
    accounts_cfg.write("""# Telepathy accounts
[%s]
manager=fakecm
protocol=fakeprotocol
DisplayName=Work account
NormalizedName=jc.denton@unatco.int
param-account=jc.denton@unatco.int
param-password=ionstorm
Enabled=1
ConnectAutomatically=1
AutomaticPresenceType=2
AutomaticPresenceStatus=available
AutomaticPresenceMessage=My vision is augmented
Nickname=JC
AvatarMime=image/jpeg
avatar_token=Deus Ex
""" % account_id)
    accounts_cfg.close()

    os.makedirs(accounts_dir + '/' + account_id)
    avatar_bin = open(accounts_dir + '/' + account_id + '/avatar.bin', 'w')
    avatar_bin.write('Deus Ex')
    avatar_bin.close()

    account_connections_file = open(accounts_dir + '/.mc_connections', 'w')
    account_connections_file.write("")
    account_connections_file.close()

def test(q, bus, unused):

    expected_params = {
            'account': 'jc.denton@unatco.int',
            'password': 'ionstorm',
            }

    mc = MC(q, bus)

    e = q.expect('dbus-method-call', method='RequestConnection',
                args=['fakeprotocol', expected_params],
                destination=cs.tp_name_prefix + '.ConnectionManager.fakecm',
                path=cs.tp_path_prefix + '/ConnectionManager/fakecm',
                interface=cs.tp_name_prefix + '.ConnectionManager',
                handled=False)

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', '_',
            'myself', has_avatars=True, avatars_persist=False)

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    account_path = (cs.tp_path_prefix + '/Account/' + account_id)

    q.expect('dbus-method-call', method='Connect',
                path=conn.object_path, handled=True, interface=cs.CONN)

    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    _, _, e = q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_AVATARS, method='GetKnownAvatarTokens',
                args=[[conn.self_handle]],
                handled=True),
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_AVATARS, method='SetAvatar',
                args=['Deus Ex', 'image/jpeg'],
                handled=True),
            EventPattern('dbus-signal', signal='AccountPropertyChanged',
                path=account_path, interface=cs.ACCOUNT,
                predicate=(lambda e:
                    e.args[0].get('ConnectionStatus') ==
                        cs.CONN_STATUS_CONNECTED),
                ),
            )

    # The avatar got migrated, too.
    assert not os.path.exists(os.environ['MC_ACCOUNT_DIR'] + '/' +
            account_id + '/avatar.bin')
    assert not os.path.exists(os.environ['MC_ACCOUNT_DIR'] + '/fakecm')
    avatar_filename = account_id
    avatar_filename = avatar_filename.replace('/', '-') + '.avatar'
    avatar_filename = (os.environ['XDG_DATA_HOME'] +
        '/telepathy/mission-control/' + avatar_filename)
    assertEquals('Deus Ex', ''.join(open(avatar_filename, 'r').readlines()))

if __name__ == '__main__':
    preseed()
    exec_test(test, {}, preload_mc=False)
