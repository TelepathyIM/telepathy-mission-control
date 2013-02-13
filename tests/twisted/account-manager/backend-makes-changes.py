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
from mctest import exec_test, create_fakecm_account, Account
import constants as cs

def test(q, bus, mc, fake_accounts_service=None, **kwargs):
    account_tail = 'fakecm/fakeprotocol/ezio_2efirenze_40fic0'
    account_path = cs.ACCOUNT_PATH_PREFIX + account_tail

    fake_accounts_service.create_account(account_tail,
            {'Enabled': False,
                'manager': 'fakecm',
                'protocol': 'fakeprotocol'},
            {}, # attr flags
            {'account': 'ezio@firenze.fic', 'password': 'nothing is true'},
            {}, # untyped parameters
            {'password': cs.PARAM_FLAG_SECRET}) # param flags
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='AccountCreated',
                args=[account_tail,
                    {'Enabled': False,
                        'manager': 'fakecm',
                        'protocol': 'fakeprotocol'},
                    {'Enabled': 0, 'manager': 0, 'protocol': 0},
                    {'account': 'ezio@firenze.fic',
                        'password': 'nothing is true'},
                    {},
                    {'account': 0, 'password': cs.PARAM_FLAG_SECRET}]),
            EventPattern('dbus-signal',
                path=cs.AM_PATH,
                signal='AccountValidityChanged',
                args=[account_path, True]),
            )
    account = Account(bus, account_path)

    # changing something via MC ensures that the plugin has caught up
    # with account creation
    call_async(q, account.Properties, 'Set', cs.ACCOUNT, 'Nickname',
            'Ezio Auditore di Firenze')
    q.expect_many(
        EventPattern('dbus-signal',
            path=account_path,
            signal='AccountPropertyChanged',
            interface=cs.ACCOUNT,
            # Can't match on args because Valid: True sometimes gets into
            # this AccountPropertyChanged signal. MC should stop merging
            # unrelated signals one day...
            predicate=(lambda e:
                e.args[0].get('Nickname') == 'Ezio Auditore di Firenze')),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='DeferringSetAttribute',
            args=[account_path, 'Nickname', 'Ezio Auditore di Firenze']),
        EventPattern('dbus-signal',
            interface=cs.TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
            signal='CommittingOne',
            args=[account_path]),
        EventPattern('dbus-method-call',
            interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
            method='UpdateAttributes',
            args=[account_tail,
                {'Nickname': 'Ezio Auditore di Firenze'},
                {'Nickname': 0}, # flags
                []],
            ),
        EventPattern('dbus-return', method='Set'),
        )

    assertEquals('Ezio Auditore di Firenze',
            account.Properties.Get(cs.ACCOUNT, 'Nickname'))

    # Now change something from the accounts service side. In principle this
    # could be a generic account-editing UI that knows nothing about
    # Telepathy, if the plugin translated between Telepathy and libaccounts
    # terminology or whatever.

    fake_accounts_service.update_attributes(account_tail,
        {'Enabled': True})
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='AttributesChanged',
                args=[account_tail,
                    {'Enabled': True},
                    {'Enabled': 0}, []]),
            EventPattern('dbus-signal',
                path=account_path,
                signal='AccountPropertyChanged',
                interface=cs.ACCOUNT,
                args=[{'Enabled': True}]),
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                signal='Toggled',
                args=[account_path, True]),
            )
    assertEquals(True, account.Properties.Get(cs.ACCOUNT, 'Enabled'))

    fake_accounts_service.update_attributes(account_tail,
        {cs.ACCOUNT_IFACE_ADDRESSING + '.URISchemes': ['xmpp']})
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='AttributesChanged',
                args=[account_tail,
                    {cs.ACCOUNT_IFACE_ADDRESSING + '.URISchemes': ['xmpp']},
                    {cs.ACCOUNT_IFACE_ADDRESSING + '.URISchemes': 0}, []]),
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                signal='AttributeChanged',
                args=[account_path,
                    cs.ACCOUNT_IFACE_ADDRESSING + '.URISchemes']),
            )
    assertEquals(['xmpp'],
            account.Properties.Get(cs.ACCOUNT_IFACE_ADDRESSING, 'URISchemes'))

    fake_accounts_service.update_attributes(account_tail,
        {'ConnectAutomatically': True})
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='AttributesChanged',
                args=[account_tail,
                    {'ConnectAutomatically': True},
                    {'ConnectAutomatically': 0}, []]),
            EventPattern('dbus-signal',
                path=account_path,
                signal='AccountPropertyChanged',
                interface=cs.ACCOUNT,
                predicate=(lambda e:
                    e.args[0].get('ConnectAutomatically') == True)),
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                signal='AttributeChanged',
                args=[account_path, 'ConnectAutomatically']),
            )
    assertEquals(True,
            account.Properties.Get(cs.ACCOUNT, 'ConnectAutomatically'))

    fake_accounts_service.update_attributes(account_tail,
        {'Supersedes': [cs.ACCOUNT_PATH_PREFIX + 'ac1/game/altair']})
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='AttributesChanged',
                args=[account_tail,
                    {'Supersedes':
                        [cs.ACCOUNT_PATH_PREFIX + 'ac1/game/altair']},
                    {'Supersedes': 0}, []]),
            EventPattern('dbus-signal',
                path=account_path,
                signal='AccountPropertyChanged',
                interface=cs.ACCOUNT,
                args=[{'Supersedes':
                    [cs.ACCOUNT_PATH_PREFIX + 'ac1/game/altair']}],
                ),
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                signal='AttributeChanged',
                args=[account_path, 'Supersedes']),
            )
    assertEquals([cs.ACCOUNT_PATH_PREFIX + 'ac1/game/altair'],
            account.Properties.Get(cs.ACCOUNT, 'Supersedes'))

    fake_accounts_service.update_attributes(account_tail,
        {'AutomaticPresence': (dbus.UInt32(cs.PRESENCE_TYPE_HIDDEN), 'hidden',
            'in a haystack or something')})
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='AttributesChanged',
                args=[account_tail,
                    {'AutomaticPresence': (cs.PRESENCE_TYPE_HIDDEN,
                        'hidden',
                        'in a haystack or something')},
                    {'AutomaticPresence': 0},
                    []]),
            EventPattern('dbus-signal',
                path=account_path,
                signal='AccountPropertyChanged',
                interface=cs.ACCOUNT,
                args=[{'AutomaticPresence':
                    (cs.PRESENCE_TYPE_HIDDEN, 'hidden',
                        'in a haystack or something')}]),
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                signal='AttributeChanged',
                args=[account_path, 'AutomaticPresence']),
            )
    assertEquals((cs.PRESENCE_TYPE_HIDDEN, 'hidden',
        'in a haystack or something'),
            account.Properties.Get(cs.ACCOUNT, 'AutomaticPresence'))

    fake_accounts_service.update_attributes(account_tail, {
        'DisplayName': 'Ezio\'s IM account'})
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='AttributesChanged',
                args=[account_tail, {'DisplayName': 'Ezio\'s IM account'},
                    {'DisplayName': 0}, []]),
            EventPattern('dbus-signal',
                path=account_path,
                signal='AccountPropertyChanged',
                interface=cs.ACCOUNT,
                args=[{'DisplayName': 'Ezio\'s IM account'}]),
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                signal='AttributeChanged',
                args=[account_path, 'DisplayName']),
            )
    assertEquals("Ezio's IM account",
            account.Properties.Get(cs.ACCOUNT, 'DisplayName'))

    fake_accounts_service.update_attributes(account_tail, {
        'Icon': 'im-machiavelli'})
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='AttributesChanged',
                args=[account_tail, {'Icon': 'im-machiavelli'},
                    {'Icon': 0}, []]),
            EventPattern('dbus-signal',
                path=account_path,
                signal='AccountPropertyChanged',
                interface=cs.ACCOUNT,
                args=[{'Icon': 'im-machiavelli'}]),
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                signal='AttributeChanged',
                args=[account_path, 'Icon']),
            )
    assertEquals('im-machiavelli',
            account.Properties.Get(cs.ACCOUNT, 'Icon'))

    fake_accounts_service.update_attributes(account_tail, {
        'Service': 'machiavelli-talk'})
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='AttributesChanged',
                args=[account_tail, {'Service': 'machiavelli-talk'},
                    {'Service': 0}, []]),
            EventPattern('dbus-signal',
                path=account_path,
                signal='AccountPropertyChanged',
                interface=cs.ACCOUNT,
                args=[{'Service': 'machiavelli-talk'}]),
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                signal='AttributeChanged',
                args=[account_path, 'Service']),
            )
    assertEquals('machiavelli-talk',
            account.Properties.Get(cs.ACCOUNT, 'Service'))

    fake_accounts_service.update_parameters(account_tail, {
        'password': 'high profile'}, flags={'password': cs.PARAM_FLAG_SECRET})
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='ParametersChanged',
                args=[account_tail, {'password': 'high profile'},
                    {}, {'password': cs.PARAM_FLAG_SECRET}, []]),
            EventPattern('dbus-signal',
                    path=account_path,
                    signal='AccountPropertyChanged',
                    interface=cs.ACCOUNT,
                    args=[{'Parameters':
                        {'account': 'ezio@firenze.fic', 'password': 'high profile'}}]),
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                signal='ParameterChanged',
                args=[account_path, 'password']),
            )

    fake_accounts_service.update_parameters(account_tail, untyped={
        'password': r'\\\n'}, flags={'password': cs.PARAM_FLAG_SECRET})
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='ParametersChanged',
                args=[account_tail, {}, {'password': r'\\\n'},
                    {'password': cs.PARAM_FLAG_SECRET}, []]),
            EventPattern('dbus-signal',
                    path=account_path,
                    signal='AccountPropertyChanged',
                    interface=cs.ACCOUNT,
                    args=[{'Parameters':
                        {'account': 'ezio@firenze.fic', 'password': '\\\n'}}]),
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                signal='ParameterChanged',
                args=[account_path, 'password']),
            )

    fake_accounts_service.update_parameters(account_tail, deleted=[
        'password'])
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='ParametersChanged',
                args=[account_tail, {}, {}, {}, ['password']]),
            EventPattern('dbus-signal',
                    path=account_path,
                    signal='AccountPropertyChanged',
                    interface=cs.ACCOUNT,
                    args=[{'Parameters':
                        {'account': 'ezio@firenze.fic'}}]),
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                signal='ParameterDeleted',
                args=[account_path, 'password']),
            )

    fake_accounts_service.delete_account(account_tail)
    q.expect_many(
            EventPattern('dbus-signal',
                path=cs.TEST_DBUS_ACCOUNT_SERVICE_PATH,
                signal='AccountDeleted',
                args=[account_tail]),
            EventPattern('dbus-signal',
                    signal='AccountRemoved',
                    interface=cs.AM,
                    args=[account_path]),
            )

if __name__ == '__main__':
    exec_test(test, {}, pass_kwargs=True)
