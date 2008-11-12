import dbus
import dbus.service
from servicetest import Event
from servicetest import EventPattern, tp_name_prefix, tp_path_prefix

cm_iface = "org.freedesktop.Telepathy.ConnectionManager"
conn_iface = "org.freedesktop.Telepathy.Connection"

class FakeConn(dbus.service.Object):
    def __init__(self, object_path, q, bus, nameref):
        self.object_path = object_path
        self.q = q
        self.bus = bus
        # keep a reference on nameref, otherwise, the name will be lost!
        self.nameref = nameref 
        dbus.service.Object.__init__(self, bus, object_path)

    @dbus.service.method(dbus_interface=conn_iface,
                         in_signature='', out_signature='')
    def Connect(self, protocol):
        self.q.append(Event('dbus-method-call', name="Connect", obj=self))
        return None

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


