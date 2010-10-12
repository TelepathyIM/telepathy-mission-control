/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

typedef struct _McdAccountPresencePrivate McdAccountPresencePrivate;

#include "mcd-connection.h"
#include "mcd-account-manager.h"

struct _McdAccount
{
    GObject parent;
    McdAccountPrivate *priv;
    McdAccountPresencePrivate *presence_priv;
};

typedef enum
{
  MCD_ACCOUNT_ERROR_SET_PARAMETER,
  MCD_ACCOUNT_ERROR_GET_PARAMETER,
} McdAccountError;

GQuark mcd_account_error_quark (void);

#define MCD_ACCOUNT_ERROR (mcd_account_error_quark ())

typedef void (*McdAccountLoadCb) (McdAccount *account,
                                  const GError *error,
                                  gpointer user_data);
typedef void (*McdAccountDeleteCb) (McdAccount *account,
                                    const GError *error,
                                    gpointer user_data);
typedef void (*McdAccountSetParameterCb) (McdAccount *account,
                                          const GError *error,
                                          gpointer user_data);
typedef void (*McdAccountGetParameterCb) (McdAccount *account,
                                          const GValue *value,
                                          const GError *error,
                                          gpointer user_data);

struct _McdAccountClass
{
    GObjectClass parent_class;
    void (*get_parameter) (McdAccount *account, const gchar *name,
                           McdAccountGetParameterCb callback,
                           gpointer user_data);
    void (*set_parameter) (McdAccount *account, const gchar *name,
                           const GValue *value,
                           McdAccountSetParameterCb callback,
                           gpointer user_data);
    void (*delete) (McdAccount *account, McdAccountDeleteCb callback,
                    gpointer user_data);
    void (*load) (McdAccount *account, McdAccountLoadCb callback,
                  gpointer user_data);
    gboolean (*check_request) (McdAccount *account, GHashTable *request,
                               GError **error);
    void (*_mc_reserved6) (void);
    void (*_mc_reserved7) (void);
};


#define MC_ACCOUNT_DBUS_OBJECT_BASE "/org/freedesktop/Telepathy/Account/"

GType mcd_account_get_type (void);
McdAccount *mcd_account_new (McdAccountManager *account_manager,
			     const gchar *name);

TpDBusDaemon *mcd_account_get_dbus_daemon (McdAccount *account);

void mcd_account_delete (McdAccount *account, McdAccountDeleteCb callback,
                         gpointer user_data);

const gchar *mcd_account_get_unique_name (McdAccount *account);
const gchar *mcd_account_get_object_path (McdAccount *account);

gboolean mcd_account_is_valid (McdAccount *account);

typedef void (*McdAccountCheckValidityCb) (McdAccount *account,
                                           gboolean valid,
                                           gpointer user_data);
void mcd_account_check_validity (McdAccount *account,
                                 McdAccountCheckValidityCb callback,
                                 gpointer user_data);

gboolean mcd_account_is_enabled (McdAccount *account);

const gchar *mcd_account_get_manager_name (McdAccount *account);
const gchar *mcd_account_get_protocol_name (McdAccount *account);
TpConnectionManager *mcd_account_get_cm (McdAccount *account);

void mcd_account_request_presence (McdAccount *account,
				   TpConnectionPresenceType type,
				   const gchar *status, const gchar *message);
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

gchar *mcd_account_get_normalized_name (McdAccount *account);

gchar *mcd_account_get_alias (McdAccount *account);

TpConnectionStatus mcd_account_get_connection_status (McdAccount *account);
TpConnectionStatusReason mcd_account_get_connection_status_reason (McdAccount *account);

McdConnection *mcd_account_get_connection (McdAccount *account);

gboolean mcd_account_check_request (McdAccount *account, GHashTable *request,
                                    GError **error);

gboolean mcd_account_parameter_is_secret (McdAccount *self,
                                              const gchar *name);

void mcd_account_property_changed (McdAccount *account, const gchar *name);
#endif
