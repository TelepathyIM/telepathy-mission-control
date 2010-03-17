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

"""Feature test ensuring that MC dispatches channels that are created before
the connection status is connected.
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, sync_dbus
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

CHANNEL_TYPE_SERVER_VERIFICATION = \
    'org.freedesktop.Telepathy.Channel.Type.ServerVerification.DRAFT'
CHANNEL_IFACE_VERIFICATION = \
    'org.freedesktop.Telepathy.Channel.Interface.Verification.DRAFT '
CHANNEL_IFACE_IDENT_EXCHANGE = \
    'org.freedesktop.Telepathy.Channel.Interface.IdentityExchange.DRAFT'

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someone@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    # Ensure that it's enabled but has offline RP and doesn't connect
    # automatically

    verification_filter = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': 0,
        cs.CHANNEL + '.ChannelType': CHANNEL_TYPE_SERVER_VERIFICATION,
        }, signature='sv')

    verifier_bus = dbus.bus.BusConnection()
    q.attach_to_bus(verifier_bus)
    verifier = SimulatedClient(q, verifier_bus, 'Verifier',
                               handle=[verification_filter])

    # wait for MC to download the properties
    expect_client_setup(q, [verifier])

    account_props.Set(cs.ACCOUNT, 'RequestedPresence',
                      (dbus.UInt32(cs.PRESENCE_TYPE_AVAILABLE), 'available', ''))

    account_props.Set(cs.ACCOUNT, 'Enabled', True)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=cs.tp_name_prefix + '.ConnectionManager.fakecm',
            path=cs.tp_path_prefix + '/ConnectionManager/fakecm',
            interface=cs.tp_name_prefix + '.ConnectionManager',
            handled=False)

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', '_',
            'myself', has_requests=True, has_presence=True)

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    e = q.expect('dbus-method-call', method='Connect',
            path=conn.object_path,
            interface=cs.CONN)

    channel_properties = dbus.Dictionary(verification_filter, signature='sv')
    channel_properties[cs.CHANNEL + '.TargetID'] = ''
    channel_properties[cs.CHANNEL + '.TargetHandle'] = 0
    channel_properties[cs.CHANNEL + '.InitiatorID'] = ''
    channel_properties[cs.CHANNEL + '.InitiatorHandle'] = 0
    channel_properties[cs.CHANNEL + '.Requested'] = False
    channel_properties[cs.CHANNEL + '.Interfaces'] = dbus.Array([
            CHANNEL_IFACE_IDENT_EXCHANGE,
            CHANNEL_IFACE_VERIFICATION,
            cs.CHANNEL], signature='s')

    chan = SimulatedChannel(conn, channel_properties)
    chan.announce()

    e = q.expect('dbus-method-call',
            path=verifier.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

if __name__ == '__main__':
    exec_test(test, {})
