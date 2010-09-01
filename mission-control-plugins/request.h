/* Mission Control plugin API - representation of a ChannelRequest
 *
 * Copyright © 2009 Nokia Corporation
 * Copyright © 2009-2010 Collabora Ltd.
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

#ifndef MCP_REQUEST_H
#define MCP_REQUEST_H

#ifndef _MCP_IN_MISSION_CONTROL_PLUGINS_H
#error Use <mission-control-plugins/mission-control-plugins.h> instead
#endif

#include <mission-control-plugins/request.h>

G_BEGIN_DECLS

typedef struct _McpRequest McpRequest;
typedef struct _McpRequestIface McpRequestIface;

/* Opaque token representing a request being stalled until an asynchronous
 * policy action */
typedef struct _McpRequestDelay McpRequestDelay;

#define MCP_TYPE_REQUEST \
  (mcp_request_get_type ())
#define MCP_REQUEST(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCP_TYPE_REQUEST, McpRequest))
#define MCP_IS_REQUEST(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCP_TYPE_REQUEST))
#define MCP_REQUEST_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), MCP_TYPE_REQUEST, \
                                  McpRequestIface))

GType mcp_request_get_type (void) G_GNUC_CONST;

/* utility functions which will work on any implementation of this interface */

gboolean mcp_request_find_request_by_type (McpRequest *self,
    guint start_from, GQuark channel_type,
    guint *ret_index, GHashTable **ret_ref_requested_properties);

/* virtual methods */

const gchar *mcp_request_get_account_path (McpRequest *self);
const gchar *mcp_request_get_protocol (McpRequest *self);
const gchar *mcp_request_get_cm_name (McpRequest *self);

gint64 mcp_request_get_user_action_time (McpRequest *self);
guint mcp_request_get_n_requests (McpRequest *self);
GHashTable *mcp_request_ref_nth_request (McpRequest *self, guint n);

void mcp_request_deny (McpRequest *self, GQuark domain, gint code,
    const gchar *message);

McpRequestDelay *mcp_request_start_delay (McpRequest *self);
void mcp_request_end_delay (McpRequest *self, McpRequestDelay *delay);

G_END_DECLS

#endif
