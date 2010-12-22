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
"""Regression test for dispatching an incoming Text channel with bypassed
approval.
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, sync_dbus
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

text_fixed_properties = dbus.Dictionary({
    cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
    }, signature='sv')
contact_text_fixed_properties = dbus.Dictionary({
    cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
    cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
    }, signature='sv')
urgent_fixed_properties = dbus.Dictionary({
    cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
    cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
    'com.example.Urgency.Urgent': True,
    }, signature='sv')

def announce_common(q, bus, empathy, kopete, account, conn, cd_props,
        urgent=False):
    if urgent:
        jid = 'friar.lawrence'
    else:
        jid = 'juliet'

    channel_properties = dbus.Dictionary(contact_text_fixed_properties,
            signature='sv')
    channel_properties[cs.CHANNEL + '.TargetID'] = jid
    channel_properties[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, jid)
    channel_properties[cs.CHANNEL + '.InitiatorID'] = jid
    channel_properties[cs.CHANNEL + '.InitiatorHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, jid)
    channel_properties[cs.CHANNEL + '.Requested'] = False
    channel_properties[cs.CHANNEL + '.Interfaces'] = dbus.Array(signature='s')

    if urgent:
        channel_properties['com.example.Urgency.Urgent'] = True

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

    if urgent:
        # The handler with BypassApproval is first
        assert handlers[0] == cs.tp_name_prefix + '.Client.Kopete.BypassApproval'
        # Kopete's filter is more specific than Empathy's, so it comes next
        assert handlers[1] == cs.tp_name_prefix + '.Client.Kopete'
        # Empathy's filter is the least specific, so it's last
        assert handlers[2] == cs.tp_name_prefix + '.Client.Empathy'
        assert len(handlers) == 3
    else:
        handlers.sort()
        assert handlers == [cs.tp_name_prefix + '.Client.Empathy',
                cs.tp_name_prefix + '.Client.Kopete'], handlers

    assert cs.CD_IFACE_OP_LIST in cd_props.Get(cs.CD, 'Interfaces')
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') ==\
            [(cdo_path, cdo_properties)]

    cdo = bus.get_object(cs.CD, cdo_path)
    cdo_iface = dbus.Interface(cdo, cs.CDO)

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

    return cdo_iface, chan, channel_properties, [e, k]

def expect_and_exercise_approval(q, bus, chan, channel_properties,
        empathy, kopete, cdo_iface, cd_props):
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

    assert e.args[0] == [(chan.object_path, channel_properties)]
    assert k.args == e.args

    # Both Approvers indicate that they are ready to proceed
    q.dbus_return(e.message, signature='')
    q.dbus_return(k.message, signature='')

    # Both Approvers now have a flashing icon or something, trying to get the
    # user's attention

    # The user responds to Empathy first
    call_async(q, cdo_iface, 'HandleWith',
            cs.tp_name_prefix + '.Client.Empathy')

    # Empathy is asked to handle the channels
    e = q.expect('dbus-method-call',
            path=empathy.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    # Empathy accepts the channels
    q.dbus_return(e.message, signature='')

    q.expect_many(
            EventPattern('dbus-return', method='HandleWith'),
            EventPattern('dbus-signal', interface=cs.CDO, signal='Finished'),
            EventPattern('dbus-signal', interface=cs.CD_IFACE_OP_LIST,
                signal='DispatchOperationFinished'),
            )

    # Now there are no more active channel dispatch operations
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []


def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    # Two clients want to observe, approve and handle channels. Additionally,
    # Kopete recognises an "Urgent" flag on certain incoming channels, and
    # wants to bypass approval for them.
    empathy = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)
    kopete = SimulatedClient(q, bus, 'Kopete',
            observe=[contact_text_fixed_properties],
            approve=[contact_text_fixed_properties],
            handle=[contact_text_fixed_properties], bypass_approval=False)
    bypass = SimulatedClient(q, bus, 'Kopete.BypassApproval',
            observe=[], approve=[],
            handle=[urgent_fixed_properties], bypass_approval=True)

    # wait for MC to download the properties
    expect_client_setup(q, [empathy, kopete, bypass])

    # subscribe to the OperationList interface (MC assumes that until this
    # property has been retrieved once, nobody cares)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

    # First, a non-urgent channel is created

    cdo_iface, chan, channel_properties, observe_events = announce_common(q,
            bus, empathy, kopete, account, conn, cd_props, False)

    # Both Observers indicate that they are ready to proceed
    for e in observe_events:
        q.dbus_return(e.message, signature='')

    expect_and_exercise_approval(q, bus, chan, channel_properties,
            empathy, kopete, cdo_iface, cd_props)

    # Now a channel that bypasses approval comes in. During this process,
    # we should never be asked to approve anything.

    approval = [
            EventPattern('dbus-method-call', method='AddDispatchOperation'),
            ]

    q.forbid_events(approval)

    cdo_iface, chan, channel_properties, observe_events = announce_common(q,
            bus, empathy, kopete, account, conn, cd_props, True)

    # Both Observers indicate that they are ready to proceed
    for e in observe_events:
        q.dbus_return(e.message, signature='')

    # Kopete's BypassApproval part is asked to handle the channels
    e = q.expect('dbus-method-call',
            path=bypass.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)
    # Kopete accepts the channels
    q.dbus_return(e.message, signature='')

    q.unforbid_events(approval)

    # Regression test for fd.o #22670

    closure = [
            EventPattern('dbus-method-call', method='Close'),
            ]

    q.forbid_events(closure)

    bypass.release_name()
    sync_dbus(bus, q, mc)

    q.unforbid_events(closure)

    # Bring back that handler
    del bypass
    bypass = SimulatedClient(q, bus, 'Kopete.BypassApproval',
            observe=[], approve=[],
            handle=[urgent_fixed_properties], bypass_approval=True)
    expect_client_setup(q, [bypass])

    # Another channel that bypasses approval comes in, but the handler that
    # bypasses approval fails.

    cdo_iface, chan, channel_properties, observe_events = announce_common(q,
            bus, empathy, kopete, account, conn, cd_props, True)

    # Both Observers indicate that they are ready to proceed
    for e in observe_events:
        q.dbus_return(e.message, signature='')

    # Kopete's BypassApproval part is asked to handle the channels
    e = q.expect('dbus-method-call',
            path=bypass.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)
    # Kopete's BypassApproval part fails to accept the channels
    q.dbus_raise(e.message, 'com.example.Broken', 'No way')

    # MC recovers by running the approvers and doing what they say
    expect_and_exercise_approval(q, bus, chan, channel_properties,
            empathy, kopete, cdo_iface, cd_props)

if __name__ == '__main__':
    exec_test(test, {})

