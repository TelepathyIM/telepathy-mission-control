import dbus
import dbus.service
from servicetest import Event

cm_iface = "org.freedesktop.Telepathy.ConnectionManager"
class FakeCM(dbus.service.Object):
    def __init__(self, object_path, q, bus, nameref):
        self.q = q
        # keep a reference on nameref, otherwise, the name will be lost!
        self.nameref = nameref 
        dbus.service.Object.__init__(self, bus, object_path)

    @dbus.service.method(dbus_interface=cm_iface,
                         in_signature='v', out_signature='s')
    def StringifyVariant(self, variant):
        print "StringifyVariant called"
        self.q.append(Event('dbus-method-call', name="StringifyVariant"))
        return str(variant)

    @dbus.service.method(dbus_interface=cm_iface,
                         in_signature='s', out_signature='a(susv)')
    def GetParameters(self, protocol):
        self.q.append(Event('dbus-method-call', name="GetParameters",
                    arg=protocol))
        return []

def start_fake_connection_manager(q, bus, bus_name, object_path):
    nameref = dbus.service.BusName(bus_name, bus=bus)
    cm = FakeCM(object_path, q, bus, nameref)
    return cm


