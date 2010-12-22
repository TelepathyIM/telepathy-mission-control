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
from mctest import exec_test, create_fakecm_account, get_account_manager
import constants as cs

def test(q, bus, mc):
    cm_name_ref = dbus.service.BusName(
            cs.tp_name_prefix + '.ConnectionManager.onewitheverything',
            bus=bus)

    # Get the AccountManager interface
    account_manager = get_account_manager(bus)
    account_manager_iface = dbus.Interface(account_manager, cs.AM)

    params = dbus.Dictionary({
        's': 'lalala',
        'o': dbus.ObjectPath('/lalala'),
        'b': False,
        'q': dbus.UInt16(42),
        'u': dbus.UInt32(0xFFFFFFFFL),
        't': dbus.UInt64(0xFFFFffffFFFFffffL),
        'n': dbus.Int16(-42),
        'i': dbus.Int32(-42),
        'x': dbus.Int64(-1 * 0x7FFFffffFFFFffffL),
        'd': 4.5,
        'y': dbus.Byte(42),
        'as': dbus.Array(['one', 'two', 'three'], signature='s')
        }, signature='sv')

    # Create an account
    call_async(q, account_manager_iface, 'CreateAccount',
            'onewitheverything', # Connection_Manager
            'serializable', # Protocol
            'fakeaccount', #Display_Name
            params, # Parameters
            {}, # Properties
            )

    am_signal, ret = q.expect_many(
            EventPattern('dbus-signal', path=cs.AM_PATH,
                interface=cs.AM, signal='AccountValidityChanged'),
            EventPattern('dbus-return', method='CreateAccount'),
            )
    account_path = ret.value[0]
    assert am_signal.args == [account_path, True], am_signal.args
    assert account_path is not None

    account = bus.get_object(
        cs.tp_name_prefix + '.AccountManager',
        account_path)
    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    stored_params = account_props.Get(cs.ACCOUNT, 'Parameters')

    for k in stored_params:
        assert k in params, k

    for k in params:
        assert k in stored_params, k
        assert stored_params[k] == params[k], (k, stored_params[k], params[k])

if __name__ == '__main__':
    exec_test(test, {})
