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
        call_async, sync_dbus
from mctest import exec_test, create_fakecm_account, SimulatedConnection, \
        SimulatedChannel
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "smcv@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'RequestedPresence',
            (dbus.UInt32(cs.PRESENCE_TYPE_OFFLINE), 'offline', ''))
    q.expect('dbus-return', method='Set')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'AutomaticPresence',
            (dbus.UInt32(cs.PRESENCE_TYPE_BUSY), 'busy',
                'Testing automatic presence'))
    q.expect('dbus-return', method='Set')
    q.expect('dbus-signal', signal='AccountPropertyChanged',
            predicate=lambda e:
                e.args[0].get('AutomaticPresence', (None, None, None))[1]
                    == 'busy')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Enabled', False)
    q.expect('dbus-return', method='Set')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'ConnectAutomatically',
            False)
    q.expect('dbus-return', method='Set')

    # Requesting a channel won't put us online, since it's disabled

    # make sure RequestConnection doesn't get called
    events = [EventPattern('dbus-method-call', method='RequestConnection')]
    q.forbid_events(events)

    user_action_time = dbus.Int64(1238582606)

    cd = bus.get_object(cs.CD, cs.CD_PATH)

    request = dbus.Dictionary({
            cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
            cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
            cs.CHANNEL + '.TargetID': 'juliet',
            }, signature='sv')
    call_async(q, cd, 'CreateChannel',
            account.object_path, request, user_action_time, "",
            dbus_interface=cs.CD)
    ret = q.expect('dbus-return', method='CreateChannel')
    request_path = ret.value[0]

    cr = bus.get_object(cs.AM, request_path)
    request_props = cr.GetAll(cs.CR, dbus_interface=cs.PROPERTIES_IFACE)
    assert request_props['Account'] == account.object_path
    assert request_props['Requests'] == [request]
    assert request_props['UserActionTime'] == user_action_time
    assert request_props['PreferredHandler'] == ""
    assert request_props['Interfaces'] == []

    sync_dbus(bus, q, mc)

    cr.Proceed(dbus_interface=cs.CR)

    # FIXME: error isn't specified (NotAvailable perhaps?)
    q.expect('dbus-signal', path=cr.object_path,
            interface=cs.CR, signal='Failed')

if __name__ == '__main__':
    exec_test(test, {})
