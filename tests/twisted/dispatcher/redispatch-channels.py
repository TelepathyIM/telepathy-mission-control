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

"""Test CD.DelegateChannels and Cd.PresentChannel
"""

import dbus
import dbus.service

from servicetest import call_async, assertEquals
from mctest import (
    exec_test, SimulatedClient,
    create_fakecm_account, enable_fakecm_account, SimulatedChannel,
    expect_client_setup,
    ChannelDispatcher, ChannelDispatchOperation, ChannelRequest)
import constants as cs

REQUEST = dbus.Dictionary({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_ID: 'juliet',
        }, signature='sv')

def test_ensure(q, bus, account, conn, chan, expected_handler_path):
    """Tests that a client Ensure-ing the channel causes HandleChannels to be
    called on the current handler. (Previously, DelegateChannels() and
    PresentChannel() both broke this.)"""
    cd = ChannelDispatcher(bus)
    call_async(q, cd, 'EnsureChannel', account.object_path, REQUEST, 0, '')
    e = q.expect('dbus-return', method='EnsureChannel')

    cr = ChannelRequest(bus, e.value[0])
    cr.Proceed()

    e = q.expect('dbus-method-call', interface=cs.CONN_IFACE_REQUESTS,
        method='EnsureChannel',
        path=conn.object_path, args=[REQUEST], handled=False)
    q.dbus_return(e.message, False,
        chan.object_path, chan.immutable, signature='boa{sv}')

    e = q.expect('dbus-method-call',
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)
    assertEquals(expected_handler_path, e.path)
    q.dbus_return(e.message, signature='')

def test_delegate_channel(q, bus, mc, account, conn, chan, empathy, empathy_bus, gs):
    # Test that re-Ensure-ing works before we start Delegating and Presenting.
    test_ensure(q, bus, account, conn, chan, gs.object_path)

    # Now gnome-shell wants to give the channel to another handler
    gs_cd = ChannelDispatcher(bus)
    call_async(q, gs_cd, 'DelegateChannels',
        [chan.object_path], 0, "")

    # Empathy is asked to handle the channel and accept
    e = q.expect('dbus-method-call',
            path=empathy.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_return(e.message, signature='')

    e = q.expect('dbus-return', method='DelegateChannels')
    assertEquals(([chan.object_path], {}), e.value)

    # Test that re-Ensure-ing the channel still works, and sends it to
    # the right place.
    test_ensure(q, bus, account, conn, chan, empathy.object_path)

    # Let's play ping-pong with the channel! Empathy gives the channel
    # back to GS
    emp_cd = ChannelDispatcher(empathy_bus)
    call_async(q, emp_cd, 'DelegateChannels',
        [chan.object_path], 0, "")

    # gnome-shell is asked to handle the channel and accept
    e = q.expect('dbus-method-call',
            path=gs.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_return(e.message, signature='')

    e = q.expect('dbus-return', method='DelegateChannels')
    assertEquals(([chan.object_path], {}), e.value)

    # Test that re-Ensure-ing the channel sttill works, and sends it
    # to the right place.
    test_ensure(q, bus, account, conn, chan, gs.object_path)

    # gnome-shell wants to give it back, again
    call_async(q, gs_cd, 'DelegateChannels',
        [chan.object_path], 0, "")

    # Empathy is asked to handle the channel but refuses
    e = q.expect('dbus-method-call',
            path=empathy.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_raise(e.message, cs.NOT_AVAILABLE, "No thanks")

    # DelegateChannels failed so gnome-shell is still handling the channel
    e = q.expect('dbus-return', method='DelegateChannels')
    assertEquals(([], {chan.object_path: (cs.NOT_AVAILABLE, 'No thanks')}), e.value)

    # Test that re-Ensure-ing the channel sttill works, and sends it
    # to the right place.
    test_ensure(q, bus, account, conn, chan, gs.object_path)

    # Empathy doesn't handle the channel atm but tries to delegates it
    call_async(q, emp_cd, 'DelegateChannels',
        [chan.object_path], 0, "")

    q.expect('dbus-error', method='DelegateChannels', name=cs.NOT_YOURS)

    # gnome-shell which is handling the channel asks to re-ensure it
    call_async(q, gs_cd, 'PresentChannel',
        chan.object_path, 0)

    # gnome-shell is asked to re-handle the channel
    e = q.expect('dbus-method-call',
            path=gs.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_return(e.message, signature='')

    q.expect('dbus-return', method='PresentChannel')

    # empathy which is not handling the channel asks to re-ensure it
    call_async(q, emp_cd, 'PresentChannel',
        chan.object_path, 0)

    # gnome-shell is asked to re-handle the channel
    e = q.expect('dbus-method-call',
            path=gs.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_return(e.message, signature='')

    q.expect('dbus-return', method='PresentChannel')

    # Test that re-Ensure-ing the channel *still* works, and sends it
    # to the right place.
    test_ensure(q, bus, account, conn, chan, gs.object_path)

    # Empathy crashes
    empathy.release_name()

    e = q.expect('dbus-signal',
            signal='NameOwnerChanged',
            predicate=(lambda e:
                e.args[0] == empathy.bus_name and e.args[2] == ''),
            )

    # gnome-shell wants to delegate, but there is no other handler
    call_async(q, gs_cd, 'DelegateChannels',
        [chan.object_path], 0, "")

    e = q.expect('dbus-return', method='DelegateChannels')
    delegated, not_delegated = e.value
    assertEquals([], delegated)
    error = not_delegated[chan.object_path]
    assertEquals (error[0], cs.NOT_CAPABLE)

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

    cd = ChannelDispatcher(bus)

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

    cdo = ChannelDispatchOperation(bus, cdo_path)

    q.dbus_return(e.message, signature='')

    # gnome-shell handles the channel itself first
    call_async(q, cdo, 'HandleWith',
            cs.tp_name_prefix + '.Client.GnomeShell')

    e = q.expect('dbus-method-call',
            path=gs.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_return(e.message, signature='')

    # test delegating an incoming channel
    test_delegate_channel(q, bus, mc, account, conn, chan, empathy, empathy_bus, gs)

    # Empathy is back
    empathy = SimulatedClient(q, empathy_bus, 'EmpathyChat',
            handle=[text_fixed_properties], bypass_approval=False)

    expect_client_setup(q, [empathy])

    # gnome-shell requests a channel for itself
    call_async(q, cd, 'CreateChannelWithHints',
            account.object_path, REQUEST, 0,
            cs.tp_name_prefix + '.Client.GnomeShell',
            {})
    e = q.expect('dbus-return', method='CreateChannelWithHints')

    cr = ChannelRequest(bus, e.value[0])
    cr.Proceed()

    e = q.expect('dbus-method-call', interface=cs.CONN_IFACE_REQUESTS,
        method='CreateChannel',
        path=conn.object_path, args=[REQUEST], handled=False)

    # channel is created
    chan = SimulatedChannel(conn, REQUEST)

    q.dbus_return(e.message,
        chan.object_path, chan.immutable, signature='oa{sv}')
    chan.announce()

    # gnome-shell handles the channel
    e = q.expect('dbus-method-call',
            path=gs.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_return(e.message, signature='')

    # test delegating an outgoing channel
    test_delegate_channel(q, bus, mc, account, conn, chan, empathy, empathy_bus, gs)

if __name__ == '__main__':
    exec_test(test, {})
