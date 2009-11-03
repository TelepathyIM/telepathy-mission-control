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
"""Regression test for plugins rejecting an incoming channel immediately.
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, sync_dbus
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    text_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    # Throughout this entire test, we should never be asked to approve or
    # handle a channel.
    forbidden = [
            EventPattern('dbus-method-call', method='AddDispatchOperation'),
            EventPattern('dbus-method-call', method='HandleChannels'),
            ]
    q.forbid_events(forbidden)

    # Two clients want to observe, approve and handle channels
    empathy = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)
    kopete = SimulatedClient(q, bus, 'Kopete',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [empathy, kopete])

    # subscribe to the OperationList interface (MC assumes that until this
    # property has been retrieved once, nobody cares)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

    # This ID is special-cased by the mcp-plugin plugin, which rejects
    # channels to or from it by destroying them, without waiting for observers
    # to return
    target = 'rick.astley@example.net'
    channel_properties = dbus.Dictionary(text_fixed_properties,
            signature='sv')
    channel_properties[cs.CHANNEL + '.TargetID'] = target
    channel_properties[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, target)
    channel_properties[cs.CHANNEL + '.InitiatorID'] = target
    channel_properties[cs.CHANNEL + '.InitiatorHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, target)
    channel_properties[cs.CHANNEL + '.Requested'] = False
    channel_properties[cs.CHANNEL + '.Interfaces'] = \
            dbus.Array([cs.CHANNEL_IFACE_DESTROYABLE, cs.CHANNEL_IFACE_GROUP,
                ],signature='s')

    chan = SimulatedChannel(conn, channel_properties, group=True)
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
    handlers.sort()
    assert handlers == [cs.tp_name_prefix + '.Client.Empathy',
            cs.tp_name_prefix + '.Client.Kopete'], handlers

    # The plugin realises we've been rickrolled, and responds. It calls Destroy
    # even though neither Empathy nor Kopete has returned from ObserveChannels
    # yet
    destruction, e, k = q.expect_many(
            EventPattern('dbus-method-call',
                path=chan.object_path,
                interface=cs.CHANNEL_IFACE_DESTROYABLE, method='Destroy',
                args=[], handled=False),
            EventPattern('dbus-method-call',
                path=empathy.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False),
            EventPattern('dbus-method-call',
                path=kopete.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False),
            )
    # treat the destruction like Close
    chan.Close(destruction)

    # Both Observers indicate that they are ready to proceed (somewhat late)
    q.dbus_return(k.message, signature='')
    q.dbus_return(e.message, signature='')

    # When the Observers have returned, the CDO finishes
    q.expect_many(
            EventPattern('dbus-signal', path=cdo_path,
                interface=cs.CDO, signal='Finished'),
            EventPattern('dbus-signal', path=cs.CD_PATH,
                interface=cs.CD_IFACE_OP_LIST,
                signal='DispatchOperationFinished',
                args=[cdo_path]),
            )

    # This ID is also special-cased
    target = 'mc.hammer@example.net'
    channel_properties = dbus.Dictionary(text_fixed_properties,
            signature='sv')
    channel_properties[cs.CHANNEL + '.TargetID'] = target
    channel_properties[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, target)
    channel_properties[cs.CHANNEL + '.InitiatorID'] = target
    channel_properties[cs.CHANNEL + '.InitiatorHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, target)
    channel_properties[cs.CHANNEL + '.Requested'] = False
    channel_properties[cs.CHANNEL + '.Interfaces'] = \
            dbus.Array([cs.CHANNEL_IFACE_DESTROYABLE, cs.CHANNEL_IFACE_GROUP,
                ],signature='s')

    chan = SimulatedChannel(conn, channel_properties, group=True)
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
    handlers.sort()
    assert handlers == [cs.tp_name_prefix + '.Client.Empathy',
            cs.tp_name_prefix + '.Client.Kopete'], handlers

    # The plugin realises it's MC Hammer, and responds, but its response waits
    # for the observers to return
    e, k = q.expect_many(
            EventPattern('dbus-method-call',
                path=empathy.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False),
            EventPattern('dbus-method-call',
                path=kopete.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False),
            )

    sync_dbus(bus, q, account)

    # Both Observers indicate that they are ready to proceed
    q.dbus_return(k.message, signature='')
    q.dbus_return(e.message, signature='')

    _, _, e = q.expect_many(
            EventPattern('dbus-signal', path=cdo_path,
                interface=cs.CDO, signal='Finished'),
            EventPattern('dbus-signal', path=cs.CD_PATH,
                interface=cs.CD_IFACE_OP_LIST,
                signal='DispatchOperationFinished',
                args=[cdo_path]),
            EventPattern('dbus-method-call',
                path=chan.object_path,
                interface=cs.CHANNEL_IFACE_GROUP,
                method='RemoveMembersWithReason', args=[[conn.self_handle],
                    "Can't touch this", cs.GROUP_REASON_PERMISSION_DENIED],
                handled=False),
            )
    q.dbus_return(e.message, signature='')
    chan.close()

if __name__ == '__main__':
    exec_test(test, {})
