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

#include "config.h"

#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/implementation.h>
#include <mission-control-plugins/debug-internal.h>

#define MCP_DEBUG_TYPE MCP_DEBUG_ACCOUNT

/**
 * SECTION:account
 * @title: McpAccountManager
 * @short_description: Object representing the account manager, implemented
 *    by Mission Control
 * @see_also: #McpAccountStorage
 * @include: mission-control-plugins/mission-control-plugins.h
 *
 * This object represents the Telepathy AccountManager.
 *
 * Most virtual methods on the McpAccountStorageIface interface receive an
 * object provided by Mission Control that implements this interface.
 * It can be used to manipulate Mission Control's in-memory cache of accounts.
 *
 * Only Mission Control should implement this interface.
 */

GType
mcp_account_manager_get_type (void)
{
  static gsize once = 0;
  static GType type = 0;

  if (g_once_init_enter (&once))
    {
      static const GTypeInfo info = {
          sizeof (McpAccountManagerIface),
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

      type = g_type_register_static (G_TYPE_INTERFACE,
          "McpAccountManager", &info, 0);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
      g_once_init_leave (&once, 1);
    }

  return type;
}

void
mcp_account_manager_set_value (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *key,
    const gchar *value)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->set_value != NULL);

  iface->set_value (mcpa, account, key, value);
}

GStrv
mcp_account_manager_list_keys (const McpAccountManager *mcpa,
    const gchar *account)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->list_keys != NULL, NULL);
  g_return_val_if_fail (account != NULL, NULL);

  return iface->list_keys (mcpa, account);
}

/**
 * mcp_account_manager_get_value:
 * @mcpa: an #McpAccountManager instance
 * @account: the unique name of an account
 * @key: the setting whose value we want to retrieve
 *
 * Fetch a copy of the current value of an account setting held by
 * the #McdAccountManager.
 *
 * Returns: a #gchar* which should be freed when the caller is done with it.
 **/
gchar *
mcp_account_manager_get_value (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *key)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->set_value != NULL, NULL);

  return iface->get_value (mcpa, account, key);
}

/**
 * mcp_account_manager_parameter_is_secret:
 * @mcpa: an #McpAccountManager instance
 * @account: the unique name of an account
 * @key: the setting whose value we want to retrieve
 *
 * Determine whether a given account parameter is secret.
 * generally this is determined by MC and passed down to us,
 * but any #McpAccountStorage plugin may decide a setting is
 * secret, in which case the return value for this call will
 * indicate that fact.
 *
 * Returns: a #gboolean, %TRUE for secret settings, %FALSE otherwise
 **/
gboolean
mcp_account_manager_parameter_is_secret (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *key)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->is_secret != NULL, FALSE);

  return iface->is_secret (mcpa, account, key);
}

/**
 * mcp_account_manager_parameter_make_secret:
 * @mcpa: an #McpAccountManager instance
 * @account: the unique name of an account
 * @key: the setting whose value we want to retrieve
 *
 * Flag an account setting as secret for the lifetime of this
 * #McpAccountManager and its corresponding #McdAccountManager
 **/
void
mcp_account_manager_parameter_make_secret (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *key)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->make_secret != NULL);

  g_debug ("%s.%s should be secret", account, key);
  iface->make_secret (mcpa, account, key);
}

/**
 * mcp_account_manager_get_unique_name:
 * @mcpa: an #McpAccountManager instance
 * @manager: the name of the manager
 * @protocol: the name of the protocol
 * @params: A gchar * / GValue * hash table of account parameters.
 *
 * Generate and return the canonical unique name of this [new] account.
 * Should not be called for accounts which have already had a name
 * assigned: Intended for use when a plugin encounters an account which
 * MC has not previously seen before (ie one created by a 3rd party
 * in the back-end that the plugin in question provides an interface to).
 *
 * Returns: the newly allocated account name, which should be freed
 * once the caller is done with it.
 */
gchar *
mcp_account_manager_get_unique_name (McpAccountManager *mcpa,
    const gchar *manager,
    const gchar *protocol,
    const GHashTable *params)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->unique_name != NULL, NULL);

  return iface->unique_name (mcpa, manager, protocol, params);
}
