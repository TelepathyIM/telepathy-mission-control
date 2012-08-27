/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * mcd-account-manager.h - the Telepathy Account D-Bus interface (service side)
 *
 * Copyright (C) 2008-2009 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __MCD_ACCOUNT_MANAGER_PRIV_H__
#define __MCD_ACCOUNT_MANAGER_PRIV_H__

#include "mcd-account-manager.h"

#include "mcd-dbusprop.h"

/* auto-generated stubs */
#include "_gen/svc-Account_Manager_Interface_Hidden.h"

G_BEGIN_DECLS

G_GNUC_INTERNAL void _mcd_account_manager_setup
    (McdAccountManager *account_manager);

G_GNUC_INTERNAL GHashTable *_mcd_account_manager_get_accounts
    (McdAccountManager *account_manager);

typedef void (*McdGetAccountCb) (McdAccountManager *account_manager,
                                 McdAccount *account,
                                 const GError *error,
                                 gpointer user_data);

G_GNUC_INTERNAL void _mcd_account_manager_create_account
    (McdAccountManager *account_manager,
     const gchar *manager, const gchar *protocol,
     const gchar *display_name, GHashTable *params, GHashTable *properties,
     McdGetAccountCb callback, gpointer user_data, GDestroyNotify destroy);

G_END_DECLS

#endif
