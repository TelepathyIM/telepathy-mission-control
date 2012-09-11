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

/**
 * mcp_account_manager_set_value:
 * @mcpa: an #McpAccountManager instance
 * @account: the unique name of an account
 * @key: the setting whose value we wish to change: either an attribute
 *  like "DisplayName", or "param-" plus a parameter like "account"
 * @value: the new value, escaped as if for a #GKeyFile, or %NULL to delete
 *  the setting/parameter
 *
 * Inform Mission Control that @key has changed its value to @value.
 *
 * This function may either be called from mcp_account_storage_get(),
 * or just before emitting #McpAccountStorage::altered-one.
 *
 * New plugins should call mcp_account_manager_set_attribute() or
 * mcp_account_manager_set_parameter() instead.
 */
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

/**
 * mcp_account_manager_set_attribute:
 * @mcpa: an #McpAccountManager instance
 * @account: the unique name of an account
 * @attribute: the name of an attribute, such as "DisplayName"
 * @value: (allow-none): the new value, or %NULL to delete the attribute
 * @flags: flags for the new value (only used if @value is non-%NULL)
 *
 * Inform Mission Control that @attribute has changed its value to @value.
 *
 * If @value is a floating reference, Mission Control will take ownership
 * of it, much like g_variant_builder_add_value().
 *
 * This function may either be called from mcp_account_storage_get(),
 * or just before emitting #McpAccountStorage::altered-one.
 */
void
mcp_account_manager_set_attribute (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *attribute,
    GVariant *value,
    McpAttributeFlags flags)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->set_attribute != NULL);

  iface->set_attribute (mcpa, account, attribute, value, flags);
}

/**
 * mcp_account_manager_set_parameter:
 * @mcpa: an #McpAccountManager instance
 * @account: the unique name of an account
 * @parameter: the name of a parameter, such as "account", without
 *  the "param-" prefix
 * @value: (allow-none): the new value, or %NULL to delete the parameter
 * @flags: flags for the new value (only used if @value is non-%NULL)
 *
 * Inform Mission Control that @parameter has changed its value to @value.
 *
 * If @value is a floating reference, Mission Control will take ownership
 * of it, much like g_variant_builder_add_value().
 *
 * This function may either be called from mcp_account_storage_get(),
 * or just before emitting #McpAccountStorage::altered-one.
 */
void
mcp_account_manager_set_parameter (const McpAccountManager *mcpa,
    const gchar *account,
    const gchar *parameter,
    GVariant *value,
    McpParameterFlags flags)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->set_parameter != NULL);

  iface->set_parameter (mcpa, account, parameter, value, flags);
}

/**
 * mcp_account_manage_list_keys:
 * @mcpa: a #McpAccountManager instance
 * @account: the unique name of an account
 *
 * <!-- -->
 *
 * Returns: (transfer full): a list of all keys (attributes and
 *  "param-"-prefixed parameters) stored for @account by any plugin
 */
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
 * @key: the setting whose value we wish to fetch: either an attribute
 *  like "DisplayName", or "param-" plus a parameter like "account"
 *
 * Fetch a copy of the current value of an account setting held by
 * the account manager.
 *
 * Returns: (transfer full): the value of @key
 */
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
 * @key: the constant string "param-", plus a parameter name like
 *  "account" or "password"
 *
 * Determine whether a given account parameter is secret.
 * Generally this is determined by MC and passed down to plugins,
 * but any #McpAccountStorage plugin may decide a parameter is
 * secret, in which case the return value for this call will
 * indicate that fact too.
 *
 * For historical reasons, this function only operates on parameters,
 * but requires its argument to be prefixed with "param-".
 *
 * Returns: %TRUE for secret settings, %FALSE otherwise
 */
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
 * @key: the constant string "param-", plus a parameter name like
 *  "account" or "password"
 *
 * Flag an account setting as secret for the lifetime of this
 * #McpAccountManager. For instance, this should be called if
 * @key has been retrieved from gnome-keyring.
 *
 * For historical reasons, this function only operates on parameters,
 * but requires its argument to be prefixed with "param-".
 */
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

/**
 * mcp_account_manager_escape_value_from_keyfile:
 * @mcpa: a #McpAccountManager
 * @value: a value with a supported #GType
 *
 * Escape @value so it could be passed to g_key_file_set_value().
 * For instance, escaping the boolean value TRUE returns "true",
 * and escaping the string value containing one space returns "\s".
 *
 * It is a programming error to use an unsupported type.
 * The supported types are currently %G_TYPE_STRING, %G_TYPE_BOOLEAN,
 * %G_TYPE_INT, %G_TYPE_UINT, %G_TYPE_INT64, %G_TYPE_UINT64, %G_TYPE_UCHAR,
 * %G_TYPE_STRV, %DBUS_TYPE_G_OBJECT_PATH and %TP_ARRAY_TYPE_OBJECT_PATH_LIST.
 *
 * Returns: the escaped form of @value
 */
gchar *
mcp_account_manager_escape_value_for_keyfile (const McpAccountManager *mcpa,
    const GValue *value)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->escape_value_for_keyfile != NULL, NULL);

  return iface->escape_value_for_keyfile (mcpa, value);
}

/**
 * mcp_account_manager_escape_variant_for_keyfile:
 * @mcpa: a #McpAccountManager
 * @variant: a #GVariant with a supported #GVariantType
 *
 * Escape @variant so it could be passed to g_key_file_set_value().
 * For instance, escaping the boolean value TRUE returns "true",
 * and escaping the string value containing one space returns "\s".
 *
 * It is a programming error to use an unsupported type.
 * The supported types are currently %G_VARIANT_TYPE_STRING,
 * %G_VARIANT_TYPE_BOOLEAN, %G_VARIANT_TYPE_INT32, %G_VARIANT_TYPE_UINT32,
 * %G_VARIANT_TYPE_INT64, %G_VARIANT_TYPE_UINT64, %G_VARIANT_TYPE_BYTE,
 * %G_VARIANT_TYPE_STRING_ARRAY, %G_VARIANT_TYPE_OBJECT_PATH and
 * %G_VARIANT_TYPE_OBJECT_PATH_ARRAY.
 *
 * Returns: (transfer full): the escaped form of @variant
 */
gchar *
mcp_account_manager_escape_variant_for_keyfile (const McpAccountManager *mcpa,
    GVariant *variant)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->escape_variant_for_keyfile != NULL, NULL);

  return iface->escape_variant_for_keyfile (mcpa, variant);
}

/**
 * mcp_account_manager_unescape_value_from_keyfile:
 * @mcpa: a #McpAccountManager
 * @escaped: an escaped string as returned by g_key_file_get_value()
 * @value: a value to populate, with a supported #GType
 * @error: used to raise an error if %FALSE is returned
 *
 * Attempt to interpret @escaped as a value of @value's type.
 * If successful, put it in @value and return %TRUE.
 *
 * It is a programming error to try to escape an unsupported type.
 * The supported types are currently %G_TYPE_STRING, %G_TYPE_BOOLEAN,
 * %G_TYPE_INT, %G_TYPE_UINT, %G_TYPE_INT64, %G_TYPE_UINT64, %G_TYPE_UCHAR,
 * %G_TYPE_STRV, %DBUS_TYPE_G_OBJECT_PATH and %TP_ARRAY_TYPE_OBJECT_PATH_LIST.
 *
 * Returns: %TRUE if @value was filled in
 */
gboolean
mcp_account_manager_unescape_value_from_keyfile (const McpAccountManager *mcpa,
    const gchar *escaped,
    GValue *value,
    GError **error)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->unescape_value_from_keyfile != NULL, FALSE);

  return iface->unescape_value_from_keyfile (mcpa, escaped, value, error);
}

/**
 * mcp_account_manager_init_value_for_attribute:
 * @mcpa: a #McpAccountManager
 * @value: a zero-filled value to initialize
 * @attribute: a supported Mission Control attribute
 *
 * If @attribute is a known Mission Control attribute, initialize @value
 * with an appropriate type for @attribute and return %TRUE. Otherwise,
 * return %FALSE.
 *
 * Returns: %TRUE if @value was initialized
 */
gboolean
mcp_account_manager_init_value_for_attribute (const McpAccountManager *mcpa,
    GValue *value,
    const gchar *attribute)
{
  McpAccountManagerIface *iface = MCP_ACCOUNT_MANAGER_GET_IFACE (mcpa);

  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->init_value_for_attribute != NULL, FALSE);

  return iface->init_value_for_attribute (mcpa, value, attribute);
}
