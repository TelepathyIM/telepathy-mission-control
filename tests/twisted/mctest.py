# python sucks! vim: set fileencoding=utf-8 :
# Copyright © 2009–2010 Nokia Corporation
# Copyright © 2009–2010 Collabora Ltd.
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
import sys

import constants as cs
import servicetest
import twisted
from twisted.internet import reactor

import dbus
import dbus.service

from fakeaccountsservice import FakeAccountsService
from fakeconnectivity import FakeConnectivity

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

class MC(dbus.proxies.ProxyObject):
    def __init__(self, queue, bus, wait_for_names=True, initially_online=True):
        """
        Arguments:

          queue: an event queue
          bus: a D-Bus connection
          wait_for_names: if True, the constructor will wait for MC to have
                          been service-activated before returning. if False,
                          the caller may later call wait_for_names().
          initially_online: whether the fake implementations of Network Manager
                            and ConnMan should claim to be online or offline.
        """
        dbus.proxies.ProxyObject.__init__(self,
            conn=bus,
            bus_name=cs.MC,
            object_path=cs.MC_PATH,
            follow_name_owner_changes=True)

        self.connectivity = FakeConnectivity(queue, bus, initially_online)
        self.q = queue
        self.bus = bus

        if wait_for_names:
            self.wait_for_names()

    def wait_for_names(self, *also_expect):
        """
        Waits for MC to have claimed all its bus names, along with the
        (optional) EventPatterns passed as arguments.
        """

        patterns = [
            servicetest.EventPattern('dbus-signal', signal='NameOwnerChanged',
                predicate=lambda e, name=name: e.args[0] == name and e.args[2] != '')
            for name in [cs.AM, cs.CD, cs.MC]
            if not self.bus.name_has_owner(name)]

        patterns.extend(also_expect)

        events = self.q.expect_many(*patterns)

        return events[3:]

def exec_test_deferred (fun, params, protocol=None, timeout=None,
        preload_mc=True, initially_online=True, use_fake_accounts_service=True,
        pass_kwargs=False):
    colourer = None

    if sys.stdout.isatty():
        colourer = install_colourer()

    queue = servicetest.IteratingEventQueue(timeout)
    queue.verbose = (
        os.environ.get('CHECK_TWISTED_VERBOSE', '') != ''
        or '-v' in sys.argv)

    bus = dbus.SessionBus()
    queue.attach_to_bus(bus)
    error = None

    if preload_mc:
        try:
            mc = MC(queue, bus, initially_online=initially_online)
        except Exception, e:
            import traceback
            traceback.print_exc()
            os._exit(1)
    else:
        mc = None

    if use_fake_accounts_service:
        fake_accounts_service = FakeAccountsService(queue, bus)

        if preload_mc:
            queue.expect('dbus-signal',
                    path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                    interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                    signal='Active')
    else:
        fake_accounts_service = None

    if pass_kwargs:
        kwargs=dict(fake_accounts_service=fake_accounts_service)
    else:
        kwargs=dict()

    try:
        fun(queue, bus, mc, **kwargs)
    except Exception, e:
        import traceback
        traceback.print_exc()
        error = e
        queue.verbose = False

    # Clean up any accounts which are left over from the test.
    try:
        am = AccountManager(bus)
        am_props = am.Properties.GetAll(cs.AM)

        for a in (am_props.get('UsableAccounts', []) +
                am_props.get('UnusableAccounts', [])):
            account = Account(bus, a)

            try:
                account.Properties.Set(cs.ACCOUNT, 'RequestedPresence',
                        (dbus.UInt32(cs.PRESENCE_OFFLINE), 'offline',
                            ''))
            except dbus.DBusException, e:
                print >> sys.stderr, "Can't set %s offline: %s" % (a, e)

            try:
                account.Properties.Set(cs.ACCOUNT, 'Enabled', False)
            except dbus.DBusException, e:
                print >> sys.stderr, "Can't disable %s: %s" % (a, e)

            try:
                account.Remove()
            except dbus.DBusException, e:
                print >> sys.stderr, "Can't remove %s: %s" % (a, e)

            servicetest.sync_dbus(bus, queue, am)

    except dbus.DBusException, e:
        print >> sys.stderr, "Couldn't clean up left-over accounts: %s" % e

    except Exception, e:
        import traceback
        traceback.print_exc()
        error = e

    queue.cleanup()

    if error is None:
      reactor.callLater(0, reactor.stop)
    else:
      # please ignore the POSIX behind the curtain
      os._exit(1)

    if colourer:
      sys.stdout = colourer.fh

def exec_test(fun, params=None, protocol=None, timeout=None,
              preload_mc=True, initially_online=True,
              use_fake_accounts_service=True, pass_kwargs=False):
  reactor.callWhenRunning (exec_test_deferred, fun, params, protocol, timeout,
          preload_mc, initially_online, use_fake_accounts_service, pass_kwargs)
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
            self_alias=None,
            implement_get_channels=True,
            has_presence=False, has_aliasing=False, has_avatars=False,
            avatars_persist=True, extra_interfaces=[], has_hidden=False,
            implement_get_aliases=True, initial_avatar=None,
            server_delays_avatar=False):
        self.q = q
        self.bus = bus

        if self_alias is None:
            self_alias = self_ident

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
        self.self_alias = self_alias
        self.self_handle = self.ensure_handle(cs.HT_CONTACT, self_ident)
        self.channels = []
        self.has_presence = has_presence
        self.has_aliasing = has_aliasing
        self.has_avatars = has_avatars
        self.avatars_persist = avatars_persist
        self.extra_interfaces = extra_interfaces[:]
        self.avatar_delayed = server_delays_avatar

        self.interfaces = []

        if self.has_aliasing:
            self.interfaces.append(cs.CONN_IFACE_ALIASING)
        if self.has_avatars:
            self.interfaces.append(cs.CONN_IFACE_AVATARS)
        if self.has_presence:
            self.interfaces.append(cs.CONN_IFACE_PRESENCE)
        if self.extra_interfaces:
            self.interfaces.extend(self.extra_interfaces)

        if initial_avatar is not None:
            self.avatar = initial_avatar
        elif self.avatars_persist:
            self.avatar = dbus.Struct((dbus.ByteArray('my old avatar'),
                    'text/plain'), signature='ays')
        else:
            self.avatar = None

        q.add_dbus_method_impl(self.Connect,
                path=self.object_path, interface=cs.CONN, method='Connect')
        q.add_dbus_method_impl(self.Disconnect,
                path=self.object_path, interface=cs.CONN, method='Disconnect')

        q.add_dbus_method_impl(self.GetAll_Connection,
                path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CONN])

        q.add_dbus_method_impl(self.Get_Interfaces,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.CONN, 'Interfaces'])

        if implement_get_channels:
            q.add_dbus_method_impl(self.GetAll_Requests,
                    path=self.object_path,
                    interface=cs.PROPERTIES_IFACE, method='GetAll',
                    args=[cs.CONN_IFACE_REQUESTS])

        q.add_dbus_method_impl(self.GetContactAttributes,
                path=self.object_path,
                interface=cs.CONN, method='GetContactAttributes')
        q.add_dbus_method_impl(self.GetContactByID,
                path=self.object_path,
                interface=cs.CONN, method='GetContactByID')

        if has_presence:
            q.add_dbus_method_impl(self.SetPresence, path=self.object_path,
                    interface=cs.CONN_IFACE_PRESENCE,
                    method='SetPresence')
            q.add_dbus_method_impl(self.Get_PresenceStatuses,
                    path=self.object_path, interface=cs.PROPERTIES_IFACE,
                    method='Get',
                    args=[cs.CONN_IFACE_PRESENCE, 'Statuses'])
            q.add_dbus_method_impl(self.GetAll_Presence,
                    path=self.object_path, interface=cs.PROPERTIES_IFACE,
                    method='GetAll',
                    args=[cs.CONN_IFACE_PRESENCE])

        if has_avatars:
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
            'available': (cs.PRESENCE_AVAILABLE, True, True),
            'away': (cs.PRESENCE_AWAY, True, True),
            'lunch': (cs.PRESENCE_EXTENDED_AWAY, True, True),
            'busy': (cs.PRESENCE_BUSY, True, True),
            'phone': (cs.PRESENCE_BUSY, True, True),
            'offline': (cs.PRESENCE_OFFLINE, False, False),
            'error': (cs.PRESENCE_ERROR, False, False),
            'unknown': (cs.PRESENCE_UNKNOWN, False, False),
            }, signature='s(ubb)')

        if has_hidden:
            self.statuses['hidden'] = (cs.PRESENCE_HIDDEN, True, True)

        # "dbus.UInt32" to work around
        # https://bugs.freedesktop.org/show_bug.cgi?id=69967
        self.presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_OFFLINE),
            'offline', ''), signature='uss')

    def change_self_ident(self, ident):
        self.self_ident = ident
        self.self_handle = self.ensure_handle(cs.HT_CONTACT, ident)
        self.q.dbus_emit(self.object_path, cs.CONN, 'SelfContactChanged',
                self.self_handle, ident, signature='us')

    def change_self_alias(self, alias):
        self.self_alias = alias
        self.q.dbus_emit(self.object_path, cs.CONN_IFACE_ALIASING,
                'AliasesChanged', [(self.self_handle, self.self_alias)],
                signature='a(us)')

    def release_name(self):
        del self._bus_name_ref

    def GetAll_Connection(self, e):
        self.q.dbus_return(e.message, {
            'Interfaces': dbus.Array(self.interfaces, signature='s'),
            'SelfHandle': dbus.UInt32(self.self_handle),
            'SelfID': self.self_ident,
            'Status': dbus.UInt32(self.status),
            'HasImmortalHandles': dbus.Boolean(True),
            }, signature='a{sv}')

    def forget_avatar(self):
        self.avatar = (dbus.ByteArray(''), '')
        self.avatar_delayed = False

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
            if self.avatar is None:
                # GetKnownAvatarTokens has the special case that "where
                # the avatar does not persist between connections, a CM
                # should omit the self handle from the returned map until
                # an avatar is explicitly set or cleared". We'd have been
                # better off with a more explicit design, but it's too
                # late now...
                assert not self.avatars_persist
            else:
                # "a CM must always have the tokens for the self handle
                # if one is set (even if it is set to no avatar)"
                # so behave as though we'd done a network round-trip to
                # check what our token was, and found our configured
                # token
                if self.avatar_delayed:
                    self.q.dbus_emit(self.object_path, cs.CONN_IFACE_AVATARS,
                            'AvatarUpdated', self.self_handle,
                            str(self.avatar[0]), signature='us')

                # we just stringify the avatar as the token
                # (also, empty avatar => no avatar => empty token)
                ret[self.self_handle] = str(self.avatar[0])

        self.q.dbus_return(e.message, ret, signature='a{us}')

    def SetAvatar(self, e):
        self.avatar = dbus.Struct(e.args, signature='ays')
        self.avatar_delayed = False

        # we just stringify the avatar as the token
        self.q.dbus_return(e.message, str(self.avatar[0]), signature='s')
        self.q.dbus_emit(self.object_path, cs.CONN_IFACE_AVATARS,
                'AvatarRetrieved', self.self_handle, str(self.avatar[0]),
                self.avatar[0], self.avatar[1], signature='usays')

    def SetPresence(self, e):
        if e.args[0] in self.statuses:
            # "dbus.UInt32" to work around
            # https://bugs.freedesktop.org/show_bug.cgi?id=69967
            presence = dbus.Struct((dbus.UInt32(self.statuses[e.args[0]][0]),
                    e.args[0], e.args[1]), signature='uss')

            old_presence = self.presence

            if presence != old_presence:
                self.presence = presence

                self.q.dbus_emit(self.object_path,
                        cs.CONN_IFACE_PRESENCE, 'PresencesChanged',
                        { self.self_handle : presence },
                        signature='a{u(uss)}')

            self.q.dbus_return(e.message, signature='')
        else:
            self.q.dbus_raise(cs.INVALID_ARGUMENT, 'Unknown status')

    def Get_PresenceStatuses(self, e):
        self.q.dbus_return(e.message, self.statuses, signature='v')

    def GetAll_Presence(self, e):
        self.q.dbus_return(e.message,
                {'Statuses': self.statuses}, signature='a{sv}')

    def Get_Interfaces(self, e):
        self.q.dbus_return(e.message,
                dbus.Array(self.interfaces, signature='s'),
                signature='v')

    def Connect(self, e):
        self.StatusChanged(cs.CONN_STATUS_CONNECTING,
                cs.CSR_REQUESTED)
        self.q.dbus_return(e.message, signature='')

    def Disconnect(self, e):
        self.StatusChanged(cs.CONN_STATUS_DISCONNECTED,
                cs.CSR_REQUESTED)
        self.q.dbus_return(e.message, signature='')
        for c in self.channels:
            c.close()

    def inspect_handles(self, handles, htype=cs.HT_CONTACT):
        ret = []

        for h in handles:
            if (htype, h) in self._identifiers:
                ret.append(self._identifiers[(htype, h)])
            else:
                raise Exception(h)

        return ret

    def ConnectionError(self, error, details):
        self.q.dbus_emit(self.object_path, cs.CONN, 'ConnectionError',
                error, details, signature='sa{sv}')

    def StatusChanged(self, status, reason):
        self.status = status
        self.reason = reason
        self.q.dbus_emit(self.object_path, cs.CONN, 'StatusChanged',
                status, reason, signature='uu')
        if self.status == cs.CONN_STATUS_CONNECTED and self.has_presence:
            if self.presence[0] == cs.PRESENCE_OFFLINE:
                # "dbus.UInt32" to work around
                # https://bugs.freedesktop.org/show_bug.cgi?id=69967
                self.presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_AVAILABLE),
                    'available', ''), signature='uss')

            self.q.dbus_emit(self.object_path,
                    cs.CONN_IFACE_PRESENCE, 'PresencesChanged',
                    { self.self_handle : self.presence },
                    signature='a{u(uss)}')

    def get_channel_details(self):
        return dbus.Array([(c.object_path, c.immutable)
            for c in self.channels], signature='(oa{sv})')

    def GetAll_Requests(self, e):
        self.q.dbus_return(e.message, {
            'Channels': self.get_channel_details(),
        }, signature='a{sv}')

    def NewChannel(self, channel):
        assert not channel.announced
        channel.announced = True
        self.channels.append(channel)

        self.q.dbus_emit(self.object_path, cs.CONN_IFACE_REQUESTS,
                'NewChannel',
                channel.object_path, channel.immutable,
                signature='oa{sv}')

    def get_contact_attributes(self, h, ifaces):
        id = self.inspect_handles([h])[0]
        ifaces = set(ifaces).intersection(
                self.get_contact_attribute_interfaces())

        ret = dbus.Dictionary({}, signature='sv')
        ret[cs.ATTR_CONTACT_ID] = id

        if cs.CONN_IFACE_ALIASING in ifaces:
            if h == self.self_handle:
                ret[cs.ATTR_ALIAS] = self.self_alias
            else:
                ret[cs.ATTR_ALIAS] = id

        if cs.CONN_IFACE_AVATARS in ifaces:
            if h == self.self_handle:
                if self.avatar is not None and not self.avatar_delayed:
                    # We just stringify the avatar as the token
                    # (also, empty avatar => no avatar => empty token).
                    # This doesn't have the same special case that
                    # GetKnownAvatarTokens does - if we don't know the
                    # token yet, we don't wait.
                    ret[cs.ATTR_AVATAR_TOKEN] = str(self.avatar[0])

        if cs.CONN_IFACE_PRESENCE in ifaces:
            if h == self.self_handle:
                ret[cs.ATTR_PRESENCE] = self.presence
            else:
                # stub - MC doesn't care
                # "dbus.UInt32" to work around
                # https://bugs.freedesktop.org/show_bug.cgi?id=69967
                ret[cs.ATTR_PRESENCE] = (dbus.UInt32(cs.PRESENCE_UNKNOWN),
                        'unknown', '')

        return ret

    def get_contact_attribute_interfaces(self):
        return set(self.interfaces).intersection(set([
            cs.CONN_IFACE_ALIASING,
            cs.CONN_IFACE_AVATARS,
            cs.CONN_IFACE_PRESENCE,
            ]))

    def GetContactAttributes(self, e):
        ret = dbus.Dictionary({}, signature='ua{sv}')

        try:
            for h in e.args[0]:
                ret[dbus.UInt32(h)] = self.get_contact_attributes(h, e.args[1])

            self.q.dbus_return(e.message, ret, signature='a{ua{sv}}')
        except e:
            self.q.dbus_raise(e.message, INVALID_HANDLE, str(e.args[0]))

    def GetContactByID(self, e):
        h = self.ensure_handle(e.args[0])
        self.q.dbus_return(e.message, h,
                self.get_contact_attributes(h, e.args[1]), signature='ua{sv}')

class SimulatedChannel(object):
    def __init__(self, conn, immutable, mutable={},
            destroyable=False, group=False):
        self.conn = conn
        self.q = conn.q
        self.bus = conn.bus
        self.object_path = conn.object_path + ('/_%x' % id(self))
        self.immutable = immutable.copy()

        if self.immutable[cs.TARGET_HANDLE_TYPE] != cs.HT_NONE:
            if (cs.TARGET_ID in self.immutable) != (
                    cs.TARGET_HANDLE in self.immutable):
                if cs.TARGET_ID in self.immutable:
                    self.immutable[cs.TARGET_HANDLE] = conn.ensure_handle(
                            self.immutable[cs.TARGET_HANDLE_TYPE],
                            self.immutable[cs.TARGET_ID])
                else:
                    self.immutable[cs.TARGET_ID] = conn.inspect_handles(
                            [self.immutable[cs.TARGET_HANDLE]])[0]

        if cs.REQUESTED not in self.immutable:
            self.immutable[cs.REQUESTED] = False

        self.properties = dbus.Dictionary({}, signature='sv')
        self.properties.update(self.immutable)
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
        self.conn.NewChannel(self)

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
            observe=[], approve=[], handle=[],
            cap_tokens=[], bypass_approval=False, wants_recovery=False,
            request_notification=True, implement_get_interfaces=True,
            is_handler=None, delay_approvers=False):
        self.q = q
        self.bus = bus
        self.bus_name = '.'.join([cs.tp_name_prefix, 'Client', clientname])
        self._bus_name_ref = dbus.service.BusName(self.bus_name, self.bus)
        self.object_path = '/' + self.bus_name.replace('.', '/')
        self.observe = aasv(observe)
        self.approve = aasv(approve)
        self.handle = aasv(handle)
        self.bypass_approval = bool(bypass_approval)
        self.delay_approvers = bool(delay_approvers)
        self.wants_recovery = bool(wants_recovery)
        self.request_notification = bool(request_notification)
        self.handled_channels = dbus.Array([], signature='o')
        self.cap_tokens = dbus.Array(cap_tokens, signature='s')
        self.is_handler = is_handler

        if self.is_handler is None:
            self.is_handler = bool(handle)

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
        q.add_dbus_method_impl(self.Get_Capabilities,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.HANDLER, 'Capabilities'])
        q.add_dbus_method_impl(self.Get_HandledChannels,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.HANDLER, 'HandledChannels'])
        q.add_dbus_method_impl(self.Get_BypassApproval,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.HANDLER, 'BypassApproval'])
        q.add_dbus_method_impl(self.Get_Recover,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.OBSERVER, 'Recover'])
        q.add_dbus_method_impl(self.GetAll_Handler,
                path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.HANDLER])

    def release_name(self):
        del self._bus_name_ref

    def reacquire_name(self):
        self._bus_name_ref = dbus.service.BusName(self.bus_name, self.bus)

    def get_interfaces(self):
        ret = dbus.Array([], signature='s', variant_level=1)

        if self.observe:
            ret.append(cs.OBSERVER)

        if self.approve:
            ret.append(cs.APPROVER)

        if self.is_handler:
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
        self.q.dbus_return(e.message, {
            'ObserverChannelFilter': self.observe,
            'Recover': dbus.Boolean(self.wants_recovery),
            'DelayApprovers': dbus.Boolean(self.delay_approvers),
            },
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
        assert self.is_handler
        self.q.dbus_return(e.message, {
            'HandlerChannelFilter': self.handle,
            'BypassApproval': self.bypass_approval,
            'HandledChannels': self.handled_channels,
            'Capabilities': self.cap_tokens,
            },
                signature='a{sv}', bus=self.bus)

    def Get_Capabilities(self, e):
        self.q.dbus_return(e.message, self.cap_tokens, signature='v',
                bus=self.bus)

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

    def Get_Recover(self, e):
        assert self.handle
        self.q.dbus_return(e.message, self.recover, signature='v',
                bus=self.bus)

class SimulatedConnectionManager(object):
    def __init__(self, q, bus, cm_name='fakecm',
            protocol_names=['fakeprotocol'],
            has_account_storage=False):
        self.q = q
        self.bus = bus
        self.cm_name = cm_name
        self.bus_name = '.'.join([cs.CM, cm_name])
        self._bus_name_ref = dbus.service.BusName(self.bus_name, self.bus)
        self.object_path = '/' + self.bus_name.replace('.', '/')
        self.has_account_storage = has_account_storage
        self.protocol_names = list(protocol_names)

        q.add_dbus_method_impl(self.GetAll_CM,
                path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CM])

        for protocol_name in protocol_names:
            assert '-' not in protocol_name
            q.add_dbus_method_impl(self.IdentifyAccount,
                    path=self.object_path + '/' + protocol_name,
                    interface=cs.PROTOCOL, method='IdentifyAccount')
            q.add_dbus_method_impl(self.NormalizeContact,
                    path=self.object_path + '/' + protocol_name,
                    interface=cs.PROTOCOL, method='NormalizeContact')

    def release_name(self):
        del self._bus_name_ref

    def reacquire_name(self):
        self._bus_name_ref = dbus.service.BusName(self.bus_name, self.bus)

    def get_protocols(self):
        ret = dbus.Dictionary(signature='sa{sv}')

        for p in self.protocol_names:
            # stub: assume all the protocols look "reasonably normal"
            ret[p] = {
                    cs.PROTOCOL + '.Interfaces': dbus.Array(signature='s'),
                    cs.PROTOCOL + '.ConnectionInterfaces':
                        dbus.Array(signature='s'),
                    cs.PROTOCOL + '.RequestableChannelClasses':
                        dbus.Array(signature='(a{sv}as)'),
                    cs.PROTOCOL + '.VCardField': 'x-' + self.cm_name,
                    cs.PROTOCOL + '.EnglishName': self.cm_name,
                    cs.PROTOCOL + '.Icon': 'im-' + self.cm_name,
                    cs.PROTOCOL + '.AuthenticationTypes':
                        dbus.Array(signature='s'),
                    cs.PROTOCOL + '.Parameters': dbus.Array([
                        ('account', cs.PARAM_REQUIRED, 's', ''),
                        ('password', cs.PARAM_SECRET, 's', ''),
                        ], signature='(susv)'),
                    }

            if self.cm_name == 'fakecm' and p == 'fakeprotocol':
                ret[p][cs.PROTOCOL + '.Parameters'] = dbus.Array([
                    ('account', cs.PARAM_REQUIRED|cs.PARAM_REGISTER,
                        's', ''),
                    ('password',
                        cs.PARAM_SECRET|cs.PARAM_REQUIRED|cs.PARAM_REGISTER,
                        's', ''),
                    ('nickname', cs.PARAM_REGISTER, 's', ''),
                    ('register', cs.PARAM_HAS_DEFAULT, 'b', False),
                    ('com.example.Badgerable.Badgered',
                        cs.PARAM_HAS_DEFAULT|cs.PARAM_DBUS_PROPERTY,
                        'b', False),
                    ('secret-mushroom', cs.PARAM_SECRET, 's', ''),
                    ('snakes', 0, 'u', dbus.UInt32(0)),
                    ('contrived-example', cs.PARAM_HAS_DEFAULT, 'u',
                        dbus.UInt32(5)),
                    ], signature='(susv)')

            if self.cm_name == 'onewitheverything' and p == 'serializable':
                ret[p][cs.PROTOCOL + '.Parameters'] = dbus.Array([
                    ('s', cs.PARAM_REQUIRED, 's', ''),
                    ('o', 0, 'o', dbus.ObjectPath('/')),
                    ('b', 0, 'b', False),
                    ('q', 0, 'q', dbus.UInt16(0)),
                    ('u', 0, 'u', dbus.UInt32(0)),
                    ('t', 0, 't', dbus.UInt64(0)),
                    ('n', 0, 'n', dbus.Int16(0)),
                    ('i', 0, 'i', dbus.Int32(0)),
                    ('x', 0, 'x', dbus.Int64(0)),
                    ('d', 0, 'd', 0.0),
                    ('as', 0, 'as', dbus.Array(signature='s')),
                    ('y', 0, 'y', dbus.Byte(0)),
                    ], signature='(susv)')

            if self.cm_name == 'onewitheverything' and p == 'defaults':
                ret[p][cs.PROTOCOL + '.Parameters'] = dbus.Array([
                    ('s', cs.PARAM_HAS_DEFAULT, 's', 'foo'),
                    ('o', cs.PARAM_HAS_DEFAULT, 'o', dbus.ObjectPath('/foo')),
                    ('b', cs.PARAM_HAS_DEFAULT, 'b', True),
                    ('q', cs.PARAM_HAS_DEFAULT, 'q', dbus.UInt16(1)),
                    ('u', cs.PARAM_HAS_DEFAULT, 'u', dbus.UInt32(1)),
                    ('t', cs.PARAM_HAS_DEFAULT, 't', dbus.UInt64(1)),
                    ('n', cs.PARAM_HAS_DEFAULT, 'n', dbus.Int16(-1)),
                    ('i', cs.PARAM_HAS_DEFAULT, 'i', dbus.Int32(-1)),
                    ('x', cs.PARAM_HAS_DEFAULT, 'x', dbus.Int64(-1)),
                    ('d', cs.PARAM_HAS_DEFAULT, 'd', 1.5),
                    ('as', cs.PARAM_HAS_DEFAULT, 'as',
                        dbus.Array(['foo', 'bar', 'baz'], signature='s')),
                    ('y', cs.PARAM_HAS_DEFAULT, 'y', dbus.Byte(1)),
                    ], signature='(susv)')

            if self.cm_name == 'onewitheverything' and p == 'flags':
                ret[p][cs.PROTOCOL + '.Parameters'] = dbus.Array([
                    ('account', cs.PARAM_REQUIRED|cs.PARAM_REGISTER, 's', ''),
                    ('name', cs.PARAM_REGISTER, 's', ''),
                    ('key',
                        cs.PARAM_REGISTER|cs.PARAM_REQUIRED|cs.PARAM_SECRET,
                        's', ''),
                    ('com.example.Badgerable.Badgers', cs.PARAM_DBUS_PROPERTY,
                        's', ''),
                    ], signature='(susv)')

        return ret

    def get_interfaces(self):
        ret = dbus.Array([], signature='s')

        if self.has_account_storage:
            ret.append(cs.CM_IFACE_ACCOUNT_STORAGE)

        return ret

    def GetAll_CM(self, e):
        self.q.dbus_return(e.message, {
            'Protocols': self.get_protocols(),
            'Interfaces': self.get_interfaces(),
            }, signature='a{sv}', bus=self.bus)

    def IdentifyAccount(self, e):
        if 'account' in e.args[0]:
            ret = e.args[0]['account'].lower()
        else:
            ret = 'account'
        self.q.dbus_return(e.message, ret, signature='s')

    def NormalizeContact(self, e):
        self.q.dbus_return(e.message, e.args[0].lower(), signature='s')

def take_fakecm_name(bus):
    return dbus.service.BusName(cs.CM + '.fakecm', bus=bus)

def create_fakecm_account(q, bus, mc, params, properties={},
                          cm_bus=None, simulated_cm=None,
                          cm_name='fakecm', protocol_name='fakeprotocol'):
    """Create a fake connection manager and an account that uses it.

    Optional keyword arguments:
    properties -- a dictionary from qualified property names to values to pass
                  to CreateAccount. If provided, this function will check that
                  the newly-created account has these properties.
    cm_bus     -- if not None, a BusConnection via which to claim the CM's
                  name. If None, 'bus' will be used.

    Returns: (a BusName for the fake CM, an Account proxy)"""

    if cm_bus is None:
        cm_bus = bus

    if simulated_cm is None:
        simulated_cm = SimulatedConnectionManager(q, bus,
                cm_name=cm_name, protocol_names=[protocol_name])

    account_manager = AccountManager(bus)

    servicetest.call_async(q, account_manager, 'CreateAccount',
        cm_name, protocol_name, 'fakeaccount', params, properties)

    usability_changed_pattern = servicetest.EventPattern('dbus-signal',
        path=cs.AM_PATH, signal='AccountUsabilityChanged', interface=cs.AM)

    # The spec has no order guarantee here.
    # FIXME: MC ought to also introspect the CM and find out that the params
    # are in fact sufficient
    a_signal, am_signal, ret = q.expect_many(
            servicetest.EventPattern('dbus-signal',
                signal='PropertiesChanged', interface=cs.PROPERTIES_IFACE,
                predicate=(lambda e: 'Usable' in e.args[1].keys())),
            usability_changed_pattern,
            servicetest.EventPattern('dbus-return', method='CreateAccount'),
            )
    account_path = ret.value[0]
    assert am_signal.args == [account_path, True], am_signal.args
    assert a_signal.args[1]['Usable'] == True, a_signal.args

    assert account_path is not None

    account = Account(bus, account_path)

    for key, value in properties.iteritems():
        interface, prop = key.rsplit('.', 1)
        servicetest.assertEquals(value, account.Properties.Get(interface, prop))

    return (simulated_cm, account)

def get_fakecm_account(bus, mc, account_path):
    account = Account(bus, account_path)

    # Introspect Account for debugging purpose
    account_introspected = account.Introspect(
            dbus_interface=cs.INTROSPECTABLE_IFACE)
    #print account_introspected

    return account

def enable_fakecm_account(q, bus, mc, account, expected_params, **kwargs):
    # I'm too lazy to manually pass all the other kwargs to
    # expect_fakecm_connection
    try:
        requested_presence = kwargs['requested_presence']
        del kwargs['requested_presence']
    except KeyError:
        requested_presence = (2, 'available', '')

    # Enable the account
    account.Properties.Set(cs.ACCOUNT, 'Enabled', True)

    if requested_presence is not None:
        requested_presence = dbus.Struct(
                (dbus.UInt32(requested_presence[0]),) +
                tuple(requested_presence[1:]),
                signature='uss')
        account.Properties.Set(cs.ACCOUNT,
                'RequestedPresence', requested_presence)

    return expect_fakecm_connection(q, bus, mc, account, expected_params, **kwargs)

def expect_fakecm_connection(q, bus, mc, account, expected_params,
        has_presence=False, has_aliasing=False,
        has_avatars=False, avatars_persist=True,
        extra_interfaces=[],
        expect_before_connect=(), expect_after_connect=(),
        has_hidden=False,
        self_ident='myself',
        cm_name='fakecm',
        protocol_name='fakeprotocol'):
    # make (safely) mutable copies
    expect_before_connect = list(expect_before_connect)
    expect_after_connect = list(expect_after_connect)

    # for simplicity we assume the sort of name that is invariant between
    # Telepathy 0 and Telepathy 1
    assert '-' not in protocol_name

    e = q.expect('dbus-method-call', method='RequestConnection',
            args=[protocol_name, expected_params],
            destination=cs.CM + '.' + cm_name,
            path='/' + cs.CM.replace('.', '/') + '/' + cm_name,
            interface=cs.CM,
            handled=False)

    conn = SimulatedConnection(q, bus, cm_name, protocol_name,
                               account.object_path.split('/')[-1],
            self_ident, has_presence=has_presence,
            has_aliasing=has_aliasing, has_avatars=has_avatars,
            avatars_persist=avatars_persist, extra_interfaces=extra_interfaces,
            has_hidden=has_hidden)

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    expect_before_connect.append(
            servicetest.EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CONN_IFACE_REQUESTS],
                path=conn.object_path, handled=True))
    events = list(q.expect_many(*expect_before_connect))
    del events[-1]

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CSR_NONE_SPECIFIED)

    expect_after_connect = list(expect_after_connect)

    events = events + list(q.expect_many(*expect_after_connect))

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

def get_account_manager(bus):
    """
    A backwards-compatibility synonym for constructing a new AccountManager
    object. Please don't use this in new tests.
    """
    return AccountManager(bus)

class AccountManager(servicetest.ProxyWrapper):
    def __init__(self, bus):
        bare_am = bus.get_object(cs.AM, cs.AM_PATH,
            follow_name_owner_changes=True)

        servicetest.ProxyWrapper.__init__(self, bare_am, cs.AM, {})

class Account(servicetest.ProxyWrapper):
    def __init__(self, bus, account_path):
        servicetest.ProxyWrapper.__init__(self,
            bus.get_object(cs.AM, account_path),
            cs.ACCOUNT, {})

class ChannelDispatcher(servicetest.ProxyWrapper):
    def __init__(self, bus):
        bare_cd = bus.get_object(cs.CD, cs.CD_PATH,
            follow_name_owner_changes=True)

        servicetest.ProxyWrapper.__init__(self, bare_cd, cs.CD, {})

class ChannelDispatchOperation(servicetest.ProxyWrapper):
    def __init__(self, bus, path):
        bare_cdo = bus.get_object(cs.CD, path)
        servicetest.ProxyWrapper.__init__(self, bare_cdo, cs.CDO, {})

class ChannelRequest(servicetest.ProxyWrapper):
    def __init__(self, bus, path):
        bare_cr = bus.get_object(cs.CD, path)
        servicetest.ProxyWrapper.__init__(self, bare_cr, cs.CR, {})

def connect_to_mc(q, bus, mc):
    account_manager = AccountManager(bus)

    # Introspect AccountManager for debugging purpose
    account_manager_introspected = account_manager.Introspect(
            dbus_interface=cs.INTROSPECTABLE_IFACE)
    #print account_manager_introspected

    # Check AccountManager has D-Bus property interface
    properties = account_manager.Properties.GetAll(cs.AM)
    assert properties is not None
    interfaces = properties.get('Interfaces')

    return account_manager, properties, interfaces

def tell_mc_to_die(q, bus):
    """Instructs the running Mission Control to die via a magic method call in
    the version built for tests."""

    secret_debug_api = dbus.Interface(bus.get_object(cs.AM, "/"),
        'im.telepathy.v1.MissionControl6.RegressionTests')
    secret_debug_api.Abort()

    # Make sure MC exits
    q.expect('dbus-signal', signal='NameOwnerChanged',
        predicate=(lambda e:
            e.args[0] == 'im.telepathy.v1.AccountManager' and
            e.args[2] == ''))

def resuscitate_mc(q, bus, mc):
    """Having killed MC with tell_mc_to_die(), this function revives it."""
    # We kick the daemon asynchronously because nm-glib makes blocking calls
    # back to us during initialization...
    bus.call_async(dbus.BUS_DAEMON_NAME, dbus.BUS_DAEMON_PATH,
        dbus.BUS_DAEMON_IFACE, 'StartServiceByName', 'su', (cs.MC, 0),
        reply_handler=None, error_handler=None)

    # Wait until it's up
    mc.wait_for_names()

    return connect_to_mc(q, bus, mc)

def keyfile_read(fname):
    groups = { None: {} }
    group = None
    for line in open(fname):
        line = line[:-1].decode('utf-8').strip()
        if not line or line.startswith('#'):
            continue

        if line.startswith('[') and line.endswith(']'):
            group = line[1:-1]
            groups[group] = {}
            continue

        if '=' in line:
            k, v = line.split('=', 1)
        else:
            k = line
            v = None

        groups[group][k] = v
    return groups

def read_account_keyfile():
    """Reads the keyfile used by the 'diverted' storage plugin used by most of
    the tests."""
    key_file_name = os.path.join(os.getenv('XDG_CACHE_HOME'),
        'mcp-test-diverted-account-plugin.conf')
    return keyfile_read(key_file_name)
