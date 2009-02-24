/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * mc-account-manager.h - the Telepathy Account Manager D-Bus interface
 * (client side)
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

#ifndef __LIBMCCLIENT_ACCOUNT_MANAGER_H__
#define __LIBMCCLIENT_ACCOUNT_MANAGER_H__

#include <telepathy-glib/proxy.h>

G_BEGIN_DECLS

typedef struct _McAccountManager McAccountManager;
typedef struct _McAccountManagerClass McAccountManagerClass;
typedef struct _McAccountManagerPrivate McAccountManagerPrivate;

#include <libmcclient/mc-account.h>

#ifndef MC_ACCOUNT_MANAGER_DBUS_SERVICE
#define MC_ACCOUNT_MANAGER_DBUS_SERVICE \
    "org.freedesktop.Telepathy.AccountManager"
#define MC_ACCOUNT_MANAGER_DBUS_OBJECT \
    "/org/freedesktop/Telepathy/AccountManager"
#endif

GType mc_account_manager_get_type (void);

#define MC_TYPE_ACCOUNT_MANAGER \
  (mc_account_manager_get_type ())
#define MC_ACCOUNT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MC_TYPE_ACCOUNT_MANAGER, \
                               McAccountManager))
#define MC_ACCOUNT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MC_TYPE_ACCOUNT_MANAGER, \
                            McAccountManagerClass))
#define MC_IS_ACCOUNT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MC_TYPE_ACCOUNT_MANAGER))
#define MC_IS_ACCOUNT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MC_TYPE_ACCOUNT_MANAGER))
#define MC_ACCOUNT_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MC_TYPE_ACCOUNT_MANAGER, \
                              McAccountManagerClass))

McAccountManager *mc_account_manager_new (TpDBusDaemon *dbus);

typedef void (*McAccountManagerWhenReadyCb) (McAccountManager *manager,
					     const GError *error,
					     gpointer user_data);

void mc_account_manager_call_when_ready (McAccountManager *manager,
					 McAccountManagerWhenReadyCb callback,
					 gpointer user_data);

const gchar * const *mc_account_manager_get_valid_accounts (McAccountManager
							    *manager);
const gchar * const *mc_account_manager_get_invalid_accounts (McAccountManager
							      *manager);

typedef void (*McAccountManagerWhenReadyObjectCb) (McAccountManager *manager,
						   const GError *error,
						   gpointer user_data,
						   GObject *weak_object);

void mc_account_manager_call_when_iface_ready (McAccountManager *manager,
				    GQuark interface,
				    McAccountManagerWhenReadyObjectCb callback,
				    gpointer user_data,
				    GDestroyNotify destroy,
				    GObject *weak_object);
void mc_account_manager_call_when_ready_with_accounts (
				    McAccountManager *manager,
				    McAccountManagerWhenReadyObjectCb callback,
				    gpointer user_data,
				    GDestroyNotify destroy,
				    GObject *weak_object, ...);

McAccount *mc_account_manager_get_account (McAccountManager *manager,
					   const gchar *account_name);

typedef gboolean (*McAccountFilterFunc) (McAccount *account,
					 gpointer user_data);
GList *mc_account_manager_list_accounts (McAccountManager *manager,
					 McAccountFilterFunc filter,
					 gpointer user_data);
G_END_DECLS

/* auto-generated stubs */
#include <libmcclient/_gen/cli-account-manager.h>

#endif
