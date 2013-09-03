/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * mcd-account-manager.h - the Telepathy Account D-Bus interface (service side)
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
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

#ifndef __MCD_ACCOUNT_MANAGER_H__
#define __MCD_ACCOUNT_MANAGER_H__

#include <telepathy-glib/telepathy-glib.h>
#include "mission-control-plugins/mission-control-plugins.h"
#include "mcd-storage.h"
#include "connectivity-monitor.h"

G_BEGIN_DECLS
#define MCD_TYPE_ACCOUNT_MANAGER         (mcd_account_manager_get_type ())
#define MCD_ACCOUNT_MANAGER(o)           \
    (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_ACCOUNT_MANAGER, McdAccountManager))
#define MCD_ACCOUNT_MANAGER_CLASS(k)     \
    (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_ACCOUNT_MANAGER, McdAccountManagerClass))
#define MCD_IS_ACCOUNT_MANAGER(o)        \
    (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_ACCOUNT_MANAGER))
#define MCD_IS_ACCOUNT_MANAGER_CLASS(k)  \
    (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_ACCOUNT_MANAGER))
#define MCD_ACCOUNT_MANAGER_GET_CLASS(o) \
    (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_ACCOUNT_MANAGER, McdAccountManagerClass))

typedef struct _McdAccountManager McdAccountManager;
typedef struct _McdAccountManagerPrivate McdAccountManagerPrivate;
typedef struct _McdAccountManagerClass McdAccountManagerClass;

#include "mcd-account.h"

struct _McdAccountManager
{
    GObject parent;
    McdAccountManagerPrivate *priv;
};

struct _McdAccountManagerClass
{
    GObjectClass parent_class;
};

GType mcd_account_manager_get_type (void);
McdAccountManager *mcd_account_manager_new (
    TpSimpleClientFactory *client_factory);

TpDBusDaemon *mcd_account_manager_get_dbus_daemon
    (McdAccountManager *account_manager);

typedef void (McdAccountManagerWriteConfCb) (McdAccountManager *account_manager,
                                             const GError *error,
                                             gpointer user_data);

void mcd_account_manager_write_conf_async (McdAccountManager *account_manager,
                                           McdAccount *account,
                                           McdAccountManagerWriteConfCb callback,
                                           gpointer user_data);

McdAccount *mcd_account_manager_lookup_account (McdAccountManager *account_manager,
						const gchar *name);
McdAccount *mcd_account_manager_lookup_account_by_path (McdAccountManager *account_manager,
						       	const gchar *object_path);

McdStorage *mcd_account_manager_get_storage (McdAccountManager *manager);

McdConnectivityMonitor *mcd_account_manager_get_connectivity_monitor (
    McdAccountManager *self);

G_END_DECLS

#endif
