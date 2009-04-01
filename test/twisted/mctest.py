
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


def exec_test_deferred (fun, params, protocol=None, timeout=None):
    colourer = None

    if sys.stdout.isatty():
        colourer = install_colourer()

    queue = servicetest.IteratingEventQueue(timeout)
    queue.verbose = (
        os.environ.get('CHECK_TWISTED_VERBOSE', '') != ''
        or '-v' in sys.argv)

    bus = dbus.SessionBus()
    queue.attach_to_bus(bus)
    mc = make_mc(bus, queue.append, params)
    error = None

    try:
        fun(queue, bus, mc)
    except Exception, e:
        import traceback
        traceback.print_exc()
        error = e

    try:
        if colourer:
          sys.stdout = colourer.fh

        am_props_iface = dbus.Interface(bus.get_object(cs.AM, cs.AM_PATH),
                cs.PROPERTIES_IFACE)
        am_props = am_props_iface.GetAll(cs.AM)

        for a in (am_props.get('ValidAccounts', []) +
                am_props.get('InvalidAccounts', [])):
            account_iface = dbus.Interface(bus.get_object(cs.AM, a),
                    cs.ACCOUNT)
            account_iface.Remove()

        if error is None:
          reactor.callLater(0, reactor.stop)
        else:
          # please ignore the POSIX behind the curtain
          os._exit(1)

    except dbus.DBusException, e:
        pass

def exec_test(fun, params=None, protocol=None, timeout=None):
  reactor.callWhenRunning (exec_test_deferred, fun, params, protocol, timeout)
  reactor.run()

class SimulatedConnection(object):

    def ensure_handle(self, type, identifier):
        if (type, identifier) in self._handles:
            return self._handles[identifier]

        self._last_handle += 1
        self._handles[(type, identifier)] = self._last_handle
        self._identifiers[(type, self._last_handle)] = identifier
        return self._last_handle

    def __init__(self, q, bus, cmname, protocol, account_part, self_ident):
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

        q.add_dbus_method_impl(self.Connect,
                path=self.object_path, interface=cs.CONN, method='Connect')
        q.add_dbus_method_impl(self.Disconnect,
                path=self.object_path, interface=cs.CONN, method='Disconnect')
        q.add_dbus_method_impl(self.GetSelfHandle,
                path=self.object_path,
                interface=cs.CONN, method='GetSelfHandle')
        q.add_dbus_method_impl(self.GetStatus,
                path=self.object_path, interface=cs.CONN, method='GetStatus')
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

    def GetInterfaces(self, e):
        # FIXME: when needed, allow altering this return somehow
        self.q.dbus_return(e.message, [cs.CONN_IFACE_REQUESTS], signature='as')

    def Connect(self, e):
        self.StatusChanged(cs.CONN_STATUS_CONNECTING,
                cs.CONN_STATUS_REASON_REQUESTED)
        self.q.dbus_return(e.message, signature='')

    def Disconnect(self, e):
        self.StatusChanged(cs.CONN_STATUS_DISCONNECTED,
                cs.CONN_STATUS_REASON_REQUESTED)
        self.q.dbus_return(e.message, signature='')

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

    def get_channel_details(self):
        return dbus.Array([(c.object_path, c.immutable)
            for c in self.channels], signature='(oa{sv})')

    def GetAll_Requests(self, e):
        # FIXME: stub: assumes no channels
        self.q.dbus_return(e.message, {
            'Channels': self.get_channel_details(),
        }, signature='a{sv}')

    def GetSelfHandle(self, e):
        self.q.dbus_return(e.message, self.self_handle, signature='u')

class SimulatedChannel(object):
    def __init__(self, conn, immutable, mutable={}):
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

        self.announced = False
        self.closed = False

    def announce(self):
        assert not self.announced
        self.announced = True
        self.conn.channels.append(self)
        self.q.dbus_emit(self.conn.object_path, cs.CONN_IFACE_REQUESTS,
                'NewChannels', [(self.object_path, self.immutable)],
                signature='a(oa{sv})')

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
            observe=[], approve=[], handle=[], bypass_approval=False):
        self.q = q
        self.bus = bus
        self.bus_name = '.'.join([cs.tp_name_prefix, 'Client', clientname])
        self._bus_name_ref = dbus.service.BusName(self.bus_name, self.bus)
        self.object_path = '/' + self.bus_name.replace('.', '/')
        self.observe = aasv(observe)
        self.approve = aasv(approve)
        self.handle = aasv(handle)
        self.bypass_approval = bool(bypass_approval)
        self.handled_channels = dbus.Array([], signature='o')

        q.add_dbus_method_impl(self.Get_Interfaces,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.CLIENT, 'Interfaces'])
        q.add_dbus_method_impl(self.GetAll_Client, path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CLIENT])

        q.add_dbus_method_impl(self.Get_ObserverChannelFilter,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.OBSERVER, 'ObserverChannelFilter'])
        q.add_dbus_method_impl(self.GetAll_Observer, path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.OBSERVER])

        q.add_dbus_method_impl(self.Get_ApproverChannelFilter,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.APPROVER, 'ApproverChannelFilter'])
        q.add_dbus_method_impl(self.GetAll_Approver, path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.APPROVER])

        q.add_dbus_method_impl(self.Get_HandlerChannelFilter,
                path=self.object_path, interface=cs.PROPERTIES_IFACE,
                method='Get', args=[cs.HANDLER, 'HandlerChannelFilter'])
        q.add_dbus_method_impl(self.GetAll_Handler, path=self.object_path,
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.HANDLER])

    def get_interfaces(self):
        ret = dbus.Array([], signature='s', variant_level=1)

        if self.observe:
            ret.append(cs.OBSERVER)

        if self.approve:
            ret.append(cs.APPROVER)

        if self.handle:
            ret.append(cs.HANDLER)

        return ret

    def Get_Interfaces(self, e):
        self.q.dbus_return(e.message, self.get_interfaces(), signature='v')

    def GetAll_Client(self, e):
        self.q.dbus_return(e.message, {'Interfaces': self.get_interfaces()},
                signature='a{sv}')

    def GetAll_Observer(self, e):
        assert self.observe
        self.q.dbus_return(e.message, {'ObserverChannelFilter': self.observe},
                signature='a{sv}')

    def Get_ObserverChannelFilter(self, e):
        assert self.observe
        self.q.dbus_return(e.message, self.observe, signature='v')

    def GetAll_Approver(self, e):
        assert self.approve
        self.q.dbus_return(e.message, {'ApproverChannelFilter': self.approve},
                signature='a{sv}')

    def Get_ApproverChannelFilter(self, e):
        assert self.approve
        self.q.dbus_return(e.message, self.approve, signature='v')

    def GetAll_Handler(self, e):
        assert self.handle
        self.q.dbus_return(e.message, {
            'HandlerChannelFilter': self.handle,
            'BypassApproval': self.bypass_approval,
            'HandledChannels': self.handled_channels,
            },
                signature='a{sv}')

    def Get_HandlerChannelFilter(self, e):
        assert self.handle
        self.q.dbus_return(e.message, self.handle, signature='v')

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
            )
    # The spec has no order guarantee here.
    # FIXME: MC ought to also introspect the CM and find out that the params
    # are in fact sufficient

    a_signal, am_signal, ret = q.expect_many(
            servicetest.EventPattern('dbus-signal',
                signal='AccountPropertyChanged', interface=cs.ACCOUNT),
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

def enable_fakecm_account(q, bus, mc, account, expected_params):
    # Enable the account
    account.Set(cs.ACCOUNT, 'Enabled', True,
            dbus_interface=cs.PROPERTIES_IFACE)

    requested_presence = dbus.Struct((dbus.UInt32(2L),
        dbus.String(u'available'), dbus.String(u'')))
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
            'myself')

    q.dbus_return(e.message, conn.bus_name, conn.object_path, signature='so')

    q.expect('dbus-method-call', method='Connect',
            path=conn.object_path, handled=True)
    conn.StatusChanged(cs.CONN_STATUS_CONNECTED, cs.CONN_STATUS_REASON_NONE)

    q.expect_many(
            servicetest.EventPattern('dbus-method-call',
                interface=cs.PROPERTIES_IFACE, method='GetAll',
                args=[cs.CONN_IFACE_REQUESTS],
                path=conn.object_path, handled=True),
            )

    return conn
