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

import dbus
import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async, sync_dbus
from mctest import exec_test, create_fakecm_account, enable_fakecm_account
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "jc.denton@example.com",
        "password": "ionstorm"}, signature='sv')
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_BUSY), 'busy',
            'Fighting conspiracies'), signature='uss')

    def mk_offline(message=''):
        return dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_OFFLINE), 'offline',
            message), signature='uss')

    offline = mk_offline()

    # While the account is disabled, pushing stuff into RequestedPresence
    # should not make ChangingPresence become True.
    assert not account.Properties.Get(cs.ACCOUNT, 'Enabled')
    assert not account.Properties.Get(cs.ACCOUNT, 'ChangingPresence')
    events = [
        EventPattern('dbus-signal', signal='AccountPropertyChanged',
            predicate=lambda e: 'ChangingPresence' in e.args[0]),
        EventPattern('dbus-method-call', method='RequestConnection'),
        ]
    q.forbid_events(events)
    account.Properties.Set(cs.ACCOUNT, 'RequestedPresence', presence)
    account.Properties.Set(cs.ACCOUNT, 'RequestedPresence', offline)
    account.Properties.Set(cs.ACCOUNT, 'RequestedPresence', presence)
    account.Properties.Set(cs.ACCOUNT, 'RequestedPresence', offline)

    # Check that changing the message associated with our requested offline
    # presence doesn't make anything happen either.
    account.Properties.Set(cs.ACCOUNT, 'RequestedPresence',
        mk_offline('byeeee'))

    # Enable the account; RequestedPresence is still offline, so this should
    # have no effect on ChangingPresence.
    account.Properties.Set(cs.ACCOUNT, 'Enabled', True)
    account.Properties.Set(cs.ACCOUNT, 'Enabled', False)

    sync_dbus(bus, q, account)
    assert not account.Properties.Get(cs.ACCOUNT, 'ChangingPresence')
    q.unforbid_events(events)

    # Go online with a particular presence
    log = []

    # FIXME: using predicate for its side-effects here is weird

    conn, _, _, _, _, _, _ = enable_fakecm_account(q, bus, mc, account, params,
            has_presence=True,
            requested_presence=presence,
            expect_before_connect=[
                EventPattern('dbus-method-call',
                    interface=cs.CONN, method='GetInterfaces',
                    args=[],
                    handled=True,
                    predicate=(lambda e: log.append('GetInterfaces') or True)),
                EventPattern('dbus-method-call',
                    interface=cs.PROPERTIES_IFACE, method='Get',
                    args=[cs.CONN_IFACE_SIMPLE_PRESENCE, 'Statuses'],
                    handled=True,
                    predicate=(lambda e: log.append('Get(Statuses)[1]') or True)),
                EventPattern('dbus-method-call',
                    interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                    method='SetPresence',
                    args=list(presence[1:]),
                    handled=True,
                    predicate=(lambda e: log.append('SetPresence[1]') or True)),
                ],
            expect_after_connect=[
                EventPattern('dbus-method-call',
                    interface=cs.PROPERTIES_IFACE, method='Get',
                    args=[cs.CONN_IFACE_SIMPLE_PRESENCE, 'Statuses'],
                    handled=True,
                    predicate=(lambda e: log.append('Get(Statuses)[2]') or True)),
                EventPattern('dbus-method-call',
                    interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                    method='SetPresence',
                    args=list(presence[1:]),
                    handled=True,
                    predicate=(lambda e: log.append('SetPresence[2]') or True)),
                EventPattern('dbus-signal', path=account.object_path,
                    interface=cs.ACCOUNT, signal='AccountPropertyChanged',
                    predicate=lambda e:
                        e.args[0].get('CurrentPresence') == presence),
                ])

    # The events before Connect must happen in this order. GetInterfaces() may
    # be called once or 2 times
    if len(log) == 5:
        assert log == ['GetInterfaces', 'Get(Statuses)[1]', 'SetPresence[1]',
                'Get(Statuses)[2]', 'SetPresence[2]'], log
    else:
        assert log == ['GetInterfaces', 'GetInterfaces', 'Get(Statuses)[1]', 'SetPresence[1]',
                'Get(Statuses)[2]', 'SetPresence[2]'], log

    # Change requested presence after going online
    presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_AWAY), 'away',
            'In Hong Kong'), signature='uss')
    call_async(q, account.Properties, 'Set', cs.ACCOUNT, 'RequestedPresence',
            presence)

    e, _, _ = q.expect_many(
        EventPattern('dbus-method-call',
            interface=cs.CONN_IFACE_SIMPLE_PRESENCE, method='SetPresence',
            args=list(presence[1:]),
            handled=True),
        EventPattern('dbus-signal', path=account.object_path,
            interface=cs.ACCOUNT, signal='AccountPropertyChanged',
            predicate=lambda e: e.args[0].get('ChangingPresence') == True and
                                e.args[0].get('RequestedPresence') == presence),
        EventPattern('dbus-signal', path=account.object_path,
            interface=cs.ACCOUNT, signal='AccountPropertyChanged',
            predicate=lambda e: e.args[0].get('CurrentPresence') == presence and
                                e.args[0].get('ChangingPresence') == False))

    # Setting RequestedPresence=RequestedPresence causes a (possibly redundant)
    # call to the CM, so we get any side-effects there might be, either in the
    # CM or in MC (e.g. asking connectivity services to go online). However,
    # AccountPropertyChanged is not emitted for RequestedPresence.

    sync_dbus(bus, q, mc)
    events = [EventPattern('dbus-signal', signal='AccountPropertyChanged',
        predicate=lambda e: e.args[0].get('RequestedPresence') is not None)]
    q.forbid_events(events)

    presence = dbus.Struct((dbus.UInt32(cs.PRESENCE_TYPE_AWAY), 'away',
            'In Hong Kong'), signature='uss')
    call_async(q, account.Properties, 'Set', cs.ACCOUNT, 'RequestedPresence',
            presence)

    e = q.expect('dbus-method-call',
        interface=cs.CONN_IFACE_SIMPLE_PRESENCE, method='SetPresence',
        args=list(presence[1:]),
        handled=True)

    sync_dbus(bus, q, mc)
    q.unforbid_events(events)

if __name__ == '__main__':
    exec_test(test, {})
