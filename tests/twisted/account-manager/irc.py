# encoding: utf-8
#
# Test the odd things about IRC: it has no presence, and nicknames
# are the same thing as identifiers.
#
# Copyright © 2009 Nokia Corporation
# Copyright © 2009-2013 Collabora Ltd.
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

from servicetest import (EventPattern, call_async, assertEquals, sync_dbus)
from mctest import (exec_test, create_fakecm_account, enable_fakecm_account)
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "brucewayne",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Nickname',
            "BruceWayne")
    q.expect_many(
        EventPattern('dbus-signal',
            path=account.object_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            args=[{'Nickname': "BruceWayne"}]),
        EventPattern('dbus-return', method='Set'),
        )
    assertEquals("BruceWayne", account_props.Get(cs.ACCOUNT, 'Nickname'))

    expect_after_connect = [
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_CONTACTS,
                predicate=(lambda e: e.method in (
                    'GetContactAttributes', 'GetContactByID'
                    ) and
                    cs.CONN_IFACE_ALIASING in e.args[1]),
                handled=True),
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_ALIASING, method='SetAliases',
                handled=False),
            EventPattern('dbus-signal',
                interface=cs.ACCOUNT,
                predicate=lambda e:
                    e.args[0].get('CurrentPresence') ==
                        (cs.PRESENCE_TYPE_UNSET, '', '')),
            ]

    conn, get_aliases, set_aliases, _ = enable_fakecm_account(q, bus, mc,
            account, params, has_aliasing=True,
            expect_after_connect=expect_after_connect,
            self_ident=params['account'])

    assert get_aliases.args[0] == [ conn.self_handle ]

    assert set_aliases.args[0] == { conn.self_handle: 'BruceWayne' }
    q.dbus_return(set_aliases.message, signature='')

    # FIXME: fd.o #55666 in telepathy-glib breaks the rest of this test.
    # Reinstate it when we depend on a version that has that fixed.
    return

    # Another client changes our alias remotely, but because this is IRC,
    # that manifests itself as a handle change
    conn.change_self_ident('thebatman')
    conn.change_self_alias('TheBatman')

    get_aliases, _ = q.expect_many(
        EventPattern('dbus-method-call',
            interface=cs.CONN_IFACE_CONTACTS,
            predicate=(lambda e: e.method in (
                'GetContactAttributes', 'GetContactByID'
                ) and
                cs.CONN_IFACE_ALIASING in e.args[1]),
            handled=True),
        EventPattern('dbus-signal', path=account.object_path,
            signal='AccountPropertyChanged', interface=cs.ACCOUNT,
            predicate=(lambda e:
                e.args[0].get('NormalizedName') == 'thebatman')),
        )
    assert get_aliases.args[0] in ([conn.self_handle], conn.self_id)
    q.expect('dbus-signal', path=account.object_path,
            signal='AccountPropertyChanged', interface=cs.ACCOUNT,
            args=[{'Nickname': 'TheBatman'}])

    # We change our nickname back
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Nickname',
            'BruceWayne')
    _, _, e = q.expect_many(
        EventPattern('dbus-signal',
            path=account.object_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            predicate=(lambda e: e.args[0].get('Nickname') == 'BruceWayne')),
        EventPattern('dbus-return', method='Set'),
        EventPattern('dbus-method-call',
            interface=cs.CONN_IFACE_ALIASING, method='SetAliases',
            args=[{ conn.self_handle: 'BruceWayne', }],
            handled=False)
        )
    assertEquals('BruceWayne', account_props.Get(cs.ACCOUNT, 'Nickname'))
    conn.change_self_ident('brucewayne')
    conn.change_self_alias('BruceWayne')
    q.dbus_return(e.message, signature='')

    # In response to the self-handle change, we check our nickname again
    get_aliases, _ = q.expect_many(
        EventPattern('dbus-method-call',
            interface=cs.CONN_IFACE_CONTACTS,
            predicate=(lambda e: e.method in (
                'GetContactAttributes', 'GetContactByID'
                ) and
                cs.CONN_IFACE_ALIASING in e.args[1]),
            handled=True),
        EventPattern('dbus-signal', path=account.object_path,
            signal='AccountPropertyChanged', interface=cs.ACCOUNT,
            predicate=(lambda e:
                e.args[0].get('NormalizedName') == 'brucewayne')),
        )
    assert get_aliases.args[0] in ([conn.self_handle], conn.self_id)

    forbidden = [EventPattern('dbus-signal', signal='AccountPropertyChanged',
        predicate=lambda e: 'Nickname' in e.args[0])]
    q.forbid_events(forbidden)
    sync_dbus(bus, q, mc)

if __name__ == '__main__':
    exec_test(test, {})
