# Copyright (C) 2009,2010 Nokia Corporation
# Copyright (C) 2009,2010 Collabora Ltd.
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
"""Regression test for respawning crashed Observers.
"""

import dbus
import dbus.bus
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

    # Empathy is an Observers who will crash
    empathy_bus = dbus.bus.BusConnection()
    empathy_bus.set_exit_on_disconnect(False)   # we'll disconnect later

    # Kopete is an Approver, Handler and will not crash
    kopete_bus = dbus.bus.BusConnection()
    q.attach_to_bus(empathy_bus)
    q.attach_to_bus(kopete_bus)

    # Two clients want to observe, approve and handle channels
    empathy = SimulatedClient(q, empathy_bus, 'Empathy',
            observe=[text_fixed_properties], approve=[],
            handle=[], wants_recovery=True)
    kopete = SimulatedClient(q, kopete_bus, 'Kopete',
            observe=[], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [empathy, kopete])

    # subscribe to the OperationList interface (MC assumes that until this
    # property has been retrieved once, nobody cares)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

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
    assert handlers == [cs.tp_name_prefix + '.Client.Kopete'], handlers

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

    # The Observer (Empathy) is told about the new channel

    e = q.expect('dbus-method-call',
                path=empathy.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False)

    assert e.args[0] == account.object_path, e.args
    assert e.args[1] == conn.object_path, e.args
    assert e.args[3] == cdo_path, e.args
    assert e.args[4] == [], e.args      # no requests satisfied
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == chan.object_path, channels
    assert channels[0][1] == channel_properties, channels

    # Empathy indicates that it is ready to proceed
    q.dbus_return(e.message, bus=empathy_bus, signature='')

    # The Approver (Kopete) is next

    k = q.expect('dbus-method-call',
                path=kopete.object_path,
                interface=cs.APPROVER, method='AddDispatchOperation',
                handled=False)

    assert k.args == [[(chan.object_path, channel_properties)],
            cdo_path, cdo_properties]

    q.dbus_return(k.message, bus=kopete_bus, signature='')

    # The user responds to Kopete
    call_async(q, cdo_iface, 'HandleWith',
            cs.tp_name_prefix + '.Client.Kopete')

    # Kopete is asked to handle the channels
    k = q.expect('dbus-method-call',
            path=kopete.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    # Kopete accepts the channels
    q.dbus_return(k.message, bus=kopete_bus, signature='')

    q.expect_many(
            EventPattern('dbus-return', method='HandleWith'),
            EventPattern('dbus-signal', interface=cs.CDO, signal='Finished'),
            EventPattern('dbus-signal', interface=cs.CD_IFACE_OP_LIST,
                signal='DispatchOperationFinished'),
            )

    # Now there are no more active channel dispatch operations
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

    # Another channel: this one will remain unapproved

    channel2_properties = dbus.Dictionary(text_fixed_properties,
            signature='sv')
    channel2_properties[cs.CHANNEL + '.TargetID'] = 'mercutio'
    channel2_properties[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'mercutio')
    channel2_properties[cs.CHANNEL + '.InitiatorID'] = 'mercutio'
    channel2_properties[cs.CHANNEL + '.InitiatorHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'mercutio')
    channel2_properties[cs.CHANNEL + '.Requested'] = False
    channel2_properties[cs.CHANNEL + '.Interfaces'] = dbus.Array(signature='s')

    chan2 = SimulatedChannel(conn, channel2_properties)
    chan2.announce()

    # A channel dispatch operation is created

    e = q.expect('dbus-signal',
            path=cs.CD_PATH,
            interface=cs.CD_IFACE_OP_LIST,
            signal='NewDispatchOperation')

    cdo2_path = e.args[0]
    cdo2_properties = e.args[1]

    assert cdo2_properties[cs.CDO + '.Account'] == account.object_path
    assert cdo2_properties[cs.CDO + '.Connection'] == conn.object_path
    assert cs.CDO + '.Interfaces' in cdo_properties

    handlers = cdo_properties[cs.CDO + '.PossibleHandlers'][:]
    handlers.sort()
    assert handlers == [cs.tp_name_prefix + '.Client.Kopete'], handlers

    assert cs.CD_IFACE_OP_LIST in cd_props.Get(cs.CD, 'Interfaces')
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') ==\
            [(cdo2_path, cdo2_properties)]

    cdo2 = bus.get_object(cs.CD, cdo2_path)
    cdo2_iface = dbus.Interface(cdo2, cs.CDO)
    cdo2_props_iface = dbus.Interface(cdo2, cs.PROPERTIES_IFACE)

    assert cdo2_props_iface.Get(cs.CDO, 'Interfaces') == \
            cdo2_properties[cs.CDO + '.Interfaces']
    assert cdo2_props_iface.Get(cs.CDO, 'Connection') == conn.object_path
    assert cdo2_props_iface.Get(cs.CDO, 'Account') == account.object_path
    assert cdo2_props_iface.Get(cs.CDO, 'Channels') == [(chan2.object_path,
        channel2_properties)]
    assert cdo2_props_iface.Get(cs.CDO, 'PossibleHandlers') == \
            cdo2_properties[cs.CDO + '.PossibleHandlers']

    # The Observer (Empathy) is told about the new channel

    e = q.expect('dbus-method-call',
                path=empathy.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False)

    assert e.args[0] == account.object_path, e.args
    assert e.args[1] == conn.object_path, e.args
    assert e.args[3] == cdo2_path, e.args
    assert e.args[4] == [], e.args      # no requests satisfied
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == chan2.object_path, channels
    assert channels[0][1] == channel2_properties, channels

    # Empathy indicates that it is ready to proceed
    q.dbus_return(e.message, bus=empathy_bus, signature='')

    # The Approver (Kopete) is next; this time, we don't approve

    k = q.expect('dbus-method-call',
                path=kopete.object_path,
                interface=cs.APPROVER, method='AddDispatchOperation',
                handled=False)

    assert k.args == [[(chan2.object_path, channel2_properties)],
            cdo2_path, cdo2_properties]

    q.dbus_return(k.message, bus=kopete_bus, signature='')

    # Empathy crashes
    empathy.release_name()

    e = q.expect('dbus-signal',
            signal='NameOwnerChanged',
            predicate=(lambda e:
                e.args[0] == empathy.bus_name and e.args[2] == ''),
            )
    empathy_unique_name = e.args[1]

    empathy_bus.flush()

    # Empathy gets restarted
    empathy.reacquire_name()

    e = q.expect('dbus-signal',
            signal='NameOwnerChanged',
            predicate=(lambda e:
                e.args[0] == empathy.bus_name and e.args[1] == ''),
            )
    empathy_unique_name = e.args[2]

    e1, e2 = q.expect_many(
            EventPattern('dbus-method-call',
                path=empathy.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                predicate=lambda e: e.args[2][0][0] == chan.object_path,
                handled=False),
            EventPattern('dbus-method-call',
                path=empathy.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                predicate=lambda e: e.args[2][0][0] == chan2.object_path,
                handled=False),
            )

    assert e1.args[0] == account.object_path, e1.args
    assert e1.args[1] == conn.object_path, e1.args
    assert e1.args[4] == [], e1.args      # no requests satisfied
    assert e1.args[5]['recovering'] == 1, e1.args # due to observer recovery
    channels = e1.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == chan.object_path, channels
    assert channels[0][1] == channel_properties, channels

    assert e2.args[0] == account.object_path, e2.args
    assert e2.args[1] == conn.object_path, e2.args
    assert e2.args[4] == [], e2.args      # no requests satisfied
    assert e2.args[5]['recovering'] == 1, e2.args # due to observer recovery
    channels = e2.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == chan2.object_path, channels
    assert channels[0][1] == channel2_properties, channels

    # Empathy indicates that it is ready to proceed
    q.dbus_return(e1.message, bus=empathy_bus, signature='')
    q.dbus_return(e2.message, bus=empathy_bus, signature='')

    sync_dbus(bus, q, mc)

if __name__ == '__main__':
    exec_test(test, {})
