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
import time

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, assertEquals
from mctest import exec_test, SimulatedConnection, create_fakecm_account,\
        SimulatedChannel
import constants as cs

def test(q, bus, mc):
    cm_name_ref = dbus.service.BusName(
            tp_name_prefix + '.ConnectionManager.fakecm', bus=bus)

    # Create an account
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy", 'nickname': 'albinoblacksheep'}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    # Enable the account
    account.Set(cs.ACCOUNT, 'Enabled', True,
            dbus_interface=cs.PROPERTIES_IFACE)
    q.expect('dbus-signal',
            path=account.object_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT)

    # Go online
    requested_presence = dbus.Struct((dbus.UInt32(2L), dbus.String(u'brb'),
                dbus.String(u'Be back soon!')))
    account.Set(cs.ACCOUNT,
            'RequestedPresence', requested_presence,
            dbus_interface=cs.PROPERTIES_IFACE)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=tp_name_prefix + '.ConnectionManager.fakecm',
            path=tp_path_prefix + '/ConnectionManager/fakecm',
            interface=tp_name_prefix + '.ConnectionManager',
            handled=False)

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', '_',
            'myself')

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    # MC calls GetStatus (maybe) and then Connect

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)

    # Connect succeeds
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    # MC does some setup, including fetching the list of Channels

    q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CONN_IFACE_REQUESTS],
                path=conn.object_path, handled=True),
            )

    # Assert that the NormalizedName is harvested from the Connection at some
    # point
    while 1:
        e = q.expect('dbus-signal',
                interface=cs.ACCOUNT, signal='AccountPropertyChanged',
                path=account.object_path)
        if 'NormalizedName' in e.args[0]:
            assert e.args[0]['NormalizedName'] == 'myself', e.args
            break

    # Check the requested presence is online
    properties = account.GetAll(cs.ACCOUNT,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert properties is not None
    assert properties.get('RequestedPresence') == requested_presence, \
        properties.get('RequestedPresence')

    # Set some parameters. They include setting account to \\, as a regression
    # test for part of fd.o #28557.
    call_async(q, account, 'UpdateParameters',
            {
                'account': r'\\',
                'secret-mushroom': '/Amanita muscaria/',
                'snakes': dbus.UInt32(42),
                'com.example.Badgerable.Badgered': True,
            },
            [],
            dbus_interface=cs.ACCOUNT)

    set_call, ret, _ = q.expect_many(
            EventPattern('dbus-method-call',
                path=conn.object_path,
                interface=cs.PROPERTIES_IFACE, method='Set',
                args=['com.example.Badgerable', 'Badgered', True],
                handled=False),
            EventPattern('dbus-return',
                method='UpdateParameters'),
            EventPattern('dbus-signal',
                path=account.object_path,
                interface=cs.ACCOUNT, signal='AccountPropertyChanged',
                args=[{'Parameters': {
                    'account': r'\\',
                    'com.example.Badgerable.Badgered': True,
                    'password': 'secrecy',
                    'nickname': 'albinoblacksheep',
                    'secret-mushroom': '/Amanita muscaria/',
                    'snakes': 42,
                    }}]),
            )

    # the D-Bus property should be set instantly; the others will take effect
    # on reconnection
    not_yet = ret.value[0]
    not_yet.sort()
    assert not_yet == ['account', 'secret-mushroom', 'snakes'], not_yet

    # Unset some parameters
    call_async(q, account, 'UpdateParameters',
            {},
            ['nickname', 'com.example.Badgerable.Badgered'],
            dbus_interface=cs.ACCOUNT)

    ret, _ = q.expect_many(
            EventPattern('dbus-return',
                method='UpdateParameters'),
            EventPattern('dbus-signal',
                path=account.object_path,
                interface=cs.ACCOUNT, signal='AccountPropertyChanged',
                args=[{'Parameters': {
                    'account': r'\\',
                    'password': 'secrecy',
                    'secret-mushroom': '/Amanita muscaria/',
                    'snakes': 42,
                    }}]),
            )

    # there's no well-defined way to unset a D-Bus property, so it'll go back
    # to its implied default value only after reconnection
    #
    # FIXME: in a perfect implementation, we know that this particular D-Bus
    # property has a default, so maybe we should set it back to that?
    not_yet = ret.value[0]
    not_yet.sort()
    assert not_yet == ['com.example.Badgerable.Badgered', 'nickname'], not_yet

    accounts_dir = os.environ['MC_ACCOUNT_DIR']

    # fd.o #28557: when the file has been updated, the account parameter
    # has its two backslashes doubled to 4 (because of the .desktop encoding),
    # but they are not doubled again.
    i = 0
    updated = False
    while i < 500:

        for line in open(accounts_dir + '/accounts.cfg', 'r'):
            if line.startswith('param-account=') and '\\' in line:
                assertEquals(r'param-account=\\\\' + '\n', line)
                updated = True

        if updated:
            break

        # just to not busy-wait
        time.sleep(0.1)
        i += 1

    assert updated

if __name__ == '__main__':
    exec_test(test, {})
