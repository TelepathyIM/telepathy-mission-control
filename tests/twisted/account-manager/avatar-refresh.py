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

from servicetest import (EventPattern, tp_name_prefix, tp_path_prefix,
        call_async, assertEquals, sync_dbus)
from mctest import exec_test, SimulatedConnection, create_fakecm_account, MC
import constants as cs

cm_name_ref = dbus.service.BusName(
        cs.tp_name_prefix + '.ConnectionManager.fakecm', bus=dbus.SessionBus())

class Account(object):
    def __init__(self, fake_accounts_service, accounts_dir,
            avatars_persist, server_delays, local_avatar, remote_avatar):

        self.avatars_persist = avatars_persist
        self.server_delays = server_delays
        self.local_avatar = local_avatar
        self.remote_avatar = remote_avatar

        if avatars_persist:
            s_persist = 'persist'
        else:
            s_persist = 'transient'

        if server_delays:
            s_delay = 'delay'
        else:
            s_delay = 'immediate'

        self.id = ('fakecm/fakeprotocol/%s_%s_L%s_R%s' %
                (s_persist, s_delay, local_avatar, remote_avatar))

        fake_accounts_service.update_attributes(self.id, changed={
            'manager': 'fakecm',
            'protocol': 'fakeprotocol',
            'DisplayName': 'Test account',
            'NormalizedName': 'jc.denton@unatco.int',
            'Enabled': False,
            'ConnectAutomatically': True,
            'AutomaticPresence': (dbus.UInt32(2), 'available',
                'My vision is augmented'),
            'Nickname': 'JC',
            })
        fake_accounts_service.update_parameters(self.id, untyped={
            'account': self.id,
            'password': self.id,
            })

        self.avatar_location = None

        if local_avatar is not None:
            if local_avatar:
                mime = 'image/jpeg'

                if remote_avatar and avatars_persist:
                    # exercise override of an avatar in $XDG_DATA_DIRS
                    self.avatar_location = 'datadir'
                    avatar_filename = self.id
                    datadir = os.environ['XDG_DATA_DIRS'].split(':')[0]
                    datadir += '/telepathy/mission-control'
                    avatar_filename = (
                            avatar_filename.replace('/', '-') + '.avatar')
                    if not os.path.isdir(datadir):
                        os.makedirs(datadir)
                    avatar_filename = datadir + '/' + avatar_filename
                    avatar_bin = open(avatar_filename, 'w')
                    avatar_bin.write(local_avatar)
                    avatar_bin.close()
                elif not avatars_persist:
                    self.avatar_location = 'old'
                    # exercise migration from ~/.mission-control in a
                    # situation where MC should "win"
                    os.makedirs(accounts_dir + '/' + self.id)
                    avatar_bin = open(
                            accounts_dir + '/' + self.id + '/avatar.bin', 'w')
                    avatar_bin.write(local_avatar)
                    avatar_bin.close()
                else:
                    # store it in the normal location
                    self.avatar_location = 'home'
                    avatar_filename = self.id
                    datadir = os.environ['XDG_DATA_HOME']
                    datadir += '/telepathy/mission-control'
                    avatar_filename = (
                            avatar_filename.replace('/', '-') + '.avatar')
                    if not os.path.isdir(datadir):
                        os.makedirs(datadir)
                    avatar_filename = datadir + '/' + avatar_filename
                    avatar_bin = open(avatar_filename, 'w')
                    avatar_bin.write(local_avatar)
                    avatar_bin.close()
            else:
                mime = ''

            if local_avatar == 'old':
                # the fake CM just uses the avatar string as its own token
                fake_accounts_service.update_attributes(self.id, changed={
                    'AvatarMime': mime,
                    'avatar_token': local_avatar,
                    })
            else:
                # either the local avatar is "no avatar", or it was set
                # while offline so we don't know its token
                fake_accounts_service.update_attributes(self.id, changed={
                    'AvatarMime': mime,
                    'avatar_token': '',
                    })

        # Ideal behaviour:
        if local_avatar == 'new':
            # If the local avatar has been set since last login, MC
            # should upload it
            self.winner = 'MC'
        elif local_avatar and not avatars_persist:
            # If we have an avatar and it doesn't persist, MC should
            # upload it
            self.winner = 'MC'
        elif avatars_persist and remote_avatar:
            # If the server stores our avatar, MC should download it
            self.winner = 'service'
        else:
            # Nobody has an avatar - nothing should happen
            self.winner = None

    def test(self, q, bus, mc):
        expected_params = {
                'account': self.id,
                'password': self.id,
                }

        account_path = (cs.ACCOUNT_PATH_PREFIX + self.id)
        account_proxy = bus.get_object(cs.AM, account_path)

        account_proxy.Set(cs.ACCOUNT, 'Enabled', True,
                dbus_interface=cs.PROPERTIES_IFACE)

        e = q.expect('dbus-method-call', method='RequestConnection',
                    args=['fakeprotocol', expected_params],
                    destination=cs.tp_name_prefix + '.ConnectionManager.fakecm',
                    path=cs.tp_path_prefix + '/ConnectionManager/fakecm',
                    interface=cs.tp_name_prefix + '.ConnectionManager',
                    handled=False)

        if self.remote_avatar is None:
            initial_avatar = None
        elif self.remote_avatar:
            initial_avatar = dbus.Struct((dbus.ByteArray(self.remote_avatar),
                'text/plain'), signature='ays')
        else:
            initial_avatar = dbus.Struct((dbus.ByteArray(''), ''),
                    signature='ays')

        conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol',
                self.id.replace('fakecm/fakeprotocol/', ''),
                'myself', has_avatars=True,
                avatars_persist=self.avatars_persist,
                server_delays_avatar=self.server_delays,
                initial_avatar=initial_avatar,
                )

        q.dbus_return(e.message, conn.bus_name, conn.object_path,
                signature='so')

        forbidden = []

        if self.winner != 'MC':
            forbidden.append(
                EventPattern('dbus-method-call', method='SetAvatar'))

        if self.winner != 'service':
            forbidden.append(
                EventPattern('dbus-signal', signal='AvatarChanged',
                    path=account_path))

        q.forbid_events(forbidden)

        q.expect('dbus-method-call', method='Connect',
                    path=conn.object_path, handled=True, interface=cs.CONN)

        conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

        if self.winner == 'MC':
            # MC should upload the avatar.
            _, e = q.expect_many(
                    EventPattern('dbus-method-call',
                        interface=cs.CONN_IFACE_AVATARS, method='SetAvatar',
                        args=[self.local_avatar, 'image/jpeg'],
                        handled=True),
                    EventPattern('dbus-signal', signal='AccountPropertyChanged',
                        path=account_path, interface=cs.ACCOUNT,
                        predicate=(lambda e:
                            e.args[0].get('ConnectionStatus') ==
                                cs.CONN_STATUS_CONNECTED),
                        ),
                    )
        elif self.winner == 'service':
            # We haven't changed the avatar since we last signed in, so we
            # don't set it - on the contrary, we pick up the remote avatar
            # (which has changed since we were last here) to store it in the
            # Account, unless the token says there is no avatar.
            if conn.avatar[0]:
                request_avatars_call, e = q.expect_many(
                        EventPattern('dbus-method-call',
                            interface=cs.CONN_IFACE_AVATARS,
                            method='RequestAvatars',
                            args=[[conn.self_handle]],
                            handled=False),
                        EventPattern('dbus-signal',
                            signal='AccountPropertyChanged',
                            path=account_path, interface=cs.ACCOUNT,
                            predicate=(lambda e:
                                e.args[0].get('ConnectionStatus') ==
                                    cs.CONN_STATUS_CONNECTED),
                            ),
                        )

                q.dbus_return(request_avatars_call.message, signature='')

                q.dbus_emit(conn.object_path, cs.CONN_IFACE_AVATARS,
                        'AvatarRetrieved',
                        conn.self_handle, str(conn.avatar[0]),
                        dbus.ByteArray(conn.avatar[0]), conn.avatar[1],
                            signature='usays')

            q.expect('dbus-signal', path=account_path,
                    interface=cs.ACCOUNT_IFACE_AVATAR, signal='AvatarChanged'),

            account_props = dbus.Interface(account_proxy, cs.PROPERTIES_IFACE)
            assert account_props.Get(cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
                    byte_arrays=True) == conn.avatar

        sync_dbus(bus, q, mc)
        q.unforbid_events(forbidden)

        if self.local_avatar:
            self.test_migration(bus, q, conn, account_proxy)

    def test_migration(self, bus, q, conn, account_proxy):
        if self.avatar_location == 'old':
            # The avatar got migrated to the new location.
            assert not os.path.exists(os.environ['MC_ACCOUNT_DIR'] + '/' +
                    self.id + '/avatar.bin')
            assert not os.path.exists(os.environ['MC_ACCOUNT_DIR'] + '/fakecm')
            avatar_filename = self.id
            avatar_filename = avatar_filename.replace('/', '-') + '.avatar'
            avatar_filename = (os.environ['XDG_DATA_HOME'] +
                '/telepathy/mission-control/' + avatar_filename)
            assertEquals(conn.avatar[0], ''.join(open(avatar_filename,
                'r').readlines()))
        elif self.avatar_location == 'datadir' and self.winner == 'service':
            # The avatar wasn't deleted from $XDG_DATA_DIRS, but it was
            # overridden.
            assert not os.path.exists(os.environ['MC_ACCOUNT_DIR'] + '/' +
                    self.id + '/avatar.bin')
            assert not os.path.exists(os.environ['MC_ACCOUNT_DIR'] + '/fakecm')

            avatar_filename = self.id
            avatar_filename = avatar_filename.replace('/', '-') + '.avatar'
            avatar_filename = (os.environ['XDG_DATA_HOME'] +
                '/telepathy/mission-control/' + avatar_filename)
            assertEquals(self.remote_avatar, ''.join(open(avatar_filename,
                'r').readlines()))

            datadirs = os.environ['XDG_DATA_DIRS'].split(':')
            low_prio_filename = self.id
            low_prio_filename = low_prio_filename.replace('/', '-') + '.avatar'
            low_prio_filename = (datadirs[0] +
                '/telepathy/mission-control/' + low_prio_filename)
            assertEquals(self.local_avatar, ''.join(open(low_prio_filename,
                'r').readlines()))

            account_props = dbus.Interface(account_proxy, cs.PROPERTIES_IFACE)

            # If we set the avatar to be empty, that's written out as a file,
            # so it'll override the one in XDG_DATA_DIRS
            call_async(q, account_props, 'Set', cs.ACCOUNT_IFACE_AVATAR,
                    'Avatar', (dbus.ByteArray(''), ''))

            q.expect_many(
                    EventPattern('dbus-method-call',
                        interface=cs.CONN_IFACE_AVATARS, method='ClearAvatar',
                        args=[]),
                    EventPattern('dbus-signal', path=account_proxy.object_path,
                        interface=cs.ACCOUNT_IFACE_AVATAR,
                        signal='AvatarChanged'),
                    EventPattern('dbus-return', method='Set')
                    )

            assert account_props.Get(cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
                    byte_arrays=True) == ('', '')

            assertEquals('', ''.join(open(avatar_filename, 'r').readlines()))
            assertEquals(self.local_avatar, ''.join(open(low_prio_filename,
                'r').readlines()))

def preseed(q, bus, fake_accounts_service):
    accounts = []
    accounts_dir = os.environ['MC_ACCOUNT_DIR']

    try:
        os.mkdir(accounts_dir, 0700)
    except OSError:
        pass

    account_connections_file = open(accounts_dir + '/.mc_connections', 'w')
    account_connections_file.write("")
    account_connections_file.close()

    i = 0
    for local_avatar in ('new', 'old', '', None):
        # This is what the spec says Salut should do: omit the remote
        # avatar from the result of ContactAttributeInterfaces
        # and GetKnownAvatarTokens.
        accounts.append(Account(fake_accounts_service, accounts_dir,
            avatars_persist=False, server_delays=False,
            local_avatar=local_avatar, remote_avatar=None))

        for have_remote_avatar in (True, False):
            for server_delays in (True, False):
                if have_remote_avatar:
                    # the avatars have to be unique, otherwise the
                    # avatar cache will break our RequestAvatars expectation
                    remote_avatar = 'remote%d' % i
                    i += 1
                else:
                    remote_avatar = ''

                accounts.append(Account(fake_accounts_service, accounts_dir,
                    avatars_persist=True, server_delays=server_delays,
                    local_avatar=local_avatar,
                    remote_avatar=remote_avatar))

    return accounts

def test(q, bus, unused, **kwargs):
    fake_accounts_service = kwargs['fake_accounts_service']
    accounts = preseed(q, bus, fake_accounts_service)

    mc = MC(q, bus)

    for account in accounts:
        account.test(q, bus, mc)

if __name__ == '__main__':
    exec_test(test, {}, preload_mc=False, use_fake_accounts_service=True,
            pass_kwargs=True)
