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
import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, create_fakecm_account, enable_fakecm_account
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "wjt@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Nickname',
            'resiak')
    q.expect_many(
        EventPattern('dbus-signal',
            path=account.object_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            args=[{'Nickname': 'resiak'}]),
        EventPattern('dbus-return', method='Set'),
        )
    assert account_props.Get(cs.ACCOUNT, 'Nickname') == 'resiak'

    # OK, let's go online
    conn, get_aliases, set_aliases = enable_fakecm_account(q, bus, mc,
            account, params, has_aliasing=True,
            expect_after_connect=[
                EventPattern('dbus-method-call',
                    interface=cs.CONN_IFACE_ALIASING, method='GetAliases',
                    handled=False),
                EventPattern('dbus-method-call',
                    interface=cs.CONN_IFACE_ALIASING, method='SetAliases',
                    handled=False),
                ])

    assert get_aliases.args[0] == [ conn.self_handle ]
    q.dbus_return(get_aliases.message, { conn.self_handle: 'wjt@example.com' },
            signature='a{us}')

    assert set_aliases.args[0] == { conn.self_handle: 'resiak' }
    q.dbus_return(set_aliases.message, signature='')

    # Change alias after going online
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Nickname',
            'Will Thomspon')

    e = q.expect('dbus-method-call',
        interface=cs.CONN_IFACE_ALIASING, method='SetAliases',
        args=[{ conn.self_handle: 'Will Thomspon' }],
        handled=False)

    # Set returns immediately; the change happens asynchronously
    q.expect('dbus-return', method='Set')

    q.dbus_return(e.message, signature='')

    someone_else = conn.ensure_handle(cs.HT_CONTACT, 'alberto@example.com')

    # Another client changes our alias remotely
    q.dbus_emit(conn.object_path, cs.CONN_IFACE_ALIASING, 'AliasesChanged',
            dbus.Array([(conn.self_handle, 'wjt'), (someone_else, 'mardy')],
                signature='(us)'), signature='a(us)')

    q.expect('dbus-signal', path=account.object_path,
            signal='AccountPropertyChanged', interface=cs.ACCOUNT,
            args=[{'Nickname': 'wjt'}])

if __name__ == '__main__':
    exec_test(test, {})
