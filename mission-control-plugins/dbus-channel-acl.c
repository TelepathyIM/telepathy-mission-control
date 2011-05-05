/* Mission Control plugin API - DBus Channel Handler/Observer ACL
 *
 * Copyright © 2011 Nokia Corporation
 * Copyright © 2011 Collabora Ltd.
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
 * SECTION:dbus-channel-acl
 * @title: McpDBusChannelAcl
 * @short_description: DBus ACLs for channel handlers
 * @see_also:
 * @include: mission-control-plugins/dbus-channel-acl.h
 *
 * Plugins may implement #McpDBusChannelAcl in order to provide checks on
 * whether a Handler should be given a channel to process
 *
 * To do so, the plugin must implement a #GObject subclass that implements
 * #McpDBusChannelAcl, then return an instance of that subclass from
 * mcp_plugin_ref_nth_object().
 *
 * The contents of the #McpDBusChannelAcl struct are not public,
 * so to provide an implementation of the virtual methods,
 * plugins should call mcp_dbus_channel_acl_iface_implement_*()
 * from the interface initialization function, like this:
 *
 * <example><programlisting>
 * G_DEFINE_TYPE_WITH_CODE (APlugin, a_plugin,
 *    G_TYPE_OBJECT,
 *    G_IMPLEMENT_INTERFACE (...);
 *    G_IMPLEMENT_INTERFACE (MCP_TYPE_DBUS_CHANNEL_ACL, acl_iface_init));
 * /<!-- -->* ... *<!-- -->/
 * static void
 * acl_iface_init (McpDBusChannelAclIface *iface,
 *     gpointer unused G_GNUC_UNUSED)
 * {
 *   mcp_dbus_channel_acl_iface_set_name (iface, PLUGIN_NAME);
 *   mcp_dbus_channel_acl_iface_set_desc (iface, PLUGIN_DESCRIPTION);
 *   mcp_dbus_channel_acl_iface_implement_authorised (iface, _authorised);
 * /<!-- -->* ... *<!-- -->/
 * }
 * </programlisting></example>
 *
 * A single object can implement more than one interface.
 */

#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/mcp-signals-marshal.h>
#include <glib.h>
#include <telepathy-glib/telepathy-glib.h>
#include "config.h"

#ifdef ENABLE_DEBUG

#define DEBUG(_p, _format, ...) \
  g_debug ("dbus-handler-acl: %s: %s: " _format, G_STRFUNC, \
      (_p != NULL) ? mcp_dbus_channel_acl_name (_p) : "-", ##__VA_ARGS__)

#else  /* ENABLE_DEBUG */

#define DEBUG(_p, _format, ...) do {} while (0);

#endif /* ENABLE_DEBUG */

struct _McpDBusChannelAclIface
{
  GTypeInterface parent;

  const gchar *name;
  const gchar *desc;

  DBusChannelAclAuthoriser authorised;
};

GType
mcp_dbus_channel_acl_get_type (void)
{
  static gsize once = 0;
  static GType type = 0;

  if (g_once_init_enter (&once))
    {
      static const GTypeInfo info =
        {
          sizeof (McpDBusChannelAclIface),
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
          "McpDBusChannelAcl", &info, 0);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);

      g_once_init_leave (&once, 1);
    }

  return type;
}

/**
 * mcp_dbus_channel_acl_iface_set_name:
 * @iface: an instance implementing McpDBusChannelAclIface
 * @name: the plugin's name (used in debugging and some return values)
 *
 * Sets the name of the plugin. Intended for use by the plugin implementor.
 **/
void
mcp_dbus_channel_acl_iface_set_name (McpDBusChannelAclIface *iface,
    const gchar *name)
{
  iface->name = name;
}

/**
 * mcp_dbus_channel_acl_iface_set_desc:
 * @iface: an instance implementing McpDBusChannelAclIface
 * @desc: the plugin's description
 *
 * Sets the plugin's description. Intended for use by the plugin implementor.
 **/
void
mcp_dbus_channel_acl_iface_set_desc (McpDBusChannelAclIface *iface,
    const gchar *desc)
{
  iface->desc = desc;
}

void
mcp_dbus_channel_acl_iface_implement_authorised (McpDBusChannelAclIface *iface,
    DBusChannelAclAuthoriser method)
{
  iface->authorised = method;
}

/* plugin meta-data */
const gchar *
mcp_dbus_channel_acl_name (const McpDBusChannelAcl *self)
{
  McpDBusChannelAclIface *iface = MCP_DBUS_CHANNEL_ACL_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->name;
}

const gchar *
mcp_dbus_channel_acl_description (const McpDBusChannelAcl *self)
{
  McpDBusChannelAclIface *iface = MCP_DBUS_CHANNEL_ACL_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->desc;
}

gboolean
mcp_dbus_channel_acl_authorised (McpDBusChannelAcl *self,
    TpDBusDaemon *dbus,
    TpProxy *recipient,
    const GPtrArray *channels)
{
  McpDBusChannelAclIface *iface = MCP_DBUS_CHANNEL_ACL_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, FALSE);

  if (iface->authorised != NULL)
    return iface->authorised (self, dbus, recipient, channels);
  else
    return TRUE;
}
