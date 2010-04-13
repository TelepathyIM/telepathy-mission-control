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

#ifndef MCD_CHANNEL_PRIV_H
#define MCD_CHANNEL_PRIV_H

#include "mcd-channel.h"

G_BEGIN_DECLS

G_GNUC_INTERNAL
gboolean _mcd_channel_create_proxy (McdChannel *channel,
                                    TpConnection *connection,
                                    const gchar *object_path,
                                    const GHashTable *properties);

G_GNUC_INTERNAL
void _mcd_channel_set_status (McdChannel *channel, McdChannelStatus status);

/* not exported: */
G_GNUC_INTERNAL void _mcd_channel_undispatchable (McdChannel *self);

G_GNUC_INTERNAL
GHashTable *_mcd_channel_get_immutable_properties (McdChannel *channel);

G_GNUC_INTERNAL
const gchar *_mcd_channel_get_target_id (McdChannel *channel);
G_GNUC_INTERNAL
GHashTable *_mcd_channel_get_requested_properties (McdChannel *channel);
G_GNUC_INTERNAL
const gchar *_mcd_channel_get_request_path (McdChannel *channel);
G_GNUC_INTERNAL
const GList *_mcd_channel_get_satisfied_requests (McdChannel *channel,
                                                  gint64 *get_latest_time);
G_GNUC_INTERNAL
guint64 _mcd_channel_get_request_user_action_time (McdChannel *channel);
G_GNUC_INTERNAL
const gchar *_mcd_channel_get_request_preferred_handler (McdChannel *channel);
G_GNUC_INTERNAL
gboolean _mcd_channel_get_request_use_existing (McdChannel *channel);

G_GNUC_INTERNAL gboolean _mcd_channel_request_cancel (McdChannel *self,
                                                      GError **error);

G_GNUC_INTERNAL
void _mcd_channel_copy_details (McdChannel *channel, McdChannel *source);
G_GNUC_INTERNAL
void _mcd_channel_set_request_proxy (McdChannel *channel, McdChannel *source);

void _mcd_channel_close (McdChannel *channel);

G_GNUC_INTERNAL void _mcd_channel_depart (McdChannel *channel,
                                          TpChannelGroupChangeReason reason,
                                          const gchar *message);

G_END_DECLS
#endif

