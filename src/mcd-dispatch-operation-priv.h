/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mcd-operation.h - the Telepathy DispatchOperation D-Bus interface (service side)
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

#ifndef __MCD_DISPATCH_OPERATION_PRIV_H__
#define __MCD_DISPATCH_OPERATION_PRIV_H__

#include "mcd-dispatch-operation.h"

G_BEGIN_DECLS

G_GNUC_INTERNAL McdDispatchOperation *_mcd_dispatch_operation_new (
    TpDBusDaemon *dbus_daemon, GList *channels, GStrv possible_handlers);
G_GNUC_INTERNAL void _mcd_dispatch_operation_lose_channel (
    McdDispatchOperation *self, McdChannel *channel, GList **channels);

G_GNUC_INTERNAL GPtrArray *_mcd_dispatch_operation_dup_channel_details (
    McdDispatchOperation *self);
G_GNUC_INTERNAL gboolean _mcd_dispatch_operation_is_finished (
    McdDispatchOperation *self);
G_GNUC_INTERNAL void _mcd_dispatch_operation_block_finished (
    McdDispatchOperation *self);
G_GNUC_INTERNAL void _mcd_dispatch_operation_unblock_finished (
    McdDispatchOperation *self);
G_GNUC_INTERNAL const gchar *_mcd_dispatch_operation_get_claimer (
    McdDispatchOperation *operation);
G_GNUC_INTERNAL gboolean _mcd_dispatch_operation_finish (
    McdDispatchOperation *operation);

G_END_DECLS

#endif

