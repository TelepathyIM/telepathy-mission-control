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

"""Regression test for pushing clients' capabilities into a CM with
ContactCapabilities (final version, which is the same as draft 2).
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

def test(q, bus, mc):
    forbidden = [
            EventPattern('dbus-method-call', handled=False,
                interface=cs.CONN_IFACE_CAPS,
                method='AdvertiseCapabilities'),
            ]
    q.forbid_events(forbidden)

    # Two clients want to handle channels: MediaCall is running, and AbiWord
    # is activatable.

    # this must match the .client file
    abi_contact_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_STREAM_TUBE,
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL_TYPE_STREAM_TUBE + '.Service': 'x-abiword',
        }, signature='sv')
    abi_room_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_STREAM_TUBE,
        cs.CHANNEL + '.TargetHandleType': cs.HT_ROOM,
        cs.CHANNEL_TYPE_STREAM_TUBE + '.Service': 'x-abiword',
        }, signature='sv')

    media_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_STREAMED_MEDIA,
        }, signature='sv')
    media_call = SimulatedClient(q, bus, 'MediaCall',
            observe=[], approve=[], handle=[media_fixed_properties],
            cap_tokens=[cs.CHANNEL_IFACE_MEDIA_SIGNALLING + '/ice-udp',
                cs.CHANNEL_IFACE_MEDIA_SIGNALLING + '/audio/speex',
                cs.CHANNEL_IFACE_MEDIA_SIGNALLING + '/video/theora'],
            bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [media_call])

    def check_contact_caps(e):
        structs = e.args[0]

        filters = {}
        tokens = {}

        assert len(structs) == 3

        for struct in structs:
            assert struct[0] not in filters
            filters[struct[0]] = sorted(struct[1])
            tokens[struct[0]] = sorted(struct[2])

        assert media_fixed_properties in filters[cs.CLIENT + '.MediaCall']
        assert len(filters[cs.CLIENT + '.MediaCall']) == 1

        assert abi_room_fixed_properties in filters[cs.CLIENT + '.AbiWord']
        assert abi_contact_fixed_properties in filters[cs.CLIENT + '.AbiWord']
        assert len(filters[cs.CLIENT + '.AbiWord']) == 2

        assert len(tokens[cs.CLIENT + '.MediaCall']) == 3
        assert cs.CHANNEL_IFACE_MEDIA_SIGNALLING + '/ice-udp' in \
                tokens[cs.CLIENT + '.MediaCall']
        assert cs.CHANNEL_IFACE_MEDIA_SIGNALLING + '/audio/speex' in \
                tokens[cs.CLIENT + '.MediaCall']
        assert cs.CHANNEL_IFACE_MEDIA_SIGNALLING + '/video/theora' in \
                tokens[cs.CLIENT + '.MediaCall']

        assert len(tokens[cs.CLIENT + '.AbiWord']) == 2
        assert 'com.example.Foo' in tokens[cs.CLIENT + '.AbiWord']
        assert 'com.example.Bar' in tokens[cs.CLIENT + '.AbiWord']

        return True

    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn, before = enable_fakecm_account(q, bus, mc, account, params,
            extra_interfaces=[cs.CONN_IFACE_CONTACT_CAPS,
                cs.CONN_IFACE_CAPS],
            expect_before_connect=[
                EventPattern('dbus-method-call', handled=False,
                    interface=cs.CONN_IFACE_CONTACT_CAPS,
                    method='UpdateCapabilities',
                    predicate=check_contact_caps),
                ])
    q.dbus_return(before.message, signature='')

    irssi_bus = dbus.bus.BusConnection()
    irssi_bus.set_exit_on_disconnect(False)   # we'll disconnect later
    q.attach_to_bus(irssi_bus)
    irssi_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        cs.CHANNEL + '.TargetHandleType': cs.HT_ROOM,
        }, signature='sv')
    irssi = SimulatedClient(q, irssi_bus, 'Irssi',
            observe=[], approve=[], handle=[irssi_fixed_properties],
            cap_tokens=[],
            bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [irssi])

    e = q.expect('dbus-method-call', handled=False,
        interface=cs.CONN_IFACE_CONTACT_CAPS,
        method='UpdateCapabilities')

    assert len(e.args[0]) == 1
    struct = e.args[0][0]
    assert struct[0] == cs.CLIENT + '.Irssi'
    assert struct[1] == [irssi_fixed_properties]
    assert struct[2] == []

    # When Irssi exits, the CM is told it has gone
    irssi.release_name()
    del irssi
    irssi_bus.flush()
    irssi_bus.close()

    e = q.expect('dbus-method-call', handled=False,
        interface=cs.CONN_IFACE_CONTACT_CAPS,
        method='UpdateCapabilities')

    assert len(e.args[0]) == 1
    struct = e.args[0][0]
    assert struct[0] == cs.CLIENT + '.Irssi'
    assert struct[1] == []
    assert struct[2] == []

if __name__ == '__main__':
    exec_test(test, {})
