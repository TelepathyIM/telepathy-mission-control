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
"""Feature test for signing in and setting an avatar, on CMs like Gabble where
the avatar is stored by the server.
"""

import os

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, SimulatedConnection, create_fakecm_account, \
        make_mc
import constants as cs

cm_name_ref = dbus.service.BusName(
        cs.tp_name_prefix + '.ConnectionManager.fakecm', bus=dbus.SessionBus())

account_id = 'fakecm/fakeprotocol/jc_2edenton_40unatco_2eint'

def preseed():

    accounts_dir = os.environ['MC_ACCOUNT_DIR']

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

    mc = make_mc(bus, q.append)

    e, _ = q.expect_many(
            EventPattern('dbus-method-call', method='RequestConnection',
                args=['fakeprotocol', expected_params],
                destination=cs.tp_name_prefix + '.ConnectionManager.fakecm',
                path=cs.tp_path_prefix + '/ConnectionManager/fakecm',
                interface=cs.tp_name_prefix + '.ConnectionManager',
                handled=False),
            EventPattern('dbus-signal', signal='NameOwnerChanged',
                    predicate=lambda e: e.args[0] == cs.AM and e.args[2]),
            )

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', '_',
            'myself', has_avatars=True, avatars_persist=True)
    conn.avatar = dbus.Struct((dbus.ByteArray('MJ12'), 'image/png'),
            signature='ays')

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    account_path = (cs.tp_path_prefix + '/Account/' + account_id)

    q.expect('dbus-method-call', method='Connect',
                path=conn.object_path, handled=True, interface=cs.CONN)

    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    # We haven't changed the avatar since we last signed in, so we don't set
    # it - on the contrary, we pick up the remote avatar (which has changed
    # since we were last here) to store it in the Account
    _, request_avatars_call, e = q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_AVATARS, method='GetKnownAvatarTokens',
                args=[[conn.self_handle]],
                handled=True),
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_AVATARS, method='RequestAvatars',
                args=[[conn.self_handle]],
                handled=False),
            EventPattern('dbus-signal', signal='AccountPropertyChanged',
                path=account_path, interface=cs.ACCOUNT,
                predicate=(lambda e:
                    e.args[0].get('ConnectionStatus') ==
                        cs.CONN_STATUS_CONNECTED),
                ),
            )

    q.dbus_return(request_avatars_call.message, signature='')

    q.dbus_emit(conn.object_path, cs.CONN_IFACE_AVATARS, 'AvatarRetrieved',
            conn.self_handle, str(conn.avatar[0]),
            dbus.ByteArray(conn.avatar[0]), conn.avatar[1], signature='usays')

    q.expect('dbus-signal', path=account_path,
            interface=cs.ACCOUNT_IFACE_AVATAR, signal='AvatarChanged'),

    account = bus.get_object(cs.AM, account_path)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)
    assert account_props.Get(cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
            byte_arrays=True) == conn.avatar

if __name__ == '__main__':
    preseed()
    exec_test(test, {}, preload_mc=False)
