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

#include <mission-control-plugins/mission-control-plugins.h>

struct _McpRequestPolicyIface {
    GTypeInterface parent;

    void (*check) (McpRequestPolicy *, McpRequest *);
};

GType
mcp_request_policy_get_type (void)
{
  static gsize once = 0;
  static GType type = 0;

  if (g_once_init_enter (&once))
    {
      static const GTypeInfo info = {
          sizeof (McpRequestPolicyIface),
          NULL, /* base_init */
          NULL, /* base_finalize */
          NULL, /* class_init */
          NULL, /* class_finalize */
          NULL, /* class_data */
          0, /* instance_size */
          0, /* n_preallocs */
          NULL, /* instance_init */
          NULL /* value_table */
      };

      type = g_type_register_static (G_TYPE_INTERFACE, "McpRequestPolicy",
          &info, 0);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);

      g_once_init_leave (&once, 1);
    }

  return type;
}

void
mcp_request_policy_check (McpRequestPolicy *policy,
    McpRequest *request)
{
  McpRequestPolicyIface *iface = MCP_REQUEST_POLICY_GET_IFACE (policy);

  g_return_if_fail (iface != NULL);

  if (iface->check != NULL)
    iface->check (policy, request);
}

void
mcp_request_policy_iface_implement_check (McpRequestPolicyIface *iface,
    void (*impl) (McpRequestPolicy *, McpRequest *))
{
  iface->check = impl;
}
