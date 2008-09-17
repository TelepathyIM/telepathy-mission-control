import dbus

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix
from fakecm import start_fake_connection_manager
from mctest import exec_test

FakeCM_bus_name = "com.example.FakeCM"
ConnectionManager_object_path = "/com/example/FakeCM/ConnectionManager"


def test(q, bus, mc):
    # Get the AccountManager interface
    account_manager = bus.get_object(
        tp_name_prefix + '.AccountManager',
        tp_path_prefix + '/AccountManager')
    account_manager_iface = dbus.Interface(account_manager,
            'org.freedesktop.Telepathy.AccountManager')

    # Introspect AccountManager for debugging purpose
    account_manager_introspected = account_manager.Introspect(
            dbus_interface='org.freedesktop.DBus.Introspectable')
    #print account_manager_introspected

    # Check AccountManager has D-Bus property interface
    properties = account_manager.GetAll(
            'org.freedesktop.Telepathy.AccountManager',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert properties is not None
    assert properties.get('Interfaces') == [
            'org.freedesktop.Telepathy.AccountManager',
            'com.nokia.AccountManager.Interface.Query',
            'org.freedesktop.Telepathy.AccountManager.Interface.Creation.DRAFT'
        ], properties.get('Interfaces')
    assert properties.get('ValidAccounts') == [], \
        properties.get('ValidAccounts')
    assert properties.get('InvalidAccounts') == [], \
        properties.get('InvalidAccounts')

    # Create an account
    params = dbus.Dictionary({"nickname": "fakenick"}, signature='sv')
    account_path = account_manager_iface.CreateAccount(
            'fakecm', # Connection_Manager
            'fakeprotocol', # Protocol
            'fakeaccount', #Display_Name
            params, # Parameters
            )
    assert account_path is not None

    # Check the account is correctly created
    properties = account_manager.GetAll(
            'org.freedesktop.Telepathy.AccountManager',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert properties is not None
    assert properties.get('ValidAccounts') == [account_path], properties
    assert properties.get('InvalidAccounts') == [], properties

    # Get the Account interface
    account = bus.get_object(
        tp_name_prefix + '.AccountManager',
        account_path)
    account_iface = dbus.Interface(account,
            'org.freedesktop.Telepathy.Account')
    # Introspect Account for debugging purpose
    account_introspected = account.Introspect(
            dbus_interface='org.freedesktop.DBus.Introspectable')
    #print account_introspected

    # Check Account has D-Bus property interface
    properties = account.GetAll(
            'org.freedesktop.Telepathy.Account',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert properties is not None
    assert 'org.freedesktop.Telepathy.Account' \
        in properties.get('Interfaces'), properties.get('Interfaces')
    assert 'org.freedesktop.Telepathy.Account.Interface.Avatar' \
        in properties.get('Interfaces'), properties.get('Interfaces')
    assert 'org.freedesktop.Telepathy.Account.Interface.Compat' \
        in properties.get('Interfaces'), properties.get('Interfaces')
    assert 'com.nokia.Account.Interface.Conditions' \
        in properties.get('Interfaces'), properties.get('Interfaces')
    assert properties.get('DisplayName') == 'fakeaccount', \
        properties.get('DisplayName')
    assert properties.get('Icon') == '', properties.get('Icon')
    assert properties.get('Valid') == True, properties.get('Valid')
    assert properties.get('Enabled') == False, properties.get('Enabled')
    #assert properties.get('Nickname') == 'fakenick', properties.get('Nickname')
    assert properties.get('Parameters') == params, properties.get('Parameters')
    assert properties.get('Connection') == '', properties.get('Connection')
    assert properties.get('NormalizedName') == '', \
        properties.get('NormalizedName')

    # Delete the account
    assert account_iface.Remove() is None
    account_event, account_manager_event = q.expect_many(
        EventPattern('dbus-signal',
            path=account_path,
            signal='Removed',
            interface='org.freedesktop.Telepathy.Account',
            args=[]
            ),
        EventPattern('dbus-signal',
            path=tp_path_prefix + '/AccountManager',
            signal='AccountRemoved',
            interface='org.freedesktop.Telepathy.AccountManager',
            args=[account_path]
            ),
        )

    # Check the account is correctly deleted
    properties = account_manager.GetAll(
            'org.freedesktop.Telepathy.AccountManager',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert properties is not None
    assert properties.get('ValidAccounts') == [], properties
    assert properties.get('InvalidAccounts') == [], properties


if __name__ == '__main__':
    exec_test(test, {})
