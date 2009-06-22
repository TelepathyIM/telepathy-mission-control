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

#ifndef __MCD_DISPATCH_OPERATION_H__
#define __MCD_DISPATCH_OPERATION_H__

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>

G_BEGIN_DECLS
#define MCD_TYPE_DISPATCH_OPERATION         (mcd_dispatch_operation_get_type ())
#define MCD_DISPATCH_OPERATION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_DISPATCH_OPERATION, McdDispatchOperation))
#define MCD_DISPATCH_OPERATION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_DISPATCH_OPERATION, McdDispatchOperationClass))
#define MCD_IS_DISPATCH_OPERATION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_DISPATCH_OPERATION))
#define MCD_IS_DISPATCH_OPERATION_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_DISPATCH_OPERATION))
#define MCD_DISPATCH_OPERATION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_DISPATCH_OPERATION, McdDispatchOperationClass))

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


#define MC_DISPATCH_OPERATION_DBUS_OBJECT_BASE "/org/freedesktop/Telepathy/DispatchOperation/"

GType mcd_dispatch_operation_get_type (void);

const gchar *mcd_dispatch_operation_get_path (McdDispatchOperation *operation);
GHashTable *mcd_dispatch_operation_get_properties
    (McdDispatchOperation *operation);
gboolean mcd_dispatch_operation_is_claimed (McdDispatchOperation *operation);
const gchar *mcd_dispatch_operation_get_handler
    (McdDispatchOperation *operation);
void mcd_dispatch_operation_handle_with (McdDispatchOperation *operation,
                                         const gchar *handler_path,
                                         GError **error);

G_END_DECLS
#endif
