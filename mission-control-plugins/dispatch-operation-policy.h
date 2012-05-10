/* Mission Control plugin API - ChannelDispatchOperation policy hook.
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

#ifndef MCP_DISPATCH_OPERATION_POLICY_H
#define MCP_DISPATCH_OPERATION_POLICY_H

#ifndef _MCP_IN_MISSION_CONTROL_PLUGINS_H
#error Use <mission-control-plugins/mission-control-plugins.h> instead
#endif

#include <mission-control-plugins/dispatch-operation.h>
#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

/* API for plugins to implement */

typedef struct _McpDispatchOperationPolicy McpDispatchOperationPolicy;
typedef struct _McpDispatchOperationPolicyIface McpDispatchOperationPolicyIface;

#define MCP_TYPE_DISPATCH_OPERATION_POLICY \
  (mcp_dispatch_operation_policy_get_type ())
#define MCP_DISPATCH_OPERATION_POLICY(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCP_TYPE_DISPATCH_OPERATION_POLICY, \
                               McpDispatchOperationPolicy))
#define MCP_IS_DISPATCH_OPERATION_POLICY(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCP_TYPE_DISPATCH_OPERATION_POLICY))
#define MCP_DISPATCH_OPERATION_POLICY_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), MCP_TYPE_DISPATCH_OPERATION_POLICY, \
                                  McpDispatchOperationPolicyIface))

GType mcp_dispatch_operation_policy_get_type (void) G_GNUC_CONST;

/* virtual methods */

typedef void (*McpDispatchOperationPolicyCb) (
    McpDispatchOperationPolicy *policy,
    McpDispatchOperation *dispatch_operation);

void mcp_dispatch_operation_policy_check (McpDispatchOperationPolicy *policy,
    McpDispatchOperation *dispatch_operation);

typedef void (*McpDispatchOperationPolicyHandlerIsSuitableAsync) (
    McpDispatchOperationPolicy *policy,
    TpClient *handler,
    const gchar *unique_name,
    McpDispatchOperation *dispatch_operation,
    GAsyncReadyCallback callback,
    gpointer user_data);
typedef gboolean (*McpDispatchOperationPolicyFinisher) (
    McpDispatchOperationPolicy *policy,
    GAsyncResult *result,
    GError **error);

void mcp_dispatch_operation_policy_handler_is_suitable_async (
    McpDispatchOperationPolicy *policy,
    TpClient *handler,
    const gchar *unique_name,
    McpDispatchOperation *dispatch_operation,
    GAsyncReadyCallback callback,
    gpointer user_data);
gboolean mcp_dispatch_operation_policy_handler_is_suitable_finish (
    McpDispatchOperationPolicy *policy,
    GAsyncResult *result,
    GError **error);

void mcp_dispatch_operation_policy_iface_implement_check (
    McpDispatchOperationPolicyIface *iface,
    McpDispatchOperationPolicyCb impl);

struct _McpDispatchOperationPolicyIface {
    GTypeInterface parent;

    McpDispatchOperationPolicyCb check;
    McpDispatchOperationPolicyHandlerIsSuitableAsync handler_is_suitable_async;
    McpDispatchOperationPolicyFinisher handler_is_suitable_finish;
};

G_END_DECLS

#endif
