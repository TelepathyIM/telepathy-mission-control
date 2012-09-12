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
"""Regression test for activating MC due to, or immediately before, a request.
"""

import os

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup, MC
import constants as cs

account_id = 'fakecm/fakeprotocol/jc_2edenton_40unatco_2eint'

def preseed(q, bus, fake_accounts_service):
    accounts_dir = os.environ['MC_ACCOUNT_DIR']

    try:
        os.mkdir(accounts_dir, 0700)
    except OSError:
        pass

    fake_accounts_service.update_attributes(account_id, changed={
        'manager': 'fakecm',
        'protocol': 'fakeprotocol',
        'DisplayName': 'Work account',
        'NormalizedName': 'jc.denton@unatco.int',
        'Enabled': True,
        'ConnectAutomatically': False,
        'AutomaticPresence': (dbus.UInt32(2), 'available', ''),
        })
    fake_accounts_service.update_parameters(account_id, untyped={
        'account': 'jc.denton@unatco.int',
        'password': 'ionstorm',
        })

    account_connections_file = open(accounts_dir + '/.mc_connections', 'w')
    account_connections_file.write("")
    account_connections_file.close()

def test(q, bus, unused, **kwargs):
    fake_accounts_service = kwargs['fake_accounts_service']
    preseed(q, bus, fake_accounts_service)

    text_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    user_action_time = dbus.Int64(1238582606)

    # A client and a CM are already running
    client = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False,
            implement_get_interfaces=False)
    cm_name_ref = dbus.service.BusName(
            cs.tp_name_prefix + '.ConnectionManager.fakecm', bus=bus)

    # service-activate MC; it will try to introspect the running client.
    mc = MC(q, bus, wait_for_names=False)
    get_interfaces, = mc.wait_for_names(
        EventPattern('dbus-method-call', path=client.object_path,
            interface=cs.PROPERTIES_IFACE, method='Get',
            args=[cs.CLIENT, 'Interfaces'],
            handled=False))

    # The client doesn't reply just yet; meanwhile, immediately make a channel
    # request
    account = bus.get_object(cs.MC,
            cs.tp_path_prefix +
            '/Account/fakecm/fakeprotocol/jc_2edenton_40unatco_2eint')
    cd = bus.get_object(cs.MC, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)

    request = dbus.Dictionary({
            cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
            cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
            cs.CHANNEL + '.TargetID': 'bob.page@versalife.com',
            }, signature='sv')
    call_async(q, cd, 'CreateChannel',
            account.object_path, request, user_action_time, client.bus_name,
            dbus_interface=cs.CD)

    ret = q.expect('dbus-return', method='CreateChannel')
    request_path = ret.value[0]

    # chat UI connects to signals and calls ChannelRequest.Proceed()
    cr = bus.get_object(cs.MC, request_path)
    request_props = cr.GetAll(cs.CR, dbus_interface=cs.PROPERTIES_IFACE)
    assert request_props['Account'] == account.object_path
    assert request_props['Requests'] == [request]
    assert request_props['UserActionTime'] == user_action_time
    assert request_props['PreferredHandler'] == client.bus_name
    assert request_props['Interfaces'] == []

    call_async(q, cr, 'Proceed', dbus_interface=cs.CR)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', {
                'account': 'jc.denton@unatco.int',
                'password': 'ionstorm',
                }],
            destination=cs.tp_name_prefix + '.ConnectionManager.fakecm',
            path=cs.tp_path_prefix + '/ConnectionManager/fakecm',
            interface=cs.tp_name_prefix + '.ConnectionManager',
            handled=False)
    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', 'the_conn',
            'jc.denton@unatco.int')
    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    # A channel appears spontaneously

    announcement_immutable = dbus.Dictionary(text_fixed_properties)
    announcement_immutable[cs.CHANNEL + '.TargetID'] = 'gunther@unatco.int'
    announcement_immutable[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'gunther@unatco.int')
    announcement_immutable[cs.CHANNEL + '.InitiatorHandle'] = \
        announcement_immutable[cs.CHANNEL + '.TargetHandle']
    announcement_immutable[cs.CHANNEL + '.InitiatorID'] = \
        announcement_immutable[cs.CHANNEL + '.TargetID']
    announcement_immutable[cs.CHANNEL + '.Interfaces'] = \
            dbus.Array([], signature='s')
    announcement_immutable[cs.CHANNEL + '.Requested'] = False
    announcement = SimulatedChannel(conn, announcement_immutable)
    announcement.announce()

    # Now the Client returns its info
    q.dbus_return(get_interfaces.message,
            dbus.Array([cs.HANDLER, cs.OBSERVER, cs.APPROVER,
                cs.CLIENT_IFACE_REQUESTS], signature='s'), signature='v')

    expect_client_setup(q, [client], got_interfaces_already=True)

    # Now that the dispatcher is ready to go, we start looking for channels,
    # and also make the actual request
    # Empathy observes the channel we originally requested.
    _, a, cm_request_call = q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CONN_IFACE_REQUESTS],
                path=conn.object_path, handled=True),
            EventPattern('dbus-method-call',
                path=client.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False),
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_REQUESTS, method='CreateChannel',
                path=conn.object_path, args=[request], handled=False),
            )

    assert a.args[0] == account.object_path, a.args
    assert a.args[1] == conn.object_path, a.args
    assert a.args[3] != '/', a.args         # there is a dispatch operation
    assert a.args[4] == [], a.args
    channels = a.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == announcement.object_path, channels
    assert channels[0][1] == announcement_immutable, channels

    # Time passes. A channel is returned.

    channel_immutable = dbus.Dictionary(request)
    channel_immutable[cs.CHANNEL + '.InitiatorID'] = conn.self_ident
    channel_immutable[cs.CHANNEL + '.InitiatorHandle'] = conn.self_handle
    channel_immutable[cs.CHANNEL + '.Requested'] = True
    channel_immutable[cs.CHANNEL + '.Interfaces'] = \
        dbus.Array([], signature='s')
    channel_immutable[cs.CHANNEL + '.TargetHandle'] = \
        conn.ensure_handle(cs.HT_CONTACT, 'bob.page@versalife.com')
    channel = SimulatedChannel(conn, channel_immutable)

    q.dbus_return(cm_request_call.message,
            channel.object_path, channel.immutable, signature='oa{sv}')
    channel.announce()

    # Empathy observes the newly-created channel.
    e = q.expect('dbus-method-call',
            path=client.object_path,
            interface=cs.OBSERVER, method='ObserveChannels',
            handled=False)

    assert e.args[0] == account.object_path, e.args
    assert e.args[1] == conn.object_path, e.args
    assert e.args[3] == '/', e.args         # no dispatch operation
    assert e.args[4] == [request_path], e.args
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == channel.object_path, channels
    assert channels[0][1] == channel_immutable, channels

    # Observer says "OK, go"
    q.dbus_return(a.message, signature='')
    q.dbus_return(e.message, signature='')

    # Empathy is asked to handle the channel
    e = q.expect('dbus-method-call',
            path=client.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)
    assert e.args[0] == account.object_path, e.args
    assert e.args[1] == conn.object_path, e.args
    channels = e.args[2]
    assert len(channels) == 1, channels
    assert channels[0][0] == channel.object_path, channels
    assert channels[0][1] == channel_immutable, channels
    assert e.args[3] == [request_path], e.args
    assert e.args[4] == user_action_time
    assert isinstance(e.args[5], dict)
    assert len(e.args) == 6

if __name__ == '__main__':
    exec_test(test, {}, preload_mc=False, use_fake_accounts_service=True,
            pass_kwargs=True)
