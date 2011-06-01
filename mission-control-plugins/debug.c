/* Mission Control plugin API - plugin debug infrastructure
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

#include <config.h>
#include <mission-control-plugins/mission-control-plugins.h>

static McpDebugFlags debug_flags;

static GDebugKey const keys[] = {
  { "account",                   MCP_DEBUG_ACCOUNT                   },
  { "account-storage",           MCP_DEBUG_ACCOUNT_STORAGE           },
  { "dbus-acl",                  MCP_DEBUG_DBUS_ACL                  },
  { "dispatch-operation",        MCP_DEBUG_DISPATCH_OPERATION        },
  { "dispatch-operation-policy", MCP_DEBUG_DISPATCH_OPERATION_POLICY },
  { "loader",                    MCP_DEBUG_LOADER                    },
  { "request",                   MCP_DEBUG_REQUEST                   },
  { "request-policy",            MCP_DEBUG_REQUEST_POLICY            },
  { NULL, 0 }
};

void
mcp_debug_init (void)
{
  const gchar *p_debug = g_getenv ("MCP_DEBUG");
  const gchar *d_debug = g_getenv ("MC_DEBUG");
  const gchar *debug = NULL;

  debug_flags = MCP_DEBUG_NONE;

  if (p_debug != NULL)
    debug = p_debug;
  else if (g_strcmp0 ("all", d_debug) == 0)
    debug = d_debug;
  else
    return;

  debug_flags = g_parse_debug_string (debug, keys, G_N_ELEMENTS (keys) - 1);
}

gboolean
mcp_is_debugging (McpDebugFlags flags)
{
#ifdef ENABLE_DEBUG
  return (flags & debug_flags) != 0;
#else
  return FALSE;
#endif
}
