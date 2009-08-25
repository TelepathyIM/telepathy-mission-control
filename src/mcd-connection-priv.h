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

#ifndef __MCD_CONNECTION_PRIV_H__
#define __MCD_CONNECTION_PRIV_H__

#include "mcd-connection.h"

G_BEGIN_DECLS

G_GNUC_INTERNAL void _mcd_connection_request_presence (McdConnection *self,
    TpConnectionPresenceType type, const gchar *status, const gchar *message);

G_GNUC_INTERNAL void _mcd_connection_connect (McdConnection *connection,
                                              GHashTable *params);
G_GNUC_INTERNAL
void _mcd_connection_update_property (McdConnection *connection,
                                      const gchar *name, const GValue *value);
G_GNUC_INTERNAL
void _mcd_connection_set_tp_connection (McdConnection *connection,
                                        const gchar *bus_name,
                                        const gchar *obj_path, GError **error);

G_GNUC_INTERNAL void _mcd_connection_start_dispatching (McdConnection *self,
    GPtrArray *client_caps);

G_GNUC_INTERNAL gboolean _mcd_connection_is_ready (McdConnection *self);

G_GNUC_INTERNAL void _mcd_connection_set_nickname (McdConnection *self,
                                                   const gchar *nickname);

G_GNUC_INTERNAL void _mcd_connection_set_avatar (McdConnection *self,
                                                 const GArray *avatar,
                                                 const gchar *mime_type);
G_GNUC_INTERNAL void _mcd_connection_update_client_caps (McdConnection *self,
    GPtrArray *client_caps);

G_END_DECLS

#endif

