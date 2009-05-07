print "Skipping test that is known to fail:"
print "    http://bugs.freedesktop.org/show_bug.cgi?id=20880"
raise SystemExit(77)

import dbus

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from fakecm import start_fake_connection_manager
from mctest import exec_test
import constants as cs

FakeCM_bus_name = "com.example.FakeCM"
ConnectionManager_object_path = "/com/example/FakeCM/ConnectionManager"


def test(q, bus, mc):
    # Get the AccountManager interface
    account_manager = bus.get_object(cs.AM, cs.AM_PATH)
    account_manager_iface = dbus.Interface(account_manager, cs.AM)

    # Create an account with a bad Connection_Manager - it should fail

    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    call_async(q, account_manager_iface, 'CreateAccount',
            'nonexistent_cm', # Connection_Manager
            'fakeprotocol', # Protocol
            'fakeaccount', #Display_Name
            params, # Parameters
            {}, # Properties
            )
    q.expect('dbus-error', method='CreateAccount')

    # Create an account with a bad Protocol - it should fail

    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    call_async(q, account_manager_iface, 'CreateAccount',
            'fakecm', # Connection_Manager
            'nonexistent-protocol', # Protocol
            'fakeaccount', #Display_Name
            params, # Parameters
            {}, # Properties
            )
    q.expect('dbus-error', method='CreateAccount')

    # Create an account with incomplete Parameters - it should fail

    params = dbus.Dictionary({"account": "someguy@example.com"},
            signature='sv')
    call_async(q, account_manager_iface, 'CreateAccount',
            'fakecm', # Connection_Manager
            'fakeprotocol', # Protocol
            'fakeaccount', #Display_Name
            params, # Parameters
            {}, # Properties
            )
    q.expect('dbus-error', method='CreateAccount')

if __name__ == '__main__':
    exec_test(test, {})
