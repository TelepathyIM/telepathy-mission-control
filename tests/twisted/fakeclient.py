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
import dbus.service
from servicetest import Event
from servicetest import EventPattern, tp_name_prefix, tp_path_prefix

client_iface = "org.freedesktop.Telepathy.Client"
client_observer_iface = "org.freedesktop.Telepathy.Client.Observer"
client_approver_iface = "org.freedesktop.Telepathy.Client.Approver"
client_handler_iface = "org.freedesktop.Telepathy.Client.Handler"

properties_iface = "org.freedesktop.DBus.Properties"

empty_caps = dbus.Array([], signature='a{sv}')

class FakeClient(dbus.service.Object):
    def __init__(self, object_path, q, bus, bus_name, nameref,
            caps = empty_caps):
        self.object_path = object_path
        self.q = q
        self.bus = bus
        self.bus_name = bus_name
        # keep a reference on nameref, otherwise, the name will be lost!
        self.nameref = nameref 
        self.caps = caps
        dbus.service.Object.__init__(self, bus, object_path)

    @dbus.service.method(dbus_interface=properties_iface,
                         in_signature='ss', out_signature='v')
    def Get(self, interface_name, property_name):
        self.q.append(Event('dbus-method-call', name="Get",
                    obj=self, interface_name=interface_name,
                    property_name=property_name))
        if interface_name == client_iface and property_name == "Interfaces":
            return dbus.Array([
                    client_observer_iface,
                    client_approver_iface,
                    client_handler_iface
                    ], signature='s')
        if interface_name == client_observer_iface and \
                           property_name == "ObserverChannelFilter":
            return empty_caps
        if interface_name == client_approver_iface and \
                           property_name == "ApproverChannelFilter":
            return empty_caps
        if interface_name == client_handler_iface and \
                           property_name == "HandlerChannelFilter":
            return self.caps
        print "Error: interface_name=%s property_name=%s" % \
            (interface_name, property_name)
        return None

    @dbus.service.method(dbus_interface=properties_iface,
                         in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface_name):
        self.q.append(Event('dbus-method-call', name="GetAll",
                    obj=self, interface_name=interface_name))
        if interface_name == client_iface:
            return dbus.Dictionary({
                    'Interfaces': dbus.Array([
                        client_observer_iface, 
                        client_approver_iface, 
                        client_handler_iface
                        ])
                    }, signature='sv')
        return None

    @dbus.service.method(dbus_interface=client_handler_iface,
                         in_signature='ooa(oa{sv})aot', out_signature='')
    def HandleChannels(self, account, connection, channels,
            requests_satisfied, user_action_time):
        self.q.append(Event('dbus-method-call', name="HandleChannels",
                    obj=self, account=account, connection=connection,
                    channels=channels, requests_satisfied=requests_satisfied,
                    user_action_time=user_action_time))

def start_fake_client(q, bus, bus_name, object_path, caps):
    nameref = dbus.service.BusName(bus_name, bus=bus)
    client = FakeClient(object_path, q, bus, bus_name, nameref, caps)
    return client


