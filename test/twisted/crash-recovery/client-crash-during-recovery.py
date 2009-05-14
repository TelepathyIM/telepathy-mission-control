"""Regression test for a client crashing while recovering from an MC crash.
"""

import os

import dbus
import dbus.service

from servicetest import EventPattern, call_async, sync_dbus
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup, make_mc
import constants as cs

def preseed():
    accounts_dir = os.environ['MC_ACCOUNT_DIR']

    accounts_cfg = open(accounts_dir + '/accounts.cfg', 'w')

    accounts_cfg.write("""# Telepathy accounts
[fakecm/fakeprotocol/jc_2edenton_40unatco_2eint]
manager=fakecm
protocol=fakeprotocol
DisplayName=Work account
NormalizedName=jc.denton@unatco.int
param-account=jc.denton@unatco.int
param-password=ionstorm
Enabled=1
""")

    accounts_cfg.close()

    account_connections_file = open(accounts_dir + '/.mc_connections', 'w')

    account_connections_file.write("%s\t%s\t%s\n" %
            (cs.tp_path_prefix + '/Connection/fakecm/fakeprotocol/jc',
                cs.tp_name_prefix + '.Connection.fakecm.fakeprotocol.jc',
                'fakecm/fakeprotocol/jc_2edenton_40unatco_2eint'))

def test(q, bus, unused):
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

    bus_name = '.'.join([cs.tp_name_prefix, 'Client.CrashMe'])
    bus_name_ref = dbus.service.BusName(bus_name, bus)
    object_path = '/' + bus_name.replace('.', '/')

    # service-activate MC
    mc = make_mc(bus, q.append)

    e = q.expect('dbus-method-call',
            interface=cs.PROPERTIES_IFACE, method='Get',
            path=object_path,
            args=[cs.CLIENT, 'Interfaces'],
            handled=False)
    q.dbus_return(e.message, dbus.Array([cs.HANDLER], signature='s'),
            signature='v')

    e = q.expect('dbus-method-call',
            interface=cs.PROPERTIES_IFACE, method='GetAll',
            path=object_path,
            args=[cs.HANDLER],
            handled=False)

    q.dbus_return(e.message,
            dbus.Dictionary({
                'HandlerChannelFilter': text_fixed_properties,
                'BypassApproval': True,
                }, signature='sv'),
            signature='a{sv}')

    # FIXME: in an ideal world, MC would use its call to GetAll to pre-load
    # this property anyway
    e = q.expect('dbus-method-call',
            args=[cs.HANDLER, 'HandledChannels'],
            path=object_path,
            handled=False,
            interface=cs.PROPERTIES_IFACE,
            method='Get')
    # Oops! This would crash earlier versions of MC
    del bus_name_ref
    q.dbus_raise(e.message, cs.DBUS_ERROR_NO_REPLY, 'I crashed')

    # Well, there are no clients any more... so nobody can possibly
    # handle this channel. MC closes it.
    q.expect('dbus-method-call',
            path=unhandled_chan.object_path,
            method='Close')

if __name__ == '__main__':
    preseed()
    exec_test(test, {}, preload_mc=False)
