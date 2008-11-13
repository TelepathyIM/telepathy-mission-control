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

    # Check the requested presence is offline
    properties = account.GetAll(
            'org.freedesktop.Telepathy.Account',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert properties is not None
    # the requested presence is defined by Connection_Presence_Type:
    #  Connection_Presence_Type_Unset = 0
    #  Connection_Presence_Type_Offline = 1
    #  Connection_Presence_Type_Available = 2
    assert properties.get('RequestedPresence') == \
        dbus.Struct((dbus.UInt32(0L), dbus.String(u''), dbus.String(u''))), \
        properties.get('RequestedPresence')  # FIXME: we should expect 1

    # Go online
    requested_presence = dbus.Struct((dbus.UInt32(2L), dbus.String(u'brb'),
                dbus.String(u'Be back soon!')))
    account.Set('org.freedesktop.Telepathy.Account',
            'RequestedPresence', requested_presence,
            dbus_interface='org.freedesktop.DBus.Properties')

    e = q.expect('dbus-method-call', name='RequestConnection',
            protocol='fakeprotocol')
    conn_object_path = e.conn.object_path
    assert e.parameters == params

    e = q.expect('dbus-method-call', name='Connect',
            path=conn_object_path)

    e = q.expect('dbus-method-call', name='SetSelfCapabilities',
            path=conn_object_path)
    assert e.caps == dbus.Array([]), e.caps

    # Check the requested presence is online
    properties = account.GetAll(
            'org.freedesktop.Telepathy.Account',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert properties is not None
    assert properties.get('RequestedPresence') == requested_presence, \
        properties.get('RequestedPresence')

if __name__ == '__main__':
    exec_test(test, {})
