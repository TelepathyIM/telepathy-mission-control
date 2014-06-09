/* Mission Control plugin API - for plugin debug messages
 *
 * Copyright © 2011 Nokia Corporation
 * Copyright © 2011 Collabora Ltd.
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

#ifndef MCP_DEBUG_H
#define MCP_DEBUG_H

#ifndef _MCP_IN_MISSION_CONTROL_PLUGINS_H
#error Use <mission-control-plugins/mission-control-plugins.h> instead
#endif

#define MCP_DEBUG(_type, _fmt, ...) \
  G_STMT_START { if (mcp_is_debugging (_type)) \
      g_debug ("%s: " _fmt, G_STRFUNC, ##__VA_ARGS__); } G_STMT_END

G_BEGIN_DECLS

typedef enum {
  MCP_DEBUG_NONE                      = 0,
  MCP_DEBUG_ACCOUNT                   = 1 << 0,
  MCP_DEBUG_ACCOUNT_STORAGE           = 1 << 1,
  MCP_DEBUG_DBUS_ACL                  = 1 << 2,
  MCP_DEBUG_DISPATCH_OPERATION        = 1 << 3,
  MCP_DEBUG_DISPATCH_OPERATION_POLICY = 1 << 4,
  MCP_DEBUG_LOADER                    = 1 << 5,
  MCP_DEBUG_REQUEST                   = 1 << 6,
  MCP_DEBUG_REQUEST_POLICY            = 1 << 7,
} McpDebugFlags;

gboolean mcp_is_debugging (McpDebugFlags type);
void mcp_debug_init (void);

G_END_DECLS

#endif
