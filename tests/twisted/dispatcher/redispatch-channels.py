# Copyright (C) 2009-2011 Collabora Ltd.
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

"""Test CD.RedispatchChannels
"""

import dbus
import dbus.service

from servicetest import call_async
from mctest import exec_test, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

hints = dbus.Dictionary({ 'badger': 42, 'snake': 'pony' },
    signature='sv')

def test_redispatch_channel(q, bus, mc, account, chan, empathy, empathy_bus, gs):
    # Now gnome-shell wants to give the channel to another handle
    gs_cd = bus.get_object(cs.CD, cs.CD_PATH)
    gs_cd_redispatch = dbus.Interface(gs_cd, cs.CD_REDISPATCH)

    call_async(q, gs_cd_redispatch, 'RedispatchChannels',
        account.object_path, [chan.object_path], 0, "", hints)

    # Empathy is asked to handle the channel and accept
    e = q.expect('dbus-method-call',
            path=empathy.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_return(e.message, signature='')

    q.expect('dbus-return', method='RedispatchChannels')

    # Let's play ping-pong channel! Empathy give the channel back to GS
    emp_cd = empathy_bus.get_object(cs.CD, cs.CD_PATH)
    emp_cd_redispatch = dbus.Interface(emp_cd, cs.CD_REDISPATCH)

    call_async(q, emp_cd_redispatch, 'RedispatchChannels',
        account.object_path, [chan.object_path], 0, "", hints)

    # gnome-shell is asked to handle the channel and accept
    e = q.expect('dbus-method-call',
            path=gs.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_return(e.message, signature='')

    q.expect('dbus-return', method='RedispatchChannels')

    # gnome-shell wants to give it back, again
    call_async(q, gs_cd_redispatch, 'RedispatchChannels',
        account.object_path, [chan.object_path], 0, "", hints)

    # Empathy is asked to handle the channel but refuses
    e = q.expect('dbus-method-call',
            path=empathy.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_raise(e.message, cs.NOT_AVAILABLE, "No thanks")

    # RedispatchChannels failed so gnome-shell is still handling the channel
    q.expect('dbus-error', method='RedispatchChannels', name=cs.NOT_CAPABLE)

    # Empathy doesn't handle the channel atm but tries to redispatch it
    call_async(q, emp_cd_redispatch, 'RedispatchChannels',
        account.object_path, [chan.object_path], 0, "", hints)

    q.expect('dbus-error', method='RedispatchChannels', name=cs.NOT_YOURS)

    # Empathy crashes
    empathy.release_name()

    e = q.expect('dbus-signal',
            signal='NameOwnerChanged',
            predicate=(lambda e:
                e.args[0] == empathy.bus_name and e.args[2] == ''),
            )

    # gnome-shell wants to redispatch, but there is no other handler
    call_async(q, gs_cd_redispatch, 'RedispatchChannels',
        account.object_path, [chan.object_path], 0, "", hints)

    q.expect('dbus-error', method='RedispatchChannels', name=cs.NOT_CAPABLE)

    chan.close()

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    text_fixed_properties = dbus.Dictionary({
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    # Empathy Chat is a text handler
    empathy_bus = dbus.bus.BusConnection()
    empathy = SimulatedClient(q, empathy_bus, 'EmpathyChat',
            handle=[text_fixed_properties], bypass_approval=False)
    q.attach_to_bus(empathy_bus)

    # gnome-shell is a text approver and handler
    gs = SimulatedClient(q, bus, 'GnomeShell',
           approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [empathy, gs])

    cd = bus.get_object(cs.CD, cs.CD_PATH)

    # incoming text channel
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

    # gnome-shell's approver is notified
    e = q.expect('dbus-method-call', path=gs.object_path,
        interface=cs.APPROVER, method='AddDispatchOperation',
        handled=False)

    channels, cdo_path,props = e.args

    cdo = bus.get_object(cs.CD, cdo_path)
    cdo_iface = dbus.Interface(cdo, cs.CDO)

    q.dbus_return(e.message, signature='')

    # gnome-shell handles the channel itself first
    call_async(q, cdo_iface, 'HandleWith',
            cs.tp_name_prefix + '.Client.GnomeShell')

    e = q.expect('dbus-method-call',
            path=gs.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_return(e.message, signature='')

    # test redispatching an incoming channel
    test_redispatch_channel(q, bus, mc, account, chan, empathy, empathy_bus, gs)

    # Empathy is back
    empathy = SimulatedClient(q, empathy_bus, 'EmpathyChat',
            handle=[text_fixed_properties], bypass_approval=False)

    expect_client_setup(q, [empathy])

    # gnome-shell requests a channel for itself
    request = dbus.Dictionary({
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_ID: 'juliet',
            }, signature='sv')

    call_async(q, cd, 'CreateChannelWithHints',
            account.object_path, request, 0,
            cs.tp_name_prefix + '.Client.GnomeShell',
            hints, dbus_interface=cs.CD)
    e = q.expect('dbus-return', method='CreateChannelWithHints')

    cr = bus.get_object(cs.AM, e.value[0])
    cr.Proceed(dbus_interface=cs.CR)

    e = q.expect('dbus-method-call', interface=cs.CONN_IFACE_REQUESTS,
        method='CreateChannel',
        path=conn.object_path, args=[request], handled=False)

    # channel is created
    chan = SimulatedChannel(conn, request)

    q.dbus_return(e.message,
        chan.object_path, chan.immutable, signature='oa{sv}')
    chan.announce()

    # gnome-shell handles the channel
    e = q.expect('dbus-method-call',
            path=gs.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_return(e.message, signature='')

    # test redispatching an outgoing channel
    test_redispatch_channel(q, bus, mc, account, chan, empathy, empathy_bus, gs)

if __name__ == '__main__':
    exec_test(test, {})
