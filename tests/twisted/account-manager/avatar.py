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

import os

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, assertEquals
from mctest import exec_test, create_fakecm_account, enable_fakecm_account
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "me@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    assertEquals(cs.ACCOUNT_PATH_PREFIX,
            account.object_path[:len(cs.ACCOUNT_PATH_PREFIX)])
    avatar_filename = account.object_path[len(cs.ACCOUNT_PATH_PREFIX):]
    avatar_filename = avatar_filename.replace('/', '-') + '.avatar'
    avatar_filename = (os.environ['XDG_DATA_HOME'] +
        '/telepathy/mission-control/' + avatar_filename)

    call_async(q, account_props, 'Set', cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
            dbus.Struct((dbus.ByteArray('AAAA'), 'image/jpeg')))
    q.expect_many(
        EventPattern('dbus-signal',
            path=account.object_path,
            signal='AvatarChanged',
            interface=cs.ACCOUNT_IFACE_AVATAR,
            args=[]),
        EventPattern('dbus-return', method='Set'),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='DeferringSetAttribute',
            args=[account.object_path, 'AvatarMime', 'image/jpeg']),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='CommittingOne',
            args=[account.object_path]),
        EventPattern('dbus-method-call',
            interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
            method='UpdateAttributes'),
        )
    assert account_props.Get(cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
            byte_arrays=True) == ('AAAA', 'image/jpeg')

    assertEquals('AAAA', ''.join(open(avatar_filename, 'r').readlines()))
    # We aren't storing in the old location
    assert not os.path.exists(os.environ['MC_ACCOUNT_DIR'] + '/fakecm')

    # OK, let's go online. The avatar is set regardless of the CM
    conn, e = enable_fakecm_account(q, bus, mc, account, params,
            has_avatars=True, avatars_persist=True,
            expect_after_connect=[
                EventPattern('dbus-method-call',
                    interface=cs.CONN_IFACE_AVATARS, method='SetAvatar',
                    handled=True, args=['AAAA', 'image/jpeg']),
                ])

    # Change avatar after going online
    call_async(q, account_props, 'Set', cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
            (dbus.ByteArray('BBBB'), 'image/png'))

    q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_AVATARS, method='SetAvatar',
                args=['BBBB', 'image/png'],
                handled=True),
            EventPattern('dbus-signal', path=account.object_path,
                interface=cs.ACCOUNT_IFACE_AVATAR, signal='AvatarChanged'),
            EventPattern('dbus-return', method='Set'),
            EventPattern('dbus-signal',
                interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                signal='DeferringSetAttribute',
                args=[account.object_path, 'AvatarMime', 'image/png']),
            EventPattern('dbus-signal',
                interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                signal='CommittingOne',
                args=[account.object_path]),
            EventPattern('dbus-method-call',
                interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                method='UpdateAttributes'),
            )

    assert account_props.Get(cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
            byte_arrays=True) == ('BBBB', 'image/png')

    assertEquals('BBBB', ''.join(open(avatar_filename, 'r').readlines()))
    assert not os.path.exists(os.environ['MC_ACCOUNT_DIR'] + '/fakecm')

    someone_else = conn.ensure_handle(cs.HT_CONTACT, 'alberto@example.com')

    # Another contact changes their avatar: ignored
    q.dbus_emit(conn.object_path, cs.CONN_IFACE_AVATARS, 'AvatarUpdated',
            someone_else, "mardy's avatar token", signature='us')

    # Another client changes our avatar remotely
    q.dbus_emit(conn.object_path, cs.CONN_IFACE_AVATARS, 'AvatarUpdated',
            conn.self_handle, 'CCCC', signature='us')

    e = q.expect('dbus-method-call',
            interface=cs.CONN_IFACE_AVATARS, method='RequestAvatars',
            args=[[conn.self_handle]],
            handled=False)
    q.dbus_return(e.message, signature='')

    q.dbus_emit(conn.object_path, cs.CONN_IFACE_AVATARS,
            'AvatarRetrieved', conn.self_handle, 'CCCC',
            dbus.ByteArray('CCCC'), 'image/svg', signature='usays')
    q.expect_many(
            EventPattern('dbus-signal', path=account.object_path,
                interface=cs.ACCOUNT_IFACE_AVATAR, signal='AvatarChanged'),
            EventPattern('dbus-signal',
                interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                signal='DeferringSetAttribute',
                args=[account.object_path, 'avatar_token', 'CCCC']),
            EventPattern('dbus-signal',
                interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                signal='CommittingOne',
                args=[account.object_path]),
            EventPattern('dbus-method-call',
                interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                method='UpdateAttributes'),
            )

    assert account_props.Get(cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
            byte_arrays=True) == ('CCCC', 'image/svg')

    assertEquals('CCCC', ''.join(open(avatar_filename, 'r').readlines()))

    # empty avatar tests
    conn.forget_avatar()
    q.dbus_emit(conn.object_path, cs.CONN_IFACE_AVATARS, 'AvatarUpdated',
                conn.self_handle, '', signature='us')
    q.expect_many(
            EventPattern('dbus-signal',
                interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                signal='DeferringSetAttribute',
                args=[account.object_path, 'avatar_token', '']),
            EventPattern('dbus-signal',
                interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                signal='DeferringSetAttribute',
                args=[account.object_path, 'AvatarMime', '']),
            EventPattern('dbus-signal', path=account.object_path,
                interface=cs.ACCOUNT_IFACE_AVATAR, signal='AvatarChanged'),
            EventPattern('dbus-signal',
                interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                signal='CommittingOne',
                args=[account.object_path]),
            EventPattern('dbus-method-call',
                interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                method='UpdateAttributes'),
            )

    assertEquals(account_props.Get(cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
                                   byte_arrays=False), ([], ''))

    # empty avatars are represented by an empty file, not no file,
    # to get the right precedence over XDG_DATA_DIRS
    assertEquals('', ''.join(open(avatar_filename, 'r').readlines()))

if __name__ == '__main__':
    exec_test(test, {})
