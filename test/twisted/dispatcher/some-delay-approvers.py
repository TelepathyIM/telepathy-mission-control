# Copyright (C) 2010 Collabora Ltd.
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

    # Empathy is an observer for text channels with
    # DelayApprovers=TRUE.
    empathy = SimulatedClient(q, bus, 'Empathy',
        observe=[text_fixed_properties], approve=[],
        handle=[], delay_approvers=True)

    # Loggy is an observer for text channels with
    # DelayApprovers=FALSE.
    loggy = SimulatedClient(q, bus, 'Loggy',
        observe=[text_fixed_properties], approve=[],
        handle=[], delay_approvers=False)

    # Kopete is an approver and handler for text channels.
    kopete = SimulatedClient(q, bus, 'Kopete',
        observe=[], approve=[text_fixed_properties],
        handle=[text_fixed_properties])

    expect_client_setup(q, [empathy, loggy, kopete])

    # subscribe to the OperationList interface (MC assumes that until this
    # property has been retrieved once, nobody cares)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

    # A text channel appears!
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

    e = q.expect('dbus-signal',
            path=cs.CD_PATH,
            interface=cs.CD_IFACE_OP_LIST,
            signal='NewDispatchOperation')

    cdo_path = e.args[0]
    cdo = bus.get_object(cs.CD, cdo_path)
    cdo_iface = dbus.Interface(cdo, cs.CDO)
    cdo_props_iface = dbus.Interface(cdo, cs.PROPERTIES_IFACE)

    # Empathy, the observer, gets the channel to observe. Because it
    # has DelayApprovers=TRUE, Kopete should not have
    # AddDispatchOperation called on it until Empathy returns from
    # ObserveChannels. Because Loggy has DelayApprovers=False,
    # however, ADO can be called on Kopete before Loggy returns, but
    # again, only after Empathy returns.
    forbidden = [EventPattern('dbus-method-call',
            path=kopete.object_path,
            interface=cs.APPROVER, method='AddDispatchOperation')]
    q.forbid_events(forbidden)

    e, l = q.expect_many(EventPattern('dbus-method-call',
                 path=empathy.object_path,
                 interface=cs.OBSERVER, method='ObserveChannels',
                 handled=False),
             EventPattern('dbus-method-call',
                 path=loggy.object_path,
                 interface=cs.OBSERVER, method='ObserveChannels',
                 handled=False),
             )

    # Waste a little time here and there.  We can't call sync_dbus
    # here because it calls Ping and libdbus returns from Ping
    # synchronously and doesn't turn the main loop handle enough.
    call_async(q, cd_props, 'Get', cs.CD_IFACE_OP_LIST, 'DispatchOperations')
    event = q.expect('dbus-return', method='Get')

    # Finally return from ObserveChannels from Empathy, so now we
    # expect ADO to be called on Kopete.
    q.dbus_return(e.message, bus=bus, signature='')
    q.unforbid_events(forbidden)

    e = q.expect('dbus-method-call',
             path=kopete.object_path,
             interface=cs.APPROVER, method='AddDispatchOperation',
             handled=False)

    q.dbus_return(e.message, bus=bus, signature='')

    # Return from loggy's ObserveChannels.
    q.dbus_return(l.message, bus=bus, signature='')

    # The user responds to Kopete
    call_async(q, cdo_iface, 'HandleWith',
            cs.tp_name_prefix + '.Client.Kopete')

    # Kopete is asked to handle the channels
    k = q.expect('dbus-method-call',
            path=kopete.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    # Kopete accepts the channels
    q.dbus_return(k.message, bus=bus, signature='')

    q.expect_many(
            EventPattern('dbus-return', method='HandleWith'),
            EventPattern('dbus-signal', interface=cs.CDO, signal='Finished'),
            EventPattern('dbus-signal', interface=cs.CD_IFACE_OP_LIST,
                signal='DispatchOperationFinished'),
            )

    # Now there are no more active channel dispatch operations
    assert cd_props.Get(cs.CD_IFACE_OP_LIST, 'DispatchOperations') == []

if __name__ == '__main__':
    exec_test(test, {})
