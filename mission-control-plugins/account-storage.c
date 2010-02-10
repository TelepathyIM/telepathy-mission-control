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
 *   mcp_account_storage_iface_implement_fetch (iface, _plugin_fetch);
 *   mcp_account_storage_iface_implement_store (iface, _plugin_store);
 *   mcp_account_storage_iface_implement_list  (iface, _plugin_list);
 * /<!-- -->* ... *<!-- -->/
 * }
 * </programlisting></example>
 *
 * A single object can implement more than one interface; It is currently
 * unlikely that you would find it useful to implement anything other than
 * an account storage plugin in an account storage object, though.
 */

#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/implementation.h>
#include <glib/gmessages.h>

struct _McpAccountStorageIface
{
  GTypeInterface parent;

  guint priority;

  /* save group parameters from gkeyfile to plugin's store, TRUE on success */
  gboolean (*store) (const McpAccountStorage *,const GKeyFile *, const gchar *);

  /* read group parameters from plugin storage into gkeyfile, true on SUCCESS */
  gboolean (*fetch) (const McpAccountStorage *,GKeyFile *, const gchar *);

  /* remove group from plugin storage, TRUE on success */
  gboolean (*remove) (const McpAccountStorage *,const gchar *);

  /* list groups (ie accounts (gchar *)) in this plugin's storage */
  GList * (*list) (const McpAccountStorage *);
};

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
          NULL, /* class_init     */
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
mcp_account_storage_iface_implement_fetch (McpAccountStorageIface *iface,
    gboolean (*method) (const McpAccountStorage *, GKeyFile *,
        const gchar *))
{
  iface->fetch = method;
}

void
mcp_account_storage_iface_implement_store (McpAccountStorageIface *iface,
    gboolean (*method) (const McpAccountStorage *, const GKeyFile *,
        const gchar *))
{
  iface->store = method;
}

void
mcp_account_storage_iface_implement_remove (McpAccountStorageIface *iface,
    gboolean (*method) (const McpAccountStorage *, const gchar *))
{
  iface->remove = method;
}

void
mcp_account_storage_iface_implement_list (McpAccountStorageIface *iface,
    GList * (*method) (const McpAccountStorage *))
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
mcp_account_storage_fetch (const McpAccountStorage *storage,
    GKeyFile *accts,
    const gchar *key)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->fetch (storage, accts, key);
}

gboolean
mcp_account_storage_store (const McpAccountStorage *storage,
    const GKeyFile *accts,
    const gchar *key)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->store (storage, accts, key);
}

gboolean
mcp_account_storage_remove (const McpAccountStorage *storage,
    const gchar *key)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->remove (storage, key);
}

GList *
mcp_account_storage_list (const McpAccountStorage *storage)
{
  McpAccountStorageIface *iface = MCP_ACCOUNT_STORAGE_GET_IFACE (storage);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->list (storage);
}
