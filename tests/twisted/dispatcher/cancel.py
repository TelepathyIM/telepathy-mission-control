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
"""Regression test for the unofficial Account.Interface.Requests API when
a channel can be created successfully.
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
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    text_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    client = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # No Approver should be invoked at any point during this test, because the
    # Channel was Requested
    def fail_on_approval(e):
        raise AssertionError('Approver should not be invoked')
    q.add_dbus_method_impl(fail_on_approval, path=client.object_path,
            interface=cs.APPROVER, method='AddDispatchOperation')

    # wait for MC to download the properties
    expect_client_setup(q, [client])

    test_channel_creation(q, bus, account, client, conn, False)
    test_channel_creation(q, bus, account, client, conn, True)

def test_channel_creation(q, bus, account, client, conn,
        cancel_before_proceed):

    user_action_time = dbus.Int64(1238582606)

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)

    # chat UI calls ChannelDispatcher.CreateChannel
    request = dbus.Dictionary({
            cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
            cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
            cs.CHANNEL + '.TargetID': 'juliet',
            }, signature='sv')
    call_async(q, cd, 'CreateChannel',
            account.object_path, request, user_action_time, client.bus_name,
            dbus_interface=cs.CD)
    ret = q.expect('dbus-return', method='CreateChannel')
    request_path = ret.value[0]

    cr = bus.get_object(cs.AM, request_path)
    request_props = cr.GetAll(cs.CR, dbus_interface=cs.PROPERTIES_IFACE)
    assert request_props['Account'] == account.object_path
    assert request_props['Requests'] == [request]
    assert request_props['UserActionTime'] == user_action_time

    if cancel_before_proceed:
        cr.Cancel(dbus_interface=cs.CR)

        stdsig = q.expect('dbus-signal', path=request_path,
                    interface=cs.CR, signal='Failed')
        assert stdsig.args[0] == cs.CANCELLED

        # the channel request has gone away
        try:
            cr.Proceed(dbus_interface=cs.CR)
        except dbus.DBusException, e:
            pass
        else:
            raise AssertionError('Wanted an error')

        return

    cr.Proceed(dbus_interface=cs.CR)

    cm_request_call, add_request_call = q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_REQUESTS, method='CreateChannel',
                path=conn.object_path, args=[request], handled=False),
            EventPattern('dbus-method-call', handled=False,
                interface=cs.CLIENT_IFACE_REQUESTS,
                method='AddRequest', path=client.object_path),
            )

    assert add_request_call.args[0] == request_path
    q.dbus_return(add_request_call.message, signature='')

    # Actually, never mind.
    cr.Cancel(dbus_interface=cs.CR)

    # Time passes. A channel is returned.

    channel_immutable = dbus.Dictionary(request)
    channel_immutable[cs.CHANNEL + '.InitiatorID'] = conn.self_ident
    channel_immutable[cs.CHANNEL + '.InitiatorHandle'] = conn.self_handle
    channel_immutable[cs.CHANNEL + '.Requested'] = True
    channel_immutable[cs.CHANNEL + '.Interfaces'] = \
        dbus.Array([], signature='s')
    channel_immutable[cs.CHANNEL + '.TargetHandle'] = \
        conn.ensure_handle(cs.HT_CONTACT, 'juliet')
    channel = SimulatedChannel(conn, channel_immutable)

    # this order of events is guaranteed by telepathy-spec (since 0.17.14)
    q.dbus_return(cm_request_call.message,
            channel.object_path, channel.immutable, signature='oa{sv}')
    channel.announce()

    # Channel is unwanted now, MC stabs it in the face
    stdsig, _ = q.expect_many(
            EventPattern('dbus-signal', path=request_path,
                interface=cs.CR, signal='Failed'),
            EventPattern('dbus-method-call', path=channel.object_path,
                interface=cs.CHANNEL, method='Close', handled=True),
            )

    assert stdsig.args[0] == cs.CANCELLED

if __name__ == '__main__':
    exec_test(test, {})
