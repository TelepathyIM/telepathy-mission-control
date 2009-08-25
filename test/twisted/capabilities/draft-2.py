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
draft 2 of ContactCapabilities.
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
            observe=[], approve=[],
            handle=[media_fixed_properties], bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [media_call])

    def check_draft_2_caps(e):
        structs = e.args[0]

        filters = {}
        tokens = {}

        assert len(structs) == 2

        for struct in structs:
            assert struct[0] not in filters
            filters[struct[0]] = sorted(struct[1])
            tokens[struct[0]] = sorted(struct[2])

        assert media_fixed_properties in filters[cs.CLIENT + '.MediaCall']
        assert len(filters[cs.CLIENT + '.MediaCall']) == 1

        assert abi_room_fixed_properties in filters[cs.CLIENT + '.AbiWord']
        assert abi_contact_fixed_properties in filters[cs.CLIENT + '.AbiWord']
        assert len(filters[cs.CLIENT + '.AbiWord']) == 2

        return True

    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params,
            extra_interfaces=[cs.CONN_IFACE_CONTACT_CAPS_DRAFT2,
                cs.CONN_IFACE_CAPS],
            expect_after_connect=[
                EventPattern('dbus-method-call', handled=False,
                    interface=cs.CONN_IFACE_CONTACT_CAPS_DRAFT2,
                    method='UpdateCapabilities',
                    predicate=check_draft_2_caps),
                ])

if __name__ == '__main__':
    exec_test(test, {})
