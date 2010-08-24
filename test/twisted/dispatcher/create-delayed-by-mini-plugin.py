# -*- coding: utf-8 -*-

# Copyright © 2009 Nokia Corporation.
# Copyright © 2009-2010 Collabora Ltd.
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
"""Regression test for ChannelDispatcher delaying channel creation due to
a plugin.
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, sync_dbus
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

# Rejected by the plugin
DELAYED_CTYPE = 'com.example.QuestionableChannel'

def test(q, bus, mc):
    policy_bus_name_ref = dbus.service.BusName('com.example.Policy', bus)

    # For now, we should never actually be asked to make a channel.
    forbidden = [
            EventPattern('dbus-method-call', method='CreateChannel'),
            EventPattern('dbus-method-call', method='EnsureChannel'),
            EventPattern('dbus-method-call', method='ObserveChannels'),
            EventPattern('dbus-method-call', method='AddDispatchOperation'),
            EventPattern('dbus-method-call', method='HandleChannels'),
            ]
    q.forbid_events(forbidden)

    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': DELAYED_CTYPE,
        }, signature='sv')

    client = SimulatedClient(q, bus, 'Empathy',
            observe=[fixed_properties], approve=[fixed_properties],
            handle=[fixed_properties], bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [client])

    user_action_time = dbus.Int64(1238582606)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)

    # UI calls ChannelDispatcher.CreateChannel
    request = dbus.Dictionary({
            cs.CHANNEL + '.ChannelType': DELAYED_CTYPE,
            cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
            cs.CHANNEL + '.TargetID': 'juliet',
            }, signature='sv')
    call_async(q, cd, 'CreateChannel',
            account.object_path, request, user_action_time, client.bus_name,
            dbus_interface=cs.CD)

    ret = q.expect('dbus-return',
            method='CreateChannel')
    request_path = ret.value[0]

    # UI connects to signals and calls ChannelRequest.Proceed()

    cr = bus.get_object(cs.AM, request_path)
    request_props = cr.GetAll(cs.CR, dbus_interface=cs.PROPERTIES_IFACE)
    assert request_props['Account'] == account.object_path
    assert request_props['Requests'] == [request]
    assert request_props['UserActionTime'] == user_action_time
    assert request_props['PreferredHandler'] == client.bus_name
    assert request_props['Interfaces'] == []

    call_async(q, cr, 'Proceed', dbus_interface=cs.CR)

    q.expect('dbus-return', method='Proceed')

    # What does the policy service think?
    permission = q.expect('dbus-method-call', path='/com/example/Policy',
            interface='com.example.Policy', method='RequestRequest')

    # Think about it for a bit, then allow dispatching to continue
    sync_dbus(bus, q, account)
    q.unforbid_events(forbidden)
    q.dbus_return(permission.message, signature='')

    # Only now does the CM's CreateChannel method get called
    cm_request_call = q.expect('dbus-method-call',
            interface=cs.CONN_IFACE_REQUESTS,
            method='CreateChannel',
            path=conn.object_path, args=[request], handled=False)
    q.dbus_raise(cm_request_call.message, cs.INVALID_ARGUMENT, 'No')

if __name__ == '__main__':
    exec_test(test, {})
