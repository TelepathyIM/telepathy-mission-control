/*
 * mcd-account.h - the Telepathy Account D-Bus interface (service side)
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

#ifndef __MCD_ACCOUNT_H__
#define __MCD_ACCOUNT_H__

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
/* auto-generated stubs */
#include "_gen/svc-Account.h"

G_BEGIN_DECLS
#define MCD_TYPE_ACCOUNT         (mcd_account_get_type ())
#define MCD_ACCOUNT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_ACCOUNT, McdAccount))
#define MCD_ACCOUNT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_ACCOUNT, McdAccountClass))
#define MCD_IS_ACCOUNT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_ACCOUNT))
#define MCD_IS_ACCOUNT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_ACCOUNT))
#define MCD_ACCOUNT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_ACCOUNT, McdAccountClass))

typedef struct _McdAccount McdAccount;
typedef struct _McdAccountPrivate McdAccountPrivate;
typedef struct _McdAccountClass McdAccountClass;

#include "mcd-connection.h"

struct _McdAccount
{
    GObject parent;
    McdAccountPrivate *priv;
};

struct _McdAccountClass
{
    GObjectClass parent_class;
};


#define MC_ACCOUNT_DBUS_OBJECT_BASE "/org/freedesktop/Telepathy/Account/"

GType mcd_account_get_type (void);
McdAccount *mcd_account_new (TpDBusDaemon *dbus_daemon, GKeyFile *keyfile,
			     const gchar *name);

gboolean mcd_account_delete (McdAccount *account, GError **error);

const gchar *mcd_account_get_unique_name (McdAccount *account);
const gchar *mcd_account_get_object_path (McdAccount *account);

GKeyFile *mcd_account_get_keyfile (McdAccount *account);

gboolean mcd_account_is_valid (McdAccount *account);
gboolean mcd_account_check_validity (McdAccount *account);

gboolean mcd_account_is_enabled (McdAccount *account);

const gchar *mcd_account_get_manager_name (McdAccount *account);
const gchar *mcd_account_get_protocol_name (McdAccount *account);

gboolean mcd_account_set_parameters (McdAccount *account, GHashTable *params,
				     GError **error);
GHashTable *mcd_account_get_parameters (McdAccount *account);
gboolean mcd_account_check_parameters (McdAccount *account);

void mcd_account_request_presence (McdAccount *account,
				   TpConnectionPresenceType type,
				   const gchar *status, const gchar *message);
void mcd_account_set_current_presence (McdAccount *account,
				       TpConnectionPresenceType presence,
				       const gchar *status,
				       const gchar *message);
void mcd_account_get_current_presence (McdAccount *account,
				       TpConnectionPresenceType *presence,
				       const gchar **status,
				       const gchar **message);
void mcd_account_get_requested_presence (McdAccount *account,
					 TpConnectionPresenceType *presence,
					 const gchar **status,
					 const gchar **message);

gboolean mcd_account_get_connect_automatically (McdAccount *account);
void mcd_account_get_automatic_presence (McdAccount *account,
					 TpConnectionPresenceType *presence,
					 const gchar **status,
					 const gchar **message);

void mcd_account_set_normalized_name (McdAccount *account, const gchar *name);
gchar *mcd_account_get_normalized_name (McdAccount *account);

gboolean mcd_account_set_avatar (McdAccount *account, const GArray *avatar,
				 const gchar *mime_type, const gchar *token,
				 GError **error);
void mcd_account_get_avatar (McdAccount *account, GArray **avatar,
			     gchar **mime_type);
void mcd_account_set_avatar_token (McdAccount *account, const gchar *token);
gchar *mcd_account_get_avatar_token (McdAccount *account);

void mcd_account_set_alias (McdAccount *account, const gchar *alias);

gchar *mcd_account_get_alias (McdAccount *account);

void mcd_account_set_connection_status (McdAccount *account,
					TpConnectionStatus status,
					TpConnectionStatusReason reason);
TpConnectionStatus mcd_account_get_connection_status (McdAccount *account);
TpConnectionStatusReason mcd_account_get_connection_status_reason (McdAccount *account);

McdConnection *mcd_account_get_connection (McdAccount *account);

gboolean mcd_account_request_channel_nmc4 (McdAccount *account,
					   const struct mcd_channel_request *req,
					   GError **error);

gchar *mcd_account_get_avatar_filename (McdAccount *account);

#endif
