/* Mission Control plugin API - internals, for MC to use for account storage
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

#ifndef MCP_ACCOUNT_MANAGER_H
#define MCP_ACCOUNT_MANAGER_H

#ifndef _MCP_IN_MISSION_CONTROL_PLUGINS_H
#error Use <mission-control-plugins/mission-control-plugins.h> instead
#endif

G_BEGIN_DECLS

typedef struct _McpAccountManager McpAccountManager;
typedef struct _McpAccountManagerIface McpAccountManagerIface;

#define MCP_TYPE_ACCOUNT_MANAGER (mcp_account_manager_get_type ())

#define MCP_ACCOUNT_MANAGER(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCP_TYPE_ACCOUNT_MANAGER, \
      McpAccountManager))

#define MCP_IS_ACCOUNT_MANAGER(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCP_TYPE_ACCOUNT_MANAGER))

#define MCP_ACCOUNT_MANAGER_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), MCP_TYPE_ACCOUNT_MANAGER, \
      McpAccountManagerIface))

GType mcp_account_manager_get_type (void) G_GNUC_CONST;

void mcp_account_manager_set_value (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *key,
    const gchar *value);

void mcp_account_manager_set_attribute (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *attribute,
    GVariant *value,
    McpAttributeFlags flags);

void mcp_account_manager_set_parameter (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *parameter,
    GVariant *value,
    McpParameterFlags flags);

gchar * mcp_account_manager_get_value (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *key);

gboolean mcp_account_manager_parameter_is_secret (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *key);

void mcp_account_manager_parameter_make_secret (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *key);

gchar * mcp_account_manager_get_unique_name (McpAccountManager *mcpa,
    const gchar *manager,
    const gchar *protocol,
    const GHashTable *params);

GStrv mcp_account_manager_list_keys (const McpAccountManager *mcpa,
    const gchar *account);

gchar *mcp_account_manager_escape_value_for_keyfile (
    const McpAccountManager *mcpa,
    const GValue *value);

gchar *mcp_account_manager_escape_variant_for_keyfile (
    const McpAccountManager *mcpa,
    GVariant *variant);

gboolean mcp_account_manager_unescape_value_from_keyfile (
    const McpAccountManager *mcpa,
    const gchar *escaped,
    GValue *value,
    GError **error);

gboolean mcp_account_manager_init_value_for_attribute (
    const McpAccountManager *mcpa,
    GValue *value,
    const gchar *attribute);

G_END_DECLS

#endif
