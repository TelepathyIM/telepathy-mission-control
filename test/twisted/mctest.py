# Copyright (C) 2009 Nokia Corporation
# Copyright (C) 2009 Collabora Ltd.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
# 02110-1301 USA

"""
Infrastructure code for testing Mission Control
"""

import base64
import os
import sha
import sys

import constants as cs
import servicetest
import twisted
from twisted.internet import reactor

import dbus
import dbus.service

def make_mc(bus, event_func, params=None):
    default_params = {
        }

    if params:
        default_params.update(params)

    return servicetest.make_mc(bus, event_func, default_params)

def install_colourer():
    def red(s):
        return '\x1b[31m%s\x1b[0m' % s

    def green(s):
        return '\x1b[32m%s\x1b[0m' % s

    patterns = {
        'handled': green,
        'not handled': red,
        }

    class Colourer:
        def __init__(self, fh, patterns):
            self.fh = fh
            self.patterns = patterns

        def write(self, s):
            f = self.patterns.get(s, lambda x: x)
            self.fh.write(f(s))

    sys.stdout = Colourer(sys.stdout, patterns)
    return sys.stdout


def exec_test_deferred (fun, params, protocol=None, timeout=None,
        preload_mc=True):
    colourer = None

    if sys.stdout.isatty():
        colourer = install_colourer()

    queue = servicetest.IteratingEventQueue(timeout)
    queue.verbose = (
        os.environ.get('CHECK_TWISTED_VERBOSE', '') != ''
        or '-v' in sys.argv)

    bus = dbus.SessionBus()
    queue.attach_to_bus(bus)
    if preload_mc:
        mc = make_mc(bus, queue.append, params)
    else:
        mc = None
    error = None

    try:
        fun(queue, bus, mc)
    except Exception, e:
        import traceback
        traceback.print_exc()
        error = e

    try:
        am_props_iface = dbus.Interface(bus.get_object(cs.AM, cs.AM_PATH),
                cs.PROPERTIES_IFACE)
        am_props = am_props_iface.GetAll(cs.AM)

        for a in (am_props.get('ValidAccounts', []) +
                am_props.get('InvalidAccounts', [])):
            try:
                account_props_iface = dbus.Interface(bus.get_object(cs.AM, a),
                        cs.PROPERTIES_IFACE)
                account_props_iface.Set(cs.ACCOUNT, 'RequestedPresence',
                        (dbus.UInt32(cs.PRESENCE_TYPE_OFFLINE), 'offline',
                            ''))
            except dbus.DBusException, e:
                print >> sys.stderr, e

            try:
                account_props_iface = dbus.Interface(bus.get_object(cs.AM, a),
                        cs.PROPERTIES_IFACE)
                account_props_iface.Set(cs.ACCOUNT, 'Enabled', False)
            except dbus.DBusException, e:
                print >> sys.stderr, e

            try:
                account_iface = dbus.Interface(bus.get_object(cs.AM, a),
                        cs.ACCOUNT)
                account_iface.Remove()
            except dbus.DBusException, e:
                print >> sys.stderr, e

            servicetest.sync_dbus(bus, queue, am_props_iface)

    except dbus.DBusException, e:
        print >> sys.stderr, e

    queue.cleanup()

    if error is None:
      reactor.callLater(0, reactor.stop)
    else:
      # please ignore the POSIX behind the curtain
      os._exit(1)

    if colourer:
      sys.stdout = colourer.fh

def exec_test(fun, params=None, protocol=None, timeout=None, preload_mc=True):
  reactor.callWhenRunning (exec_test_deferred, fun, params, protocol, timeout,
          preload_mc)
  reactor.run()

class SimulatedConnection(object):

    def ensure_handle(self, type, identifier):
        if (type, identifier) in self._handles:
            return self._handles[(type, identifier)]

        self._last_handle += 1
        self._handles[(type, identifier)] = self._last_handle
        self._identifiers[(type, self._last_handle)] = identifier
        return self._last_handle

    def __init__(self, q, bus, cmname, protocol, account_part, self_ident,
            implement_get_interfaces=True, has_requests=True,
            has_presence=False, has_aliasing=False, has_avatars=False,
            avatars_persist=True, extra_interfaces=[]):
        self.q = q
        self.bus = bus

        self.bus_name = '.'.join([cs.tp_name_prefix, 'Connection',
                cmname, protocol.replace('-', '_'), account_part])
        self._bus_name_ref = dbus.service.BusName(self.bus_name, self.bus)
        self.object_path = '/' + self.bus_name.replace('.', '/')

        self._last_handle = 41
        self._handles = {}
        self._identifiers = {}
        self.status = cs.CONN_STATUS_DISCONNECTED
        self.reason = cs.CONN_STATUS_CONNECTING
        self.self_ident = self_ident
        self.self_handle = self.ensure_handle(cs.HT_CONTACT, self_ident)
        self.channels = []
        self.has_requests = has_requests
        self.has_presence = has_presence
        self.has_aliasing = has_aliasing
        self.has_avatars = has_avatars
        self.avatars_persist = avatars_persist
        self.extra_interfaces = extra_interfaces[:]

        if self.avatars_persist:
            self.avatar = dbus.Struct((dbus.ByteArray('my old avatar'),
                'text/plain'), signature='ays')
        else:
            self.avatar = None

        q.add_dbus_method_impl(self.Connect,
                path=self.object_path, interface=cs.CONN, method='Connect')
        q.add_dbus_method_impl(self.Disconnect,
                path=self.object_path, interface=cs.CONN, method='Disconnect')
        q.add_dbus_method_impl(self.GetSelfHandle,
                path=self.object_path,
                interface=cs.CONN, method='GetSelfHandle')
        q.add_dbus_method_impl(self.GetStatus,
                path=self.object_path, interface=cs.CONN, method='GetStatus')

        if implement_get_interfaces:
            q.add_dbus_method_impl(self.GetInterfaces,
                    path=self.object_path, interface=cs.CONN,
                    method='GetInterfaces')

        q.add_dbus_method_impl(self.InspectHandles,
                path=self.object_path, interface=cs.CONN,
                method='InspectHandles')
        q.add_dbus_method_impl(self.GetAll_Requests,
                path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CONN_IFACE_REQUESTS])

        if not has_requests:
            q.add_dbus_method_impl(self.ListChannels,
                    path=self.object_path, interface=cs.CONN,
                    method='ListChannels')

        if has_presence:
            q.add_dbus_method_impl(self.SetPresence, path=self.object_path,
                    interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                    method='SetPresence')
            q.add_dbus_method_impl(self.GetPresences, path=self.object_path,
                    interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                    method='GetPresences')
            q.add_dbus_method_impl(self.Get_SimplePresenceStatuses,
                    path=self.object_path, interface=cs.PROPERTIES_IFACE,
                    method='Get',
                    args=[cs.CONN_IFACE_SIMPLE_PRESENCE, 'Statuses'])
            q.add_dbus_method_impl(self.GetAll_SimplePresence,
                    path=self.object_path, interface=cs.PROPERTIES_IFACE,
                    method='GetAll',
                    args=[cs.CONN_IFACE_SIMPLE_PRESENCE])

        if has_aliasing:
            q.add_dbus_method_impl(self.GetAliasFlags,
                    path=self.object_path, interface=cs.CONN_IFACE_ALIASING,
                    method='GetAliasFlags',
                    args=[])

        if has_avatars:
            q.add_dbus_method_impl(self.GetAvatarRequirements,
                    path=self.object_path, interface=cs.CONN_IFACE_AVATARS,
                    method='GetAvatarRequirements', args=[])
            q.add_dbus_method_impl(self.GetAll_Avatars,
                    path=self.object_path, interface=cs.PROPERTIES_IFACE,
                    method='GetAll', args=[cs.CONN_IFACE_AVATARS])
            q.add_dbus_method_impl(self.GetKnownAvatarTokens,
                    path=self.object_path, interface=cs.CONN_IFACE_AVATARS,
                    method='GetKnownAvatarTokens')
            q.add_dbus_method_impl(self.SetAvatar,
                    path=self.object_path, interface=cs.CONN_IFACE_AVATARS,
                    method='SetAvatar')

        self.statuses = dbus.Dictionary({
            'available': (cs.PRESENCE_TYPE_AVAILABLE, True, True),
            'away': (cs.PRESENCE_TYPE_AWAY, True, True),
            'lunch': (cs.PRESENCE_TYPE_XA, True, True),
            'busy': (cs.PRESENCE_TYPE_BUSY, True, True),
            'phone': (cs.PRESENCE_TYPE_BUSY, True, True),
            'offline': (cs.PRESENCE_TYPE_OFFLINE, False, False),
            'error': (cs.PRESENCE_TYPE_ERROR, False, False),
            'unknown': (cs.PRESENCE_TYPE_UNKNOWN, False, False),
            }, signature='s(ubb)')
        self.presence = dbus.Struct((cs.PRESENCE_TYPE_OFFLINE, 'offline', ''),
                signature='uss')

    # not actually very relevant for MC so hard-code 0 for now
    def GetAliasFlags(self, e):
        self.q.dbus_return(e.message, 0, signature='u')

    # mostly for the UI's benefit; for now hard-code the requirements from XMPP
    def GetAvatarRequirements(self, e):
        self.q.dbus_return(e.message, ['image/jpeg'], 0, 0, 96, 96, 8192,
                signature='asqqqqu')
    def GetAll_Avatars(self, e):
        self.q.dbus_return(e.message, {
            'SupportedAvatarMIMETypes': ['image/jpeg'],
            'MinimumAvatarWidth': 0,
            'RecommendedAvatarWidth': 64,
            'MaximumAvatarWidth': 96,
            'MinimumAvatarHeight': 0,
            'RecommendedAvatarHeight': 64,
            'MaximumAvatarHeight': 96,
            'MaximumAvatarBytes': 8192,
            }, signature='a{sv}')

    def GetKnownAvatarTokens(self, e):
        ret = dbus.Dictionary(signature='us')

        # the user has an avatar already, if they persist; nobody else does
        if self.self_handle in e.args[0]:
            if self.avatar is not None:
                # we just stringify the avatar as the token
                # (also, empty avatar => no avatar => empty token)
                ret[self.self_handle] = str(self.avatar[0])

        self.q.dbus_return(e.message, ret, signature='a{us}')

    def SetAvatar(self, e):
        self.avatar = dbus.Struct(e.args, signature='ays')

        # we just stringify the avatar as the token
        self.q.dbus_return(e.message, str(self.avatar[0]), signature='s')
        self.q.dbus_emit(self.object_path, cs.CONN_IFACE_AVATARS,
                'AvatarRetrieved', self.self_handle, str(self.avatar[0]),
                self.avatar[0], self.avatar[1], signature='usays')

    def GetPresences(self, e):
        ret = dbus.Dictionary(signature='u(uss)')
        contacts = e.args[0]
        for contact in contacts:
            if contact == self.self_handle:
                ret[contact] = self.presence
            else:
                # stub - MC doesn't care
                ret[contact] = dbus.Struct(
                        (cs.PRESENCE_TYPE_UNKNOWN, 'unknown', ''),
                        signature='uss')
        self.q.dbus_return(e.message, ret, signature='a{u(uss)}')

    def SetPresence(self, e):
        if e.args[0] in self.statuses:
            presence = dbus.Struct((self.statuses[e.args[0]][0],
                    e.args[0], e.args[1]), signature='uss')

            old_presence = self.presence

            if presence != old_presence:
                self.presence = presence

                if self.status == cs.CONN_STATUS_CONNECTED:
                    self.q.dbus_emit(self.object_path,
                            cs.CONN_IFACE_SIMPLE_PRESENCE, 'PresencesChanged',
                            { self.self_handle : presence },
                            signature='a{u(uss)}')

            self.q.dbus_return(e.message, signature='')
        else:
            self.q.dbus_raise(cs.INVALID_ARGUMENT, 'Unknown status')

    def Get_SimplePresenceStatuses(self, e):
        self.q.dbus_return(e.message, self.statuses, signature='v')

    def GetAll_SimplePresence(self, e):
        self.q.dbus_return(e.message,
                {'Statuses': self.statuses}, signature='a{sv}')

    def GetInterfaces(self, e):
        interfaces = []

        if self.has_requests:
            interfaces.append(cs.CONN_IFACE_REQUESTS)

        if self.has_aliasing:
            interfaces.append(cs.CONN_IFACE_ALIASING)

        if self.has_avatars:
            interfaces.append(cs.CONN_IFACE_AVATARS)

        if self.has_presence:
            interfaces.append(cs.CONN_IFACE_SIMPLE_PRESENCE)

        if self.extra_interfaces:
            interfaces.extend(self.extra_interfaces)

        self.q.dbus_return(e.message, interfaces, signature='as')

    def Connect(self, e):
        self.StatusChanged(cs.CONN_STATUS_CONNECTING,
                cs.CONN_STATUS_REASON_REQUESTED)
        self.q.dbus_return(e.message, signature='')

    def Disconnect(self, e):
        self.StatusChanged(cs.CONN_STATUS_DISCONNECTED,
                cs.CONN_STATUS_REASON_REQUESTED)
        self.q.dbus_return(e.message, signature='')
        for c in self.channels:
            c.close()

    def InspectHandles(self, e):
        htype, hs = e.args
        ret = []

        for h in hs:
            if (htype, h) in self._identifiers:
                ret.append(self._identifiers[(htype, h)])
            else:
                self.q.dbus_raise(e.message, INVALID_HANDLE, str(h))
                return

        self.q.dbus_return(e.message, ret, signature='as')

    def GetStatus(self, e):
        self.q.dbus_return(e.message, self.status, signature='u')

    def StatusChanged(self, status, reason):
        self.status = status
        self.reason = reason
        self.q.dbus_emit(self.object_path, cs.CONN, 'StatusChanged',
                status, reason, signature='uu')
        if self.status == cs.CONN_STATUS_CONNECTED and self.has_presence:
            if self.presence[0] == cs.PRESENCE_TYPE_OFFLINE:
                self.presence = dbus.Struct((cs.PRESENCE_TYPE_AVAILABLE,
                    'available', ''), signature='uss')

            self.q.dbus_emit(self.object_path,
                    cs.CONN_IFACE_SIMPLE_PRESENCE, 'PresencesChanged',
                    { self.self_handle : self.presence },
                    signature='a{u(uss)}')

    def ListChannels(self, e):
        arr = dbus.Array(signature='(osuu)')

        for c in self.channels:
            arr.append(dbus.Struct(
                (c.object_path,
                 c.immutable[cs.CHANNEL + '.ChannelType'],
                 c.immutable.get(cs.CHANNEL + '.TargetHandleType', 0),
                 c.immutable.get(cs.CHANNEL + '.TargetHandle', 0)
                ), signature='osuu'))

        self.q.dbus_return(e.message, arr, signature='a(osuu)')

    def get_channel_details(self):
        return dbus.Array([(c.object_path, c.immutable)
            for c in self.channels], signature='(oa{sv})')

    def GetAll_Requests(self, e):
        if self.has_requests:
            self.q.dbus_return(e.message, {
                'Channels': self.get_channel_details(),
            }, signature='a{sv}')
        else:
            self.q.dbus_raise(e.message, cs.NOT_IMPLEMENTED, 'no Requests')

    def GetSelfHandle(self, e):
        self.q.dbus_return(e.message, self.self_handle, signature='u')

    def NewChannels(self, channels):
        for channel in channels:
            assert not channel.announced
            channel.announced = True
            self.channels.append(channel)

            self.q.dbus_emit(self.object_path, cs.CONN,
                    'NewChannel',
                    channel.object_path,
                    channel.immutable[cs.CHANNEL + '.ChannelType'],
                    channel.immutable.get(cs.CHANNEL + '.TargetHandleType', 0),
                    channel.immutable.get(cs.CHANNEL + '.TargetHandle', 0),
                    channel.immutable.get(cs.CHANNEL + '.Requested', False),
                    signature='osuub')

        if self.has_requests:
            self.q.dbus_emit(self.object_path, cs.CONN_IFACE_REQUESTS,
                    'NewChannels',
                    [(channel.object_path, channel.immutable)
                        for channel in channels],
                    signature='a(oa{sv})')

class SimulatedChannel(object):
    def __init__(self, conn, immutable, mutable={},
            destroyable=False, group=False):
        self.conn = conn
        self.q = conn.q
        self.bus = conn.bus
        self.object_path = conn.object_path + ('/_%x' % id(self))
        self.immutable = immutable
        self.properties = dbus.Dictionary({}, signature='sv')
        self.properties.update(immutable)
        self.properties.update(mutable)

        self.q.add_dbus_method_impl(self.GetAll,
                path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll')
        self.q.add_dbus_method_impl(self.Get,
                path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='Get')
        self.q.add_dbus_method_impl(self.Close,
                path=self.object_path,
                interface=cs.CHANNEL, method='Close')
        self.q.add_dbus_method_impl(self.GetInterfaces,
                path=self.object_path,
                interface=cs.CHANNEL, method='GetInterfaces')

        if destroyable:
            self.q.add_dbus_method_impl(self.Close,
                path=self.object_path,
                interface=cs.CHANNEL_IFACE_DESTROYABLE,
                method='Destroy')

        if group:
            self.q.add_dbus_method_impl(self.GetGroupFlags,
                path=self.object_path,
                interface=cs.CHANNEL_IFACE_GROUP,
                method='GetGroupFlags')
            self.q.add_dbus_method_impl(self.GetSelfHandle,
                path=self.object_path,
                interface=cs.CHANNEL_IFACE_GROUP,
                method='GetSelfHandle')
            self.q.add_dbus_method_impl(self.GetAllMembers,
                path=self.object_path,
                interface=cs.CHANNEL_IFACE_GROUP,
                method='GetAllMembers')
            self.q.add_dbus_method_impl(self.GetLocalPendingMembersWithInfo,
                path=self.object_path,
                interface=cs.CHANNEL_IFACE_GROUP,
                method='GetLocalPendingMembersWithInfo')
            self.properties[cs.CHANNEL_IFACE_GROUP + '.SelfHandle'] \
                    = self.conn.self_handle

        self.announced = False
        self.closed = False

    def GetGroupFlags(self, e):
        self.q.dbus_return(e.message, 0, signature='u')

    def GetSelfHandle(self, e):
        self.q.dbus_return(e.message,
                self.properties[cs.CHANNEL_IFACE_GROUP + '.SelfHandle'],
                signature='u')

    def GetAllMembers(self, e):
        # stub
        self.q.dbus_return(e.message,
                [self.properties[cs.CHANNEL_IFACE_GROUP + '.SelfHandle']],
                [], [],
                signature='auauau')

    def GetLocalPendingMembersWithInfo(self, e):
        # stub
        self.q.dbus_return(e.message, [], signature='a(uuus)')

    def announce(self):
        self.conn.NewChannels([self])

    def Close(self, e):
        if not self.closed:
            self.close()
        self.q.dbus_return(e.message, signature='')

    def close(self):
        assert self.announced
        assert not self.closed
        self.closed = True
        self.conn.channels.remove(self)
        self.q.dbus_emit(self.object_path, cs.CHANNEL, 'Closed', signature='')
        self.q.dbus_emit(self.conn.object_path, cs.CONN_IFACE_REQUESTS,
                'ChannelClosed', self.object_path, signature='o')

    def GetInterfaces(self, e):
        self.q.dbus_return(e.message,
                self.properties[cs.CHANNEL + '.Interfaces'], signature='as')

    def GetAll(self, e):
        iface = e.args[0] + '.'

        ret = dbus.Dictionary({}, signature='sv')
        for k in self.properties:
            if k.startswith(iface):
                tail = k[len(iface):]
                if '.' not in tail:
                    ret[tail] = self.properties[k]
        assert ret  # die on attempts to get unimplemented interfaces
        self.q.dbus_return(e.message, ret, signature='a{sv}')

    def Get(self, e):
        prop = e.args[0] + '.' + e.args[1]
        self.q.dbus_return(e.message, self.properties[prop],
                signature='v')

def aasv(x):
    return dbus.Array([dbus.Dictionary(d, signature='sv') for d in x],
            signature='a{sv}')

class SimulatedClient(object):
    def __init__(self, q, bus, clientname,
            observe=[], approve=[], handle=[], bypass_approval=False,
            request_notification=True, implement_get_interfaces=True):
        self.q = q
        self.bus = bus
        self.bus_name = '.'.join([cs.tp_name_prefix, 'Client', clientname])
        self._bus_name_ref = dbus.service.BusName(self.bus_name, self.bus)
        self.object_path = '/' + self.bus_name.replace('.', '/')
        self.observe = aasv(observe)
        self.approve = aasv(approve)
        self.handle = aasv(handle)
        self.bypass_approval = bool(bypass_approval)
        self.request_notification = bool(request_notification)
        self.handled_channels = dbus.Array([], signature='o')

        if implement_get_interfaces:
            q.add_dbus_method_impl(self.Get_Interfaces,
                    path=self.object_path, interface=cs.PROPERTIES_IFACE,
                    method='Get', args=[cs.CLIENT, 'Interfaces'])
            q.add_dbus_method_impl(self.GetAll_Client,
                    path=self.object_path,
                    interface=cs.PROPERTIES_IFACE, method='GetAll',
                    args=[cs.CLIENT])

        q.add_dbus_method_impl(self.Get_ObserverChannelFilter,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.OBSERVER, 'ObserverChannelFilter'])
        q.add_dbus_method_impl(self.GetAll_Observer,
                path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.OBSERVER])

        q.add_dbus_method_impl(self.Get_ApproverChannelFilter,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.APPROVER, 'ApproverChannelFilter'])
        q.add_dbus_method_impl(self.GetAll_Approver,
                path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.APPROVER])

        q.add_dbus_method_impl(self.Get_HandlerChannelFilter,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.HANDLER, 'HandlerChannelFilter'])
        q.add_dbus_method_impl(self.Get_HandledChannels,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.HANDLER, 'HandledChannels'])
        q.add_dbus_method_impl(self.Get_BypassApproval,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.HANDLER, 'BypassApproval'])
        q.add_dbus_method_impl(self.GetAll_Handler,
                path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.HANDLER])

    def release_name(self):
        del self._bus_name_ref

    def get_interfaces(self):
        ret = dbus.Array([], signature='s', variant_level=1)

        if self.observe:
            ret.append(cs.OBSERVER)

        if self.approve:
            ret.append(cs.APPROVER)

        if self.handle:
            ret.append(cs.HANDLER)

        if self.request_notification:
            ret.append(cs.CLIENT_IFACE_REQUESTS)

        return ret

    def Get_Interfaces(self, e):
        self.q.dbus_return(e.message, self.get_interfaces(), signature='v',
                bus=self.bus)

    def GetAll_Client(self, e):
        self.q.dbus_return(e.message, {'Interfaces': self.get_interfaces()},
                signature='a{sv}', bus=self.bus)

    def GetAll_Observer(self, e):
        assert self.observe
        self.q.dbus_return(e.message, {'ObserverChannelFilter': self.observe},
                signature='a{sv}', bus=self.bus)

    def Get_ObserverChannelFilter(self, e):
        assert self.observe
        self.q.dbus_return(e.message, self.observe, signature='v',
                bus=self.bus)

    def GetAll_Approver(self, e):
        assert self.approve
        self.q.dbus_return(e.message, {'ApproverChannelFilter': self.approve},
                signature='a{sv}', bus=self.bus)

    def Get_ApproverChannelFilter(self, e):
        assert self.approve
        self.q.dbus_return(e.message, self.approve, signature='v',
                bus=self.bus)

    def GetAll_Handler(self, e):
        assert self.handle
        self.q.dbus_return(e.message, {
            'HandlerChannelFilter': self.handle,
            'BypassApproval': self.bypass_approval,
            'HandledChannels': self.handled_channels,
            },
                signature='a{sv}', bus=self.bus)

    def Get_HandledChannels(self, e):
        self.q.dbus_return(e.message, self.handled_channels, signature='v',
                bus=self.bus)

    def Get_HandlerChannelFilter(self, e):
        assert self.handle
        self.q.dbus_return(e.message, self.handle, signature='v',
                bus=self.bus)

    def Get_BypassApproval(self, e):
        assert self.handle
        self.q.dbus_return(e.message, self.bypass_approval, signature='v',
                bus=self.bus)

def create_fakecm_account(q, bus, mc, params):
    """Create a fake connection manager and an account that uses it.
    """
    cm_name_ref = dbus.service.BusName(
            cs.tp_name_prefix + '.ConnectionManager.fakecm', bus=bus)

    # Get the AccountManager interface
    account_manager = bus.get_object(cs.AM, cs.AM_PATH)
    account_manager_iface = dbus.Interface(account_manager, cs.AM)

    # Create an account
    servicetest.call_async(q, account_manager_iface, 'CreateAccount',
            'fakecm', # Connection_Manager
            'fakeprotocol', # Protocol
            'fakeaccount', #Display_Name
            params, # Parameters
            {}, # Properties
            )
    # The spec has no order guarantee here.
    # FIXME: MC ought to also introspect the CM and find out that the params
    # are in fact sufficient

    a_signal, am_signal, ret = q.expect_many(
            servicetest.EventPattern('dbus-signal',
                signal='AccountPropertyChanged', interface=cs.ACCOUNT,
                predicate=(lambda e: 'Valid' in e.args[0])),
            servicetest.EventPattern('dbus-signal', path=cs.AM_PATH,
                signal='AccountValidityChanged', interface=cs.AM),
            servicetest.EventPattern('dbus-return', method='CreateAccount'),
            )
    account_path = ret.value[0]
    assert am_signal.args == [account_path, True], am_signal.args
    assert a_signal.args[0]['Valid'] == True, a_signal.args

    assert account_path is not None

    # Get the Account interface
    account = bus.get_object(
        cs.tp_name_prefix + '.AccountManager',
        account_path)
    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)
    # Introspect Account for debugging purpose
    account_introspected = account.Introspect(
            dbus_interface=cs.INTROSPECTABLE_IFACE)
    #print account_introspected

    return (cm_name_ref, account)

def enable_fakecm_account(q, bus, mc, account, expected_params,
        has_requests=True, has_presence=False, has_aliasing=False,
        has_avatars=False, avatars_persist=True,
        extra_interfaces=[],
        requested_presence=(2, 'available', ''),
        expect_before_connect=[], expect_after_connect=[]):
    # Enable the account
    account.Set(cs.ACCOUNT, 'Enabled', True,
            dbus_interface=cs.PROPERTIES_IFACE)

    if requested_presence is not None:
        requested_presence = dbus.Struct(
                (dbus.UInt32(requested_presence[0]),) +
                tuple(requested_presence[1:]),
                signature='uss')
        account.Set(cs.ACCOUNT,
                'RequestedPresence', requested_presence,
                dbus_interface=cs.PROPERTIES_IFACE)

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=['fakeprotocol', expected_params],
            destination=cs.tp_name_prefix + '.ConnectionManager.fakecm',
            path=cs.tp_path_prefix + '/ConnectionManager/fakecm',
            interface=cs.tp_name_prefix + '.ConnectionManager',
            handled=False)

    conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol', '_',
            'myself', has_requests=has_requests, has_presence=has_presence,
            has_aliasing=has_aliasing, has_avatars=has_avatars,
            avatars_persist=avatars_persist, extra_interfaces=extra_interfaces)

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    if expect_before_connect:
        events = list(q.expect_many(*expect_before_connect))
    else:
        events = []

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    expect_after_connect = list(expect_after_connect)

    if has_requests:
        expect_after_connect.append(
                servicetest.EventPattern('dbus-method-call',
                    interface=cs.PROPERTIES_IFACE, method='GetAll',
                    args=[cs.CONN_IFACE_REQUESTS],
                    path=conn.object_path, handled=True))
    else:
        expect_after_connect.append(
                servicetest.EventPattern('dbus-method-call',
                    interface=cs.CONN, method='ListChannels', args=[],
                    path=conn.object_path, handled=True))

    events = events + list(q.expect_many(*expect_after_connect))

    del events[-1]

    if events:
        return (conn,) + tuple(events)

    return conn

def expect_client_setup(q, clients, got_interfaces_already=False):
    patterns = []

    def is_client_setup(e):
        if e.method == 'Get' and e.args == [cs.CLIENT, 'Interfaces']:
            return True
        if e.method == 'GetAll' and e.args == [cs.CLIENT]:
            return True
        return False

    def is_approver_setup(e):
        if e.method == 'Get' and \
                e.args == [cs.APPROVER, 'ApproverChannelFilter']:
            return True
        if e.method == 'GetAll' and e.args == [cs.APPROVER]:
            return True
        return False

    def is_observer_setup(e):
        if e.method == 'Get' and \
                e.args == [cs.OBSERVER, 'ObserverChannelFilter']:
            return True
        if e.method == 'GetAll' and e.args == [cs.OBSERVER]:
            return True
        return False

    def is_handler_setup(e):
        if e.method == 'Get' and \
                e.args == [cs.HANDLER, 'HandlerChannelFilter']:
            return True
        if e.method == 'GetAll' and e.args == [cs.HANDLER]:
            return True
        return False

    for client in clients:
        if not got_interfaces_already:
            patterns.append(servicetest.EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE,
                path=client.object_path, handled=True,
                predicate=is_client_setup))

        if client.observe:
            patterns.append(servicetest.EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE,
                path=client.object_path, handled=True,
                predicate=is_observer_setup))

        if client.approve:
            patterns.append(servicetest.EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE,
                path=client.object_path, handled=True,
                predicate=is_approver_setup))

        if client.handle:
            patterns.append(servicetest.EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE,
                path=client.object_path, predicate=is_handler_setup))

    q.expect_many(*patterns)
