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
#include "mcd-account-compat.h"
#include "mcd-account-config.h"
#include "mcd-channel.h"
#include "mcd-dbusprop.h"

/* auto-generated stubs */
#include "_gen/svc-Account_Interface_ChannelRequests.h"
#include "_gen/svc-Account_Interface_Compat.h"
#include "_gen/svc-Account_Interface_Conditions.h"
#include "_gen/svc-Account_Interface_Stats.h"

G_GNUC_INTERNAL void _mcd_account_maybe_autoconnect (McdAccount *account);
G_GNUC_INTERNAL void _mcd_account_connect (McdAccount *account,
                                           GHashTable *params);

G_GNUC_INTERNAL gboolean _mcd_account_set_parameters (McdAccount *account,
                                                      GHashTable *params,
                                                      const gchar **unset,
                                                      GPtrArray *not_yet,
                                                      GError **error);

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

G_GNUC_INTERNAL gchar *_mcd_account_get_avatar_filename (McdAccount *account);

G_GNUC_INTERNAL void _mcd_account_tp_connection_changed (McdAccount *account);

G_GNUC_INTERNAL void _mcd_account_load (McdAccount *account,
                                        McdAccountLoadCb callback,
                                        gpointer user_data);
G_GNUC_INTERNAL void _mcd_account_set_connection (McdAccount *account,
                                                  McdConnection *connection);

typedef void (*McdOnlineRequestCb) (McdAccount *account, gpointer userdata,
				    const GError *error);
void _mcd_account_online_request (McdAccount *account,
                                  McdOnlineRequestCb callback,
                                  gpointer userdata);
void _mcd_account_connect_with_auto_presence (McdAccount *account);
G_GNUC_INTERNAL
void _mcd_account_online_request_completed (McdAccount *account,
                                            GError *error);

typedef struct {
    McdOnlineRequestCb callback;
    gpointer user_data;
} McdOnlineRequestData;

G_GNUC_INTERNAL
GList *_mcd_account_get_online_requests (McdAccount *account);


static inline void
_mcd_account_write_conf (McdAccount *account)
{
    McdAccountManager *account_manager;

    account_manager = mcd_account_get_account_manager (account);
    g_return_if_fail (MCD_IS_ACCOUNT_MANAGER (account_manager));

    mcd_account_manager_write_conf (account_manager);
}

G_GNUC_INTERNAL void _mcd_account_compat_class_init (McdAccountClass *klass);

extern const McdDBusProp account_compat_properties[];

void account_compat_iface_init (McSvcAccountInterfaceCompatClass *iface,
				     gpointer iface_data);

G_GNUC_INTERNAL void _mcd_account_connection_begin (McdAccount *account);
G_GNUC_INTERNAL void _mcd_account_connection_class_init (McdAccountClass *klass);
G_GNUC_INTERNAL McdTransport *_mcd_account_connection_get_transport
    (McdAccount *account);

extern const McdDBusProp account_channelrequests_properties[];

G_GNUC_INTERNAL McdChannel *_mcd_account_create_request (McdAccount *account,
    GHashTable *properties, gint64 user_action_time,
    const gchar *preferred_handler, gboolean use_existing,
    gboolean proceeding, GError **error);

G_GNUC_INTERNAL void _mcd_account_proceed_with_request (McdAccount *account,
                                                        McdChannel *channel);

void account_channelrequests_iface_init
    (McSvcAccountInterfaceChannelRequestsClass *iface, gpointer iface_data);

typedef struct _McdAccountConnectionContext McdAccountConnectionContext;

G_GNUC_INTERNAL
McdAccountConnectionContext *_mcd_account_get_connection_context
    (McdAccount *self);

G_GNUC_INTERNAL
void _mcd_account_set_connection_context (McdAccount *self,
                                          McdAccountConnectionContext *c);

G_GNUC_INTERNAL void _mcd_account_connection_context_free
    (McdAccountConnectionContext *c);

G_GNUC_INTERNAL GHashTable *_mcd_account_dup_parameters (McdAccount *account);

extern const McdDBusProp account_conditions_properties[];

void account_conditions_iface_init (McSvcAccountInterfaceConditionsClass *iface,
				    gpointer iface_data);

extern const McdDBusProp account_stats_properties[];

void account_stats_iface_init (McSvcAccountInterfaceStatsClass *iface,
                               gpointer iface_data);
void account_stats_instance_init (TpSvcDBusProperties *self);

#endif /* __MCD_ACCOUNT_PRIV_H__ */

