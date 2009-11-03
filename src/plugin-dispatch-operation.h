/* Representation of a dispatch operation as presented to plugins. This is
 * deliberately a "smaller" API than McdDispatchOperation.
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef MCD_PLUGIN_DISPATCH_OPERATION_H
#define MCD_PLUGIN_DISPATCH_OPERATION_H

#include <mission-control-plugins/mission-control-plugins.h>

#include "mcd-dispatch-operation-priv.h"

G_BEGIN_DECLS

typedef struct _McdPluginDispatchOperation
    McdPluginDispatchOperation;
typedef struct _McdPluginDispatchOperationClass
    McdPluginDispatchOperationClass;
typedef struct _McdPluginDispatchOperationPrivate
    McdPluginDispatchOperationPrivate;

G_GNUC_INTERNAL GType _mcd_plugin_dispatch_operation_get_type (void);

#define MCD_TYPE_PLUGIN_DISPATCH_OPERATION \
  (_mcd_plugin_dispatch_operation_get_type ())
#define MCD_PLUGIN_DISPATCH_OPERATION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MCD_TYPE_PLUGIN_DISPATCH_OPERATION, \
                               McdPluginDispatchOperation))
#define MCD_PLUGIN_DISPATCH_OPERATION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MCD_TYPE_PLUGIN_DISPATCH_OPERATION, \
                            McdPluginDispatchOperationClass))
#define MCD_IS_PLUGIN_DISPATCH_OPERATION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCD_TYPE_PLUGIN_DISPATCH_OPERATION))
#define MCD_IS_PLUGIN_DISPATCH_OPERATION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MCD_TYPE_PLUGIN_DISPATCH_OPERATION))
#define MCD_PLUGIN_DISPATCH_OPERATION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_PLUGIN_DISPATCH_OPERATION, \
                              McdPluginDispatchOperationClass))

G_GNUC_INTERNAL
McdPluginDispatchOperation *_mcd_plugin_dispatch_operation_new (
    McdDispatchOperation *real_cdo);

G_GNUC_INTERNAL void _mcd_plugin_dispatch_operation_observers_finished (
    McdPluginDispatchOperation *self);

G_GNUC_INTERNAL gboolean _mcd_plugin_dispatch_operation_will_terminate (
    McdPluginDispatchOperation *self);

G_END_DECLS

#endif
