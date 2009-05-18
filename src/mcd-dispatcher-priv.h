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

#ifndef MCD_DISPATCHER_PRIV_H
#define MCD_DISPATCHER_PRIV_H

#include "mcd-dispatcher.h"
#include "mcd-connection.h"

G_BEGIN_DECLS

/* retrieves the channel handlers' capabilities, in a format suitable for being
 * used as a parameter for the telepathy "AdvertiseCapabilities" method */
G_GNUC_INTERNAL GPtrArray *_mcd_dispatcher_get_channel_capabilities (
    McdDispatcher *dispatcher, const gchar *protocol);

/* retrieves the channel handlers' capabilities, in a format suitable for being
 * used as a parameter for the telepathy "SetSelfCapabilities" method */
G_GNUC_INTERNAL GPtrArray *_mcd_dispatcher_get_channel_enhanced_capabilities (
    McdDispatcher *dispatcher);

/* not exported */
void _mcd_dispatcher_add_request (McdDispatcher *dispatcher,
                                  McdAccount *account, McdChannel *channel);
G_GNUC_INTERNAL void _mcd_dispatcher_take_channels (
    McdDispatcher *dispatcher, GList *channels, gboolean requested);
G_GNUC_INTERNAL
void _mcd_dispatcher_add_channel_request (McdDispatcher *dispatcher,
                                          McdChannel *channel,
                                          McdChannel *request);
G_GNUC_INTERNAL
void _mcd_dispatcher_recover_channel (McdDispatcher *dispatcher,
                                      McdChannel *channel);

G_GNUC_INTERNAL void _mcd_dispatcher_add_connection (McdDispatcher *self,
    McdConnection *connection);

G_END_DECLS

#endif /* MCD_DISPATCHER_H */

