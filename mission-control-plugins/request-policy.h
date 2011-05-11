/* Mission Control plugin API - ChannelRequest policy hook.
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

#ifndef MCP_REQUEST_POLICY_H
#define MCP_REQUEST_POLICY_H

#ifndef _MCP_IN_MISSION_CONTROL_PLUGINS_H
#error Use <mission-control-plugins/mission-control-plugins.h> instead
#endif

#include <mission-control-plugins/request.h>

G_BEGIN_DECLS

/* API for plugins to implement */

typedef struct _McpRequestPolicy McpRequestPolicy;
typedef struct _McpRequestPolicyIface McpRequestPolicyIface;

#define MCP_TYPE_REQUEST_POLICY \
  (mcp_request_policy_get_type ())
#define MCP_REQUEST_POLICY(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCP_TYPE_REQUEST_POLICY, McpRequestPolicy))
#define MCP_IS_REQUEST_POLICY(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCP_TYPE_REQUEST_POLICY))
#define MCP_REQUEST_POLICY_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), MCP_TYPE_REQUEST_POLICY, \
                                  McpRequestPolicyIface))

GType mcp_request_policy_get_type (void) G_GNUC_CONST;

/* virtual methods */

typedef void (*McpRequestPolicyCb) (McpRequestPolicy *policy,
    McpRequest *request);

void mcp_request_policy_check (McpRequestPolicy *policy, McpRequest *request);

void mcp_request_policy_iface_implement_check (McpRequestPolicyIface *iface,
    void (*impl) (McpRequestPolicy *, McpRequest *));

struct _McpRequestPolicyIface {
    GTypeInterface parent;

    McpRequestPolicyCb check;
};

G_END_DECLS

#endif
