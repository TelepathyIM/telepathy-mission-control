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
#include "client-registry.h"

G_BEGIN_DECLS

/* not exported */
G_GNUC_INTERNAL void _mcd_dispatcher_add_channel (
    McdDispatcher *dispatcher,
    McdChannel *channel,
    gboolean requested,
    gboolean only_observe);
G_GNUC_INTERNAL
void _mcd_dispatcher_add_channel_request (McdDispatcher *dispatcher,
                                          McdChannel *channel,
                                          McdChannel *request);
G_GNUC_INTERNAL
void _mcd_dispatcher_recover_channel (McdDispatcher *dispatcher,
                                      McdChannel *channel,
                                      const gchar *account_path);

G_GNUC_INTERNAL void _mcd_dispatcher_add_connection (McdDispatcher *self,
    McdConnection *connection);

G_GNUC_INTERNAL GPtrArray *_mcd_dispatcher_dup_client_caps (
    McdDispatcher *self);

G_END_DECLS

#endif /* MCD_DISPATCHER_H */

