# vim: set fileencoding=utf-8 : thanks python! you've been great
# Copyright © 2010 Nokia Corporation
# Copyright © 2010 Collabora Ltd.
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

from mctest import (
    exec_test, create_fakecm_account, Account, AccountManager,
    tell_mc_to_die, resuscitate_mc,
    )
from servicetest import (
    EventPattern, assertEquals, assertContains, assertDoesNotContain,
    call_async,
    )
import constants as cs

def test_unhidden_account(q, bus, mc):
    """
    Check that accounts don't default to being hidden, and don't show up in the
    lists of hidden accounts.
    """
    am = AccountManager(bus)

    params = { "account": "oh", "password": "hai" }
    (cm_name_ref, account) = create_fakecm_account(q, bus, mc, params)

    assert not account.Properties.Get(cs.ACCOUNT_IFACE_HIDDEN, 'Hidden')

    am_hidden_props = am.Properties.GetAll(cs.AM_IFACE_HIDDEN)
    assertEquals([], am_hidden_props['ValidHiddenAccounts'])
    assertEquals([], am_hidden_props['InvalidHiddenAccounts'])

def test_create_hidden_account(q, bus, mc):
    """
    Check that a newly-created hidden account does not show up on the main
    AccountManager interface, but does show up on AM.I.Hidden, has its
    Hidden property set to True, and can be removed.
    """
    am = AccountManager(bus)

    call_async(q, am.Properties, 'Get', cs.AM,
            'SupportedAccountProperties')
    supported = q.expect('dbus-return', method='Get').value[0]
    assertContains(cs.ACCOUNT_IFACE_HIDDEN + '.Hidden', supported)

    # Make a new hidden account, and check that it really is hidden.
    params = { "account": "aperture@porti.co", "password": "tollgate" }
    properties = { cs.ACCOUNT_IFACE_HIDDEN + '.Hidden': True }

    q.forbid_events([
        EventPattern('dbus-signal', path=cs.AM_PATH,
            signal='AccountValidityChanged', interface=cs.AM),
        ])
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params, properties)

    valid_accounts = am.Properties.Get(cs.AM, 'ValidAccounts')
    assertDoesNotContain(account.object_path, valid_accounts)

    valid_hidden_accounts = am.Properties.Get(cs.AM_IFACE_HIDDEN,
        'ValidHiddenAccounts')
    assertContains(account.object_path, valid_hidden_accounts)

    # Blow MC away, revive it, and check that the account is still hidden.
    tell_mc_to_die(q, bus)
    am, properties, interfaces = resuscitate_mc(q, bus, mc)
    account = Account(bus, account.object_path)

    assert account.Properties.Get(cs.ACCOUNT_IFACE_HIDDEN, 'Hidden')

    assertDoesNotContain(account.object_path, properties['ValidAccounts'])

    valid_hidden_accounts = am.Properties.Get(cs.AM_IFACE_HIDDEN,
        'ValidHiddenAccounts')
    assertContains(account.object_path, valid_hidden_accounts)

    # Delete the account, and check that its removal is signalled only on
    # AM.I.Hidden, not on the main AM interface.
    q.forbid_events([
        EventPattern('dbus-signal', path=cs.AM_PATH,
            signal='AccountRemoved', interface=cs.AM,
            args=[account.object_path]),
        ])

    account.Remove()
    q.expect_many(
        EventPattern('dbus-signal', path=cs.AM_PATH,
            signal='HiddenAccountRemoved', interface=cs.AM_IFACE_HIDDEN),
        EventPattern('dbus-signal', path=account.object_path,
            signal='Removed', interface=cs.ACCOUNT),
        )

def test(q, bus, mc):
    test_unhidden_account(q, bus, mc)
    test_create_hidden_account(q, bus, mc)

if __name__ == '__main__':
    exec_test(test, {}, timeout=10)
