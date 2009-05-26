import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, create_fakecm_account, enable_fakecm_account
import constants as cs

def test(q, bus, mc):
    account_manager = bus.get_object(cs.AM, cs.AM_PATH)
    account_manager_iface = dbus.Interface(account_manager, cs.AM)

    params = dbus.Dictionary({"account": "jc.denton@example.com",
        "password": "ionstorm"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_BUSY), 'busy',
            'Fighting conspiracies'), signature='uss')

    conn, e = enable_fakecm_account(q, bus, mc, account, params,
            has_presence=True,
            requested_presence=presence,
            expect_after_connect=[
                EventPattern('dbus-method-call',
                    interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                    method='SetPresence',
                    args=list(presence[1:]),
                    handled=False),
                ])

    q.dbus_return(e.message, signature='')
    q.dbus_emit(conn.object_path, cs.CONN_IFACE_SIMPLE_PRESENCE,
            'PresencesChanged', {conn.self_handle: presence},
            signature='a{u(uss)}')

    q.expect('dbus-signal', path=account.object_path,
            interface=cs.ACCOUNT, signal='AccountPropertyChanged',
            predicate=lambda e: e.args[0].get('CurrentPresence') == presence)

    # Change requested presence after going online
    presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_AWAY), 'away',
            'In Hong Kong'), signature='uss')
    call_async(q, account_props, 'Set', cs.ACCOUNT, 'RequestedPresence',
            presence)

    e = q.expect('dbus-method-call',
        interface=cs.CONN_IFACE_SIMPLE_PRESENCE, method='SetPresence',
        args=list(presence[1:]),
        handled=False)

    # Set returns immediately; the change happens asynchronously
    q.expect('dbus-return', method='Set')

    q.dbus_return(e.message, signature='')
    q.dbus_emit(conn.object_path, cs.CONN_IFACE_SIMPLE_PRESENCE,
            'PresencesChanged', {conn.self_handle: presence},
            signature='a{u(uss)}')

    q.expect('dbus-signal', path=account.object_path,
            interface=cs.ACCOUNT, signal='AccountPropertyChanged',
            predicate=lambda e: e.args[0].get('CurrentPresence') == presence)

if __name__ == '__main__':
    exec_test(test, {})
