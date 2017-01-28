import dbus
from dbus.service import Object, method, signal

import sys

class FakeConnectivity(object):
    NM_BUS_NAME = 'org.freedesktop.NetworkManager'
    NM_PATH = '/org/freedesktop/NetworkManager'
    NM_PATH_SETTINGS = NM_PATH + '/Settings'
    NM_INTERFACE = NM_BUS_NAME
    NM_INTERFACE_SETTINGS = NM_INTERFACE + '.Settings'

    NM_STATE_UNKNOWN          = 0
    NM_STATE_ASLEEP           = 10
    NM_STATE_DISCONNECTED     = 20
    NM_STATE_DISCONNECTING    = 30
    NM_STATE_CONNECTING       = 40
    NM_STATE_CONNECTED_LOCAL  = 50
    NM_STATE_CONNECTED_SITE   = 60
    NM_STATE_CONNECTED_GLOBAL = 70

    # Our fake GNetworkMonitor uses the ConnMan 0.79 D-Bus API - we don't
    # have any special support for ConnMan any more, but it's as good an
    # API as any. The important thing is that it's not NM, because we *do*
    # have a bit of special support for that.
    CONNMAN_BUS_NAME = 'net.connman'
    CONNMAN_PATH = '/'
    CONNMAN_INTERFACE = 'net.connman.Manager'

    CONNMAN_OFFLINE = "offline"
    CONNMAN_ONLINE = "online"

    def __init__(self, q, bus, initially_online):
        self.q = q
        self.bus = bus

        self.nm_name_ref = dbus.service.BusName(self.NM_BUS_NAME, bus)
        self.connman_name_ref = dbus.service.BusName(self.CONNMAN_BUS_NAME, bus)

        q.add_dbus_method_impl(self.NM_GetPermissions,
            path=self.NM_PATH, interface=self.NM_INTERFACE,
            method='GetPermissions')
        q.add_dbus_method_impl(self.NM_Get,
            path=self.NM_PATH, interface=dbus.PROPERTIES_IFACE, method='Get',
            predicate=lambda e: e.args[0] == self.NM_INTERFACE)
        q.add_dbus_method_impl(self.NM_GetAll,
            path=self.NM_PATH, interface=dbus.PROPERTIES_IFACE, method='GetAll',
            predicate=lambda e: e.args[0] == self.NM_INTERFACE)
        q.add_dbus_method_impl(self.NM_GetDevices,
            path=self.NM_PATH, interface=self.NM_INTERFACE, method='GetDevices')

        q.add_dbus_method_impl(self.NM_Settings_Get,
            path=self.NM_PATH_SETTINGS, interface=dbus.PROPERTIES_IFACE, method='Get',
            predicate=lambda e: e.args[0] == self.NM_INTERFACE_SETTINGS)
        q.add_dbus_method_impl(self.NM_Settings_GetAll,
            path=self.NM_PATH_SETTINGS, interface=dbus.PROPERTIES_IFACE, method='GetAll',
            predicate=lambda e: e.args[0] == self.NM_INTERFACE_SETTINGS)

        q.add_dbus_method_impl(self.ConnMan_GetProperties,
            path=self.CONNMAN_PATH, interface=self.CONNMAN_INTERFACE,
            method='GetProperties')

        self.change_state(initially_online)

    def NM_GetPermissions(self, e):
        permissions = {
            self.NM_INTERFACE + '.network-control': 'yes',
            self.NM_INTERFACE + '.enable-disable-wwan': 'yes',
            self.NM_INTERFACE + '.settings.modify.own': 'yes',
            self.NM_INTERFACE + '.wifi.share.protected': 'yes',
            self.NM_INTERFACE + '.wifi.share.open': 'yes',
            self.NM_INTERFACE + '.enable-disable-network': 'yes',
            self.NM_INTERFACE + '.enable-disable-wimax': 'yes',
            self.NM_INTERFACE + '.sleep-wake': 'no',
            self.NM_INTERFACE + '.enable-disable-wifi': 'yes',
            self.NM_INTERFACE + '.settings.modify.system': 'auth',
            self.NM_INTERFACE + '.settings.modify.hostname': 'auth',
        }
        self.q.dbus_return(e.message, permissions, signature='a{ss}')

    def nm_props(self):
        return {
            'NetworkingEnabled': True,
            'WirelessEnabled': True,
            'WirelessHardwareEnabled': True,
            'WwanEnabled': False,
            'WwanHardwareEnabled': True,
            'WimaxEnabled': True,
            'WimaxHardwareEnabled': True,
            'ActiveConnections': dbus.Array([], signature='o'),
            'Version': '0.9.0',
            'State': dbus.UInt32(self.nm_state),
        }

    def NM_Get(self, e):
        self.q.dbus_return(e.message, self.nm_props()[e.args[1]], signature='v')

    def NM_GetAll(self, e):
        self.q.dbus_return(e.message, self.nm_props(), signature='a{sv}')

    def NM_GetDevices(self, e):
        self.q.dbus_return(e.message, [], signature='ao')

    def nm_settings_props(self):
        return {
            'CanModify': False,
            'Hostname': 'localhost',
            'Connections': dbus.Array([], signature='o'),
        }

    def NM_Settings_Get(self, e):
        self.q.dbus_return(e.message, self.nm_settings_props()[e.args[1]], signature='v')

    def NM_Settings_GetAll(self, e):
        self.q.dbus_return(e.message, self.nm_settings_props(), signature='a{sv}')

    def Connman_props(self):
        return {
            'OfflineMode': False,
            'SessionMode': False,
            'State': self.connman_state,
        }

    def ConnMan_GetProperties(self, e):
        self.q.dbus_return(e.message, self.Connman_props(), signature='a{sv}')

    def change_state(self, online, indeterminate=False):
        if indeterminate:
            self.nm_state = self.NM_STATE_DISCONNECTING
            # keep the previous "ConnMan" (GNetworkMonitor) state;
            # any other GNetworkMonitor would probably do the same
            # while trying to disconnect, because e.g. netlink will say the
            # interface is still up
        elif online:
            self.nm_state = self.NM_STATE_CONNECTED_GLOBAL
            self.connman_state = self.CONNMAN_ONLINE
        else:
            self.nm_state = self.NM_STATE_DISCONNECTED
            self.connman_state = self.CONNMAN_OFFLINE

        self.q.dbus_emit(self.NM_PATH, self.NM_INTERFACE,
            'PropertiesChanged', { "State": dbus.UInt32(self.nm_state) },
            signature='a{sv}')
        self.q.dbus_emit(self.NM_PATH, self.NM_INTERFACE,
            'StateChanged', self.nm_state,
            signature='u')

        if not indeterminate:
            self.q.dbus_emit(self.CONNMAN_PATH, self.CONNMAN_INTERFACE,
                'PropertyChanged', "State", self.connman_state, signature='sv')

    def go_online(self):
        self.change_state(True)

    def go_offline(self):
        self.change_state(False)

    def go_indeterminate(self):
        self.change_state(None, True)
