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

# indices into the tuple of dicts representing an account
ATTRS = 0
ATTR_FLAGS = 1
PARAMS = 2
UNTYPED_PARAMS = 3
PARAM_FLAGS = 4

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
            untyped_params={}, param_flags={}):
        if account in self.accounts:
            raise KeyError('Account %s already exists' % account)
        self.accounts[account] = ({}, {}, {}, {}, {})
        self.accounts[account][ATTRS].update(attrs)
        for attr in attrs:
            self.accounts[account][ATTR_FLAGS][attr] = dbus.UInt32(0)
        self.accounts[account][ATTR_FLAGS].update(attr_flags)
        self.accounts[account][PARAMS].update(params)
        for param in params:
            self.accounts[account][PARAM_FLAGS][param] = dbus.UInt32(0)
        self.accounts[account][UNTYPED_PARAMS].update(untyped_params)
        for param in untyped_params:
            self.accounts[account][PARAM_FLAGS][param] = dbus.UInt32(0)
        self.accounts[account][PARAM_FLAGS].update(param_flags)
        self.q.dbus_emit(self.object_path, cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                'AccountCreated',
                account, *self.accounts[account],
                signature='sa{sv}a{su}a{sv}a{ss}a{su}')

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
        self.q.dbus_return(e.message, self.accounts,
                signature='a{s(a{sv}a{su}a{sv}a{ss}a{su})}')

    def update_attributes(self, account, changed={}, flags={}, deleted=[]):
        if account not in self.accounts:
            self.create_account(account)

        for (attribute, value) in changed.items():
            self.accounts[account][ATTRS][attribute] = value
            self.accounts[account][ATTR_FLAGS][attribute] = flags.get(
                    attribute, dbus.UInt32(0))

        for attribute in deleted:
            if attribute in self.accounts[account][ATTRS]:
                del self.accounts[account][ATTRS][attribute]
            if attribute in self.accounts[account][ATTR_FLAGS]:
                del self.accounts[account][ATTR_FLAGS][attribute]

        self.q.dbus_emit(self.object_path, cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                'AttributesChanged',
                account, changed,
                dict([(a, self.accounts[account][ATTR_FLAGS][a])
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
            self.accounts[account][PARAMS][param] = value
            if param in self.accounts[account][UNTYPED_PARAMS]:
                del self.accounts[account][UNTYPED_PARAMS][param]
            self.accounts[account][PARAM_FLAGS][param] = flags.get(
                    param, dbus.UInt32(0))

        for (param, value) in untyped.items():
            self.accounts[account][UNTYPED_PARAMS][param] = value
            if param in self.accounts[account][PARAMS]:
                del self.accounts[account][PARAMS][param]
            self.accounts[account][PARAM_FLAGS][param] = flags.get(
                    param, dbus.UInt32(0))

        for param in deleted:
            if param in self.accounts[account][PARAMS]:
                del self.accounts[account][PARAMS][param]
            if param in self.accounts[account][UNTYPED_PARAMS]:
                del self.accounts[account][UNTYPED_PARAMS][param]
            if param in self.accounts[account][PARAM_FLAGS]:
                del self.accounts[account][PARAM_FLAGS][param]

        self.q.dbus_emit(self.object_path, cs.TEST_DBUS_ACCOUNT_SERVICE_IFACE,
                'ParametersChanged',
                account, changed, untyped,
                dict([(p, self.accounts[account][PARAM_FLAGS][p])
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
