"""Regression test for a client crashing when Get is called.
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, sync_dbus
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    bus_name = '.'.join([tp_name_prefix, 'Client.CrashMe'])
    bus_name_ref = dbus.service.BusName(bus_name, bus)
    object_path = '/' + bus_name.replace('.', '/')

    # MC inspects it
    e = q.expect('dbus-method-call',
            interface=cs.PROPERTIES_IFACE, method='Get',
            path=object_path,
            args=[cs.CLIENT, 'Interfaces'],
            handled=False)
    # Simulate a crash
    del bus_name_ref
    sync_dbus(bus, q, account)
    # This might crash MC in sympathy
    q.dbus_raise(e.message, cs.DBUS_ERROR_NO_REPLY, 'I crashed')

    sync_dbus(bus, q, account)

    # Try again
    bus_name = '.'.join([tp_name_prefix, 'Client.CrashMeAgain'])
    bus_name_ref = dbus.service.BusName(bus_name, bus)
    object_path = '/' + bus_name.replace('.', '/')

    # MC inspects it
    e = q.expect('dbus-method-call',
            interface=cs.PROPERTIES_IFACE, method='Get',
            path=object_path,
            args=[cs.CLIENT, 'Interfaces'],
            handled=False)
    # Don't crash just yet
    q.dbus_return(e.message, dbus.Array([cs.OBSERVER], signature='s'),
            signature='v')
    # MC investigates further
    e = q.expect('dbus-method-call',
            interface=cs.PROPERTIES_IFACE, method='Get',
            path=object_path,
            args=[cs.OBSERVER, 'ObserverChannelFilter'],
            handled=False)
    # Simulate another crash
    del bus_name_ref
    sync_dbus(bus, q, account)
    q.dbus_raise(e.message, cs.DBUS_ERROR_NO_REPLY, 'I crashed')

    # Try again
    bus_name = '.'.join([tp_name_prefix, 'Client.CrashMeHarder'])
    bus_name_ref = dbus.service.BusName(bus_name, bus)
    object_path = '/' + bus_name.replace('.', '/')

    # MC inspects it
    e = q.expect('dbus-method-call',
            interface=cs.PROPERTIES_IFACE, method='Get',
            path=object_path,
            args=[cs.CLIENT, 'Interfaces'],
            handled=False)
    # Don't crash just yet
    q.dbus_return(e.message, dbus.Array([cs.OBSERVER], signature='s'),
            signature='v')
    # MC investigates further
    e = q.expect('dbus-method-call',
            interface=cs.PROPERTIES_IFACE, method='Get',
            path=object_path,
            args=[cs.OBSERVER, 'ObserverChannelFilter'],
            handled=False)
    # Simulate a crash with highly unfortunate timing
    del bus_name_ref
    sync_dbus(bus, q, account)
    q.dbus_return(e.message, dbus.Array([dbus.Dictionary({
        'x': 'y',
        }, signature='sv')], signature='a{sv}'), signature='v')
    sync_dbus(bus, q, account)

if __name__ == '__main__':
    exec_test(test, {})
