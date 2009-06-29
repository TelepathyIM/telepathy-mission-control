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
"""Regression test for dispatching an incoming AbiWord StreamTube channel.
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    abiword_fixed_properties = dbus.Dictionary({
        cs.CHANNEL_TYPE_STREAM_TUBE + '.Service': 'x-abiword',
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_STREAM_TUBE,
        }, signature='sv')

    # subscribe to the OperationList interface (MC assumes that until this
    # property has been retrieved once, nobody cares)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

    channel_properties = dbus.Dictionary(abiword_fixed_properties,
            signature='sv')
    channel_properties[cs.CHANNEL + '.TargetHandleType'] = cs.HT_CONTACT
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

    # A channel dispatch operation is created

    e = q.expect('dbus-signal',
            path=cs.CD_PATH,
            interface=cs.CD_IFACE_OP_LIST,
            signal='NewDispatchOperation')

    cdo_path = e.args[0]
    cdo_properties = e.args[1]

    assert cdo_properties[cs.CDO + '.Account'] == account.object_path
    assert cdo_properties[cs.CDO + '.Connection'] == conn.object_path
    assert cs.CDO + '.Interfaces' in cdo_properties

    handlers = cdo_properties[cs.CDO + '.PossibleHandlers'][:]
    assert handlers == [cs.tp_name_prefix + '.Client.AbiWord'], handlers

    assert cs.CD_IFACE_OP_LIST in cd_props.Get(cs.CD, 'Interfaces')
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') ==\
            [(cdo_path, cdo_properties)]

    cdo = bus.get_object(cs.CD, cdo_path)
    cdo_iface = dbus.Interface(cdo, cs.CDO)
    cdo_props_iface = dbus.Interface(cdo, cs.PROPERTIES_IFACE)

    assert cdo_props_iface.Get(cs.CDO, 'Interfaces') == \
            cdo_properties[cs.CDO + '.Interfaces']
    assert cdo_props_iface.Get(cs.CDO, 'Connection') == conn.object_path
    assert cdo_props_iface.Get(cs.CDO, 'Account') == account.object_path
    assert cdo_props_iface.Get(cs.CDO, 'Channels') == [(chan.object_path,
        channel_properties)]
    assert cdo_props_iface.Get(cs.CDO, 'PossibleHandlers') == \
            cdo_properties[cs.CDO + '.PossibleHandlers']

    abiword_object_path = cs.tp_path_prefix + '/Client/AbiWord'

    # There are no approvers so the only Handler is asked to handle the
    # channels

    # The fake AbiWord implementation is run
    e = q.expect('dbus-signal',
            path=cs.tp_path_prefix + '/RegressionTests',
            interface=cs.tp_name_prefix + '.RegressionTests',
            signal='FakeStartup',
            args=[cs.tp_name_prefix + '.Client.AbiWord'],
            )
    # We take on its identity to be able to continue with the test
    abiword = SimulatedClient(q, bus, 'AbiWord',
            handle=[abiword_fixed_properties])

    e = q.expect('dbus-method-call',
            path=abiword.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)
    # AbiWord accepts the channels
    q.dbus_return(e.message, signature='')

    # FIXME: this isn't currently emitted (part of the same bug as emitting
    # it as soon as HandleChannels is called)
    #q.expect('dbus-signal', path=cdo_path, signal='Finished')
    #q.expect('dbus-signal', path=cs.CD_PATH,
    #    signal='DispatchOperationFinished', args=[cdo_path])

    # Now there are no more active channel dispatch operations
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

if __name__ == '__main__':
    exec_test(test, {})
