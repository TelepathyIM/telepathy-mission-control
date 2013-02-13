# Copyright (C) 2009 Nokia Corporation
# Copyright (C) 2009-2012 Collabora Ltd.
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
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, assertEquals
from mctest import exec_test, create_fakecm_account, get_account_manager
import constants as cs

def test(q, bus, mc):
    # Get the AccountManager interface
    account_manager = get_account_manager(bus)
    account_manager_iface = dbus.Interface(account_manager, cs.AM)

    # Introspect AccountManager for debugging purpose
    account_manager_introspected = account_manager.Introspect(
            dbus_interface=cs.INTROSPECTABLE_IFACE)
    #print account_manager_introspected

    # Check AccountManager has D-Bus property interface
    properties = account_manager.GetAll(cs.AM,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert properties is not None
    assert properties.get('ValidAccounts') == [], \
        properties.get('ValidAccounts')
    assert properties.get('InvalidAccounts') == [], \
        properties.get('InvalidAccounts')
    interfaces = properties.get('Interfaces')

    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_path = account.__dbus_object_path__

    # Check the account is correctly created
    properties = account_manager.GetAll(cs.AM,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert properties is not None
    assert properties.get('ValidAccounts') == [account_path], properties
    account_path = properties['ValidAccounts'][0]
    assert isinstance(account_path, dbus.ObjectPath), repr(account_path)
    assert properties.get('InvalidAccounts') == [], properties

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)
    # Introspect Account for debugging purpose
    account_introspected = account.Introspect(
            dbus_interface=cs.INTROSPECTABLE_IFACE)
    #print account_introspected

    # Check Account has D-Bus property interface
    properties = account_props.GetAll(cs.ACCOUNT)
    assert properties is not None

    assert properties.get('DisplayName') == 'fakeaccount', \
        properties.get('DisplayName')
    assert properties.get('Icon') == '', properties.get('Icon')
    assert properties.get('Valid') == True, properties.get('Valid')
    assert properties.get('Enabled') == False, properties.get('Enabled')
    #assert properties.get('Nickname') == 'fakenick', properties.get('Nickname')
    assert properties.get('Parameters') == params, properties.get('Parameters')
    assert properties.get('Connection') == '/', properties.get('Connection')
    assert properties.get('NormalizedName') == '', \
        properties.get('NormalizedName')

    interfaces = properties.get('Interfaces')
    assert cs.ACCOUNT_IFACE_AVATAR in interfaces, interfaces
    assert cs.ACCOUNT_IFACE_NOKIA_CONDITIONS in interfaces, interfaces

    # sanity check
    for k in properties:
        assert account_props.Get(cs.ACCOUNT, k) == properties[k], k

    # Alter some miscellaneous r/w properties

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'DisplayName',
            'Work account')
    q.expect_many(
        EventPattern('dbus-signal',
            path=account_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            args=[{'DisplayName': 'Work account'}]),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='DeferringSetAttribute',
            args=[account_path, 'DisplayName', 'Work account']),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='CommittingOne',
            args=[account_path]),
        EventPattern('dbus-method-call',
            interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
            method='UpdateAttributes',
            args=[account_path[len(cs.ACCOUNT_PATH_PREFIX):],
                {'DisplayName': 'Work account'},
                {'DisplayName': 0}, # flags
                []],
            ),
        EventPattern('dbus-return', method='Set'),
        )
    assert account_props.Get(cs.ACCOUNT, 'DisplayName') == 'Work account'

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Icon', 'im-jabber')
    q.expect_many(
        EventPattern('dbus-signal',
            path=account_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            args=[{'Icon': 'im-jabber'}]),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='DeferringSetAttribute',
            args=[account_path, 'Icon', 'im-jabber']),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='CommittingOne',
            args=[account_path]),
        EventPattern('dbus-method-call',
            interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
            method='UpdateAttributes',
            args=[account_path[len(cs.ACCOUNT_PATH_PREFIX):],
                {'Icon': 'im-jabber'},
                {'Icon': 0}, # flags
                []],
            ),
        EventPattern('dbus-return', method='Set'),
        )
    assert account_props.Get(cs.ACCOUNT, 'Icon') == 'im-jabber'

    assert account_props.Get(cs.ACCOUNT, 'HasBeenOnline') == False
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Nickname', 'Joe Bloggs')
    q.expect_many(
        EventPattern('dbus-signal',
            path=account_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            args=[{'Nickname': 'Joe Bloggs'}]),
        EventPattern('dbus-return', method='Set'),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='DeferringSetAttribute',
            args=[account_path, 'Nickname', 'Joe Bloggs']),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='CommittingOne',
            args=[account_path]),
        EventPattern('dbus-method-call',
            interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
            method='UpdateAttributes',
            args=[account_path[len(cs.ACCOUNT_PATH_PREFIX):],
                {'Nickname': 'Joe Bloggs'},
                {'Nickname': 0}, # flags
                []],
            ),
        )
    assert account_props.Get(cs.ACCOUNT, 'Nickname') == 'Joe Bloggs'

    call_async(q, account_props, 'Set', cs.ACCOUNT_IFACE_NOKIA_CONDITIONS,
            'Condition',
            dbus.Dictionary({':foo': 'bar'}, signature='ss'))
    # there's no change notification for the Condition
    q.expect_many(
        EventPattern('dbus-return', method='Set'),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='DeferringSetAttribute',
            args=[account_path, 'condition-:foo', 'bar']),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='CommittingOne',
            args=[account_path]),
        EventPattern('dbus-method-call',
            interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
            method='UpdateAttributes',
            args=[account_path[len(cs.ACCOUNT_PATH_PREFIX):],
                {'condition-:foo': 'bar'},
                {'condition-:foo': 0}, # flags
                []],
            ),
        )
    assert account_props.Get(cs.ACCOUNT_IFACE_NOKIA_CONDITIONS,
            'Condition') == {':foo': 'bar'}

    assertEquals(dbus.Array(signature='o'),
            account_props.Get(cs.ACCOUNT, 'Supersedes'))
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Supersedes',
            dbus.Array([cs.ACCOUNT_PATH_PREFIX + 'x/y/z'],
                signature='o'))
    q.expect_many(
        EventPattern('dbus-signal',
            path=account_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            args=[{'Supersedes': [cs.ACCOUNT_PATH_PREFIX + 'x/y/z']}]),
        EventPattern('dbus-return', method='Set'),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='DeferringSetAttribute',
            args=[account_path, 'Supersedes',
                [cs.ACCOUNT_PATH_PREFIX + 'x/y/z']]),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='CommittingOne',
            args=[account_path]),
        EventPattern('dbus-method-call',
            interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
            method='UpdateAttributes',
            args=[account_path[len(cs.ACCOUNT_PATH_PREFIX):],
                {'Supersedes': [cs.ACCOUNT_PATH_PREFIX + 'x/y/z']},
                {'Supersedes': 0}, # flags
                []],
            ),
        )
    assertEquals(dbus.Array([cs.ACCOUNT_PATH_PREFIX + 'x/y/z'],
                signature='o'),
            account_props.Get(cs.ACCOUNT, 'Supersedes'))

    # Set some properties to invalidly typed values - this currently succeeds
    # but is a no-op, although in future it should change to raising an
    # exception

    forbidden = [
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='DeferringSetAttribute'),
        EventPattern('dbus-method-call',
            interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
            method='UpdateAttributes'),
       ]
    q.forbid_events(forbidden)

    # this variable's D-Bus type must differ from the types of all known
    # properties
    badly_typed = dbus.Struct(('wrongly typed',), signature='s')

    for p in ('DisplayName', 'Icon', 'Enabled', 'Nickname',
            'AutomaticPresence', 'ConnectAutomatically', 'RequestedPresence'):
        try:
            account_props.Set(cs.ACCOUNT, p, badly_typed)
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.INVALID_ARGUMENT, \
                    (p, e.get_dbus_name())
        else:
            raise AssertionError('Setting %s with wrong type should fail' % p)

    for p in ('Avatar',):
        try:
            account_props.Set(cs.ACCOUNT_IFACE_AVATAR, p, badly_typed)
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.INVALID_ARGUMENT, \
                    (p, e.get_dbus_name())
        else:
            raise AssertionError('Setting %s with wrong type should fail' % p)

    for p in ('Condition',):
        try:
            account_props.Set(cs.ACCOUNT_IFACE_NOKIA_CONDITIONS, p,
                    badly_typed)
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.INVALID_ARGUMENT, \
                    (p, e.get_dbus_name())
        else:
            raise AssertionError('Setting %s with wrong type should fail' % p)

    # Make sure MC hasn't crashed yet, and make sure some properties are what
    # we expect them to be

    properties = account_props.GetAll(cs.ACCOUNT)
    assert properties['DisplayName'] == 'Work account'
    assert properties['Icon'] == 'im-jabber'
    properties = account_props.GetAll(cs.ACCOUNT_IFACE_AVATAR)
    assert properties['Avatar'] == ([], '')

    q.unforbid_events(forbidden)

    # Delete the account
    call_async(q, account_iface, 'Remove')
    q.expect_many(
        EventPattern('dbus-return', method='Remove'),
        EventPattern('dbus-signal',
            path=account_path,
            signal='Removed',
            interface=cs.ACCOUNT,
            args=[]
            ),
        EventPattern('dbus-signal',
            path=cs.AM_PATH,
            signal='AccountRemoved',
            interface=cs.AM,
            args=[account_path]
            ),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='DeferringDelete',
            args=[account_path]),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='CommittingOne',
            args=[account_path]),
        EventPattern('dbus-method-call',
            interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
            method='DeleteAccount',
            args=[account_path[len(cs.ACCOUNT_PATH_PREFIX):]]),
        )

    # Check the account is correctly deleted
    properties = account_manager.GetAll(cs.AM,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert properties is not None
    assert properties.get('ValidAccounts') == [], properties
    assert properties.get('InvalidAccounts') == [], properties


if __name__ == '__main__':
    exec_test(test, {})
