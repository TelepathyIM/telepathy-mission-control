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
"""Regression test for EnsureChannel counting as approval of an incoming text
channel.
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

    # One client (Kopete) has less specific filters than the other (Empathy),
    # to make sure that the dispatcher would normally prefer Empathy; this
    # means that when we use Kopete as the preferred handler, we know that
    # if Kopete is invoked, then preferring the preferred handler correctly
    # took precedence over the normal logic.
    vague_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')
    text_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    empathy_bus = dbus.bus.BusConnection()
    kopete_bus = dbus.bus.BusConnection()
    q.attach_to_bus(empathy_bus)
    q.attach_to_bus(kopete_bus)
    # Two clients want to observe, approve and handle channels
    empathy = SimulatedClient(q, empathy_bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)
    kopete = SimulatedClient(q, kopete_bus, 'Kopete',
            observe=[vague_fixed_properties], approve=[vague_fixed_properties],
            handle=[vague_fixed_properties], bypass_approval=False)

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
    # Empathy has a more specific filter, so it comes first
    assert handlers == [cs.tp_name_prefix + '.Client.Empathy',
            cs.tp_name_prefix + '.Client.Kopete'], handlers

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

    # Both Observers are told about the new channel

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
    assert e.args[0] == account.object_path, e.args
    assert e.args[1] == conn.object_path, e.args
    assert e.args[3] == cdo_path, e.args
    assert e.args[4] == [], e.args      # no requests satisfied
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == chan.object_path, channels
    assert channels[0][1] == channel_properties, channels

    assert k.args == e.args

    # Both Observers indicate that they are ready to proceed
    q.dbus_return(k.message, bus=empathy_bus, signature='')
    q.dbus_return(e.message, bus=kopete_bus, signature='')

    # The Approvers are next

    e, k = q.expect_many(
            EventPattern('dbus-method-call',
                path=empathy.object_path,
                interface=cs.APPROVER, method='AddDispatchOperation',
                handled=False),
            EventPattern('dbus-method-call',
                path=kopete.object_path,
                interface=cs.APPROVER, method='AddDispatchOperation',
                handled=False),
            )

    assert e.args == [[(chan.object_path, channel_properties)],
            cdo_path, cdo_properties]
    assert k.args == e.args

    q.dbus_return(e.message, bus=empathy_bus, signature='')
    q.dbus_return(k.message, bus=kopete_bus, signature='')

    # Both Approvers now have a flashing icon or something, trying to get the
    # user's attention. However, the user is busy looking through the address
    # book, and independently decides to talk to Juliet. The address book
    # is from KDE so wants Kopete to be used.

    user_action_time = dbus.Int64(1238582606)

    request = dbus.Dictionary({
            cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
            cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
            cs.CHANNEL + '.TargetID': 'juliet',
            }, signature='sv')
    call_async(q, cd, 'EnsureChannel',
            account.object_path, request, user_action_time, kopete.bus_name,
            dbus_interface=cs.CD)
    ret, add_request_call = q.expect_many(
            EventPattern('dbus-return', method='EnsureChannel'),
            EventPattern('dbus-method-call', handled=False,
                interface=cs.CLIENT_IFACE_REQUESTS,
                method='AddRequest', path=kopete.object_path),
            )
    request_path = ret.value[0]

    assert add_request_call.args[0] == request_path
    request_props = add_request_call.args[1]
    assert request_props[cs.CR + '.Account'] == account.object_path
    assert request_props[cs.CR + '.Requests'] == [request]
    assert request_props[cs.CR + '.UserActionTime'] == user_action_time
    assert request_props[cs.CR + '.PreferredHandler'] == kopete.bus_name
    assert request_props[cs.CR + '.Interfaces'] == []

    # UI connects to signals and calls ChannelRequest.Proceed()

    cr = bus.get_object(cs.AM, request_path)
    request_props = cr.GetAll(cs.CR, dbus_interface=cs.PROPERTIES_IFACE)
    assert request_props['Account'] == account.object_path
    assert request_props['Requests'] == [request]
    assert request_props['UserActionTime'] == user_action_time
    assert request_props['PreferredHandler'] == kopete.bus_name
    assert request_props['Interfaces'] == []

    cr.Proceed(dbus_interface=cs.CR)

    cm_request_call = q.expect('dbus-method-call',
            interface=cs.CONN_IFACE_REQUESTS,
            method='EnsureChannel',
            path=conn.object_path, args=[request], handled=False)

    q.dbus_return(add_request_call.message, bus=kopete_bus, signature='')

    # Time passes. The CM returns the existing channel

    q.dbus_return(cm_request_call.message, False,
            chan.object_path, chan.immutable, signature='boa{sv}')

    # EnsureChannel constitutes approval, so Kopete is told to handle the
    # channel

    e = q.expect('dbus-method-call',
            path=kopete.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)
    assert e.args[0] == account.object_path, e.args
    assert e.args[1] == conn.object_path, e.args
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == chan.object_path, channels
    assert channels[0][1] == chan.immutable, channels
    assert e.args[3] == [request_path], e.args
    assert e.args[4] == user_action_time, (e.args[4], user_action_time)
    assert isinstance(e.args[5], dict)
    assert len(e.args) == 6

    q.dbus_return(e.message, bus=kopete_bus, signature='')

    q.expect_many(
            EventPattern('dbus-signal', interface=cs.CDO, signal='Finished'),
            EventPattern('dbus-signal', interface=cs.CD_IFACE_OP_LIST,
                signal='DispatchOperationFinished'),
            )

    # Now there are no more active channel dispatch operations
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

if __name__ == '__main__':
    exec_test(test, {})
