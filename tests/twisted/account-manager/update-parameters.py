# vim: set fileencoding=utf-8 :
# Copyright © 2009 Nokia Corporation
# Copyright © 2009–2011 Collabora Ltd.
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
        call_async, assertEquals, assertSameSets
from mctest import exec_test, SimulatedConnection, create_fakecm_account,\
        SimulatedChannel
import constants as cs

def test(q, bus, mc, **kwargs):
    cm_name_ref = dbus.service.BusName(
            tp_name_prefix + '.ConnectionManager.fakecm', bus=bus)

    # Create an account
    params = dbus.Dictionary(
        {"account": "someguy@example.com",
         "password": "secrecy",
         "nickname": "albinoblacksheep",
         }, signature='sv')
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

    # MC does some setup, including fetching the list of Channels

    q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CONN_IFACE_REQUESTS],
                path=conn.object_path, handled=True),
            )

    # MC calls GetStatus (maybe) and then Connect

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)

    # Connect succeeds
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

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

    # Try to update 'account' to a value of the wrong type; MC should complain,
    # without having changed the value of 'snakes'.
    call_async(q, account, 'UpdateParameters',
            { 'account': dbus.UInt32(39),
              'snakes': dbus.UInt32(39),
            },
            [],
            dbus_interface=cs.ACCOUNT)
    q.expect('dbus-error', name=cs.INVALID_ARGUMENT)

    props = account.Get(cs.ACCOUNT, 'Parameters',
        dbus_interface=cs.PROPERTIES_IFACE)
    assertEquals(42, props['snakes'])

    # Try to update a parameter that doesn't exist; again, 'snakes' should not
    # be changed.
    call_async(q, account, 'UpdateParameters',
            { 'accccccount': dbus.UInt32(39),
              'snakes': dbus.UInt32(39),
            },
            [],
            dbus_interface=cs.ACCOUNT)
    q.expect('dbus-error', name=cs.INVALID_ARGUMENT)

    props = account.Get(cs.ACCOUNT, 'Parameters',
        dbus_interface=cs.PROPERTIES_IFACE)
    assertEquals(42, props['snakes'])

    # Unset some parameters, including a parameter which doesn't exist at all.
    # The spec says that “If the given parameters […] do not exist at all, the
    # account manager MUST accept this without error.”
    call_async(q, account, 'UpdateParameters',
            {},
            ['nickname', 'com.example.Badgerable.Badgered', 'froufrou'],
            dbus_interface=cs.ACCOUNT)

    ret, _, _ = q.expect_many(
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
            EventPattern('dbus-method-call',
                path=conn.object_path,
                interface=cs.PROPERTIES_IFACE, method='Set',
                args=['com.example.Badgerable', 'Badgered', False],
                handled=False),
            )

    # Because com.example.Badgerable.Badgered has a default value (namely
    # False), unsetting that parameter should cause the default value to be set
    # on the CM.
    not_yet = ret.value[0]
    assertEquals(['nickname'], not_yet)

    # Set contrived-example to its default value; since there's been no
    # practical change, we shouldn't be told we need to reconnect to apply it.
    call_async(q, account, 'UpdateParameters',
        { 'contrived-example': dbus.UInt32(5) }, [])
    ret, _ = q.expect_many(
            EventPattern('dbus-return', method='UpdateParameters'),
            EventPattern('dbus-signal',
                path=account.object_path,
                interface=cs.ACCOUNT, signal='AccountPropertyChanged',
                args=[{'Parameters': {
                    'account': r'\\',
                    'password': 'secrecy',
                    'secret-mushroom': '/Amanita muscaria/',
                    'snakes': 42,
                    "contrived-example": 5,
                    }}]),
            )
    not_yet = ret.value[0]
    assertEquals([], not_yet)

    # Unset contrived-example; again, MC should be smart enough to know we
    # don't need to do anything.
    call_async(q, account, 'UpdateParameters', {}, ['contrived-example'])
    ret, _ = q.expect_many(
            EventPattern('dbus-return', method='UpdateParameters'),
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
    not_yet = ret.value[0]
    assertEquals([], not_yet)

    # Unset contrived-example again; the spec decrees that “If the given
    # parameters were not, in fact, stored, […] the account manager MUST accept
    # this without error.”
    call_async(q, account, 'UpdateParameters', {}, ['contrived-example'])
    ret = q.expect('dbus-return', method='UpdateParameters')
    not_yet = ret.value[0]
    assertEquals([], not_yet)

    cache_dir = os.environ['XDG_CACHE_HOME']

    # Now that we're using GVariant-based storage, the backslashes aren't
    # escaped.
    assertEquals(r'\\',
            kwargs['fake_accounts_service'].accounts
            [account.object_path[len(cs.ACCOUNT_PATH_PREFIX):]]
            [2]     # parameters of known type
            ['account'])
    assertEquals(None,
            kwargs['fake_accounts_service'].accounts
            [account.object_path[len(cs.ACCOUNT_PATH_PREFIX):]]
            [3]     # parameters of unknown type
            .get('account', None))

if __name__ == '__main__':
    exec_test(test, {}, pass_kwargs=True)
