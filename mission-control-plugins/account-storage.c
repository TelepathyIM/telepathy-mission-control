/* Mission Control plugin API - Account Storage plugins.
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

/**
 * SECTION:account-storage
 * @title: McpAccountStorage
 * @short_description: Account Storage object, implemented by plugins
 * @see_also:
 * @include: mission-control-plugins/mission-control-plugins.h
 *
 * Plugins may implement #McpAccountStorage in order to provide account
 * parameter storage backends to the AccountManager object.
 *
 * To do so, the plugin must implement a #GObject subclass that implements
 * #McpAccountStorage, then return an instance of that subclass from
 * mcp_plugin_ref_nth_object().
 *
 * Many methods take "the unique name of an account" as an argument.
 * In this plugin, that means the unique "tail" of the account's
 * object path, for instance "gabble/jabber/chris_40example_2ecom".
 * The account's full object path is obtained by prepending
 * %TP_ACCOUNT_OBJECT_PATH_BASE.
 *
 * A complete implementation of this interface with all methods would
 * look something like this:
 *
 * <example><programlisting>
 * G_DEFINE_TYPE_WITH_CODE (FooPlugin, foo_plugin,
 *    G_TYPE_OBJECT,
 *    G_IMPLEMENT_INTERFACE (...);
 *    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_STORAGE,
 *      account_storage_iface_init));
 * /<!-- -->* ... *<!-- -->/
 * static void
 * account_storage_iface_init (McpAccountStorageIface *iface)
 * {
 *   iface->priority = 0;
 *   iface->name = "foo";
 *   iface->desc = "The FOO storage backend";
 *   iface->provider = "org.freedesktop.Telepathy.MissionControl5.FooStorage";
 *
 *   iface->get = foo_plugin_get;
 *   iface->set = foo_plugin_get;
 *   iface->delete = foo_plugin_delete;
 *   iface->commit = foo_plugin_commit;
 *   iface->commit_one = foo_plugin_commit_one;
 *   iface->list = foo_plugin_list;
 *   iface->ready = foo_plugin_ready;
 *   iface->get_identifier = foo_plugin_get_identifier;
 *   iface->get_additional_info = foo_plugin_get_additional_info;
 *   iface->get_restrictions = foo_plugin_get_restrictions;
 *   iface->create = foo_plugin_create;
 *   iface->owns = foo_plugin_owns;
 *   iface->set_attribute = foo_plugin_set_attribute;
 *   iface->set_parameter = foo_plugin_set_parameter;
 * }
 * </programlisting></example>
 *
 * A single object can implement more than one interface; It is currently
 * unlikely that you would find it useful to implement anything other than
 * an account storage plugin in an account storage object, though.
 */

#include "config.h"

#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/mcp-signals-marshal.h>
#include <mission-control-plugins/implementation.h>
#include <mission-control-plugins/debug-internal.h>
#include <glib.h>

#define MCP_DEBUG_TYPE  MCP_DEBUG_ACCOUNT_STORAGE

#ifdef ENABLE_DEBUG

#define SDEBUG(_p, _format, ...) \
  DEBUG("%s: " _format, \
        (_p != NULL) ? mcp_account_storage_name (_p) : "NULL", ##__VA_ARGS__)

#else  /* ENABLE_DEBUG */

#define SDEBUG(_p, _format, ...) do {} while (0);

#endif /* ENABLE_DEBUG */

enum
{
  CREATED,
  ALTERED,
  TOGGLED,
  DELETED,
  ALTERED_ONE,
  RECONNECT,
  NO_SIGNAL
};

static guint signals[NO_SIGNAL] = { 0 };

static gboolean
default_set (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key,
    const gchar *val)
{
  return FALSE;
}

static gboolean
default_set_attribute (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account,
    const gchar *attribute,
    GVariant *value,
    McpAttributeFlags flags)
{
  return FALSE;
}

static gboolean
default_set_parameter (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account,
    const gchar *parameter,
    GVariant *value,
    McpParameterFlags flags)
{
  return FALSE;
}

static gboolean
default_owns (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account)
{
  /* This has the side-effect of pushing the "manager" key back into @am,
   * but that should be a no-op in practice: we always call this
   * method in priority order and stop at the first one that says "yes",
   * and @am's idea of what "manager" is should have come from that same
   * plugin anyway. */
  return mcp_account_storage_get (storage, am, account, "manager");
}

static void
class_init (gpointer klass,
    gpointer data)
{
  GType type = G_TYPE_FROM_CLASS (klass);
  McpAccountStorageIface *iface = klass;

  iface->owns = default_owns;
  iface->set = default_set;
  iface->set_attribute = default_set_attribute;
  iface->set_parameter = default_set_parameter;

  if (signals[CREATED] != 0)
    {
      DEBUG ("already registered signals");
      return;
    }

  /**
   * McpAccountStorage::created
   * @account: the unique name of the created account
   *
   * Emitted if an external entity creates an account
   * in the backend the emitting plugin handles.
   *
   * Should not be fired until mcp_account_storage_ready() has been called
   *
   */
  signals[CREATED] = g_signal_new ("created",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE,
      1, G_TYPE_STRING);

  /**
   * McpAccountStorage::altered
   * @account: the unique name of the altered account
   *
   * This signal does not appear to be fully implemented
   * (see <ulink href="https://bugs.freedesktop.org/show_bug.cgi?id=28288"
   *  >freedesktop.org bug 28288</ulink>).
   * Emit #McpAccountStorage::altered-one instead.
   *
   * Should not be fired until mcp_account_storage_ready() has been called
   *
   */
  signals[ALTERED] = g_signal_new ("altered",
      type, G_SIGNAL_RUN_LAST | G_SIGNAL_DEPRECATED, 0, NULL, NULL,
      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE,
      1, G_TYPE_STRING);

  /**
   * McpAccountStorage::altered-one
   * @account: the unique name of the altered account
   * @name: the name of the altered property (its key)
   *
   * Emitted if an external entity alters an account
   * in the backend that the emitting plugin handles.
   *
   * Before emitting this signal, the plugin must call
   * either mcp_account_manager_set_attribute(),
   * either mcp_account_manager_set_parameter() or
   * mcp_account_manager_set_value() to push the new value
   * into the account manager.
   *
   * Note that mcp_account_manager_set_parameter() does not use the
   * "param-" prefix, but this signal and mcp_account_manager_set_value()
   * both do.
   *
   * Should not be fired until mcp_account_storage_ready() has been called
   */
  signals[ALTERED_ONE] = g_signal_new ("altered-one",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      _mcp_marshal_VOID__STRING_STRING, G_TYPE_NONE,
      2, G_TYPE_STRING, G_TYPE_STRING);


  /**
   * McpAccountStorage::deleted
   * @account: the unique name of the deleted account
   *
   * Emitted if an external entity deletes an account
   * in the backend the emitting plugin handles.
   *
   * Should not be fired until mcp_account_storage_ready() has been called
   *
   */
  signals[DELETED] = g_signal_new ("deleted",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE,
      1, G_TYPE_STRING);

  /**
   * McpAccountStorage::toggled
   * @account: the unique name of the toggled account
   * @enabled: #gboolean indicating whether the account is enabled
   *
   * Emitted if an external entity enables/disables an account
   * in the backend the emitting plugin handles. This is similar to
   * emitting #McpAccountStorage::altered-one for the attribute
   * "Enabled", except that the plugin is not required to call
   * a function like mcp_account_manager_set_value() first.
   *
   * Should not be fired until mcp_account_storage_ready() has been called
   *
   */
  signals[TOGGLED] = g_signal_new ("toggled",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      _mcp_marshal_VOID__STRING_BOOLEAN, G_TYPE_NONE,
      2, G_TYPE_STRING, G_TYPE_BOOLEAN);

  /**
   * McpAccountStorage::reconnect
   * @account: the unique name of the account to reconnect
   *
   * emitted if an external entity modified important parameters of the
   * account and a reconnection is required in order to apply them.
   *
   * Should not be fired until mcp_account_storage_ready() has been called
   **/
  signals[RECONNECT] = g_signal_new ("reconnect",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE,
      1, G_TYPE_STRING);
}

GType
mcp_account_storage_get_type (void)
{
  static gsize once = 0;
  static GType type = 0;

  if (g_once_init_enter (&once))
    {
      static const GTypeInfo info =
        {
          sizeof (McpAccountStorageIface),
          NULL, /* base_init      */
          NULL, /* base_finalize  */
          class_init, /* class_init     */
          NULL, /* class_finalize */
          NULL, /* class_data     */
          0,    /* instance_size  */
          0,    /* n_preallocs    */
          NULL, /* instance_init  */
          NULL  /* value_table    */
        };

      type = g_type_register_static (G_TYPE_INTERFACE,
          "McpAccountStorage", &info, 0);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);

      g_once_init_leave (&once, 1);
    }

  return type;
}

/**
 * McpAccountStorage:
 *
 * An object implementing the account storage plugin interface.
 */

/**
 * McpAccountStorageIface:
 * @parent: the standard fields for an interface
 * @priority: returned by mcp_account_storage_priority()
 * @name: returned by mcp_account_storage_name()
 * @desc: returned by mcp_account_storage_description()
 * @provider: returned by mcp_account_storage_provider()
 * @set: implementation of mcp_account_storage_set()
 * @get: implementation of mcp_account_storage_get()
 * @delete: implementation of mcp_account_storage_delete()
 * @commit: implementation of mcp_account_storage_commit()
 * @list: implementation of mcp_account_storage_list()
 * @ready: implementation of mcp_account_storage_ready()
 * @commit_one: implementation of mcp_account_storage_commit_one()
 * @get_identifier: implementation of mcp_account_storage_get_identifier()
 * @get_additional_info: implementation of
 *  mcp_account_storage_get_additional_info()
 * @get_restrictions: implementation of mcp_account_storage_get_restrictions()
 * @create: implementation of mcp_account_storage_create()
 *
 * The interface vtable for an account storage plugin.
 */

void
mcp_account_storage_iface_set_priority (McpAccountStorageIface *iface,
    guint prio)
{
  iface->priority = prio;
}

void
mcp_account_storage_iface_set_name (McpAccountStorageIface *iface,
    const gchar *name)
{
  iface->name = name;
}

void
mcp_account_storage_iface_set_desc (McpAccountStorageIface *iface,
    const gchar *desc)
{
  iface->desc = desc;
}

void
mcp_account_storage_iface_set_provider (McpAccountStorageIface *iface,
    const gchar *provider)
{
  iface->provider = provider;
}

void
mcp_account_storage_iface_implement_get (McpAccountStorageIface *iface,
    McpAccountStorageGetFunc method)
{
  iface->get = method;
}

void
mcp_account_storage_iface_implement_set (McpAccountStorageIface *iface,
    McpAccountStorageSetFunc method)
{
  iface->set = method;
}

void
mcp_account_storage_iface_implement_delete (McpAccountStorageIface *iface,
    McpAccountStorageDeleteFunc method)
{
  iface->delete = method;
}

void
mcp_account_storage_iface_implement_commit (McpAccountStorageIface *iface,
    McpAccountStorageCommitFunc method)
{
  iface->commit = method;
}

void
mcp_account_storage_iface_implement_commit_one (McpAccountStorageIface *iface,
    McpAccountStorageCommitOneFunc method)
{
  iface->commit_one = method;
}

void
mcp_account_storage_iface_implement_list (McpAccountStorageIface *iface,
    McpAccountStorageListFunc method)
{
  iface->list = method;
}

void
mcp_account_storage_iface_implement_ready (McpAccountStorageIface *iface,
    McpAccountStorageReadyFunc method)
{
  iface->ready = method;
}

void
mcp_account_storage_iface_implement_get_identifier (
    McpAccountStorageIface *iface,
    McpAccountStorageGetIdentifierFunc method)
{
  iface->get_identifier = method;
}

void
mcp_account_storage_iface_implement_get_additional_info (
    McpAccountStorageIface *iface,
    McpAccountStorageGetAdditionalInfoFunc method)
{
  iface->get_additional_info = method;
}

void
mcp_account_storage_iface_implement_get_restrictions (
    McpAccountStorageIface *iface,
    McpAccountStorageGetRestrictionsFunc method)
{
  iface->get_restrictions = method;
}

void
mcp_account_storage_iface_implement_create (
    McpAccountStorageIface *iface,
    McpAccountStorageCreate method)
{
  iface->create = method;
}

/**
 * mcp_account_storage_priority:
 * @storage: an #McpAccountStorage instance
 *
 * Gets the priority for this plugin.
 *
 * Priorities currently run from MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_DEFAULT
 * (the default storage plugin priority) upwards. More-positive numbers
 * are higher priority.
 *
 * Plugins at a higher priority then MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_KEYRING
 * used to have the opportunity to "steal" passwords from the gnome keyring.
 * It is no longer significant.
 *
 * Plugins at a lower priority than the default plugin will never be asked to
 * store any details, although they may still be asked to list them at startup
 * time, and may asynchronously notify MC of accounts via the signals above.
 *
 * When loading accounts at startup, plugins are consulted in order from
 * lowest to highest, so that higher priority plugins may overrule settings
 * from lower priority plugins.
 *
 * Loading all the accounts is only done at startup, before the dbus name
 * is claimed, and is therefore the only time plugins are allowed to indulge
 * in blocking calls (indeed, they are expected to carry out this operation,
 * and ONLY this operation, synchronously).
 *
 * When values are being set, the plugins are invoked from highest priority
 * to lowest, with the first plugin that claims a setting being assigned
 * ownership, and all lower priority plugins being asked to delete the
 * setting in question.
 *
 * Returns: the priority of this plugin
 **/
gint
mcp_account_storage_priority (const McpAccountStorage *storage)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, -1);

  return iface->priority;
}

/**
 * McpAccountStorageGetFunc:
 * @storage: the account storage plugin
 * @am: object used to call back into the account manager
 * @account: the unique name of the account
 * @key: the setting whose value we wish to fetch: either an attribute
 *  like "DisplayName", or "param-" plus a parameter like "account"
 *
 * An implementation of mcp_account_storage_get().
 *
 * Returns: %TRUE if @storage is responsible for @account
 */

/**
 * mcp_account_storage_get:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique name of the account
 * @key: the setting whose value we wish to fetch: either an attribute
 *  like "DisplayName", or "param-" plus a parameter like "account"
 *
 * Get a value from the plugin's in-memory cache.
 * Before emitting this signal, the plugin must call
 * either mcp_account_manager_set_attribute(),
 * mcp_account_manager_set_parameter(),
 * or mcp_account_manager_set_value() and (if appropriate)
 * mcp_account_manager_parameter_make_secret()
 * before returning from this method call.
 *
 * Note that mcp_account_manager_set_parameter() does not use the
 * "param-" prefix, even if called from this function.
 *
 * If @key is %NULL the plugin should iterate through all attributes and
 * parameters, and push each of them into @am, as if this method had
 * been called once for each attribute or parameter. It must then return
 * %TRUE if any attributes or parameters were found, or %FALSE if it
 * was not responsible for @account.
 *
 * Returns: %TRUE if @storage is responsible for @account
 */
gboolean
mcp_account_storage_get (const McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account,
    const gchar *key)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  SDEBUG (storage, "");
  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->get != NULL, FALSE);

  return iface->get (storage, am, account, key);
}

/**
 * McpAccountStorageSetFunc:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique name of the account
 * @key: the setting whose value we wish to store: either an attribute
 *  like "DisplayName", or "param-" plus a parameter like "account"
 * @val: a non-%NULL value for @key
 *
 * An implementation of mcp_account_storage_set().
 *
 * Returns: %TRUE if @storage is responsible for @account
 */

/**
 * mcp_account_storage_set:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique name of the account
 * @key: the non-%NULL setting whose value we wish to store: either an
 *  attribute like "DisplayName", or "param-" plus a parameter like "account"
 * @value: a value to associate with @key, escaped as if for a #GKeyFile
 *
 * The plugin is expected to either quickly and synchronously
 * update its internal cache of values with @value, or to
 * decline to store the setting.
 *
 * The plugin is not expected to write to its long term storage
 * at this point. It can expect Mission Control to call either
 * mcp_account_storage_commit() or mcp_account_storage_commit_one()
 * after a short delay.
 *
 * Plugins that implement mcp_storage_set_attribute() and
 * mcp_account_storage_set_parameter() can just return %FALSE here.
 * There is a default implementation, which just returns %FALSE.
 *
 * Returns: %TRUE if the attribute was claimed, %FALSE otherwise
 */
gboolean
mcp_account_storage_set (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key,
    const gchar *value)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  SDEBUG (storage, "");
  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->set (storage, am, account, key, value);
}

/**
 * mcp_account_storage_set_attribute:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique name of the account
 * @attribute: the name of an attribute, e.g. "DisplayName"
 * @value: a value to associate with @attribute
 * @flags: flags influencing how the attribute is to be stored
 *
 * Store an attribute.
 *
 * The plugin is expected to either quickly and synchronously
 * update its internal cache of values with @value, or to
 * decline to store the attribute.
 *
 * The plugin is not expected to write to its long term storage
 * at this point.
 *
 * There is a default implementation, which just returns %FALSE.
 * Mission Control will call mcp_account_storage_set() instead,
 * using a keyfile-escaped version of @value.
 *
 * Returns: %TRUE if the attribute was claimed, %FALSE otherwise
 *
 * Since: 5.15.0
 */
gboolean
mcp_account_storage_set_attribute (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account,
    const gchar *attribute,
    GVariant *value,
    McpAttributeFlags flags)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  SDEBUG (storage, "");
  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->set_attribute != NULL, FALSE);

  return iface->set_attribute (storage, am, account, attribute, value, flags);
}

/**
 * mcp_account_storage_set_parameter:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique name of the account
 * @parameter: the name of a parameter, e.g. "account" (note that there
 *  is no "param-" prefix here)
 * @value: a value to associate with @parameter
 * @flags: flags influencing how the parameter is to be stored
 *
 * Store a parameter.
 *
 * The plugin is expected to either quickly and synchronously
 * update its internal cache of values with @value, or to
 * decline to store the parameter.
 *
 * The plugin is not expected to write to its long term storage
 * at this point.
 *
 * There is a default implementation, which just returns %FALSE.
 * Mission Control will call mcp_account_storage_set() instead,
 * using "param-" + @parameter as key and a keyfile-escaped version
 * of @value as value.
 *
 * Returns: %TRUE if the parameter was claimed, %FALSE otherwise
 *
 * Since: 5.15.0
 */
gboolean
mcp_account_storage_set_parameter (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account,
    const gchar *parameter,
    GVariant *value,
    McpParameterFlags flags)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  SDEBUG (storage, "");
  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->set_parameter != NULL, FALSE);

  return iface->set_parameter (storage, am, account, parameter, value, flags);
}

/**
 * McpAccountStorageCreate:
 * @storage: an #McpAccountStorage instance
 * @am: an object which can be used to call back into the account manager
 * @manager: the name of the manager
 * @protocol: the name of the protocol
 * @params: A gchar * / GValue * hash table of account parameters
 * @error: a GError to fill
 *
 * An implementation of mcp_account_storage_create()
 *
 * Returns: (transfer full): the account name or %NULL
 */

/**
 * mcp_account_storage_create:
 * @storage: an #McpAccountStorage instance
 * @am: an object which can be used to call back into the account manager
 * @manager: the name of the manager
 * @protocol: the name of the protocol
 * @params: A gchar * / GValue * hash table of account parameters
 * @error: a GError to fill
 *
 * Inform the plugin that a new account is being created. @manager, @protocol
 * and @params are given to help determining the account's unique name, but does
 * not need to be stored on the account yet, mcp_account_storage_set() and
 * mcp_account_storage_commit() will be called later.
 *
 * It is recommended to use mcp_account_manager_get_unique_name() to create the
 * unique name, but it's not mandatory. One could base the unique name on an
 * internal storage identifier, prefixed with the provider's name
 * (e.g. goa__1234).
 *
 * #McpAccountStorage::created signal should not be emitted for this account,
 * not even when mcp_account_storage_commit() will be called.
 *
 * Returns: (transfer full): the newly allocated account name, which should
 *  be freed once the caller is done with it, or %NULL if that couldn't
 *  be done.
 */
gchar *
mcp_account_storage_create (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *manager,
    const gchar *protocol,
    GHashTable *params,
    GError **error)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, NULL);

  if (iface->create == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
          "This storage does not implement create function");
      return NULL;
    }

  return iface->create (storage, am, manager, protocol, params, error);
}

/**
 * McpAccountStorageDeleteFunc:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique name of the account
 * @key: (allow-none): the setting whose value we wish to store - either an
 *  attribute like "DisplayName", or "param-" plus a parameter like
 *  "account" - or %NULL to delete the entire account
 *
 * An implementation of mcp_account_storage_delete().
 *
 * Returns: %TRUE if the setting or settings are not
 * the plugin's cache after this operation, %FALSE otherwise.
 */

/**
 * mcp_account_storage_delete:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique name of the account
 * @key: (allow-none): the setting whose value we wish to store - either an
 *  attribute like "DisplayName", or "param-" plus a parameter like
 *  "account" - or %NULL to delete the entire account
 *
 * The plugin is expected to remove the setting for @key from its
 * internal cache and to remember that its state has changed, so
 * that it can delete said setting from its long term storage if
 * its long term storage method makes this necessary.
 *
 * If @key is %NULL, the plugin should forget all its settings for
 * @account,and remember to delete the entire account from its storage later.
 *
 * The plugin is not expected to update its long term storage at
 * this point.
 *
 * Returns: %TRUE if the setting or settings are not
 * the plugin's cache after this operation, %FALSE otherwise.
 * This is very unlikely to ever be %FALSE, as a plugin is always
 * expected to be able to manipulate its own cache.
 */
gboolean
mcp_account_storage_delete (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  SDEBUG (storage, "");
  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->delete (storage, am, account, key);
}

/**
 * McpAccountStorageCommitFunc:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 *
 * An implementation of mcp_account_storage_commit().
 *
 * Returns: %TRUE if the commit process was started successfully
 */

/**
 * mcp_account_storage_commit:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 *
 * The plugin is expected to write its cache to long term storage,
 * deleting, adding or updating entries in said storage as needed.
 *
 * This call is expected to return promptly, but the plugin is
 * not required to have finished its commit operation when it returns,
 * merely to have started the operation.
 *
 * If the @commit_one method is implemented, it will be called preferentially
 * if only one account is to be committed. If the @commit_one method is
 * implemented but @commit is not, @commit_one will be called with
 * @account_name = %NULL to commit all accounts.
 *
 * Returns: %TRUE if the commit process was started (but not necessarily
 * completed) successfully; %FALSE if there was a problem that was immediately
 * obvious.
 */
gboolean
mcp_account_storage_commit (const McpAccountStorage *storage,
    const McpAccountManager *am)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  SDEBUG (storage, "committing all accounts");
  g_return_val_if_fail (iface != NULL, FALSE);

  if (iface->commit != NULL)
    {
      return iface->commit (storage, am);
    }
  else if (iface->commit_one != NULL)
    {
      return iface->commit_one (storage, am, NULL);
    }
  else
    {
      SDEBUG (storage,
          "neither commit nor commit_one is implemented; cannot save accounts");
      return FALSE;
    }
}

/**
 * McpAccountStorageCommitOneFunc:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: (allow-none): the unique suffix of an account's object path,
 *  or %NULL
 *
 * An implementation of mcp_account_storage_commit_one().
 *
 * Returns: %TRUE if the commit process was started successfully
 */

/**
 * mcp_account_storage_commit_one:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: (allow-none): the unique suffix of an account's object path,
 *  or %NULL if all accounts are to be committed and
 *  mcp_account_storage_commit() is unimplemented
 *
 * The same as mcp_account_storage_commit(), but only commit the given
 * account. This is optional to implement; the default implementation
 * is to call @commit.
 *
 * If both mcp_account_storage_commit_one() and mcp_account_storage_commit()
 * are implemented, Mission Control will never pass @account = %NULL to
 * this method.
 *
 * Returns: %TRUE if the commit process was started (but not necessarily
 * completed) successfully; %FALSE if there was a problem that was immediately
 * obvious.
 */
gboolean
mcp_account_storage_commit_one (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  SDEBUG (storage, "called for %s", account ? account : "<all accounts>");
  g_return_val_if_fail (iface != NULL, FALSE);

  if (iface->commit_one != NULL)
    return iface->commit_one (storage, am, account);
  else
    /* Fall back to plain ->commit() */
    return mcp_account_storage_commit (storage, am);
}

/**
 * McpAccountStorageListFunc:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 *
 * An implementation of mcp_account_storage_list().
 *
 * Returns: (transfer full): a list of account names
 */

/**
 * mcp_account_storage_list:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 *
 * Load details of every account stored by this plugin into an in-memory cache
 * so that it can respond to requests promptly.
 *
 * This method is called only at initialisation time, before the dbus name
 * has been claimed, and is the only one permitted to block.
 *
 * Returns: (element-type utf8) (transfer full): a list of account names that
 * the plugin has settings for. The account names should be freed with
 * g_free(), and the list with g_list_free(), when the caller is done with
 * them.
 */
GList *
mcp_account_storage_list (const McpAccountStorage *storage,
    const McpAccountManager *am)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  SDEBUG (storage, "");
  g_return_val_if_fail (iface != NULL, NULL);

  return iface->list (storage, am);
}

/**
 * McpAccountStorageReadyFunc:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 *
 * An implementation of mcp_account_storage_ready().
 */

/**
 * mcp_account_storage_ready:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 *
 * Informs the plugin that it is now permitted to create new accounts,
 * ie it can now fire its "created", "altered", "toggled" and "deleted"
 * signals.
 */
void
mcp_account_storage_ready (const McpAccountStorage *storage,
    const McpAccountManager *am)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_if_fail (iface != NULL);

  /* plugins that can't create accounts from external sources don't  *
   * need to implement this method, as they can never fire the async *
   * account change signals:                                         */
  if (iface->ready != NULL)
    iface->ready (storage, am);
}

/**
 * McpAccountStorageGetIdentifierFunc:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the account
 * @identifier: (out caller-allocates): a zero-filled #GValue whose type
 *  can be sent over D-Bus by dbus-glib, to hold the identifier.
 *
 * An implementation of mcp_account_storage_get_identifier().
 */

/**
 * mcp_account_storage_get_identifier:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the account
 * @identifier: (out caller-allocates): a zero-filled #GValue whose type
 *  can be sent over D-Bus by dbus-glib, to hold the identifier.
 *
 * Get the storage-specific identifier for this account. The type is variant,
 * hence the GValue.
 *
 * This method will only be called for the storage plugin that "owns"
 * the account.
 */
void
mcp_account_storage_get_identifier (const McpAccountStorage *storage,
    const gchar *account,
    GValue *identifier)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  SDEBUG (storage, "");
  g_return_if_fail (iface != NULL);
  g_return_if_fail (identifier != NULL);
  g_return_if_fail (!G_IS_VALUE (identifier));

  if (iface->get_identifier == NULL)
    {
      g_value_init (identifier, G_TYPE_STRING);
      g_value_set_string (identifier, account);
    }
  else
    {
      iface->get_identifier (storage, account, identifier);
    }
}

/**
 * McpAccountStorageGetAdditionalInfoFunc
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the account
 *
 * An implementation of mcp_account_storage_get_identifier().
 *
 * Returns: (transfer container) (element-type utf8 GObject.Value): additional
 *  storage-specific information
 */

/**
 * mcp_account_storage_get_additional_info:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the account
 *
 * Return additional storage-specific information about this account, which is
 * made available on D-Bus but not otherwise interpreted by Mission Control.
 *
 * This method will only be called for the storage plugin that "owns"
 * the account.
 *
 * Returns: (transfer container) (element-type utf8 GObject.Value): additional
 *  storage-specific information
 */
GHashTable *
mcp_account_storage_get_additional_info (const McpAccountStorage *storage,
    const gchar *account)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);
  GHashTable *ret = NULL;

  SDEBUG (storage, "");
  g_return_val_if_fail (iface != NULL, FALSE);

  if (iface->get_additional_info != NULL)
    ret = iface->get_additional_info (storage, account);

  if (ret == NULL)
    ret = g_hash_table_new (g_str_hash, g_str_equal);

  return ret;
}

/**
 * McpAccountStorageGetRestrictionsFunc
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the account
 *
 * An implementation of mcp_account_storage_get_restrictions().
 *
 * Returns: any restrictions that apply to this account.
 */

/**
 * mcp_account_storage_get_restrictions:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the account
 *
 * This method will only be called for the storage plugin that "owns"
 * the account.
 *
 * Returns: a bitmask of %TpStorageRestrictionFlags with the restrictions to
 *  account storage.
 */
/* FIXME: when breaking API, make this return TpStorageRestrictionFlags */
guint
mcp_account_storage_get_restrictions (const McpAccountStorage *storage,
    const gchar *account)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, 0);

  if (iface->get_restrictions == NULL)
    return 0;
  else
    return iface->get_restrictions (storage, account);
}

/**
 * mcp_account_storage_name:
 * @storage: an #McpAccountStorage instance
 *
 * <!-- nothing else to say -->
 *
 * Returns: the plugin's name (for logging etc)
 */
const gchar *
mcp_account_storage_name (const McpAccountStorage *storage)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->name;
}

/**
 * mcp_account_storage_description:
 * @storage: an #McpAccountStorage instance
 *
 * <!-- nothing else to say -->
 *
 * Returns: the plugin's description (for logging etc)
 */
const gchar *
mcp_account_storage_description (const McpAccountStorage *storage)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->desc;
}

/**
 * mcp_account_storage_provider:
 * @storage: an #McpAccountStorage instance
 *
 * <!-- nothing else to say -->
 *
 * Returns: a D-Bus namespaced name for this plugin, or "" if none
 *  was provided in #McpAccountStorageIface.provider.
 */
const gchar *
mcp_account_storage_provider (const McpAccountStorage *storage)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->provider != NULL ? iface->provider : "";
}

/**
 * mcp_account_storage_emit_create:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the created account
 *
 * Emits the #McpAccountStorage::created signal.
 */
void
mcp_account_storage_emit_created (McpAccountStorage *storage,
    const gchar *account)
{
  g_signal_emit (storage, signals[CREATED], 0, account);
}

/**
 * mcp_account_storage_emit_altered:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the altered account
 *
 * Emits the #McpAccountStorage::altered signal
 */
void
mcp_account_storage_emit_altered (McpAccountStorage *storage,
    const gchar *account)
{
  g_signal_emit (storage, signals[ALTERED], 0, account);
}

/**
 * mcp_account_storage_emit_altered_one:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the altered account
 * @key: the key of the altered property: either an attribute name
 *  like "DisplayName", or "param-" plus a parameter name like "account"
 *
 * Emits the #McpAccountStorage::altered-one signal
 */
void
mcp_account_storage_emit_altered_one (McpAccountStorage *storage,
    const gchar *account,
    const gchar *key)
{
  g_signal_emit (storage, signals[ALTERED_ONE], 0, account, key);
}

/**
 * mcp_account_storage_emit_deleted:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the deleted account
 *
 * Emits the #McpAccountStorage::deleted signal
 */
void
mcp_account_storage_emit_deleted (McpAccountStorage *storage,
    const gchar *account)
{
  g_signal_emit (storage, signals[DELETED], 0, account);
}

/**
 * mcp_account_storage_emit_toggled:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the account
 * @enabled: %TRUE if the account is now enabled
 *
 * Emits ::toggled signal
 */
void
mcp_account_storage_emit_toggled (McpAccountStorage *storage,
    const gchar *account,
    gboolean enabled)
{
  g_signal_emit (storage, signals[TOGGLED], 0, account, enabled);
}

/**
 * mcp_account_storage_emit_reconnect:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the account to reconnect
 *
 * Emits ::reconnect signal
 */
void
mcp_account_storage_emit_reconnect (McpAccountStorage *storage,
    const gchar *account)
{
  g_signal_emit (storage, signals[RECONNECT], 0, account);
}

/**
 * mcp_account_storage_owns:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique name (object-path tail) of an account
 *
 * Check whether @account is stored in @storage. The highest-priority
 * plugin for which this function returns %TRUE is considered to be
 * responsible for @account.
 *
 * There is a default implementation, which calls mcp_account_storage_get()
 * for the well-known key "manager".
 *
 * Returns: %TRUE if @account is stored in @storage
 *
 * Since: 5.15.0
 */
gboolean
mcp_account_storage_owns (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->owns != NULL, FALSE);

  return iface->owns (storage, am, account);
}
