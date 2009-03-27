
"""
Infrastructure code for testing Mission Control
"""

import base64
import os
import sha
import sys

import servicetest
import twisted
from twisted.internet import reactor

import dbus

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
