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
"""Regression test for dispatching an incoming Text channel that was already
there before the Connection became ready.
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, SimulatedChannel, expect_client_setup
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)

    text_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    # Two clients want to observe, approve and handle channels
    empathy = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)
    kopete = SimulatedClient(q, bus, 'Kopete',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [empathy, kopete])

    # Enable the account
    account.Set(cs.ACCOUNT, 'Enabled', True,
            dbus_interface=cs.PROPERTIES_IFACE)

    requested_presence = dbus.Struct((dbus.UInt32(2L),
        dbus.String(u'available'), dbus.String(u'')))
    account.Set(cs.ACCOUNT,
            'RequestedPresence', requested_presence,
            dbus_interface=cs.PROPERTIES_IFACE)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=cs.tp_name_prefix + '.ConnectionManager.fakecm',
            path=cs.tp_path_prefix + '/ConnectionManager/fakecm',
            interface=cs.tp_name_prefix + '.ConnectionManager',
            handled=False)

    # Don't allow the Connection to become ready until we want it to, by
    # avoiding a return from GetInterfaces
    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', '_',
            'myself', implement_get_interfaces=False, has_requests=False)

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    # this is the pre-Connect one
    e = q.expect('dbus-method-call', method='GetInterfaces',
            path=conn.object_path, handled=False)
    q.dbus_raise(e.message, cs.DISCONNECTED, 'Not connected yet')

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CSR_NONE_SPECIFIED)

    get_interfaces_call = q.expect('dbus-method-call', method='GetInterfaces',
            path=conn.object_path, handled=False)

    # subscribe to the OperationList interface (MC assumes that until this
    # property has been retrieved once, nobody cares)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

    # Before returning from GetInterfaces, make a Channel spring into
    # existence

    channel_properties = dbus.Dictionary(text_fixed_properties,
            signature='sv')
    channel_properties[cs.CHANNEL + '.TargetID'] = 'juliet'
    channel_properties[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'juliet')
    channel_properties[cs.CHANNEL + '.InitiatorID'] = 'juliet'
    channel_properties[cs.CHANNEL + '.InitiatorHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'juliet')
    channel_properties[cs.CHANNEL + '.Requested'] = False
    channel_properties[cs.CHANNEL + '.Interfaces'] = dbus.Array(signature='s')

    chan = SimulatedChannel(conn, channel_properties)
    chan.announce()

    # Now reply to GetInterfaces and say we don't have Requests
    conn.GetInterfaces(get_interfaces_call)

    # MC shoots down the connection. Goodbye!
    q.expect_many(
            EventPattern('dbus-signal', signal='AccountPropertyChanged',
                predicate=lambda e:
                    e.args[0].get('ConnectionError') ==
                        cs.SOFTWARE_UPGRADE_REQUIRED),
            EventPattern('dbus-method-call', method='Disconnect',
                handled=True),
            )

if __name__ == '__main__':
    exec_test(test, {})
