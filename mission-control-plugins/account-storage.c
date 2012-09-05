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
 * The contents of the #McpAccountStorage struct are not public,
 * so to provide an implementation of the virtual methods,
 * plugins should call mcp_account_operation_iface_implement_*()
 * from the interface initialization function, like this:
 *
 * <example><programlisting>
 * G_DEFINE_TYPE_WITH_CODE (APlugin, a_plugin,
 *    G_TYPE_OBJECT,
 *    G_IMPLEMENT_INTERFACE (...);
 *    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_STORAGE,
 *      account_storage_iface_init));
 * /<!-- -->* ... *<!-- -->/
 * static void
 * account_storage_iface_init (McpAccountStorageIface *iface,
 *     gpointer unused G_GNUC_UNUSED)
 * {
 *   mcp_account_storage_iface_set_priority (iface, 0);
 *   mcp_account_storage_iface_set_name (iface, "foo")
 *   mcp_account_storage_iface_set_desc (iface, "The FOO storage backend");
 *   mcp_account_storage_iface_set_provider (iface,
 *     "org.freedesktop.Telepathy.MissionControl5.FooStorage");
 *   mcp_account_storage_iface_implement_get    (iface, _plugin_getval);
 *   mcp_account_storage_iface_implement_set    (iface, _plugin_setval);
 *   mcp_account_storage_iface_implement_delete (iface, _plugin_delete);
 *   mcp_account_storage_iface_implement_commit (iface, _plugin_commit);
 *   mcp_account_storage_iface_implement_commit_one (iface, _plugin_commit_one);
 *   mcp_account_storage_iface_implement_list   (iface, _plugin_list);
 *   mcp_account_storage_iface_implement_ready  (iface, _plugin_ready);
 *   mcp_account_storage_iface_implement_get_identifier (iface,
 *     _plugin_get_identifier);
 *   mcp_account_storage_iface_implement_get_additional_info (iface,
 *     _plugin_get_additional_info);
 *   mcp_account_storage_iface_implement_get_restrictions (iface,
 *     _plugin_get_restrictions);
 * /<!-- -->* ... *<!-- -->/
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

static void
class_init (gpointer klass,
    gpointer data)
{
  GType type = G_TYPE_FROM_CLASS (klass);

  /**
   * McpAccountStorage::created
   * @account: the unique name of the created account
   *
   * emitted if an external entity creates an account
   * in the backend the emitting plugin handles
   *
   * Should not be fired until mcp_account_storage_ready() has been called
   *
   **/
  signals[CREATED] = g_signal_new ("created",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE,
      1, G_TYPE_STRING);

  /**
   * McpAccountStorage::altered
   * @account: the unique name of the altered account
   *
   * emitted if an external entity alters an account
   * in the backend the emitting plugin handles
   * should not be emitted if a single known property has been
   * altered, see McpAccountStorage::altered-one instead
   *
   * Should not be fired until mcp_account_storage_ready() has been called
   *
   **/
  signals[ALTERED] = g_signal_new ("altered",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE,
      1, G_TYPE_STRING);

  /**
   * McpAccountStorage::altered-one
   * @account: the unique name of the altered account
   * @name: the name of the altered property (its key)
   *
   * emitted if an external entity alters an account
   * in the backend that the emitting plugin handles.
   *
   * If many properties have changed, the plugin may choose to emit
   * McpAccountStorage::altered _instead_, but should not emit both.
   *
   * Should not be fired until mcp_account_storage_ready() has been called
   **/
  signals[ALTERED_ONE] = g_signal_new ("altered-one",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      _mcp_marshal_VOID__STRING_STRING, G_TYPE_NONE,
      2, G_TYPE_STRING, G_TYPE_STRING);


  /**
   * McpAccountStorage::deleted
   * @account: the unique name of the deleted account
   *
   * emitted if an external entity deletes an account
   * in the backend the emitting plugin handles
   *
   * Should not be fired until mcp_account_storage_ready() has been called
   *
   **/
  signals[DELETED] = g_signal_new ("deleted",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE,
      1, G_TYPE_STRING);

  /**
   * McpAccountStorage::toggled
   * @account: the unique name of the toggled account
   * @enabled: #gboolean indicating whether the account is enabled
   *
   * emitted if an external entity enables/disables an account
   * in the backend the emitting plugin handles
   *
   * Should not be fired until mcp_account_storage_ready() has been called
   *
   **/
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
 * (the default storage plugin priority) upwards.
 *
 * Plugins at a higher priority then MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_KEYRING
 * will have the opportunity to "steal" passwords from the gnome keyring:
 * Plugins at a lower priority than this will not receive secret parameters
 * from MC as the keyring plugin will already have claimed them.
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
 * mcp_account_storage_get:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique name of the account
 * @key: the setting whose value we wish to fetch
 *
 * The plugin is expected to quickly and synchronously update
 * the value associated with @key using calls to @am.
 *
 * The plugin is not required to consult whatever long term storage
 * it uses, and may fetch said value from its internal cache, if any.
 *
 * If @key is %NULL the plugin should write all its settings for @account
 * into the account manager via @am. The return value in this case should
 * be %TRUE if any settings were found.
 *
 * Returns: %TRUE if a value was found and %FALSE otherwise
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

  return iface->get (storage, am, account, key);
}

/**
 * mcp_account_storage_set:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique name of the account
 * @key: the setting whose value we wish to fetch
 * @value: a value to associate with @key
 *
 * The plugin is expected to either quickly and synchronously
 * update its internal cache of values with @value, or to
 * decline to store the setting.
 *
 * The plugin is not expected to write to its long term storage
 * at this point.
 *
 * Returns: %TRUE if the setting was claimed, %FALSE otherwise
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
 * mcp_account_storage_create:
 * @storage: an #McpAccountStorage instance
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
 * Returns: the newly allocated account name, which should be freed
 *  once the caller is done with it, or %NULL if that couldn't be done.
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
 * mcp_account_storage_delete:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique name of the account
 * @key: the setting whose value we wish to fetch
 *
 * The plugin is expected to remove the setting for @key from its
 * internal cache and to remember that its state has changed, so
 * that it can delete said setting from its long term storage if
 * its long term storage method makes this necessary.
 *
 * If @key is %NULL, the plugin should forget all its settings for
 * @account (and remember to delete @account from its storage later)
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
 * mcp_account_storage_commit_one:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 * @account: the unique suffix of an account's object path, or %NULL if
 *  all accounts are to be committed
 *
 * The same as mcp_account_storage_commit(), but only commit the given
 * account. This is optional to implement; the default implementation
 * is to call @commit.
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
 * mcp_account_storage_list:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 *
 * This method is called only at initialisation time, before the dbus name
 * has been claimed, and is the only one permitted to block.
 *
 * Returns: (element-type utf8) (transfer full): a list of account names that
 * the plugin has settings for. The account names should be freed with
 * g_free(), and the list with g_list_free(), when the caller is done with
 * them.
 **/
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
 * mcp_account_storage_get_identifier:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the account
 * @identifier: a zero-filled #GValue whose type can be sent over D-Bus by
 * dbus-glib to hold the identifier.
 *
 * Get the storage-specific identifier for this account. The type is variant,
 * hence the GValue.
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
 * mcp_account_storage_get_additional_info:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the account
 *
 * Return additional storage-specific information about this account, which is
 * made available on D-Bus but not otherwise interpreted by Mission Control.
 *
 * Returns: a mapping from strings to #GValue<!-- -->s, which must be freed by
 * the caller.
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
 * mcp_account_storage_get_restrictions:
 * @storage: an #McpAccountStorage instance
 * @account: the unique name of the account
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
 * Returns: a DBus namespaced name for this plugin.
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
 * Emits ::created signal
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
 * Emits ::altered signal
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
 * @key: the key of the altered property
 *
 * Emits ::created-one signal
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
 * Emits ::deleted signal
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
