# Copyright (C) 2009 Nokia Corporation
# Copyright (C) 2009-2012 Collabora Ltd.
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

import dbus
import dbus.service

from servicetest import EventPattern, call_async, assertEquals
from mctest import (exec_test, SimulatedConnection,
    SimulatedConnectionManager, Account)
import constants as cs

def test(q, bus, mc, fake_accounts_service=None, **kwargs):
    simulated_cm = SimulatedConnectionManager(q, bus)

    for enabled in (True, False):
        for online in (True, False):
            account_tail = ('fakecm/fakeprotocol/ezio_2efirenze_40fic'
                    + (enabled and '_enabled' or '_disabled')
                    + (online and '_online' or '_offline'))
            account_path = cs.ACCOUNT_PATH_PREFIX + account_tail

            try_to_connect = [
                    EventPattern('dbus-method-call', method='RequestConnection',
                        destination=cs.tp_name_prefix + '.ConnectionManager.fakecm',
                        path=cs.tp_path_prefix + '/ConnectionManager/fakecm',
                        interface=cs.tp_name_prefix + '.ConnectionManager',
                        predicate=lambda e:
                            e.args[1]['account'] == account_tail,
                        handled=False)
                    ]

            if enabled and online:
                also_expected = try_to_connect[:]
            else:
                also_expected = []
                q.forbid_events(try_to_connect)

            args = (
                    {
                        'Enabled': enabled,
                        'ConnectAutomatically': online,
                        'manager': 'fakecm',
                        'protocol': 'fakeprotocol',
                        'AutomaticPresence':
                            dbus.Struct((dbus.UInt32(cs.PRESENCE_HIDDEN),
                                'hidden', 'press X to blend'),
                                signature='uss'),
                    },
                    {
                        'Enabled': 0,
                        'ConnectAutomatically': 0,
                        'manager': 0,
                        'protocol': 0,
                        'AutomaticPresence': 0,
                    },
                    {
                        'account': account_tail,
                        'password': 'nothing is true'
                    },
                    {}, # untyped parameters
                    {
                        'account': 0,
                        'password': cs.PARAM_SECRET
                    },
                    cs.StorageRestrictionFlags.CANNOT_SET_PRESENCE |
                    cs.StorageRestrictionFlags.CANNOT_SET_ENABLED,
                )

            fake_accounts_service.create_account(account_tail, *args)

            events = q.expect_many(
                    EventPattern('dbus-signal',
                        path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                        signal='AccountCreated',
                        args=[account_tail] + list(args)),
                    EventPattern('dbus-signal',
                        path=cs.AM_PATH,
                        signal='AccountValidityChanged',
                        args=[account_path, True]),
                    *also_expected)
            account = Account(bus, account_path)

            if enabled and online:
                conn = SimulatedConnection(q, bus, 'fakecm', 'fakeprotocol',
                        account_tail.replace('/', '_'), 'ezio',
                        has_presence=True)
                q.dbus_return(events[-1].message, conn.bus_name,
                        conn.object_path, signature='so')
                q.expect('dbus-method-call', method='SetPresence',
                    # the fake CM doesn't support 'hidden' by default
                    args=['busy', 'press X to blend'])

                requested_presence = (dbus.UInt32(cs.PRESENCE_HIDDEN), 'hidden',
                        'press X to blend')
            else:
                requested_presence = (dbus.UInt32(cs.PRESENCE_OFFLINE),
                        'offline', '')

            call_async(q, account.Properties, 'Get', cs.ACCOUNT,
                    'RequestedPresence')
            q.expect('dbus-return', method='Get', value=(requested_presence,))

            # changes that are not really changes are fine
            call_async(q, account.Properties, 'Set', cs.ACCOUNT,
                    'Enabled', enabled)
            q.expect('dbus-return', method='Set')
            call_async(q, account.Properties, 'Set', cs.ACCOUNT,
                    'ConnectAutomatically', online)
            q.expect('dbus-return', method='Set')

            # changes that actually change the restrictive properties
            # are not allowed
            call_async(q, account.Properties, 'Set', cs.ACCOUNT,
                    'RequestedPresence',
                    ((dbus.UInt32(cs.PRESENCE_AVAILABLE), 'available',
                        'highly conspicuous')))
            q.expect('dbus-error', method='Set')
            call_async(q, account.Properties, 'Set', cs.ACCOUNT,
                    'AutomaticPresence',
                    ((dbus.UInt32(cs.PRESENCE_AVAILABLE), 'available',
                        'highly conspicuous')))
            q.expect('dbus-error', method='Set')
            call_async(q, account.Properties, 'Set', cs.ACCOUNT,
                    'Enabled', not enabled)
            q.expect('dbus-error', method='Set')
            call_async(q, account.Properties, 'Set', cs.ACCOUNT,
                    'ConnectAutomatically', not online)
            q.expect('dbus-error', method='Set')

            # ... but the backend can still change them
            if enabled and online:
                q.forbid_events(try_to_connect)
                fake_accounts_service.update_attributes(account_tail,
                        {
                            'Enabled': False,
                            'ConnectAutomatically': False,
                        })
                q.expect('dbus-method-call', method='Disconnect')
                q.unforbid_events(try_to_connect)
            else:
                q.unforbid_events(try_to_connect)
                fake_accounts_service.update_attributes(account_tail,
                        {
                            'Enabled': True,
                            'ConnectAutomatically': True,
                        })
                q.expect_many(*try_to_connect)

if __name__ == '__main__':
    exec_test(test, {}, pass_kwargs=True)
