import dbus
import dbus.service
from servicetest import Event
from servicetest import EventPattern, tp_name_prefix, tp_path_prefix

properties_iface = "org.freedesktop.DBus.Properties"
cm_iface = "org.freedesktop.Telepathy.ConnectionManager"
conn_iface = "org.freedesktop.Telepathy.Connection"
caps_iface = \
  "org.freedesktop.Telepathy.Connection.Interface.ContactCapabilities.DRAFT"
requests_iface = "org.freedesktop.Telepathy.Connection.Interface.Requests"

class FakeConn(dbus.service.Object):
    def __init__(self, object_path, q, bus, nameref):
        self.object_path = object_path
        self.q = q
        self.bus = bus
        # keep a reference on nameref, otherwise, the name will be lost!
        self.nameref = nameref 
        self.status = 2 # Connection_Status_Disconnected
        self.channels = []
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

    @dbus.service.method(dbus_interface=conn_iface,
                         in_signature='', out_signature='u')
    def GetSelfHandle(self):
        self.q.append(Event('dbus-method-call', name="GetSelfHandle",
                    obj=self, path=self.object_path))
        return 0

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
        return ["self@server"]

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


