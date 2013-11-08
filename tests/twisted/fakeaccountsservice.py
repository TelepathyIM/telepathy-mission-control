# vim: set fileencoding=utf-8
#
# Copyright © 2009 Nokia Corporation
# Copyright © 2009-2012 Collabora Ltd.
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

from servicetest import (Event, EventPattern)

import constants as cs

class FakeAccount(object):

    def __init__(self):
        self.attrs = dbus.Dictionary({}, signature='sv')
        self.attr_flags = dbus.Dictionary({}, signature='su')
        self.params = dbus.Dictionary({}, signature='sv')
        self.untyped_params = dbus.Dictionary({}, signature='ss')
        self.param_flags = dbus.Dictionary({}, signature='su')
        self.restrictions = 0

    SIGNATURE = 'a{sv}a{su}a{sv}a{ss}a{su}u'

    def to_dbus(self):
        return (
                self.attrs,
                self.attr_flags,
                self.params,
                self.untyped_params,
                self.param_flags,
                dbus.UInt32(self.restrictions),
                )

class FakeAccountsService(object):
    def __init__(self, q, bus):
        self.q = q
        self.bus = bus
        self.nameref = dbus.service.BusName(cs.TEST_DBUS_ACCOUNT_SERVICE,
                self.bus)
        self.object_path = cs.TEST_DBUS_ACCOUNT_SERVICE_PATH

        self.accounts = {}

        q.add_dbus_method_impl(self.GetAccounts,
                path=self.object_path,
                interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                method='GetAccounts')

        q.add_dbus_method_impl(self.CreateAccount,
                path=self.object_path,
                interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                method='CreateAccount')

        q.add_dbus_method_impl(self.DeleteAccount,
                path=self.object_path,
                interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                method='DeleteAccount')

        q.add_dbus_method_impl(self.UpdateAttributes,
                path=self.object_path,
                interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                method='UpdateAttributes')

        q.add_dbus_method_impl(self.UpdateParameters,
                path=self.object_path,
                interface=cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                method='UpdateParameters')

    def create_account(self, account, attrs={}, attr_flags={}, params={},
            untyped_params={}, param_flags={}, restrictions=0):

        if account in self.accounts:
            raise KeyError('Account %s already exists' % account)

        self.accounts[account] = FakeAccount()
        self.accounts[account].restrictions = restrictions
        self.accounts[account].attrs.update(attrs)
        for attr in attrs:
            self.accounts[account].attr_flags[attr] = dbus.UInt32(0)
        self.accounts[account].attr_flags.update(attr_flags)
        self.accounts[account].params.update(params)
        for param in params:
            self.accounts[account].param_flags[param] = dbus.UInt32(0)
        self.accounts[account].untyped_params.update(untyped_params)
        for param in untyped_params:
            self.accounts[account].param_flags[param] = dbus.UInt32(0)
        self.accounts[account].param_flags.update(param_flags)
        self.q.dbus_emit(self.object_path, cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                'AccountCreated',
                account, *(self.accounts[account].to_dbus()),
                signature='s' + FakeAccount.SIGNATURE)

    def CreateAccount(self, e):
        try:
            self.create_account(*e.raw_args)
        except Exception as ex:
            self.q.dbus_raise(e.message, cs.NOT_AVAILABLE, str(ex))
        else:
            self.q.dbus_return(e.message, signature='')

    def delete_account(self, account):
        # raises if not found
        del self.accounts[account]
        self.q.dbus_emit(self.object_path, cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                'AccountDeleted', account, signature='s')

    def DeleteAccount(self, e):
        try:
            self.delete_account(*e.raw_args)
        except Exception as ex:
            self.q.dbus_raise(e.message, cs.NOT_AVAILABLE, str(ex))
        else:
            self.q.dbus_return(e.message, signature='')

    def GetAccounts(self, e):
        accounts = {}
        for a in self.accounts:
            accounts[a] = self.accounts[a].to_dbus()
        self.q.dbus_return(e.message, accounts,
                signature='a{s(' + FakeAccount.SIGNATURE + ')}')

    def update_attributes(self, account, changed={}, flags={}, deleted=[]):
        if account not in self.accounts:
            self.create_account(account)

        for (attribute, value) in changed.items():
            self.accounts[account].attrs[attribute] = value
            self.accounts[account].attr_flags[attribute] = flags.get(
                    attribute, dbus.UInt32(0))

        for attribute in deleted:
            if attribute in self.accounts[account].attrs:
                del self.accounts[account].attrs[attribute]
            if attribute in self.accounts[account].attr_flags:
                del self.accounts[account].attr_flags[attribute]

        self.q.dbus_emit(self.object_path, cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                'AttributesChanged',
                account, changed,
                dict([(a, self.accounts[account].attr_flags[a])
                    for a in changed]),
                deleted, signature='sa{sv}a{su}as')

    def UpdateAttributes(self, e):
        try:
            self.update_attributes(*e.raw_args)
        except Exception as ex:
            self.q.dbus_raise(e.message, cs.NOT_AVAILABLE, str(ex))
        else:
            self.q.dbus_return(e.message, signature='')

    def update_parameters(self, account, changed={}, untyped={}, flags={},
            deleted=[]):
        if account not in self.accounts:
            self.create_account(account)

        for (param, value) in changed.items():
            self.accounts[account].params[param] = value
            if param in self.accounts[account].untyped_params:
                del self.accounts[account].untyped_params[param]
            self.accounts[account].param_flags[param] = flags.get(
                    param, dbus.UInt32(0))

        for (param, value) in untyped.items():
            self.accounts[account].untyped_params[param] = value
            if param in self.accounts[account].params:
                del self.accounts[account].params[param]
            self.accounts[account].param_flags[param] = flags.get(
                    param, dbus.UInt32(0))

        for param in deleted:
            if param in self.accounts[account].params:
                del self.accounts[account].params[param]
            if param in self.accounts[account].untyped_params:
                del self.accounts[account].untyped_params[param]
            if param in self.accounts[account].param_flags:
                del self.accounts[account].param_flags[param]

        self.q.dbus_emit(self.object_path, cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                'ParametersChanged',
                account, changed, untyped,
                dict([(p, self.accounts[account].param_flags[p])
                    for p in (set(changed.keys()) | set(untyped.keys()))]),
                deleted,
                signature='sa{sv}a{ss}a{su}as')

    def UpdateParameters(self, e):
        try:
            self.update_parameters(*e.raw_args)
        except Exception as ex:
            self.q.dbus_raise(e.message, cs.NOT_AVAILABLE, str(ex))
        else:
            self.q.dbus_return(e.message, signature='')
