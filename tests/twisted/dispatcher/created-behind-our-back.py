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

"""Regression test for channels created "by going behind MC's back"."""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, sync_dbus
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

def test(q, bus, mc):
    # Because the channels are handled by another process, we should never be
    # asked to approve or handle them.
    forbidden = [
            EventPattern('dbus-method-call', method='HandleChannels'),
            EventPattern('dbus-method-call', method='AddDispatchOperation'),
            ]
    q.forbid_events(forbidden)

    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    text_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    empathy = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)
    kopete = SimulatedClient(q, bus, 'Kopete',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    expect_client_setup(q, [empathy, kopete])

    # a non-MC-using client goes behind our back to call CreateChannel or
    # EnsureChannel on the Connection directly
    #
    # (This is not simulated here: we just behave as though it had happened)

    channel_immutable = dbus.Dictionary(text_fixed_properties)
    channel_immutable[cs.CHANNEL + '.InitiatorID'] = conn.self_ident
    channel_immutable[cs.CHANNEL + '.InitiatorHandle'] = conn.self_handle
    channel_immutable[cs.CHANNEL + '.Requested'] = True
    channel_immutable[cs.CHANNEL + '.Interfaces'] = \
        dbus.Array([], signature='s')
    channel_immutable[cs.CHANNEL + '.TargetHandle'] = \
        conn.ensure_handle(cs.HT_CONTACT, 'juliet')
    channel_immutable[cs.CHANNEL + '.TargetID'] = 'juliet'
    channel = SimulatedChannel(conn, channel_immutable)

    channel.announce()

    # Observer should get told, processing waits for it
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
    assert e.args[3] == '/', e.args         # no dispatch operation
    assert e.args[4] == [], e.args
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == channel.object_path, channels
    assert channels[0][1] == channel_immutable, channels

    assert e.args == k.args

    # Observers say "OK, go"
    q.dbus_return(k.message, signature='')
    q.dbus_return(e.message, signature='')

    sync_dbus(bus, q, mc)

if __name__ == '__main__':
    exec_test(test, {})
