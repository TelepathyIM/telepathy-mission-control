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
from twisted.internet import reactor

properties_iface = "org.freedesktop.DBus.Properties"
cm_iface = "org.freedesktop.Telepathy.ConnectionManager"
conn_iface = "org.freedesktop.Telepathy.Connection"
caps_iface = \
  "org.freedesktop.Telepathy.Connection.Interface.ContactCapabilities.DRAFT"
requests_iface = "org.freedesktop.Telepathy.Connection.Interface.Requests"
channel_iface = "org.freedesktop.Telepathy.Channel"

class FakeChannel(dbus.service.Object):
    def __init__(self, conn, object_path, q, bus, nameref, props):
        self.conn = conn
        self.object_path = object_path
        self.q = q
        self.bus = bus
        # keep a reference on nameref, otherwise, the name will be lost!
        self.nameref = nameref
        self.props = props

        if channel_iface + '.TargetHandle' not in props:
            self.props[channel_iface + '.TargetHandle'] = \
                self.conn.get_handle(props[channel_iface + '.TargetID'])

        dbus.service.Object.__init__(self, bus, object_path)

    def called(self, method):
        self.q.append(Event('dbus-method-call', name=method, obj=self,
                    path=self.object_path))

    @dbus.service.method(dbus_interface=channel_iface,
                         in_signature='', out_signature='as')
    def GetInterfaces(self):
        self.called('GetInterfaces')
        return [self.props[channel_iface + '.ChannelType']]

    @dbus.service.method(dbus_interface=channel_iface,
                         in_signature='', out_signature='u')
    def GetHandle(self):
        return self.props[channel_iface + '.TargetHandle']

    @dbus.service.method(dbus_interface=channel_iface,
                         in_signature='', out_signature='')
    def Close(self):
        self.Closed()

    @dbus.service.signal(dbus_interface=channel_iface, signature='')
    def Closed(self):
        pass


class FakeConn(dbus.service.Object):
    def __init__(self, object_path, q, bus, nameref):
        self.object_path = object_path
        self.q = q
        self.bus = bus
        # keep a reference on nameref, otherwise, the name will be lost!
        self.nameref = nameref 
        self.status = 2 # Connection_Status_Disconnected
        self.next_channel_id = 1
        self.channels = []
        self.handles = {}
        self.next_handle = 1337 # break people depending on SelfHandle == 1
        dbus.service.Object.__init__(self, bus, object_path)

    # interface Connection

    @dbus.service.method(dbus_interface=conn_iface,
                         in_signature='', out_signature='')
    def Connect(self):
        self.StatusChanged(1, 1)
        self.StatusChanged(0, 1)
        self.q.append(Event('dbus-method-call', name="Connect", obj=self,
                    path=self.object_path))
        return None

    @dbus.service.method(dbus_interface=conn_iface,
                         in_signature='', out_signature='as')
    def GetInterfaces(self):
        self.q.append(Event('dbus-method-call', name="GetInterfaces",
                    obj=self, path=self.object_path))
        return dbus.Array([conn_iface, caps_iface, requests_iface])

    def get_handle(self, id):
        for handle, id_ in self.handles.iteritems():
            if id_ == id:
                return handle
        handle = self.next_handle
        self.next_handle += 1

        self.handles[handle] = id
        return handle

    @dbus.service.method(dbus_interface=conn_iface,
                         in_signature='', out_signature='u')
    def GetSelfHandle(self):
        self.q.append(Event('dbus-method-call', name="GetSelfHandle",
                    obj=self, path=self.object_path))
        return self.get_handle('fakeaccount')

    @dbus.service.method(dbus_interface=conn_iface,
                         in_signature='', out_signature='u')
    def GetStatus(self):
        self.q.append(Event('dbus-method-call', name="GetStatus",
                    obj=self, path=self.object_path))
        return self.status

    @dbus.service.method(dbus_interface=conn_iface,
                         in_signature='uau', out_signature='as')
    def InspectHandles(self, handle_type, handles):
        self.q.append(Event('dbus-method-call', name="InspectHandles",
                    obj=self, path=self.object_path, handle_type=handle_type,
                    handles=handles))
        if handle_type != 1:
            raise "non-contact handles don't exist"

        ret = []
        for handle in handles:
            if handle not in self.handles:
                raise "%d is not a valid handle" % handle
            ret.append(self.handles[handle])

        return ret

    @dbus.service.method(dbus_interface=conn_iface,
                         in_signature='uas', out_signature='au')
    def RequestHandles(self, type, ids):
        if type != 1:
            raise "non-contact handles don't exist"

        ret = []
        for id in ids:
            ret.append(self.get_handle(id))

        return ret

    @dbus.service.signal(dbus_interface=conn_iface,
                         signature='uu')
    def StatusChanged(self, status, reason):
        self.status = status

    # interface Connection.Interface.ContactCapabilities.DRAFT

    @dbus.service.method(dbus_interface=caps_iface,
                         in_signature='aa{sv}', out_signature='')
    def SetSelfCapabilities(self, caps):
        self.q.append(Event('dbus-method-call', name="SetSelfCapabilities",
                    obj=self, path=self.object_path, caps=caps))
        return None

    @dbus.service.signal(dbus_interface=requests_iface,
                         signature='a(oa{sv})')
    def NewChannels(self, array):
        self.channels = self.channels + array

    @dbus.service.signal(dbus_interface=conn_iface,
                         signature='osuub')
    def NewChannel(self, object_path, channel_type, handle_type, handle,
            suppress_handle):
        pass

    @dbus.service.method(dbus_interface=properties_iface,
                         in_signature='ss', out_signature='v')
    def Get(self, interface_name, property_name):
        self.q.append(Event('dbus-method-call', name="Get",
                    obj=self, interface_name=interface_name,
                    property_name=property_name))
        if interface_name == requests_iface and \
                           property_name == "Channels":
            return dbus.Array(self.channels, signature='(oa{sv})')
        print "Error: interface_name=%s property_name=%s" % \
            (interface_name, property_name)
        return None

    @dbus.service.method(dbus_interface=properties_iface,
                         in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface_name):
        self.q.append(Event('dbus-method-call', name="GetAll",
                    obj=self, interface_name=interface_name))
        if interface_name == conn_iface:
            return dbus.Dictionary({
                    'SelfHandle': 0L
                    }, signature='sv')
        if interface_name == requests_iface:
            return dbus.Dictionary({
                    'Channels': dbus.Array(self.channels,
                        signature='(oa{sv})')
                    }, signature='sv')
        return None

    def new_incoming_channel(self, object_path, asv):
        self.NewChannels(dbus.Array([(object_path, asv)],
                    signature='(oa{sv})'))
        self.NewChannel(object_path,
                asv['org.freedesktop.Telepathy.Channel.ChannelType'],
                asv['org.freedesktop.Telepathy.Channel.TargetHandleType'],
                asv['org.freedesktop.Telepathy.Channel.TargetHandle'],
                False)

    # interface Connection.Interface.Requests
    def make_channel(self, props):
        path = self.object_path + "/channel%d" % self.next_channel_id
        self.next_channel_id += 1
        chan = FakeChannel(self, path, self.q, self.bus, self.nameref, props)
        reactor.callLater(0, self.NewChannels, [(chan, props)])
        return chan

    @dbus.service.method(dbus_interface=requests_iface,
                         in_signature='a{sv}', out_signature='oa{sv}')
    def CreateChannel(self, request):
        self.q.append(Event('dbus-method-call', name="CreateChannel",
                    obj=self, interface_name=requests_iface))
        chan = self.make_channel(request)
        return (chan, request)

    @dbus.service.method(dbus_interface=requests_iface,
                         in_signature='a{sv}', out_signature='boa{sv}')
    def EnsureChannel(self, request):
        self.q.append(Event('dbus-method-call', name="EnsureChannel",
                    obj=self, interface_name=requests_iface))
        chan = self.make_channel(request)
        self.q
        return (True, chan, request)

    @dbus.service.signal(dbus_interface=requests_iface,
                         signature="o")
    def ChannelClosed(self, channel):
        pass



class FakeCM(dbus.service.Object):
    def __init__(self, object_path, q, bus, bus_name, nameref):
        self.object_path = object_path
        self.q = q
        self.bus = bus
        self.bus_name = bus_name
        # keep a reference on nameref, otherwise, the name will be lost!
        self.nameref = nameref 
        dbus.service.Object.__init__(self, bus, object_path)

    @dbus.service.method(dbus_interface=cm_iface,
                         in_signature='s', out_signature='a(susv)')
    def GetParameters(self, protocol):
        self.q.append(Event('dbus-method-call', name="GetParameters",
                    protocol=protocol, obj=self))
        return []

    @dbus.service.method(dbus_interface=cm_iface,
                         in_signature='', out_signature='as')
    def ListProtocols(self, protocol):
        self.q.append(Event('dbus-method-call', name="ListProtocols", obj=self))
        return ['fakeprotocol']

    @dbus.service.method(dbus_interface=cm_iface,
                         in_signature='sa{sv}', out_signature='so')
    def RequestConnection(self, protocol, parameters):
        conn_path = tp_path_prefix + "/Connection/fakecm/fakeprotocol/conn1"
        conn = FakeConn(conn_path, self.q, self.bus, self.nameref)
        self.q.append(Event('dbus-method-call', name="RequestConnection",
                    protocol=protocol, parameters=parameters,
                    conn=conn, obj=self))
        return [self.bus_name, conn_path]

def start_fake_connection_manager(q, bus, bus_name, object_path):
    nameref = dbus.service.BusName(bus_name, bus=bus)
    cm = FakeCM(object_path, q, bus, bus_name, nameref)
    return cm


