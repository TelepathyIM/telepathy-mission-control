/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
 *
 * Contact: Naba Kumar  <naba.kumar@nokia.com>
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

#ifndef __MCD_ACCOUNT_PRIV_H__
#define __MCD_ACCOUNT_PRIV_H__

#include "mcd-account.h"
#include "mcd-account-config.h"
#include "mcd-channel.h"
#include "mcd-dbusprop.h"
#include "request.h"

#include <telepathy-glib/proxy-subclass.h>

/* auto-generated stubs */
#include "_gen/svc-Account_Interface_Conditions.h"
#include "_gen/svc-Account_Interface_External_Password_Storage.h"
#include "_gen/svc-Account_Interface_Hidden.h"

#include "_gen/cli-Connection_Manager_Interface_Account_Storage.h"

G_GNUC_INTERNAL void _mcd_account_maybe_autoconnect (McdAccount *account);
G_GNUC_INTERNAL void _mcd_account_connect (McdAccount *account,
                                           GHashTable *params);


typedef void (McdAccountSetParametersCb) (McdAccount *account,
                                          GPtrArray *not_yet,
                                          const GError *error,
                                          gpointer user_data);

G_GNUC_INTERNAL void _mcd_account_set_parameters (McdAccount *account,
                                                  GHashTable *params,
                                                  const gchar **unset,
                                                  McdAccountSetParametersCb callback,
                                                  gpointer user_data);

G_GNUC_INTERNAL void _mcd_account_request_temporary_presence (McdAccount *self,
    TpConnectionPresenceType type, const gchar *status);

G_GNUC_INTERNAL GKeyFile *_mcd_account_get_keyfile (McdAccount *account);

G_GNUC_INTERNAL void _mcd_account_set_has_been_online (McdAccount *account);

G_GNUC_INTERNAL void _mcd_account_set_normalized_name (McdAccount *account,
                                                       const gchar *name);

G_GNUC_INTERNAL gboolean _mcd_account_set_avatar (McdAccount *account,
                                                  const GArray *avatar,
                                                  const gchar *mime_type,
                                                  const gchar *token,
                                                  GError **error);
G_GNUC_INTERNAL void _mcd_account_get_avatar (McdAccount *account,
                                              GArray **avatar,
                                              gchar **mime_type);
G_GNUC_INTERNAL void _mcd_account_set_avatar_token (McdAccount *account,
                                                    const gchar *token);
G_GNUC_INTERNAL gchar *_mcd_account_get_avatar_token (McdAccount *account);

G_GNUC_INTERNAL void _mcd_account_set_alias (McdAccount *account,
                                             const gchar *alias);

G_GNUC_INTERNAL GPtrArray *_mcd_account_get_supersedes (McdAccount *self);

G_GNUC_INTERNAL void _mcd_account_tp_connection_changed (McdAccount *account,
    TpConnection *tp_conn);

G_GNUC_INTERNAL void _mcd_account_load (McdAccount *account,
                                        McdAccountLoadCb callback,
                                        gpointer user_data);
G_GNUC_INTERNAL void _mcd_account_set_connection (McdAccount *account,
                                                  McdConnection *connection);
G_GNUC_INTERNAL void _mcd_account_set_connection_status
    (McdAccount *account, TpConnectionStatus status,
     TpConnectionStatusReason reason, TpConnection *tp_conn,
     const gchar *dbus_error, const GHashTable *details);

typedef void (*McdOnlineRequestCb) (McdAccount *account, gpointer userdata,
				    const GError *error);
void _mcd_account_online_request (McdAccount *account,
                                  McdOnlineRequestCb callback,
                                  gpointer userdata);
void _mcd_account_connect_with_auto_presence (McdAccount *account,
                                              gboolean user_initiated);

G_GNUC_INTERNAL McdStorage *_mcd_account_get_storage (McdAccount *account);

static inline void
_mcd_account_write_conf (McdAccount *account)
{
    McdStorage *storage = _mcd_account_get_storage (account);

    g_return_if_fail (MCD_IS_STORAGE (storage));

    mcd_storage_commit (storage, mcd_account_get_unique_name (account));
}

G_GNUC_INTERNAL void _mcd_account_connection_begin (McdAccount *account,
                                                    gboolean user_initiated);

extern const McdDBusProp account_channelrequests_properties[];

G_GNUC_INTERNAL McdChannel *_mcd_account_create_request (
    McdClientRegistry *clients, McdAccount *account,
    GHashTable *properties, gint64 user_action_time,
    const gchar *preferred_handler, GHashTable *request_metadata,
    gboolean use_existing,
    McdRequest **request_out, GError **error);

typedef struct _McdAccountConnectionContext McdAccountConnectionContext;

G_GNUC_INTERNAL
McdAccountConnectionContext *_mcd_account_get_connection_context
    (McdAccount *self);

G_GNUC_INTERNAL
void _mcd_account_set_connection_context (McdAccount *self,
                                          McdAccountConnectionContext *c);

G_GNUC_INTERNAL void _mcd_account_connection_context_free
    (McdAccountConnectionContext *c);

typedef void (*McdAccountDupParametersCb) (McdAccount *account,
                                           GHashTable *params,
                                           gpointer user_data);

G_GNUC_INTERNAL G_GNUC_WARN_UNUSED_RESULT
GHashTable *_mcd_account_dup_parameters (McdAccount *account);

extern const McdDBusProp account_conditions_properties[];

void account_conditions_iface_init (McSvcAccountInterfaceConditionsClass *iface,
				    gpointer iface_data);

G_GNUC_INTERNAL gboolean _mcd_account_check_request_real (McdAccount *account,
                                                          GHashTable *request,
                                                          GError **error);

G_GNUC_INTERNAL gboolean _mcd_account_get_always_on (McdAccount *self);

G_GNUC_INTERNAL void _mcd_account_set_changing_presence (McdAccount *self,
                                                         gboolean value);
G_GNUC_INTERNAL gboolean _mcd_account_set_enabled (McdAccount *account,
                                                   gboolean enabled,
                                                   gboolean write_out,
                                                   McdDBusPropSetFlags flags,
                                                   GError **error);

G_GNUC_INTERNAL gboolean _mcd_account_presence_type_is_settable (
        TpConnectionPresenceType type);

gboolean _mcd_account_is_hidden (McdAccount *account);

G_GNUC_INTERNAL gboolean _mcd_account_needs_dispatch (McdAccount *account);

G_GNUC_INTERNAL void _mcd_account_reconnect (McdAccount *self,
    gboolean user_initiated);


#endif /* __MCD_ACCOUNT_PRIV_H__ */
