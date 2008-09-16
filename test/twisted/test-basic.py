import dbus

from mctest import exec_test

def test(q, bus, mc):
    # Introspect for debugging purpose
    mc_introspected = mc.Introspect(
            dbus_interface='org.freedesktop.DBus.Introspectable')
    #print mc_introspected

    # Check MC has D-Bus property interface
    properties = mc.GetAll(
            'org.freedesktop.Telepathy.AccountManager',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert properties is not None

    # Check the old iface
    old_iface = dbus.Interface(mc, 'org.freedesktop.Telepathy.MissionControl')
    arg0 = old_iface.GetOnlineConnections()
    assert arg0 == []
    arg0 = old_iface.GetPresence()
    assert arg0 == 0, arg0

    # Check MC has AccountManager interface
    account_manager_iface = dbus.Interface(mc,
            'org.freedesktop.Telepathy.AccountManager')

    ## Not yet implemented in MC
    #params = dbus.Dictionary({}, signature='sv')
    #account_name = account_manager_iface.CreateAccount(
    #        'salut', # Connection_Manager
    #        'local-xmpp', # Protocol
    #        'mc_test', #Display_Name
    #        params, # Parameters
    #        )
    #assert account_name is not None

if __name__ == '__main__':
    exec_test(test, {})
