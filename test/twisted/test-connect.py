import dbus

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix
from fakecm import start_fake_connection_manager
from mctest import exec_test

FakeCM_bus_name = "org.freedesktop.Telepathy.ConnectionManager.fakecm"
ConnectionManager_object_path = \
    "/org/freedesktop/Telepathy/ConnectionManager/fakecm"


def test(q, bus, mc):
    start_fake_connection_manager(q, bus, FakeCM_bus_name,
            ConnectionManager_object_path)
    
    # Check the old iface
    old_iface = dbus.Interface(mc, 'org.freedesktop.Telepathy.MissionControl')
    arg0 = old_iface.GetOnlineConnections()
    assert arg0 == []
    arg0 = old_iface.GetPresence()
    assert arg0 == 0, arg0

    # Get the AccountManager interface
    account_manager = bus.get_object(
        tp_name_prefix + '.AccountManager',
        tp_path_prefix + '/AccountManager')
    account_manager_iface = dbus.Interface(account_manager,
            'org.freedesktop.Telepathy.AccountManager')

    # Create an account
    params = dbus.Dictionary({"nickname": "fakenick"}, signature='sv')
    account_path = account_manager_iface.CreateAccount(
            'fakecm', # Connection_Manager
            'fakeprotocol', # Protocol
            'fakeaccount', #Display_Name
            params, # Parameters
            )
    assert account_path is not None

    # Get the Account interface
    account = bus.get_object(
        tp_name_prefix + '.AccountManager',
        account_path)
    account_iface = dbus.Interface(account,
            'org.freedesktop.Telepathy.Account')

    # Enable the account
    account.Set('org.freedesktop.Telepathy.Account',
            'Enabled', True,
            dbus_interface='org.freedesktop.DBus.Properties')
    q.expect('dbus-signal',
            path=account_path,
            signal='AccountPropertyChanged',
            interface='org.freedesktop.Telepathy.Account',
            args=[dbus.Dictionary({"Enabled": True}, signature='sv')])

    # Which value should I use?
    # MC_PRESENCE_AVAILABLE == 2
    # TP_CONNECTION_STATUS_CONNECTED == 0
    old_iface.SetPresence(2, "Hello, everybody!")
    e = q.expect('dbus-method-call', name='RequestConnection',
            protocol='fakeprotocol')
    assert e.parameters == params

if __name__ == '__main__':
    exec_test(test, {})
