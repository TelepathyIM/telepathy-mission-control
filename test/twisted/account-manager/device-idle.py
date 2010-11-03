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

import config

if config.HAVE_MCE:
    print "NOTE: built with real MCE support; skipping idleness test"
    raise SystemExit(77)

import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, unwrap, sync_dbus
from mctest import exec_test, create_fakecm_account, SimulatedConnection, \
    enable_fakecm_account
import constants as cs

# Fake MCE constants, cloned from mce-slacker.c
MCE_SERVICE = "org.freedesktop.Telepathy.MissionControl.Tests.MCE"

MCE_SIGNAL_IF = "org.freedesktop.Telepathy.MissionControl.Tests.MCE"

MCE_REQUEST_IF = "org.freedesktop.Telepathy.MissionControl.Tests.MCE"
MCE_REQUEST_PATH = "/org/freedesktop/Telepathy/MissionControl/Tests/MCE"

class SimulatedMCE(object):
    def __init__(self, q, bus, inactive=False):
        self.bus = bus
        self.q = q
        self.inactive = inactive
        self.object_path = MCE_REQUEST_PATH
        self._name_ref = dbus.service.BusName(MCE_SERVICE, bus)

        q.add_dbus_method_impl(self.GetInactivity,
                               path=self.object_path, interface=MCE_REQUEST_IF,
                               method='GetInactivity')


    def GetInactivity(self, e):
        self.q.dbus_return(e.message, self.inactive, signature='b')

    def InactivityChanged(self, new_value):
        self.inactive = new_value
        self.q.dbus_emit(self.object_path, MCE_SIGNAL_IF, "InactivityChanged",
                         self.inactive, signature="b")

    def release_name(self):
        del self._name_ref

def _create_and_enable(q, bus, mc, account_name, power_saving_supported,
                       expect_after_connect=[]):
    extra_interfaces = []
    if power_saving_supported:
        extra_interfaces = [cs.CONN_IFACE_POWER_SAVING]
    params = dbus.Dictionary({"account": account_name, "password": "secrecy"},
                              signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params, has_requests=False,
                                 extra_interfaces=extra_interfaces,
                                 expect_after_connect=expect_after_connect)

    if isinstance(conn, tuple):
        conn = conn[0]

    return account, conn

def _disable_account(q, bus, mc, account, conn):
    account.Set(cs.ACCOUNT, 'Enabled', False,
                dbus_interface=cs.PROPERTIES_IFACE)

    q.expect('dbus-method-call', method='Disconnect',
            path=conn.object_path, handled=True)

def test(q, bus, mc):
    mce = SimulatedMCE(q, bus, True)

    account1, conn1 = _create_and_enable(
        q, bus, mc, "first@example.com", True,
        [EventPattern('dbus-method-call', method='SetPowerSaving', args=[True])])
    account2, conn2 = _create_and_enable(q, bus, mc, "second@example.com", False)

    # Second account does not support PowerSaving interface, don't call SetPowerSaving
    forbid_no_iface =[EventPattern('dbus-method-call', method='SetPowerSaving',
                                   path=conn2.object_path)]

    q.forbid_events(forbid_no_iface)

    for enabled in [False, True, False]:
        mce.InactivityChanged(enabled)
        q.expect('dbus-method-call', method='SetPowerSaving',
                 args=[enabled], interface=cs.CONN_IFACE_POWER_SAVING,
                 path=conn1.object_path)

    _disable_account(q, bus, mc, account1, conn1)
    _disable_account(q, bus, mc, account2, conn2)

    q.unforbid_events(forbid_no_iface)

    # Make sure we don't call SetPowerSaving on a disconnected connection.

    forbid_when_disconnected =[EventPattern('dbus-method-call', method='SetPowerSaving')]

    q.forbid_events(forbid_when_disconnected)

    mce.InactivityChanged(True)

    sync_dbus(bus, q, account1)

    q.unforbid_events(forbid_when_disconnected)

    mce.release_name()

if __name__ == '__main__':
    exec_test(test, {})
