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
"""Regression test for recovering from an MC crash.
"""

import os

import dbus
import dbus.service

from servicetest import EventPattern, call_async
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
        })
    fake_accounts_service.update_parameters(account_id, untyped={
        'account': 'jc.denton@unatco.int',
        'password': 'ionstorm',
        })

    account_connections_file = open(accounts_dir + '/.mc_connections', 'w')

    account_connections_file.write("%s\t%s\t%s\n" %
            (cs.tp_path_prefix + '/Connection/fakecm/fakeprotocol/jc',
                cs.tp_name_prefix + '.Connection.fakecm.fakeprotocol.jc',
                'fakecm/fakeprotocol/jc_2edenton_40unatco_2eint'))

def test(q, bus, unused, **kwargs):
    fake_accounts_service = kwargs['fake_accounts_service']
    preseed(q, bus, fake_accounts_service)

    text_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol',
            'jc', 'jc.denton@unatco.int')
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, 0)

    unhandled_properties = dbus.Dictionary(text_fixed_properties, signature='sv')
    unhandled_properties[cs.CHANNEL + '.Interfaces'] = dbus.Array(signature='s')
    unhandled_properties[cs.CHANNEL + '.TargetID'] = 'anna.navarre@unatco.int'
    unhandled_properties[cs.CHANNEL + '.TargetHandle'] = \
            dbus.UInt32(conn.ensure_handle(cs.HT_CONTACT, 'anna.navarre@unatco.int'))
    unhandled_properties[cs.CHANNEL + '.InitiatorHandle'] = dbus.UInt32(conn.self_handle)
    unhandled_properties[cs.CHANNEL + '.InitiatorID'] = conn.self_ident
    unhandled_properties[cs.CHANNEL + '.Requested'] = True
    unhandled_chan = SimulatedChannel(conn, unhandled_properties)
    unhandled_chan.announce()

    handled_properties = dbus.Dictionary(text_fixed_properties, signature='sv')
    handled_properties[cs.CHANNEL + '.Interfaces'] = dbus.Array(signature='s')
    handled_properties[cs.CHANNEL + '.TargetID'] = 'gunther.hermann@unatco.int'
    handled_properties[cs.CHANNEL + '.TargetHandle'] = \
            dbus.UInt32(conn.ensure_handle(cs.HT_CONTACT, 'gunther.hermann@unatco.int'))
    handled_properties[cs.CHANNEL + '.InitiatorHandle'] = dbus.UInt32(conn.self_handle)
    handled_properties[cs.CHANNEL + '.InitiatorID'] = conn.self_ident
    handled_properties[cs.CHANNEL + '.Requested'] = True
    handled_chan = SimulatedChannel(conn, handled_properties)
    handled_chan.announce()

    client = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)
    client.handled_channels.append(handled_chan.object_path)

    # Service-activate MC.
    # We're told about the other channel as an observer...
    mc = MC(q, bus, wait_for_names=False)
    e, = mc.wait_for_names(
            EventPattern('dbus-method-call',
                path=client.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False),
            )

    assert e.args[1] == conn.object_path, e.args
    channels = e.args[2]
    assert channels[0][0] == unhandled_chan.object_path, channels
    q.dbus_return(e.message, signature='')

    # ... and as a handler
    e = q.expect('dbus-method-call',
            path=client.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)
    assert e.args[1] == conn.object_path, e.args
    channels = e.args[2]
    assert channels[0][0] == unhandled_chan.object_path, channels
    q.dbus_return(e.message, signature='')

if __name__ == '__main__':
    exec_test(test, {}, preload_mc=False, use_fake_accounts_service=True,
            pass_kwargs=True)
