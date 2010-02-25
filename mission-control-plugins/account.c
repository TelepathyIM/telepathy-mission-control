/* Mission Control plugin API - interface to an McdAccountManager for plugins
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010 Collabora Ltd.
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
#include <mission-control-plugins/implementation.h>

GType
mcp_account_get_type (void)
{
  static gsize once = 0;
  static GType type = 0;

  if (g_once_init_enter (&once))
    {
      static const GTypeInfo info = {
          sizeof (McpAccountIface),
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

      type = g_type_register_static (G_TYPE_INTERFACE, "McpAccount", &info, 0);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
      g_once_init_leave (&once, 1);
    }

  return type;
}

void
mcp_account_set_value (const McpAccount *mcpa,
    const gchar *acct,
    const gchar *key,
    const gchar *value)
{
  McpAccountIface *iface = MCP_ACCOUNT_GET_IFACE (mcpa);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->set_value != NULL);

  iface->set_value (mcpa, acct, key, value);
}

gchar *
mcp_account_get_value (const McpAccount *mcpa,
    const gchar *acct,
    const gchar *key)
{
  McpAccountIface *iface = MCP_ACCOUNT_GET_IFACE (mcpa);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->set_value != NULL, NULL);

  return iface->get_value (mcpa, acct, key);
}

gboolean
mcp_account_parameter_is_secret (const McpAccount *mcpa,
    const gchar *acct,
    const gchar *key)
{
  McpAccountIface *iface = MCP_ACCOUNT_GET_IFACE (mcpa);

  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->is_secret != NULL, FALSE);

  return iface->is_secret (mcpa, acct, key);
}

void
mcp_account_parameter_make_secret (const McpAccount *mcpa,
    const gchar *acct,
    const gchar *key)
{
  McpAccountIface *iface = MCP_ACCOUNT_GET_IFACE (mcpa);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->make_secret != NULL);

  g_debug ("%s.%s should be secret", acct, key);
  return iface->make_secret (mcpa, acct, key);
}
