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
 *   mcp_account_storage_iface_implement_list   (iface, _plugin_list);
 * /<!-- -->* ... *<!-- -->/
 * }
 * </programlisting></example>
 *
 * A single object can implement more than one interface; It is currently
 * unlikely that you would find it useful to implement anything other than
 * an account storage plugin in an account storage object, though.
 */

#include <src/mcd-signals-marshal.h>
#include <src/mcd-debug.h>

#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/implementation.h>
#include <glib/gmessages.h>

enum
{
  CREATED,
  ALTERED,
  TOGGLED,
  DELETED,
  NO_SIGNAL
};

static guint signals[NO_SIGNAL] = { 0 };

struct _McpAccountStorageIface
{
  GTypeInterface parent;

  guint priority;
  const gchar *name;
  const gchar *desc;

  gboolean (*set) (
      const McpAccountStorage *self,
      const McpAccount *am,
      const gchar *acct,
      const gchar *key,
      const gchar *val);

  gboolean (*get) (
      const McpAccountStorage *self,
      const McpAccount *am,
      const gchar *acct,
      const gchar *key);

  gboolean (*delete) (
      const McpAccountStorage *self,
      const McpAccount *am,
      const gchar *acct,
      const gchar *key);

  gboolean (*commit) (
      const McpAccountStorage *self,
      const McpAccount *am);

  GList * (*list) (
      const McpAccountStorage *self,
      const McpAccount *am);
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
   **/
  signals[CREATED] = g_signal_new ("created",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      _mcd_marshal_VOID__STRING, G_TYPE_NONE,
      1, G_TYPE_STRING);

  /**
   * McpAccountStorage::altered
   * @account: the unique name of the created account
   *
   * emitted if an external entity alters an account
   * in the backend the emitting plugin handles
   **/
  signals[ALTERED] = g_signal_new ("altered",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      _mcd_marshal_VOID__STRING, G_TYPE_NONE,
      1, G_TYPE_STRING);

  /**
   * McpAccountStorage::deleted
   * @account: the unique name of the created account
   *
   * emitted if an external entity deletes an account
   * in the backend the emitting plugin handles
   **/
  signals[DELETED] = g_signal_new ("deleted",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      _mcd_marshal_VOID__STRING, G_TYPE_NONE,
      1, G_TYPE_STRING);

  /**
   * McpAccountStorage::toggled
   * @account: the unique name of the created account
   * @enabled: #gboolean indicating whether the account is enabled
   *
   * emitted if an external entity enables/disables an account
   * in the backend the emitting plugin handles
   **/
  signals[TOGGLED] = g_signal_new ("toggled",
      type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      _mcd_marshal_VOID__STRING_BOOLEAN, G_TYPE_NONE,
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

      g_debug ("%s -> %lu", g_type_name (type), type);

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
        const McpAccount *,
        const gchar *,
        const gchar *))
{
  iface->get = method;
}

void
mcp_account_storage_iface_implement_set (McpAccountStorageIface *iface,
    gboolean (*method) (
        const McpAccountStorage *,
        const McpAccount *,
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
        const McpAccount *,
        const gchar *,
        const gchar *))
{
  iface->delete = method;
}

void
mcp_account_storage_iface_implement_commit (McpAccountStorageIface *iface,
    gboolean (*method) (const McpAccountStorage *, const McpAccount *am))
{
  iface->commit = method;
}

void
mcp_account_storage_iface_implement_list (McpAccountStorageIface *iface,
    GList * (*method) (
        const McpAccountStorage *,
        const McpAccount *))
{
  iface->list = method;
}

gint
mcp_account_storage_priority (const McpAccountStorage *storage)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, -1);

  return iface->priority;
}

gboolean
mcp_account_storage_get (const McpAccountStorage *storage,
    McpAccount *am,
    const gchar *acct,
    const gchar *key)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  DEBUG ();
  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->get (storage, am, acct, key);
}

gboolean
mcp_account_storage_set (const McpAccountStorage *storage,
    const McpAccount *am,
    const gchar *acct,
    const gchar *key,
    const gchar *val)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  DEBUG ();
  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->set (storage, am, acct, key, val);
}

gboolean
mcp_account_storage_delete (const McpAccountStorage *storage,
    const McpAccount *am,
    const gchar *acct,
    const gchar *key)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  DEBUG ();
  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->delete (storage, am, acct, key);
}

gboolean
mcp_account_storage_commit (const McpAccountStorage *storage,
    const McpAccount *am)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  DEBUG ();
  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->commit (storage, am);
}

GList *
mcp_account_storage_list (const McpAccountStorage *storage,
    const McpAccount *am)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  DEBUG ();
  g_return_val_if_fail (iface != NULL, NULL);

  return iface->list (storage, am);
}

const gchar *
mcp_account_storage_name (const McpAccountStorage *storage)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->name;
}

const gchar *
mcp_account_storage_description (const McpAccountStorage *storage)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->desc;
}
