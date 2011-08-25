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

"""Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=20880
"""

import dbus

from servicetest import call_async, assertEquals, assertContains
from mctest import exec_test, AccountManager
import constants as cs

def test(q, bus, mc):
    am = AccountManager(bus)

    def call_create(cm='fakecm', protocol='fakeprotocol', parameters=None):
        if parameters is None:
            parameters = {"account": "someguy@example.com",
                          "password": "secrecy",
                         }

        call_async(q, am, 'CreateAccount',
            cm, protocol, 'this is a beautiful account',
            dbus.Dictionary(parameters, signature='sv'),
            {})

    # Create an account with a bad Connection_Manager - it should fail
    call_create(cm='nonexistent_cm')
    e = q.expect('dbus-error', method='CreateAccount')
    assertEquals(cs.NOT_IMPLEMENTED, e.name)
    assertContains("nonexistent_cm", e.message)

    # Create an account with a bad Protocol - it should fail
    call_create(protocol='nonexistent-protocol')
    e = q.expect('dbus-error', method='CreateAccount')
    assertEquals(cs.NOT_IMPLEMENTED, e.name)
    assertContains("nonexistent-protocol", e.message)

    # Create an account with incomplete Parameters - it should fail
    call_create(parameters={"account": "someguy@example.com"})
    e = q.expect('dbus-error', method='CreateAccount')
    assertEquals(cs.INVALID_ARGUMENT, e.name)
    assertContains("password", e.message)

    # Create an account with unknown parameters
    call_create(parameters={ "account": "someguy@example.com",
                             "password": "secrecy",
                             "deerhoof": "evil",
                           })
    e = q.expect('dbus-error', method='CreateAccount')
    assertEquals(cs.INVALID_ARGUMENT, e.name)
    assertContains("deerhoof", e.message)

    # Create an account with parameters with the wrong types
    call_create(parameters={ "account": "someguy@example.com",
                             "password": 1234,
                           })
    e = q.expect('dbus-error', method='CreateAccount')
    assertEquals(cs.INVALID_ARGUMENT, e.name)
    assertContains("password", e.message)

if __name__ == '__main__':
    exec_test(test, {})
