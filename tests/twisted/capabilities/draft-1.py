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
draft 1 of ContactCapabilities.
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

    had = []

    def check_legacy_caps(e):
        # Because MC has no idea how to map Client capabilities into legacy
        # capabilities, it assumes that every client has all the flags in
        # the world. In this example we have (only) a StreamedMedia client
        # and a stream-tube client, so that's what MC will tell us.
        add = e.args[0]
        remove = e.args[1]

        assert (cs.CHANNEL_TYPE_STREAMED_MEDIA, 2L**32-1) in add
        assert (cs.CHANNEL_TYPE_STREAM_TUBE, 2L**32-1) in add

        # MC puts StreamTube in the list twice - arguably a bug, but
        # CMs should cope. So, don't assert about the length of the list
        for item in add:
            assert item in (
                    (cs.CHANNEL_TYPE_STREAMED_MEDIA, 2L**32-1),
                    (cs.CHANNEL_TYPE_STREAM_TUBE, 2L**32-1),
                    )

        assert len(remove) == 0

        had.append('legacy')
        return True

    def check_draft_1_caps(e):
        aasv = e.args[0]

        assert media_fixed_properties in aasv
        assert abi_room_fixed_properties in aasv
        assert abi_contact_fixed_properties in aasv
        assert len(aasv) == 3

        had.append('draft-1')
        return True

    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params,
            extra_interfaces=[cs.CONN_IFACE_CONTACT_CAPS_DRAFT1,
                cs.CONN_IFACE_CAPS],
            expect_after_connect=[
                EventPattern('dbus-method-call', handled=False,
                    interface=cs.CONN_IFACE_CONTACT_CAPS_DRAFT1,
                    method='SetSelfCapabilities',
                    predicate=check_draft_1_caps),
                EventPattern('dbus-method-call', handled=False,
                    interface=cs.CONN_IFACE_CAPS,
                    method='AdvertiseCapabilities',
                    predicate=check_legacy_caps),
                ])

    # MC currently calls AdvertiseCapabilities first, and changing this
    # risks causing regressions (the test case in telepathy-gabble assumes
    # this order).
    assert had == ['legacy', 'draft-1']

if __name__ == '__main__':
    exec_test(test, {})
