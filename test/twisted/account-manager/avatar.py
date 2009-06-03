import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, create_fakecm_account, enable_fakecm_account
import constants as cs

def test(q, bus, mc):
    account_manager = bus.get_object(cs.AM, cs.AM_PATH)
    account_manager_iface = dbus.Interface(account_manager, cs.AM)

    params = dbus.Dictionary({"account": "me@example.com",
        "password": "secrecy"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    call_async(q, account_props, 'Set', cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
            dbus.Struct((dbus.ByteArray('AAAA'), 'image/jpeg')))
    q.expect_many(
        EventPattern('dbus-signal',
            path=account.object_path,
            signal='AvatarChanged',
            interface=cs.ACCOUNT_IFACE_AVATAR,
            args=[]),
        EventPattern('dbus-return', method='Set'),
        )
    assert account_props.Get(cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
            byte_arrays=True) == ('AAAA', 'image/jpeg')

    # OK, let's go online. The avatar is set regardless of the CM
    conn, e = enable_fakecm_account(q, bus, mc, account, params,
            has_avatars=True, avatars_persist=True,
            expect_after_connect=[
                EventPattern('dbus-method-call',
                    interface=cs.CONN_IFACE_AVATARS, method='SetAvatar',
                    handled=True, args=['AAAA', 'image/jpeg']),
                ])

    # Change avatar after going online
    call_async(q, account_props, 'Set', cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
            (dbus.ByteArray('BBBB'), 'image/png'))

    q.expect_many(
            EventPattern('dbus-method-call',
                interface=cs.CONN_IFACE_AVATARS, method='SetAvatar',
                args=['BBBB', 'image/png'],
                handled=True),
            EventPattern('dbus-signal', path=account.object_path,
                interface=cs.ACCOUNT_IFACE_AVATAR, signal='AvatarChanged'),
            EventPattern('dbus-return', method='Set')
            )

    assert account_props.Get(cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
            byte_arrays=True) == ('BBBB', 'image/png')

    someone_else = conn.ensure_handle(cs.HT_CONTACT, 'alberto@example.com')

    # Another contact changes their avatar: ignored
    q.dbus_emit(conn.object_path, cs.CONN_IFACE_AVATARS, 'AvatarUpdated',
            someone_else, "mardy's avatar token", signature='us')

    # Another client changes our avatar remotely
    q.dbus_emit(conn.object_path, cs.CONN_IFACE_AVATARS, 'AvatarUpdated',
            conn.self_handle, 'CCCC', signature='us')

    e = q.expect('dbus-method-call',
            interface=cs.CONN_IFACE_AVATARS, method='RequestAvatars',
            args=[[conn.self_handle]],
            handled=False)

    q.dbus_return(e.message, signature='')
    q.dbus_emit(conn.object_path, cs.CONN_IFACE_AVATARS,
            'AvatarRetrieved', conn.self_handle, 'CCCC',
            dbus.ByteArray('CCCC'), 'image/svg', signature='usays')
    q.expect('dbus-signal', path=account.object_path,
            interface=cs.ACCOUNT_IFACE_AVATAR, signal='AvatarChanged'),

    assert account_props.Get(cs.ACCOUNT_IFACE_AVATAR, 'Avatar',
            byte_arrays=True) == ('CCCC', 'image/svg')

if __name__ == '__main__':
    exec_test(test, {})
