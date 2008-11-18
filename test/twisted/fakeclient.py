import dbus
import dbus.service
from servicetest import Event
from servicetest import EventPattern, tp_name_prefix, tp_path_prefix

client_iface = "org.freedesktop.Telepathy.Client.DRAFT"
client_observer_iface = "org.freedesktop.Telepathy.Client.Observer.DRAFT"
client_approver_iface = "org.freedesktop.Telepathy.Client.Approver.DRAFT"
client_handler_iface = "org.freedesktop.Telepathy.Client.Handler.DRAFT"

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

def start_fake_client(q, bus, bus_name, object_path, caps):
    nameref = dbus.service.BusName(bus_name, bus=bus)
    client = FakeClient(object_path, q, bus, bus_name, nameref, caps)
    return client


