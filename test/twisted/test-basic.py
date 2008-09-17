import dbus

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix
from fakecm import start_fake_connection_manager
from mctest import exec_test

FakeCM_bus_name = "com.example.FakeCM"
ConnectionManager_object_path = "/com/example/FakeCM/ConnectionManager"


def test(q, bus, mc):
    start_fake_connection_manager(q, bus, FakeCM_bus_name,
            ConnectionManager_object_path)
    
    # Introspect the old iface for debugging purpose
    mc_introspected = mc.Introspect(
            dbus_interface='org.freedesktop.DBus.Introspectable')
    #print mc_introspected

    # Check the old iface
    old_iface = dbus.Interface(mc, 'org.freedesktop.Telepathy.MissionControl')
    arg0 = old_iface.GetOnlineConnections()
    assert arg0 == []
    arg0 = old_iface.GetPresence()
    assert arg0 == 0, arg0

    ## Test the fake connection manager (test the test). The fake connection
    ## manager aims to be used by Mission Control
    fake_cm = bus.get_object(FakeCM_bus_name, ConnectionManager_object_path)

    def reply_handler_cb(dummy):
        print "ok: " + str(dummy)
    def error_handler_cb(dummy):
        print "error: " + str(dummy)

    fake_cm.GetParameters("awrty", reply_handler=reply_handler_cb,
            error_handler=error_handler_cb,
            dbus_interface="org.freedesktop.Telepathy.ConnectionManager")
    print "method called"
    e = q.expect('dbus-method-call')
    print e.name

if __name__ == '__main__':
    exec_test(test, {})
