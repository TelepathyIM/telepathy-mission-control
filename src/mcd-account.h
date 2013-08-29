/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * mcd-account.h - the Telepathy Account D-Bus interface (service side)
 *
 * Copyright © 2008–2011 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright © 2008 Nokia Corporation
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

#include <telepathy-glib/telepathy-glib.h>

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
#include "mcd-account-manager.h"

struct _McdAccount
{
    GObject parent;
    McdAccountPrivate *priv;
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

struct _McdAccountClass
{
    GObjectClass parent_class;
    /* Used to be get_parameter; set_parameter; delete; load. These are not
     * implementeed in any known subclass of this class, so have been removed;
     * these padding fields are to preserve ABI compatibility.
     */
    GCallback dummy[4];
    gboolean (*check_request) (McdAccount *account, GHashTable *request,
                               GError **error);
    void (*_mc_reserved6) (void);
    void (*_mc_reserved7) (void);
};

GType mcd_account_get_type (void);

McdAccount *mcd_account_new (McdAccountManager *account_manager,
    const gchar *name,
    McdConnectivityMonitor *minotaur);

void mcd_account_delete (McdAccount *account, McdAccountDeleteCb callback,
                         gpointer user_data);

const gchar *mcd_account_get_unique_name (McdAccount *account);
const gchar *mcd_account_get_object_path (McdAccount *account);

gboolean mcd_account_is_valid (McdAccount *account);

typedef void (*McdAccountCheckValidityCb) (McdAccount *account,
                                           const GError *invalid_reason,
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

gboolean mcd_account_would_like_to_connect (McdAccount *account);

TpConnectionStatus mcd_account_get_connection_status (McdAccount *account);

McdConnection *mcd_account_get_connection (McdAccount *account);

gboolean mcd_account_check_request (McdAccount *account, GHashTable *request,
                                    GError **error);

gboolean mcd_account_parameter_is_secret (McdAccount *self,
                                              const gchar *name);

void mcd_account_altered_by_plugin (McdAccount *account, const gchar *name);

gchar * mcd_account_dup_display_name (McdAccount *self);

gboolean mcd_account_get_parameter (McdAccount *account, const gchar *name,
                           GValue *parameter,
                           GError **error);

gboolean mcd_account_get_parameter_of_known_type (McdAccount *account,
                                                  const gchar *name,
                                                  GType type,
                                                  GValue *parameter,
                                                  GError **error);

gchar * mcd_account_dup_icon (McdAccount *self);

gchar * mcd_account_dup_nickname (McdAccount *self);

McdConnectivityMonitor *mcd_account_get_connectivity_monitor (
    McdAccount *self);

gboolean mcd_account_get_waiting_for_connectivity (McdAccount *self);
void mcd_account_set_waiting_for_connectivity (McdAccount *self,
    gboolean waiting);

void mcd_account_connection_proceed (McdAccount *account, gboolean success);
void mcd_account_connection_proceed_with_reason
    (McdAccount *account, gboolean success, TpConnectionStatusReason reason);

G_END_DECLS

#endif
