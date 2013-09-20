# vim: set fileencoding=utf-8 :
# Copyright © 2011 Collabora Ltd.
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
import dbus.service

from servicetest import (
    EventPattern, call_async, sync_dbus, assertEquals,
)
from mctest import (
    exec_test, create_fakecm_account, expect_fakecm_connection,
    SimulatedConnection,
)
import constants as cs

import config

def sync_connectivity_state(mc):
    # We cannot simply use sync_dbus here, because nm-glib reports property
    # changes in an idle (presumably to batch them all together). This is fine
    # and all that, but means we have to find a way to make sure MC has flushed
    # its idle queue to avoid this test being racy. (This isn't just
    # theoretical: this test failed about once per five runs when it used sync_dbus.)
    #
    # The test-specific version of MC implements the 'BillyIdle' method, which
    # returns from a low-priority idle.
    mc.BillyIdle(dbus_interface='org.freedesktop.Telepathy.MissionControl5.RegressionTests')

def test(q, bus, mc):
    params = dbus.Dictionary(
        {"account": "yum yum network manager",
         "password": "boo boo connman (although your API *is* simpler)",
        }, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    # While we're not connected to the internet, RequestConnection should not
    # be called.
    request_connection_event = [
        EventPattern('dbus-method-call', method='RequestConnection'),
    ]
    q.forbid_events(request_connection_event)

    account.Properties.Set(cs.ACCOUNT, 'RequestedPresence',
        (dbus.UInt32(cs.PRESENCE_TYPE_BUSY), 'busy', 'hlaghalgh'))

    # Turn the account on, re-request an online presence, and even tell it to
    # connect automatically, to check that none of these make it sign in.
    call_async(q, account.Properties, 'Set', cs.ACCOUNT, 'Enabled', True)
    q.expect('dbus-return', method='Set')
    requested_presence = (dbus.UInt32(cs.PRESENCE_TYPE_BUSY), 'busy', 'gtfo')
    call_async(q, account.Properties, 'Set', cs.ACCOUNT, 'RequestedPresence',
        requested_presence)
    q.expect('dbus-return', method='Set')
    call_async(q, account.Properties, 'Set', cs.ACCOUNT, 'ConnectAutomatically',
        True)
    q.expect('dbus-return', method='Set')
    # (but actually let's turn ConnectAutomatically off: we want to check that
    # MC continues to try to apply RequestedPresence if the network connection
    # goes away and comes back again, regardless of this setting)
    call_async(q, account.Properties, 'Set', cs.ACCOUNT, 'ConnectAutomatically',
        False)
    q.expect('dbus-return', method='Set')

    sync_dbus(bus, q, mc)
    q.unforbid_events(request_connection_event)

    # Okay, I'm satisfied. Turn the network on.
    mc.connectivity.go_online()

    expect_fakecm_connection(q, bus, mc, account, params, has_presence=True,
        expect_before_connect=[
            EventPattern('dbus-method-call', method='SetPresence',
                args=list(requested_presence[1:])),
        ])

    if config.HAVE_NM:
        # If NetworkManager tells us that it is going to disconnect soon,
        # the connection should be banished. GNetworkMonitor can't tell us
        # that; either it's online or it isn't.
        mc.connectivity.go_indeterminate()
        q.expect('dbus-method-call', method='Disconnect')

        mc.connectivity.go_offline()
        sync_connectivity_state(mc)

        # When we turn the network back on, MC should try to sign us back on.
        # In the process, our RequestedPresence should not have been
        # trampled on, as below.
        mc.connectivity.go_online()
        expect_fakecm_connection(q, bus, mc, account, params,
            has_presence=True,
            expect_before_connect=[
                EventPattern('dbus-method-call', method='SetPresence',
                    args=list(requested_presence[1:])),
            ])

        assertEquals(requested_presence,
            account.Properties.Get(cs.ACCOUNT, 'RequestedPresence'))

    # If we turn the network off, the connection should be banished.
    mc.connectivity.go_offline()
    q.expect('dbus-method-call', method='Disconnect')

    # When we turn the network back on, MC should try to sign us back on.
    mc.connectivity.go_online()
    e = q.expect('dbus-method-call', method='RequestConnection')

    # In the process, our RequestedPresence should not have been trampled on.
    # (Historically, MC would replace it with the AutomaticPresence, but that
    # behaviour was unexpected: if the user explicitly set a status or message,
    # why should the network connection cutting out and coming back up cause
    # that to be lost?)
    assertEquals(requested_presence,
        account.Properties.Get(cs.ACCOUNT, 'RequestedPresence'))

    # But if we get disconnected before RequestConnection returns, MC should
    # clean up the new connection when it does, rather than trying to sign it
    # in.
    connect_event = [ EventPattern('dbus-method-call', method='Connect'), ]
    q.forbid_events(connect_event)

    mc.connectivity.go_offline()
    # Make sure that MC has noticed that the network connection has gone away.
    sync_connectivity_state(mc)

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol',
        account.object_path.split('/')[-1], 'myself')
    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    q.expect('dbus-method-call', method='Disconnect')

    # So now the user gives up and sets their RequestedPresence to offline.
    # Because this account does not ConnectAutomatically, if the network
    # connection comes back up the account should not be brought back online.
    q.forbid_events(request_connection_event)
    account.Properties.Set(cs.ACCOUNT, 'RequestedPresence',
        (dbus.UInt32(cs.PRESENCE_TYPE_OFFLINE), 'offline', ''))
    mc.connectivity.go_online()
    # Make sure MC has noticed that the network connection has come back.
    sync_connectivity_state(mc)

if __name__ == '__main__':
    exec_test(test, initially_online=False)
