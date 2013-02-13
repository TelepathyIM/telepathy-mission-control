# Copyright (C) 2009-2010 Nokia Corporation
# Copyright (C) 2009-2010 Collabora Ltd.
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

raise AssertionError('Disabled for 5.6 branch')

import time
import os
import os.path
import signal
import sys

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, create_fakecm_account, get_account_manager, \
    get_fakecm_account
import constants as cs

if ('ACCOUNTS' not in os.environ or not os.environ['ACCOUNTS']):
    print "Not testing accounts-sso storage"
    sys.exit(0)

def account_store(op, backend, key=None, value=None):
    cmd = [ '../account-store', op, backend,
        'colltest42@googlemail.com' ]
    if key:
        cmd.append(key)
        if value:
            cmd.append(value)

    lines = os.popen(' '.join(cmd)).read()
    ret = []
    for line in lines.split('\n'):
        if line.startswith('** '):
            continue

        if line:
            ret.append(line)

    if len(ret) > 0:
        return ret[0]
    else:
        return None

def prepare_accounts_db(ctl_dir):
    os.system('cp %s/../tools/example-accounts.db %s/accounts.db' % (ctl_dir, ctl_dir))
    os.system('cp %s/../tools/accounts-sso-example.service %s/google-talk.service' % (ctl_dir, ctl_dir))

def test(q, bus, mc):
    account_manager, properties, interfaces = connect_to_mc(q, bus, mc)

    va = properties.get('ValidAccounts')
    assert va == [], va

    ia = properties.get('InvalidAccounts')
    assert len(ia) == 1

    account_path = ia[0]
    print repr(account_path)

    account = get_fakecm_account(bus, mc, account_path)
    account_iface = dbus.Interface(account, cs.ACCOUNT)
    account_props = dbus.Interface(account, cs.PROPERTIES_IFACE)

    # FIXME at this point MC crashes
    properties = account_props.GetAll(cs.ACCOUNT)


if __name__ == '__main__':
    ctl_dir = os.environ['ACCOUNTS']
    prepare_accounts_db(ctl_dir)
    exec_test(test, {}, timeout=10, use_fake_accounts_service=False)
