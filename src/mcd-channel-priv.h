/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright © 2007-2009 Nokia Corporation.
 * Copyright © 2009-2010 Collabora Ltd.
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

#include "client-registry.h"
#include "mcd-channel.h"
#include "request.h"

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

G_GNUC_INTERNAL McdRequest *_mcd_channel_get_request (McdChannel *self);

G_GNUC_INTERNAL
GHashTable *_mcd_channel_get_requested_properties (McdChannel *channel);
G_GNUC_INTERNAL
GHashTable *_mcd_channel_get_satisfied_requests (McdChannel *channel,
                                                  gint64 *get_latest_time);
G_GNUC_INTERNAL
const gchar *_mcd_channel_get_request_preferred_handler (McdChannel *channel);
G_GNUC_INTERNAL
gboolean _mcd_channel_get_request_use_existing (McdChannel *channel);

G_GNUC_INTERNAL void _mcd_channel_request_proceed (McdChannel *self,
    DBusGMethodInvocation *context);

G_GNUC_INTERNAL
void _mcd_channel_copy_details (McdChannel *channel, McdChannel *source);
G_GNUC_INTERNAL
void _mcd_channel_set_request_proxy (McdChannel *channel, McdChannel *source);

void _mcd_channel_close (McdChannel *channel);

G_GNUC_INTERNAL void _mcd_channel_depart (McdChannel *channel,
                                          TpChannelGroupChangeReason reason,
                                          const gchar *message);

G_GNUC_INTERNAL gboolean _mcd_channel_is_primary_for_path (McdChannel *self,
    const gchar *channel_path);

G_GNUC_INTERNAL McdChannel *_mcd_channel_new_request (McdRequest *request);

G_END_DECLS
#endif

