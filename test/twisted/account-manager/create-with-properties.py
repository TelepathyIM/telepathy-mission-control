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
from mctest import exec_test, create_fakecm_account
import constants as cs

def test(q, bus, mc):
    # Get the AccountManager interface
    account_manager = bus.get_object(cs.AM, cs.AM_PATH)
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
    supported = properties.get('SupportedAccountProperties')

    # assert that current functionality exists
    assert cs.AM_IFACE_CREATION_DRAFT in interfaces, interfaces
    assert cs.AM_IFACE_NOKIA_QUERY in interfaces, interfaces

    assert (cs.ACCOUNT + '.AutomaticPresence') in supported
    assert (cs.ACCOUNT + '.Enabled') in supported
    assert (cs.ACCOUNT + '.Icon') in supported
    assert (cs.ACCOUNT + '.Nickname') in supported
    assert (cs.ACCOUNT + '.ConnectAutomatically') in supported
    assert (cs.ACCOUNT_IFACE_AVATAR + '.Avatar') in supported
    assert (cs.ACCOUNT_IFACE_NOKIA_COMPAT + '.Profile') in supported
    assert (cs.ACCOUNT_IFACE_NOKIA_COMPAT + '.SecondaryVCardFields') in supported
    assert (cs.ACCOUNT_IFACE_NOKIA_CONDITIONS + '.Condition') in supported

    assert (cs.ACCOUNT + '.RequestedPresence') in supported

    params = dbus.Dictionary({"account": "anarki@example.com",
        "password": "secrecy"}, signature='sv')

    cm_name_ref = dbus.service.BusName(cs.tp_name_prefix +
            '.ConnectionManager.fakecm', bus=bus)
    account_manager = bus.get_object(cs.AM, cs.AM_PATH)
    am_iface = dbus.Interface(account_manager, cs.AM)

    creation_properties = dbus.Dictionary({
        cs.ACCOUNT + '.Enabled': True,
        cs.ACCOUNT + '.AutomaticPresence': dbus.Struct((
            dbus.UInt32(cs.PRESENCE_TYPE_BUSY),
            'busy', 'Exploding'), signature='uss'),
        cs.ACCOUNT + '.RequestedPresence': dbus.Struct((
            dbus.UInt32(cs.PRESENCE_TYPE_AWAY),
            'away', 'Respawning'), signature='uss'),
        cs.ACCOUNT + '.Icon': 'quake3arena',
        cs.ACCOUNT + '.Nickname': 'AnArKi',
        cs.ACCOUNT + '.ConnectAutomatically': True,
        cs.ACCOUNT_IFACE_AVATAR + '.Avatar': (dbus.ByteArray('foo'),
            'image/jpeg'),
        cs.ACCOUNT_IFACE_NOKIA_COMPAT + '.Profile': 'openarena',
        cs.ACCOUNT_IFACE_NOKIA_COMPAT + '.SecondaryVCardFields':
            dbus.Array(['x-ioquake3', 'x-quake3'], signature='s'),
        cs.ACCOUNT_IFACE_NOKIA_CONDITIONS + '.Condition':
            dbus.Dictionary({ 'has-quad-damage': ':y' }, signature='ss'),
        }, signature='sv')

    call_async(q, am_iface, 'CreateAccount',
            'fakecm',
            'fakeprotocol',
            'fakeaccount',
            params,
            creation_properties)

    # The spec has no order guarantee here.
    # FIXME: MC ought to also introspect the CM and find out that the params
    # are in fact sufficient

    a_signal, am_signal, ret, rc = q.expect_many(
            EventPattern('dbus-signal',
                signal='AccountPropertyChanged', interface=cs.ACCOUNT,
                predicate=(lambda e: 'Valid' in e.args[0])),
            EventPattern('dbus-signal', path=cs.AM_PATH,
                signal='AccountValidityChanged', interface=cs.AM),
            EventPattern('dbus-return', method='CreateAccount'),
            EventPattern('dbus-method-call', method='RequestConnection'),
            )
    account_path = ret.value[0]
    assert am_signal.args == [account_path, True], am_signal.args
    assert a_signal.args[0]['Valid'] == True, a_signal.args

    assert account_path is not None

    account = bus.get_object(
        cs.tp_name_prefix + '.AccountManager',
        account_path)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    properties = account_props.GetAll(cs.ACCOUNT)
    assert properties.get('AutomaticPresence') == (cs.PRESENCE_TYPE_BUSY,
            'busy', 'Exploding'), \
        properties.get('AutomaticPresence')
    assert properties.get('RequestedPresence') == (cs.PRESENCE_TYPE_AWAY,
            'away', 'Respawning'), \
        properties.get('RequestedPresence')
    assert properties.get('ConnectAutomatically') == True, \
        properties.get('ConnectAutomatically')
    assert properties.get('Enabled') == True, \
        properties.get('Enabled')
    assert properties.get('Icon') == 'quake3arena', \
        properties.get('Icon')
    assert properties.get('Nickname') == 'AnArKi', \
        properties.get('Nickname')

    properties = account_props.GetAll(cs.ACCOUNT_IFACE_AVATAR)
    assert properties.get('Avatar') == ([ord('f'), ord('o'), ord('o')],
            'image/jpeg')

    properties = account_props.GetAll(cs.ACCOUNT_IFACE_NOKIA_COMPAT)
    assert properties.get('Profile') == 'openarena'
    assert sorted(properties.get('SecondaryVCardFields')) == \
            ['x-ioquake3', 'x-quake3']

    properties = account_props.GetAll(cs.ACCOUNT_IFACE_NOKIA_CONDITIONS)
    assert properties.get('Condition') == {
            'has-quad-damage': ':y',
            }

if __name__ == '__main__':
    exec_test(test, {})
