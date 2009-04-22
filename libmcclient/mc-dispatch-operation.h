/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mc-dispatch-operation.h - the Telepathy Account Manager D-Bus interface
 * (client side)
 *
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

#ifndef __LIBMCCLIENT_DISPATCH_OPERATION_H__
#define __LIBMCCLIENT_DISPATCH_OPERATION_H__

#include <telepathy-glib/proxy.h>

G_BEGIN_DECLS

typedef struct _McDispatchOperation McDispatchOperation;
typedef struct _McDispatchOperationClass McDispatchOperationClass;
typedef struct _McDispatchOperationPrivate McDispatchOperationPrivate;

GType mc_dispatch_operation_get_type (void);

#define MC_TYPE_DISPATCH_OPERATION (mc_dispatch_operation_get_type ())
#define MC_DISPATCH_OPERATION(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), MC_TYPE_DISPATCH_OPERATION, \
                                 McDispatchOperation))
#define MC_DISPATCH_OPERATION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), MC_TYPE_DISPATCH_OPERATION, \
                              McDispatchOperationClass))
#define MC_IS_DISPATCH_OPERATION(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MC_TYPE_DISPATCH_OPERATION))
#define MC_IS_DISPATCH_OPERATION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), MC_TYPE_DISPATCH_OPERATION))
#define MC_DISPATCH_OPERATION_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), MC_TYPE_DISPATCH_OPERATION, \
                                McDispatchOperationClass))

typedef struct {
    gchar *object_path;
    GHashTable *properties;
} McChannelDetails;

McDispatchOperation *mc_dispatch_operation_new_ready (TpDBusDaemon *dbus,
                                                      const GPtrArray *channels,
                                                      const gchar *object_path,
                                                      GHashTable *properties);

const gchar *mc_dispatch_operation_get_connection_path
                                            (McDispatchOperation *operation);
const gchar *mc_dispatch_operation_get_account_path
                                            (McDispatchOperation *operation);
const gchar * const *mc_dispatch_operation_get_possible_handlers
                                            (McDispatchOperation *operation);
GList *mc_dispatch_operation_get_channels (McDispatchOperation *operation);

G_END_DECLS

/* auto-generated stubs */
#include <libmcclient/_gen/cli-dispatch-operation.h>

#endif
