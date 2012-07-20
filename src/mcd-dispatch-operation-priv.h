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

#include <telepathy-glib/telepathy-glib.h>

#include "client-registry.h"
#include "mcd-handler-map-priv.h"

G_BEGIN_DECLS

typedef struct _McdDispatchOperation McdDispatchOperation;
typedef struct _McdDispatchOperationPrivate McdDispatchOperationPrivate;
typedef struct _McdDispatchOperationClass McdDispatchOperationClass;

#include "mcd-account.h"

struct _McdDispatchOperation
{
    GObject parent;
    McdDispatchOperationPrivate *priv;
};

struct _McdDispatchOperationClass
{
    GObjectClass parent_class;
};

#define CDO_INTERNAL_HANDLER ""

#define MC_DISPATCH_OPERATION_DBUS_OBJECT_BASE "/org/freedesktop/Telepathy/DispatchOperation/"

G_GNUC_INTERNAL GType _mcd_dispatch_operation_get_type (void);

G_GNUC_INTERNAL const gchar *_mcd_dispatch_operation_get_path
    (McdDispatchOperation *operation);
G_GNUC_INTERNAL GHashTable *_mcd_dispatch_operation_get_properties
    (McdDispatchOperation *operation);
G_GNUC_INTERNAL void _mcd_dispatch_operation_approve
    (McdDispatchOperation *self, const gchar *preferred_handler);

#define MCD_TYPE_DISPATCH_OPERATION         (_mcd_dispatch_operation_get_type ())
#define MCD_DISPATCH_OPERATION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_DISPATCH_OPERATION, McdDispatchOperation))
#define MCD_DISPATCH_OPERATION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_DISPATCH_OPERATION, McdDispatchOperationClass))
#define MCD_IS_DISPATCH_OPERATION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_DISPATCH_OPERATION))
#define MCD_IS_DISPATCH_OPERATION_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_DISPATCH_OPERATION))
#define MCD_DISPATCH_OPERATION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_DISPATCH_OPERATION, McdDispatchOperationClass))

G_GNUC_INTERNAL McdDispatchOperation *_mcd_dispatch_operation_new (
    McdClientRegistry *client_registry,
    McdHandlerMap *handler_map,
    gboolean needs_approval,
    gboolean observe_only,
    McdChannel *channel,
    const gchar * const *possible_handlers);

G_GNUC_INTERNAL gboolean _mcd_dispatch_operation_has_channel (
    McdDispatchOperation *self, McdChannel *channel);
G_GNUC_INTERNAL McdChannel *_mcd_dispatch_operation_peek_channel (
    McdDispatchOperation *self);
G_GNUC_INTERNAL McdChannel *_mcd_dispatch_operation_dup_channel (
    McdDispatchOperation *self);

G_GNUC_INTERNAL gboolean _mcd_dispatch_operation_is_finished (
    McdDispatchOperation *self);
G_GNUC_INTERNAL gboolean _mcd_dispatch_operation_needs_approval (
    McdDispatchOperation *self);

G_GNUC_INTERNAL gboolean _mcd_dispatch_operation_get_cancelled (
    McdDispatchOperation *self);

G_GNUC_INTERNAL void _mcd_dispatch_operation_run_clients (
    McdDispatchOperation *self);

G_GNUC_INTERNAL const gchar *_mcd_dispatch_operation_get_account_path (
    McdDispatchOperation *self);
G_GNUC_INTERNAL const gchar *_mcd_dispatch_operation_get_protocol (
    McdDispatchOperation *self);
G_GNUC_INTERNAL const gchar *_mcd_dispatch_operation_get_cm_name (
    McdDispatchOperation *self);
G_GNUC_INTERNAL const gchar *_mcd_dispatch_operation_get_connection_path (
    McdDispatchOperation *self);

G_GNUC_INTERNAL void _mcd_dispatch_operation_start_plugin_delay (
    McdDispatchOperation *self);
G_GNUC_INTERNAL void _mcd_dispatch_operation_end_plugin_delay (
    McdDispatchOperation *self);

G_GNUC_INTERNAL void _mcd_dispatch_operation_leave_channels (
    McdDispatchOperation *self, TpChannelGroupChangeReason reason,
    const gchar *message);
G_GNUC_INTERNAL void _mcd_dispatch_operation_close_channels (
    McdDispatchOperation *self);
G_GNUC_INTERNAL void _mcd_dispatch_operation_destroy_channels (
    McdDispatchOperation *self);
G_GNUC_INTERNAL void _mcd_dispatch_operation_forget_channels (
    McdDispatchOperation *self);

G_GNUC_INTERNAL gboolean _mcd_dispatch_operation_is_internal (
    McdDispatchOperation *self);
G_GNUC_INTERNAL gboolean _mcd_dispatch_operation_has_invoked_observers (
    McdDispatchOperation *self);

G_END_DECLS

#endif
