import dbus

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix
from fakecm import start_fake_connection_manager
from fakeclient import start_fake_client
from mctest import exec_test
import constants as cs

_last_handle = 41
_handles = {}
def allocate_handle(identifier):
    global _last_handle

    if identifier in _handles:
        return _handles[identifier]

    _last_handle += 1
    _handles[identifier] = _last_handle
    return _last_handle

def test(q, bus, mc):
    cm_name_ref = dbus.service.BusName(
            tp_name_prefix + '.ConnectionManager.fakecm', bus=bus)

    http_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': 1L,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_STREAM_TUBE,
        cs.CHANNEL_TYPE_STREAM_TUBE + '.Service':
            'http'
        }, signature='sv')
    caps = dbus.Array([http_fixed_properties], signature='a{sv}')

    # Be a Client
    client_name_ref = dbus.service.BusName(
            tp_name_prefix + '.Client.downloader', bus=bus)

    e = q.expect('dbus-method-call',
            interface=cs.PROPERTIES_IFACE, method='Get',
            args=[cs.CLIENT, 'Interfaces'],
            handled=False)
    q.dbus_return(e.message, dbus.Array([cs.HANDLER], signature='s',
        variant_level=1), signature='v')

    e = q.expect('dbus-method-call',
            interface=cs.PROPERTIES_IFACE, method='Get',
            args=[cs.HANDLER, 'HandlerChannelFilter'],
            handled=False)
    q.dbus_return(e.message, dbus.Array([http_fixed_properties],
        signature='a{sv}', variant_level=1), signature='v')

    # Get the AccountManager interface
    account_manager = bus.get_object(cs.AM, cs.AM_PATH)
    account_manager_iface = dbus.Interface(account_manager, cs.AM)

    # Create an account
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
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
    account_iface = dbus.Interface(account, cs.ACCOUNT)

    # FIXME: MC ought to introspect the CM and find out that the params are
    # in fact sufficient

    # The account is initially valid but disabled
    assert not account.Get(cs.ACCOUNT, 'Enabled',
            dbus_interface=cs.PROPERTIES_IFACE)
    assert account.Get(cs.ACCOUNT, 'Valid',
            dbus_interface=cs.PROPERTIES_IFACE)

    # Enable the account
    account.Set(cs.ACCOUNT, 'Enabled', True,
            dbus_interface=cs.PROPERTIES_IFACE)
    q.expect('dbus-signal',
            path=account_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT)

    assert account.Get(cs.ACCOUNT, 'Enabled',
            dbus_interface=cs.PROPERTIES_IFACE)
    assert account.Get(cs.ACCOUNT, 'Valid',
            dbus_interface=cs.PROPERTIES_IFACE)

    # Check the requested presence is offline
    properties = account.GetAll(cs.ACCOUNT,
            dbus_interface=cs.PROPERTIES_IFACE)
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
    account.Set(cs.ACCOUNT,
            'RequestedPresence', requested_presence,
            dbus_interface=cs.PROPERTIES_IFACE)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', params],
            destination=tp_name_prefix + '.ConnectionManager.fakecm',
            path=tp_path_prefix + '/ConnectionManager/fakecm',
            interface=tp_name_prefix + '.ConnectionManager',
            handled=False)

    # FIXME: this next bit makes far too many assumptions about the precise
    # order of things

    conn_object_path = dbus.ObjectPath(tp_path_prefix +
            '/Connection/fakecm/fakeprotocol/_')
    conn_bus_name = tp_name_prefix + '.Connection.fakecm.fakeprotocol._'
    conn_bus_name_ref = dbus.service.BusName(conn_bus_name, bus=bus)
    q.dbus_return(e.message, conn_bus_name, conn_object_path, signature='so')

    e = q.expect('dbus-method-call', method='GetStatus',
            path=conn_object_path, handled=False)
    q.dbus_return(e.message, cs.CONN_STATUS_DISCONNECTED, signature='u')

    e = q.expect('dbus-method-call', method='Connect',
            path=conn_object_path, handled=False)
    q.dbus_return(e.message, signature='')

    q.dbus_emit(conn_object_path, cs.CONN, 'StatusChanged',
            cs.CONN_STATUS_CONNECTING, cs.CONN_STATUS_REASON_NONE,
            signature='uu')

    q.dbus_emit(conn_object_path, cs.CONN, 'StatusChanged',
            cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE,
            signature='uu')

    e = q.expect('dbus-method-call',
            interface=cs.CONN, method='GetInterfaces',
            path=conn_object_path, handled=False)
    q.dbus_return(e.message, [cs.CONN_IFACE_REQUESTS], signature='as')

    e = q.expect('dbus-method-call',
            interface=cs.CONN, method='GetSelfHandle',
            path=conn_object_path, handled=False)
    q.dbus_return(e.message, allocate_handle("myself"), signature='u')

    e = q.expect('dbus-method-call',
            interface=cs.CONN, method='InspectHandles',
            args=[cs.HT_CONTACT, [allocate_handle("myself")]],
            path=conn_object_path, handled=False)
    q.dbus_return(e.message, ["myself"], signature='as')

    e = q.expect('dbus-method-call',
            interface=cs.PROPERTIES_IFACE, method='GetAll',
            args=[cs.CONN_IFACE_REQUESTS],
            path=conn_object_path, handled=False)
    q.dbus_return(e.message, {
        'Channels': dbus.Array(signature='(oa{sv})'),
        }, signature='a{sv}')

    # this secretly indicates that the TpConnection is ready
    e = q.expect('dbus-signal',
            interface=cs.ACCOUNT, signal='AccountPropertyChanged',
            path=account_path,
            args=[{'NormalizedName': 'myself'}])

    #e = q.expect('dbus-method-call', name='SetSelfCapabilities',
    #        path=conn_object_path)
    #assert e.caps == caps, e.caps

    # Check the requested presence is online
    properties = account.GetAll(cs.ACCOUNT,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert properties is not None
    assert properties.get('RequestedPresence') == requested_presence, \
        properties.get('RequestedPresence')

    new_channel = http_fixed_properties
    buddy_handle = allocate_handle("buddy")
    new_channel[cs.CHANNEL + '.TargetID'] = "buddy"
    new_channel[cs.CHANNEL + '.TargetHandle'] = buddy_handle

    channel_path = dbus.ObjectPath(conn_object_path + '/channel')
    q.dbus_emit(conn_object_path, cs.CONN_IFACE_REQUESTS, 'NewChannels',
            [(channel_path, new_channel)], signature='a(oa{sv})')
    q.dbus_emit(conn_object_path, cs.CONN, 'NewChannel',
            channel_path, cs.CHANNEL_TYPE_STREAM_TUBE,
            cs.HT_CONTACT, buddy_handle, False, signature='osuub')

    e = q.expect('dbus-method-call', method='HandleChannels')

if __name__ == '__main__':
    exec_test(test, {})
