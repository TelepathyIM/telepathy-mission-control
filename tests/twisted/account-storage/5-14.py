# Test for a former default account storage backend:
# XDG_DATA_HOME/telepathy/mission-control/accounts.cfg, as used in MC 5.14
#
# Copyright (C) 2009-2010 Nokia Corporation
# Copyright (C) 2009-2014 Collabora Ltd.
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

from storage_helper import test_keyfile
from mctest import exec_test

def test_5_14(q, bus, mc):
    test_keyfile(q, bus, mc, '5.14')

if __name__ == '__main__':
    exec_test(test_5_14, {}, preload_mc=False, use_fake_accounts_service=False)
