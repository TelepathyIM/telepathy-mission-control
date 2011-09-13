# Python is really rubbish. vim: set fileencoding=utf-8 :
# Copyright © 2009–2010 Nokia Corporation
# Copyright © 2009–2010 Collabora Ltd.
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
    EventPattern, tp_name_prefix, tp_path_prefix, assertEquals,
)
from mctest import  (
    exec_test, SimulatedConnection, create_fakecm_account,
    SimulatedChannel, SimulatedClient, expect_client_setup,
)
import constants as cs

def test(q, bus, mc):
    cm_name_ref = dbus.service.BusName(
            tp_name_prefix + '.ConnectionManager.fakecm', bus=bus)

    http_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': 1L,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_STREAM_TUBE,
        cs.CHANNEL_TYPE_STREAM_TUBE + '.Service':
            'http'
        }, signature='sv')
    caps = dbus.Array([http_fixed_properties], signature='a{sv}')

    # Be a Client
    client = SimulatedClient(q, bus, 'downloader',
            observe=[], approve=[], handle=[http_fixed_properties],
            bypass_approval=False)
    expect_client_setup(q, [client])

    # Create an account
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    # The account is initially valid but disabled, and hence offline
    props = account.GetAll(cs.ACCOUNT, dbus_interface=cs.PROPERTIES_IFACE)
    assert not props['Enabled']
    assert props['Valid']
    # The spec says it should be (Offline, "", "") but I don't think the
    # strings really matter. If anything, the second one should start out at
    # "offline".
    assertEquals(cs.PRESENCE_TYPE_OFFLINE, props['CurrentPresence'][0])

    # Enable the account
    account.Set(cs.ACCOUNT, 'Enabled', True,
            dbus_interface=cs.PROPERTIES_IFACE)
    q.expect('dbus-signal',
            path=account.object_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT)

    props = account.GetAll(cs.ACCOUNT, dbus_interface=cs.PROPERTIES_IFACE)
    assert props['Enabled']
    assert props['Valid']
    # Ditto above re. string fields.
    assertEquals(cs.PRESENCE_TYPE_OFFLINE, props['CurrentPresence'][0])

    # Go online
    requested_presence = dbus.Struct((dbus.UInt32(2L), dbus.String(u'brb'),
                dbus.String(u'Be back soon!')))
    account.Set(cs.ACCOUNT,
            'RequestedPresence', requested_presence,
            dbus_interface=cs.PROPERTIES_IFACE)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=tp_name_prefix + '.ConnectionManager.fakecm',
            path=tp_path_prefix + '/ConnectionManager/fakecm',
            interface=tp_name_prefix + '.ConnectionManager',
            handled=False)

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', '_',
            'myself')

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    # MC does some setup, including fetching the list of Channels

    q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CONN_IFACE_REQUESTS],
                path=conn.object_path, handled=True),
            )
    # MC calls GetStatus (maybe) and then Connect

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)

    # Connect succeeds
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    # Assert that the NormalizedName is harvested from the Connection at some
    # point
    while 1:
        e = q.expect('dbus-signal',
                interface=cs.ACCOUNT, signal='AccountPropertyChanged',
                path=account.object_path)
        if 'NormalizedName' in e.args[0]:
            assert e.args[0]['NormalizedName'] == 'myself', e.args
            break

    # Check the requested presence is online
    properties = account.GetAll(cs.ACCOUNT,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert properties is not None
    assert properties.get('HasBeenOnline')
    assertEquals(requested_presence, properties.get('RequestedPresence'))

    # Since this Connection doesn't support SimplePresence, but it's online,
    # the spec says that CurrentPresence should be Unset.
    assertEquals((cs.PRESENCE_TYPE_UNSET, "", ""),
        properties.get('CurrentPresence'))

    new_channel = http_fixed_properties
    buddy_handle = conn.ensure_handle(cs.HT_CONTACT, "buddy")
    new_channel[cs.CHANNEL + '.TargetID'] = "buddy"
    new_channel[cs.CHANNEL + '.TargetHandle'] = buddy_handle
    new_channel[cs.CHANNEL + '.Requested'] = False
    new_channel[cs.CHANNEL + '.Interfaces'] = dbus.Array(signature='s')

    chan = SimulatedChannel(conn, new_channel)
    chan.announce()

    e = q.expect('dbus-method-call', method='HandleChannels')
    q.dbus_return(e.message, signature='')

    # Put the account offline
    requested_presence = (dbus.UInt32(cs.PRESENCE_TYPE_OFFLINE), 'offline', '')
    account.Set(cs.ACCOUNT,
            'RequestedPresence', requested_presence,
            dbus_interface=cs.PROPERTIES_IFACE)

    # In response, MC tells us to Disconnect, and we do. But it should not
    # Close() the open channel.
    q.forbid_events([
        EventPattern('dbus-method-call', method='Close', path=conn.object_path),
        ])
    q.expect('dbus-method-call', method='Disconnect',
            path=conn.object_path, handled=True)

    properties = account.GetAll(cs.ACCOUNT, dbus_interface=cs.PROPERTIES_IFACE)
    assertEquals('/', properties['Connection'])
    assertEquals(cs.CONN_STATUS_DISCONNECTED, properties['ConnectionStatus'])
    assertEquals(requested_presence, properties['CurrentPresence'])
    assertEquals(requested_presence, properties['RequestedPresence'])

if __name__ == '__main__':
    exec_test(test, {})
