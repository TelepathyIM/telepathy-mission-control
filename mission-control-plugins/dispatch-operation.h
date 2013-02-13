/* Mission Control plugin API - representation of a ChannelDispatchOperation
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
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

#ifndef MCP_DISPATCH_OPERATION_H
#define MCP_DISPATCH_OPERATION_H

#ifndef _MCP_IN_MISSION_CONTROL_PLUGINS_H
#error Use <mission-control-plugins/mission-control-plugins.h> instead
#endif

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

typedef struct _McpDispatchOperation McpDispatchOperation;
typedef struct _McpDispatchOperationIface McpDispatchOperationIface;

/* Opaque token representing a DO being stalled until an asynchronous
 * policy action */
typedef struct _McpDispatchOperationDelay McpDispatchOperationDelay;

#define MCP_TYPE_DISPATCH_OPERATION \
  (mcp_dispatch_operation_get_type ())
#define MCP_DISPATCH_OPERATION(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCP_TYPE_DISPATCH_OPERATION, \
                               McpDispatchOperation))
#define MCP_IS_DISPATCH_OPERATION(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCP_TYPE_DISPATCH_OPERATION))
#define MCP_DISPATCH_OPERATION_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), MCP_TYPE_DISPATCH_OPERATION, \
                                  McpDispatchOperationIface))

GType mcp_dispatch_operation_get_type (void) G_GNUC_CONST;

/* utility functions which will work on any implementation of this interface */

gboolean mcp_dispatch_operation_find_channel_by_type (
    McpDispatchOperation *self,
    guint start_from, TpHandleType handle_type, GQuark channel_type,
    guint *ret_index, gchar **ret_dup_path,
    GHashTable **ret_ref_immutable_properties, TpChannel **ret_ref_channel);

TpConnection *mcp_dispatch_operation_ref_connection (
    McpDispatchOperation *self);
TpChannel *mcp_dispatch_operation_ref_nth_channel (McpDispatchOperation *self,
    guint n);

/* virtual methods */

const gchar *mcp_dispatch_operation_get_account_path (
    McpDispatchOperation *self);

const gchar *mcp_dispatch_operation_get_connection_path (
    McpDispatchOperation *self);

const gchar *mcp_dispatch_operation_get_protocol (McpDispatchOperation *self);

const gchar *mcp_dispatch_operation_get_cm_name (McpDispatchOperation *self);

guint mcp_dispatch_operation_get_n_channels (McpDispatchOperation *self);
const gchar *mcp_dispatch_operation_get_nth_channel_path (
    McpDispatchOperation *self, guint n);
GHashTable *mcp_dispatch_operation_ref_nth_channel_properties (
    McpDispatchOperation *self, guint n);

McpDispatchOperationDelay *mcp_dispatch_operation_start_delay (
    McpDispatchOperation *self);
void mcp_dispatch_operation_end_delay (McpDispatchOperation *self,
    McpDispatchOperationDelay *delay);

#ifndef MC_DISABLE_DEPRECATED
G_DEPRECATED_FOR (mcp_dispatch_operation_close_channels or tp_call_channel_hangup_async)
void mcp_dispatch_operation_leave_channels (McpDispatchOperation *self,
    gboolean wait_for_observers, TpChannelGroupChangeReason reason,
    const gchar *message);
#endif

void mcp_dispatch_operation_close_channels (McpDispatchOperation *self,
    gboolean wait_for_observers);
void mcp_dispatch_operation_destroy_channels (McpDispatchOperation *self,
    gboolean wait_for_observers);

G_END_DECLS

#endif
