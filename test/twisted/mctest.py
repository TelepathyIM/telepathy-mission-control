
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

        q.add_dbus_method_impl(self.Connect,
                path=self.object_path, interface=cs.CONN, method='Connect')
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
        self.q.dbus_emit(self.object_path, cs.CONN, 'StatusChanged',
                cs.CONN_STATUS_CONNECTING, cs.CONN_STATUS_REASON_NONE,
                signature='uu')
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

    def GetAll_Requests(self, e):
        # FIXME: stub: assumes no channels
        self.q.dbus_return(e.message, {
            'Channels': dbus.Array(signature='(oa{sv})'),
        }, signature='a{sv}')

    def GetSelfHandle(self, e):
        self.q.dbus_return(e.message, self.self_handle, signature='u')
