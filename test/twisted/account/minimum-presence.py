# Copyright (C) 2010 Nokia Corporation
# Copyright (C) 2010 Collabora Ltd.
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
        call_async, sync_dbus, unwrap, assertEquals, assertContains
from mctest import exec_test, create_fakecm_account, enable_fakecm_account, \
        SimulatedConnection
import constants as cs

ACCOUNT_IFACE_MINIMUM_PRESENCE = cs.ACCOUNT + \
    '.Interface.MinimumPresence.DRAFT'

def presence(type, status, message):
    return dbus.Struct((dbus.UInt32(type), status, message),
        signature='uss')

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "jc.denton@example.com",
        "password": "ionstorm"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)
    mp_iface = dbus.Interface(account, ACCOUNT_IFACE_MINIMUM_PRESENCE)

    account_props.Set(cs.ACCOUNT, 'Enabled', True)

    reqs = unwrap(account_props.Get(ACCOUNT_IFACE_MINIMUM_PRESENCE,
        'Requests'))
    assertEquals({}, reqs)

    mp_iface.Request(presence(cs.PRESENCE_TYPE_HIDDEN, 'hidden',
       'Sneaking through shadows'))

    # Setting minimum presence should get us online immediately
    conn = enable_fakecm_account(q, bus, mc, account, params,
        requested_presence=None, has_hidden=True,
        has_presence=True)

    q.expect('dbus-method-call',
        method='SetPresence',
        interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
        args=['hidden', 'Sneaking through shadows'])

    # Requests should contain our minimum presence request
    reqs = unwrap(account_props.Get(ACCOUNT_IFACE_MINIMUM_PRESENCE,
        'Requests'))
    my_unique_name = bus.get_unique_name()
    assertContains(my_unique_name, reqs)
    assertEquals((cs.PRESENCE_TYPE_HIDDEN, 'hidden',
        'Sneaking through shadows'), reqs[my_unique_name])

    # Setting requested presence of the same priority should
    # result in requested presence being set
    mp_iface.Request(presence(cs.PRESENCE_TYPE_HIDDEN, 'hidden', 'Lurking'))

    q.expect('dbus-method-call',
        method='SetPresence',
        interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
        args=['hidden', 'Lurking'])

    # Setting requested presence of higher priority should
    # result in requested presence being set
    mp_iface.Request(presence(cs.PRESENCE_TYPE_BUSY, 'busy',
        'Fighting conspiracies'))

    q.expect('dbus-method-call',
        method='SetPresence',
        interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
        args=['busy', 'Fighting conspiracies'])

    # But, setting minimum presence to higher priority than requested
    # one, minimum presence should be set
    mp_iface.Request(presence(cs.PRESENCE_TYPE_AVAILABLE, 'available', 'I am'))

    q.expect('dbus-method-call',
        method='SetPresence',
        interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
        args=['available', 'I am'])

    # Setting requested presence offline should have us still online
    # if that was in minimum presence requests
    mp_iface.Request(presence(cs.PRESENCE_TYPE_OFFLINE, 'offline', ''))

    requested_presence = unwrap(account_props.Get(cs.ACCOUNT,
        'RequestedPresence'))
    assertEquals((cs.PRESENCE_TYPE_OFFLINE, 'offline', ''),
        requested_presence)

    current_presence = unwrap(account_props.Get(cs.ACCOUNT,
        'CurrentPresence'))
    assertEquals((cs.PRESENCE_TYPE_AVAILABLE, 'available', 'I am'),
        current_presence)

    # Releasing the minimum presence request should drop us offline
    mp_iface.Release()

    q.expect('dbus-method-call',
        method='Disconnect',
        interface=cs.CONN)

    reqs = unwrap(account_props.Get(ACCOUNT_IFACE_MINIMUM_PRESENCE,
        'Requests'))
    assertEquals({}, reqs)

if __name__ == '__main__':
    exec_test(test, {})
