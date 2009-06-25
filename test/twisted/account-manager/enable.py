import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, create_fakecm_account
import constants as cs

def test(q, bus, mc):
    account_manager = bus.get_object(cs.AM, cs.AM_PATH)
    account_manager_iface = dbus.Interface(account_manager, cs.AM)

    params = dbus.Dictionary({"account": "smcv@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'RequestedPresence',
            (dbus.UInt32(cs.PRESENCE_TYPE_OFFLINE), 'offline', ''))
    q.expect('dbus-return', method='Set')
    q.expect('dbus-signal', signal='AccountPropertyChanged',
            predicate=lambda e:
                e.args[0].get('RequestedPresence', (None, None, None))[1]
                    == 'offline')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Enabled', False)
    q.expect('dbus-return', method='Set')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'ConnectAutomatically',
            False)
    q.expect('dbus-return', method='Set')

    call_async(q, account_props, 'Set', cs.ACCOUNT, 'RequestedPresence',
            (dbus.UInt32(cs.PRESENCE_TYPE_BUSY), 'busy', 'Testing Enabled'))
    q.expect('dbus-return', method='Set')

    # Go online by setting Enabled
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'Enabled', True)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=cs.tp_name_prefix + '.ConnectionManager.fakecm',
            path=cs.tp_path_prefix + '/ConnectionManager/fakecm',
            interface=cs.tp_name_prefix + '.ConnectionManager',
            handled=False)

if __name__ == '__main__':
    exec_test(test, {})
