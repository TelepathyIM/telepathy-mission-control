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

#include <telepathy-glib/dbus.h>
/* auto-generated stubs */
#include "_gen/svc-Account_Manager.h"

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

struct _McdAccountManager
{
    GObject parent;
    McdAccountManagerPrivate *priv;
};

struct _McdAccountManagerClass
{
    GObjectClass parent_class;
};


#define MC_ACCOUNT_MANAGER_DBUS_OBJECT "/org/freedesktop/Telepathy/AccountManager"

GType mcd_account_manager_get_type (void);
McdAccountManager *mcd_account_manager_new (TpDBusDaemon *dbus_daemon);

#endif
