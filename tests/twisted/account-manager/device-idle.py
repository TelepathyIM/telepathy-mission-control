# Copyright (C) 2009 Nokia Corporation
# Copyright (C) 2009 Collabora Ltd.
# Copyright (C) 2013 Intel Corporation
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

import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, unwrap, sync_dbus
from mctest import exec_test, create_fakecm_account, SimulatedConnection, \
    enable_fakecm_account
import constants as cs

# Fake SessionManager constants, cloned from mcd-slacker.c
STATUS_AVAILABLE = 0
STATUS_INVISIBLE = 1
STATUS_BUSY = 2
STATUS_IDLE = 3

SERVICE_NAME = "org.gnome.SessionManager"
SERVICE_OBJECT_PATH = "/org/gnome/SessionManager/Presence"
SERVICE_INTERFACE = "org.gnome.SessionManager.Presence"
SERVICE_PROP_NAME = "status"
SERVICE_SIG_NAME = "StatusChanged"

class SimulatedSession(object):
    def __init__(self, q, bus, status=STATUS_AVAILABLE):
        self.bus = bus
        self.q = q
        self.status = status
        self.object_path = SERVICE_OBJECT_PATH
        self._name_ref = dbus.service.BusName(SERVICE_NAME, bus)

        self.q.add_dbus_method_impl(self.GetAll,
                path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll')
        self.q.add_dbus_method_impl(self.Get,
                path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='Get')

    def GetAll(self, e):
        ret = dbus.Dictionary({}, signature='sv')
        ret[SERVICE_PROP_NAME] = dbus.UInt32(self.status)
        self.q.dbus_return(e.message, ret, signature='a{sv}')

    def Get(self, e):
        if e.args[0] == SERVICE_INTERFACE and e.args[1] == SERVICE_PROP_NAME:
            self.q.dbus_return(e.message, dbus.UInt32(self.status), signature='v')
            return

        self.q.dbus_raise(e.message, cs.NOT_IMPLEMENTED, \
            "Unknown property %s on interface %s" % (e.args[0], e.args[1]))

    def StatusChanged(self, new_value):
        self.status = new_value
        self.q.dbus_emit(self.object_path, SERVICE_INTERFACE, SERVICE_SIG_NAME,
                         dbus.UInt32(self.status), signature="u")

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
    service = SimulatedSession(q, bus, STATUS_IDLE)

    account1, conn1 = _create_and_enable(
        q, bus, mc, "first@example.com", True,
        [EventPattern('dbus-method-call', method='SetPowerSaving', args=[True])])
    account2, conn2 = _create_and_enable(q, bus, mc, "second@example.com", False)

    # Second account does not support PowerSaving interface, don't call SetPowerSaving
    forbid_no_iface =[EventPattern('dbus-method-call', method='SetPowerSaving',
                                   path=conn2.object_path)]

    q.forbid_events(forbid_no_iface)

    for status in [STATUS_AVAILABLE, STATUS_IDLE, STATUS_BUSY]:
        service.StatusChanged(status)
        q.expect('dbus-method-call', method='SetPowerSaving',
                 args=[status == STATUS_IDLE], interface=cs.CONN_IFACE_POWER_SAVING,
                 path=conn1.object_path)

    _disable_account(q, bus, mc, account1, conn1)
    _disable_account(q, bus, mc, account2, conn2)

    q.unforbid_events(forbid_no_iface)

    # Make sure we don't call SetPowerSaving on a disconnected connection.

    forbid_when_disconnected =[EventPattern('dbus-method-call', method='SetPowerSaving')]

    q.forbid_events(forbid_when_disconnected)

    service.StatusChanged(STATUS_IDLE)

    sync_dbus(bus, q, account1)

    q.unforbid_events(forbid_when_disconnected)

    service.release_name()

if __name__ == '__main__':
    exec_test(test, {})
