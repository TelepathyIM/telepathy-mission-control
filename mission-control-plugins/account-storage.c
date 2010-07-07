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
 * @include: mission-control-plugins/account-storage.h
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
 *   mcp_account_storage_iface_implement_get    (iface, _plugin_getval);
 *   mcp_account_storage_iface_implement_set    (iface, _plugin_setval);
 *   mcp_account_storage_iface_implement_delete (iface, _plugin_delete);
 *   mcp_account_storage_iface_implement_commit (iface, _plugin_commit);
 *   mcp_account_storage_iface_implement_commit_one (iface, _plugin_commit_one);
 *   mcp_account_storage_iface_implement_list   (iface, _plugin_list);
 *   mcp_account_storage_iface_implement_ready  (iface, _plugin_ready);
 * /<!-- -->* ... *<!-- -->/
 * }
 * </programlisting></example>
 *
 * A single object can implement more than one interface; It is currently
 * unlikely that you would find it useful to implement anything other than
 * an account storage plugin in an account storage object, though.
 */

#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/mcp-signals-marshal.h>
#include <mission-control-plugins/implementation.h>
#include <glib.h>

#ifdef ENABLE_DEBUG

#define DEBUG(_p, _format, ...) \
  g_debug ("%s: %s: " _format, G_STRFUNC, \
      (_p != NULL) ? mcp_account_storage_name (_p) : "NULL", ##__VA_ARGS__)

#else  /* ENABLE_DEBUG */

#define DEBUG(_p, _format, ...) do {} while (0);

#endif /* ENABLE_DEBUG */

enum
{
  CREATED,
  ALTERED,
  TOGGLED,
  DELETED,
  ALTERED_ONE,
  NO_SIGNAL
};

static guint signals[NO_SIGNAL] = { 0 };

struct _McpAccountStorageIface
{
  GTypeInterface parent;

  gint priority;
  const gchar *name;
  const gchar *desc;

  gboolean (*set) (
      const McpAccountStorage *self,
      const McpAccountManager *am,
      const gchar *account,
      const gchar *key,
      const gchar *val);

  gboolean (*get) (
      const McpAccountStorage *self,
      const McpAccountManager *am,
      const gchar *account,
      const gchar *key);

  gboolean (*delete) (
      const McpAccountStorage *self,
      const McpAccountManager *am,
      const gchar *account,
      const gchar *key);

  gboolean (*commit) (
      const McpAccountStorage *self,
      const McpAccountManager *am);

  GList * (*list) (
      const McpAccountStorage *self,
      const McpAccountManager *am);

  void (*ready) (
      const McpAccountStorage *self,
      const McpAccountManager *am);

  gboolean (*commit_one) (
      const McpAccountStorage *self,
      const McpAccountManager *am,
      const gchar *account);
};

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
   * @account: the unique name of the created account
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
   * @account: the unique name of the created account
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
  signals[ALTERED] = g_signal_new ("altered-one",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      _mcp_marshal_VOID__STRING_STRING, G_TYPE_NONE,
      2, G_TYPE_STRING, G_TYPE_STRING);


  /**
   * McpAccountStorage::deleted
   * @account: the unique name of the created account
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
   * @account: the unique name of the created account
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
mcp_account_storage_iface_implement_get (McpAccountStorageIface *iface,
    gboolean (*method) (
        const McpAccountStorage *,
        const McpAccountManager *,
        const gchar *,
        const gchar *))
{
  iface->get = method;
}

void
mcp_account_storage_iface_implement_set (McpAccountStorageIface *iface,
    gboolean (*method) (
        const McpAccountStorage *,
        const McpAccountManager *,
        const gchar *,
        const gchar *,
        const gchar *))
{
  iface->set = method;
}

void
mcp_account_storage_iface_implement_delete (McpAccountStorageIface *iface,
    gboolean (*method) (
        const McpAccountStorage *,
        const McpAccountManager *,
        const gchar *,
        const gchar *))
{
  iface->delete = method;
}

void
mcp_account_storage_iface_implement_commit (McpAccountStorageIface *iface,
    gboolean (*method) (
        const McpAccountStorage *,
        const McpAccountManager *))
{
  iface->commit = method;
}

void
mcp_account_storage_iface_implement_commit_one (McpAccountStorageIface *iface,
    gboolean (*method) (
        const McpAccountStorage *,
        const McpAccountManager *,
        const gchar *))
{
  iface->commit_one = method;
}

void
mcp_account_storage_iface_implement_list (McpAccountStorageIface *iface,
    GList * (*method) (
        const McpAccountStorage *,
        const McpAccountManager *))
{
  iface->list = method;
}

void
mcp_account_storage_iface_implement_ready (McpAccountStorageIface *iface,
    void  (*method) (
        const McpAccountStorage *,
        const McpAccountManager *))
{
  iface->ready = method;
}

/**
 * mcp_account_storage_priority:
 * @storage: an #McpAccountStorage instance
 *
 * Returns a #gint indicating the priority of the plugin.
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
 * Returns: a #gboolean - %TRUE if a value was found and %FALSE otherwise
 */
gboolean
mcp_account_storage_get (const McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account,
    const gchar *key)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  DEBUG (storage, "");
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
 * Returns: a #gboolean - %TRUE if the setting was claimed, %FALSE otherwise
 */
gboolean
mcp_account_storage_set (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key,
    const gchar *val)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  DEBUG (storage, "");
  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->set (storage, am, account, key, val);
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
 * Returns: a #gboolean - %TRUE if the setting or settings are not
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

  DEBUG (storage, "");
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
 * Returns: a gboolean - normally %TRUE, %FALSE if there was a problem
 * that was immediately obvious.
 */
gboolean
mcp_account_storage_commit (const McpAccountStorage *storage,
    const McpAccountManager *am)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  DEBUG (storage, "");
  g_return_val_if_fail (iface != NULL, FALSE);

  if (iface->commit == NULL && iface->commit_one != NULL)
    return iface->commit_one (storage, am, NULL);

  return iface->commit (storage, am);
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
 * Returns: a gboolean - normally %TRUE, %FALSE if there was a problem
 * that was immediately obvious.
 */
gboolean
mcp_account_storage_commit_one (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  DEBUG (storage, "");
  g_return_val_if_fail (iface != NULL, FALSE);

  if (iface->commit_one == NULL || (account == NULL && iface->commit != NULL))
    return iface->commit (storage, am);

  return iface->commit_one (storage, am, account);
}

/**
 * mcp_account_storage_list:
 * @storage: an #McpAccountStorage instance
 * @am: an #McpAccountManager instance
 *
 * This method is called only at initialisation time, before the dbus name
 * has been claimed, and is the only one permitted to block.
 *
 * Returns: a #GList of #gchar* (the unique account names) that the plugin
 * has settings for. The #GList (and its contents) should be freed when the
 * caller is done with them.
 **/
GList *
mcp_account_storage_list (const McpAccountStorage *storage,
    const McpAccountManager *am)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  DEBUG (storage, "");
  g_return_val_if_fail (iface != NULL, NULL);

  return iface->list (storage, am);
}

/**
 * mcp_account_storage_ready:
 * @storage: an #McpAccountStorage instance
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
 * mcp_account_storage_name:
 * @storage: an #McpAccountStorage instance
 *
 * Returns: a const #gchar* : the plugin's name (for logging etc)
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
 * Returns: a const #gchar* : the plugin's description (for logging etc)
 */
const gchar *
mcp_account_storage_description (const McpAccountStorage *storage)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->desc;
}
