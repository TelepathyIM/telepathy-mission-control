/* Mission Control plugin API - DBus Caller ID.
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
 * SECTION:dbus-acl
 * @title: McpDBusAcl
 * @short_description: DBus ACLs, implemented by plugins
 * @see_also:
 * @include: mission-control-plugins/mission-control-plugins.h
 *
 * Plugins may implement #McpDBusAcl in order to provide checks on whether
 * a DBus method call or property get/set operation should be allowed.
 *
 * To do so, the plugin must implement a #GObject subclass that implements
 * #McpDBusAcl, then return an instance of that subclass from
 * mcp_plugin_ref_nth_object().
 *
 * An implementation of this interface might look like this:
 *
 * <example><programlisting>
 * G_DEFINE_TYPE_WITH_CODE (APlugin, a_plugin,
 *    G_TYPE_OBJECT,
 *    G_IMPLEMENT_INTERFACE (...);
 *    G_IMPLEMENT_INTERFACE (MCP_TYPE_DBUS_ACL, dbus_acl_iface_init);
 *    G_IMPLEMENT_INTERFACE (...))
 * /<!-- -->* ... *<!-- -->/
 * static void
 * dbus_acl_iface_init (McpDBusAclIface *iface,
 *     gpointer unused G_GNUC_UNUSED)
 * {
 *   iface-&gt;name = "APlugin";
 *   iface-&gt;desc = "A plugin that checks some conditions";
 *   iface-&gt;authorised = _authorised;
 *   iface-&gt;authorised_async = _authorised_async;
 * }
 * </programlisting></example>
 *
 * A single object can implement more than one interface.
 */

#include "config.h"

#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/debug-internal.h>
#include <mission-control-plugins/mcp-signals-marshal.h>
#include <glib.h>
#include <telepathy-glib/telepathy-glib.h>

#define MCP_DEBUG_TYPE MCP_DEBUG_DBUS_ACL
#define ACL_DEBUG(_p, _format, ...) \
  DEBUG("%s: " _format, \
      (_p != NULL) ? mcp_dbus_acl_name (_p) : "-", ##__VA_ARGS__)

/**
 * McpDBusAclIface:
 * @parent: the parent type
 * @name: the name of the plugin, or %NULL to use the GObject class name
 * @desc: the description of the plugin, or %NULL
 * @authorised: an implementation of part of mcp_dbus_acl_authorised()
 * @authorised_async: an implementation of part of
 *    mcp_dbus_acl_authorised_async()
 */

GType
mcp_dbus_acl_get_type (void)
{
  static gsize once = 0;
  static GType type = 0;

  if (g_once_init_enter (&once))
    {
      static const GTypeInfo info =
        {
          sizeof (McpDBusAclIface),
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
          "McpDBusAcl", &info, 0);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);

      g_once_init_leave (&once, 1);
    }

  return type;
}

static GList *
cached_acls (void)
{
  static gboolean acl_plugins_cached = FALSE;
  static GList *dbus_acls = NULL;

  const GList *p;

  if (acl_plugins_cached)
    return dbus_acls;

  /* insert the default storage plugin into the sorted plugin list */
  for (p = mcp_list_objects(); p != NULL; p = g_list_next (p))
    {
      if (MCP_IS_DBUS_ACL (p->data))
        {
          dbus_acls = g_list_prepend (dbus_acls, g_object_ref (p->data));
        }
    }

  acl_plugins_cached = TRUE;

  return dbus_acls;
}


static DBusAclAuthData *
auth_data_new (TpDBusDaemon *dbus, const gchar *name, GHashTable *params)
{
  DBusAclAuthData *data = g_slice_new0 (DBusAclAuthData);

  data->dbus = g_object_ref (dbus);
  data->params = (params != NULL) ? g_hash_table_ref (params) : NULL;
  data->name = g_strdup (name);

  return data;
}

static void
auth_data_free (DBusAclAuthData *data)
{
  data->cleanup (data->data); /* free the callback data */

  tp_clear_pointer (&data->params, g_hash_table_unref);
  tp_clear_object (&data->dbus);
  g_free (data->name);

  g_slice_free (DBusAclAuthData, data);
}
/**
 * mcp_dbus_acl_iface_set_name:
 * @iface: an instance implementing McpDBusAclIface
 * @name: the plugin's name (used in debugging and some return values)
 *
 * Sets the name of the plugin. Intended for use by the plugin implementor.
 *
 * This is no longer necessary: just use "iface->name = name".
 **/
void
mcp_dbus_acl_iface_set_name (McpDBusAclIface *iface,
    const gchar *name)
{
  iface->name = name;
}

/**
 * mcp_dbus_acl_iface_set_desc:
 * @iface: an instance implementing McpDBusAclIface
 * @desc: the plugin's description
 *
 * Sets the plugin's description. Intended for use by the plugin implementor.
 *
 * This is no longer necessary: just use "iface->desc = desc".
 **/
void
mcp_dbus_acl_iface_set_desc (McpDBusAclIface *iface,
    const gchar *desc)
{
  iface->desc = desc;
}

/**
 * mcp_dbus_acl_iface_implement_authorised:
 * @iface: an instance implementing McpDBusAclIface
 * @method: the plugin's description
 *
 * Implements this plugin's part of the mcp_dbus_acl_authorised() method.
 *
 * This is no longer necessary: just use "iface->authorised = method".
 **/
void
mcp_dbus_acl_iface_implement_authorised (McpDBusAclIface *iface,
    DBusAclAuthoriser method)
{
  iface->authorised = method;
}

/**
 * mcp_dbus_acl_iface_implement_authorised_async:
 * @iface: an instance implementing McpDBusAclIface
 * @method: the plugin's description
 *
 * Implements this plugin's part of the mcp_dbus_acl_authorised_async() method.
 *
 * This is no longer necessary: just use "iface->authorised_async = method".
 **/
void
mcp_dbus_acl_iface_implement_authorised_async (McpDBusAclIface *iface,
    DBusAclAsyncAuthoriser method)
{
  iface->authorised_async = method;
}

/* FIXME: when we break ABI, this should move to src/ under a different name,
 * and mcp_dbus_acl_authorised() should be a trivial wrapper around
 * iface->authorised() */
/**
 * mcp_dbus_acl_authorised:
 * @dbus: a #TpDBusDaemon instance
 * @context: a #DBusGMethodInvocation corresponding to the DBus call
 * @type: a #DBusAclType value (method call, get or set property)
 * @name: the name of the method or property in question
 * @params: A #GHashTable of #gchar * / #GValue parameters relating to the
 *          call which are deemed to be "of interest" for ACL plugins, or %NULL
 *
 * This method calls each #DBusAcl plugin's authorised method, set by
 * mcp_dbus_acl_iface_implement_authorised()
 *
 * How a plugin deals with @params is entirely plugin dependent.
 *
 * If any plugin returns %FALSE, the call is considered to be forbidden.
 *
 * Returns: a #gboolean - %TRUE for permitted, %FALSE for forbidden.
 **/
gboolean
mcp_dbus_acl_authorised (const TpDBusDaemon *dbus,
    DBusGMethodInvocation *context,
    DBusAclType type,
    const gchar *name,
    const GHashTable *params)
{
  GList *p;
  GList *acls = cached_acls ();
  gboolean permitted = TRUE;

  for (p = acls; permitted && p != NULL; p = g_list_next (p))
    {
      McpDBusAcl *plugin = MCP_DBUS_ACL (p->data);
      McpDBusAclIface *iface = MCP_DBUS_ACL_GET_IFACE (p->data);

      ACL_DEBUG (plugin, "checking ACL for %s", name);

      if (iface->authorised != NULL)
        permitted = iface->authorised (plugin, dbus, context, type, name, params);

      if (!permitted)
        break;
    }

  if (!permitted)
    {
      GError *denied = NULL;
      const gchar *denier = mcp_dbus_acl_name (p->data);

      denied = g_error_new (DBUS_GERROR, DBUS_GERROR_ACCESS_DENIED,
          "permission denied by DBus ACL plugin '%s'", denier);

      dbus_g_method_return_error (context, denied);

      g_error_free (denied);
    }

  return permitted;
}

/**
 * mcp_dbus_acl_authorised_async_step:
 * @ad: a #DBusAclAuthData pointer
 * @permitted: whether the last plugin permitted the call being inspected
 *
 * This call is intended for use in the authorised_async mehod of a
 * #DBusAcl plugin - it allows the plugin to hand control back to the
 * overall ACL infrastructure, informing it of its decision as it does.
 **/
void
mcp_dbus_acl_authorised_async_step (DBusAclAuthData *ad,
    gboolean permitted)
{
  if (permitted)
    {
      while (ad->next_acl != NULL && ad->next_acl->data != NULL)
        {
          McpDBusAcl *plugin = MCP_DBUS_ACL (ad->next_acl->data);
          McpDBusAclIface *iface = MCP_DBUS_ACL_GET_IFACE (plugin);

          if (ad->acl != NULL)
            ACL_DEBUG (ad->acl, "passed ACL for %s", ad->name);

          /* take the next plugin off the next_acl list */
          ad->next_acl = g_list_next (ad->next_acl);
          ad->acl = plugin;

          if (iface->authorised_async != NULL)
            {
              /* kick off the next async authoriser in the chain */
              iface->authorised_async (plugin, ad);

              /* don't clean up, the next async acl will call us when it's
               * done: */
              return;
            }
        }

      if (ad->acl != NULL)
        ACL_DEBUG (ad->acl, "passed final ACL for %s", ad->name);

      ad->handler (ad->context, ad->data);
    }
  else
    {
      const gchar *who = (ad->acl != NULL) ? mcp_dbus_acl_name (ad->acl) : NULL;
      GError *denied = g_error_new (DBUS_GERROR, DBUS_GERROR_ACCESS_DENIED,
          "%s permission denied by DBus ACL plugin '%s'",
          ad->name,
          (who != NULL) ? who : "*unknown*");

      dbus_g_method_return_error (ad->context, denied);

      g_error_free (denied);
    }

  auth_data_free (ad);    /* done with internal bookkeeping */
}

/* FIXME: when we break ABI, this should move to src/ under a different name,
 * and mcp_dbus_acl_authorised_async() should be a trivial wrapper around
 * iface->authorised_async(); it should also use GIO-style asynchronicity */
/**
 * mcp_dbus_acl_authorised_async:
 * @dbus: a #TpDBusDaemon instance
 * @context: a #DBusGMethodInvocation corresponding to the DBus call
 * @type: a #DBusAclType value (method call, get or set property)
 * @name: the name of the method or property in question
 * @params: A #GHashTable of #gchar * / #GValue parameters relating to the
 *          call which are deemed to be "of interest" for ACL plugins, or %NULL
 * @handler: callback to call if the ACL decides the call is permitted
 * @data: a #gpointer to pass to the @handler
 * @cleanup: a #GDestroyNotify to use to deallocate @data
 *
 * This method calls each #DBusAcl plugin's authorised_async method, set by
 * mcp_dbus_acl_iface_implement_authorised_async()
 *
 * How a plugin deals with @parameters is entirely plugin dependent.
 *
 * The plugin should implement an async (or at least non-blocking)
 * check, which should signal that it has finished by calling
 * mcp_dbus_acl_authorised_async_step()
 *
 * If all the plugins permit this call, then @handler will be invoked
 * with @context and @data as its arguments.
 *
 * @cleanup wll be called if the call is forbidden, or after @handler is
 * invoked. If the call is forbidden, a DBus error will be returned to the
 * caller automatically.
 *
 **/
void
mcp_dbus_acl_authorised_async (TpDBusDaemon *dbus,
    DBusGMethodInvocation *context,
    DBusAclType type,
    const gchar *name,
    GHashTable *params,
    DBusAclAuthorised handler,
    gpointer data,
    GDestroyNotify cleanup)
{
  GList *acls = cached_acls ();
  DBusAclAuthData *ad = auth_data_new (dbus, name, params);

  ad->acl = NULL; /* first step, there's no current ACL yet */
  ad->type = type;
  ad->data = data;
  ad->cleanup = cleanup;
  ad->context = context;
  ad->handler = handler;
  ad->next_acl = acls;

  ACL_DEBUG (NULL, "DBus access ACL verification: %u rules for %s",
      g_list_length (acls),
      name);
  mcp_dbus_acl_authorised_async_step (ad, TRUE);
}

/* plugin meta-data */
const gchar *
mcp_dbus_acl_name (const McpDBusAcl *self)
{
  McpDBusAclIface *iface = MCP_DBUS_ACL_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, FALSE);

  if (iface->name == NULL)
    return G_OBJECT_TYPE_NAME (self);

  return iface->name;
}

const gchar *
mcp_dbus_acl_description (const McpDBusAcl *self)
{
  McpDBusAclIface *iface = MCP_DBUS_ACL_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, FALSE);

  if (iface->desc == NULL)
    return "(no description)";

  return iface->desc;
}
