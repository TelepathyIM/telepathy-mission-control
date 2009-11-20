/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
 *
 * Contact: Alberto Mardegan  <alberto.mardegan@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <config.h>

#include <libmcclient/mc-interfaces.h>

#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>
#include "mcd-account.h"
#include "mcd-account-manager.h"
#include "mcd-account-manager-priv.h"
#include "mcd-account-priv.h"


const McdDBusProp account_manager_reloadable_properties[] = {
    { 0 },
};


static void
account_manager_reload (McSvcAccountManagerInterfaceReloadable *self,
                        DBusGMethodInvocation *context)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (self);
    GHashTable *accounts;
    GHashTableIter iter;
    McdAccount *account;
    gpointer k;

    DEBUG ("called");

    accounts = _mcd_account_manager_get_accounts (account_manager);

    g_hash_table_iter_init (&iter, accounts);
    while (g_hash_table_iter_next (&iter, &k, (gpointer)&account))
    {
        _mcd_account_reload (account);
    }

    mc_svc_account_manager_interface_reloadable_return_from_reload (context);
}


void
account_manager_reloadable_iface_init (McSvcAccountManagerInterfaceReloadableClass *iface,
                                       gpointer iface_data)
{
#define IMPLEMENT(x) \
    mc_svc_account_manager_interface_reloadable_implement_##x (\
        iface, account_manager_##x)
    IMPLEMENT(reload);
#undef IMPLEMENT
}

