/* Mission Control plugin API - representation of a ChannelDispatchOperation
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
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

#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/implementation.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

GType
mcp_dispatch_operation_get_type (void)
{
  static gsize once = 0;
  static GType type = 0;

  if (g_once_init_enter (&once))
    {
      static const GTypeInfo info = {
          sizeof (McpDispatchOperationIface),
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

      type = g_type_register_static (G_TYPE_INTERFACE, "McpDispatchOperation",
          &info, 0);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);

      g_once_init_leave (&once, 1);
    }

  return type;
}

const gchar *
mcp_dispatch_operation_get_account_path (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_account_path != NULL, NULL);
  return iface->get_account_path (self);
}

const gchar *
mcp_dispatch_operation_get_connection_path (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_connection_path != NULL, NULL);
  return iface->get_connection_path (self);
}

const gchar *
mcp_dispatch_operation_get_protocol (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_protocol != NULL, NULL);
  return iface->get_protocol (self);
}

const gchar *
mcp_dispatch_operation_get_cm_name (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_cm_name != NULL, NULL);
  return iface->get_cm_name (self);
}

guint
mcp_dispatch_operation_get_n_channels (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, 0);
  g_return_val_if_fail (iface->get_n_channels != NULL, 0);
  return iface->get_n_channels (self);
}

const gchar *
mcp_dispatch_operation_get_nth_channel_path (McpDispatchOperation *self,
    guint n)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_nth_channel_path != NULL, NULL);

  if (n >= mcp_dispatch_operation_get_n_channels (self))
    {
      return NULL;  /* not considered to be an error, to make iterating easy */
    }

  return iface->get_nth_channel_path (self, n);
}

GHashTable *
mcp_dispatch_operation_ref_nth_channel_properties (McpDispatchOperation *self,
    guint n)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->ref_nth_channel_properties != NULL, NULL);

  if (n >= mcp_dispatch_operation_get_n_channels (self))
    {
      return NULL;  /* not considered to be an error, to make iterating easy */
    }

  return iface->ref_nth_channel_properties (self, n);
}

McpDispatchOperationDelay *
mcp_dispatch_operation_start_delay (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->start_delay != NULL, NULL);
  return iface->start_delay (self);
}

void
mcp_dispatch_operation_end_delay (McpDispatchOperation *self,
    McpDispatchOperationDelay *delay)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (delay != NULL);
  g_return_if_fail (iface->end_delay != NULL);
  iface->end_delay (self, delay);
}

void
mcp_dispatch_operation_leave_channels (McpDispatchOperation *self,
    gboolean wait_for_observers,
    TpChannelGroupChangeReason reason,
    const gchar *message)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->leave_channels != NULL);

  if (message == NULL)
    message = "";

  iface->leave_channels (self, wait_for_observers, reason, message);
}

void
mcp_dispatch_operation_close_channels (McpDispatchOperation *self,
    gboolean wait_for_observers)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->close_channels != NULL);
  iface->close_channels (self, wait_for_observers);
}

void
mcp_dispatch_operation_destroy_channels (McpDispatchOperation *self,
    gboolean wait_for_observers)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->destroy_channels != NULL);
  iface->destroy_channels (self, wait_for_observers);
}

gboolean
mcp_dispatch_operation_find_channel_by_type (McpDispatchOperation *self,
    guint start_from,
    TpHandleType handle_type,
    GQuark channel_type,
    guint *ret_index,
    gchar **ret_dup_path,
    GHashTable **ret_ref_immutable_properties,
    TpChannel **ret_ref_channel)
{
  const gchar *channel_type_str = g_quark_to_string (channel_type);
  gboolean valid;
  guint i;

  g_return_val_if_fail (MCP_IS_DISPATCH_OPERATION (self), FALSE);
  g_return_val_if_fail (channel_type != 0, FALSE);

  for (i = start_from; i < mcp_dispatch_operation_get_n_channels (self); i++)
    {
      GHashTable *properties =
        mcp_dispatch_operation_ref_nth_channel_properties (self, i);
      const gchar *channel_path =
        mcp_dispatch_operation_get_nth_channel_path (self, i);

      if (properties != NULL &&
          channel_path != NULL &&
          !tp_strdiff (tp_asv_get_string (properties,
              TP_IFACE_CHANNEL ".ChannelType"),
            channel_type_str) &&
          tp_asv_get_uint32 (properties, TP_IFACE_CHANNEL ".TargetHandleType",
            &valid) == handle_type &&
          valid)
        {
          if (ret_index != NULL)
            *ret_index = i;

          if (ret_ref_immutable_properties != NULL)
            *ret_ref_immutable_properties = properties;
          else
            g_hash_table_unref (properties);

          if (ret_dup_path != NULL)
            *ret_dup_path = g_strdup (channel_path);

          if (ret_ref_channel != NULL)
            {
              TpConnection *connection =
                mcp_dispatch_operation_ref_connection (self);

              *ret_ref_channel = tp_channel_new_from_properties (connection,
                  channel_path, properties, NULL);

              g_object_unref (connection);
            }

          return TRUE;
        }

      g_hash_table_unref (properties);
    }

  return FALSE;
}

TpConnection *
mcp_dispatch_operation_ref_connection (McpDispatchOperation *self)
{
  TpDBusDaemon *daemon = tp_dbus_daemon_dup (NULL);
  TpConnection *connection = NULL;
  const gchar *conn_path;

  conn_path = mcp_dispatch_operation_get_connection_path (self);

  if (conn_path != NULL && daemon != NULL)
    {
      connection = tp_connection_new (daemon, NULL, conn_path, NULL);
    }

  g_object_unref (daemon);
  return connection;
}

TpChannel *
mcp_dispatch_operation_ref_nth_channel (McpDispatchOperation *self,
    guint n)
{
  TpConnection *connection = mcp_dispatch_operation_ref_connection (self);
  GHashTable *channel_properties = NULL;
  const gchar *channel_path = NULL;
  TpChannel *channel = NULL;

  if (connection == NULL)
    goto finally;

  channel_path = mcp_dispatch_operation_get_nth_channel_path (self, n);

  if (channel_path == NULL)
    goto finally;

  channel_properties = mcp_dispatch_operation_ref_nth_channel_properties (self,
      n);

  if (channel_properties == NULL)
    goto finally;

  channel = tp_channel_new_from_properties (connection,
      channel_path,
      channel_properties,
      NULL);

finally:
  if (connection != NULL)
    g_object_unref (connection);

  if (channel_properties != NULL)
    g_hash_table_unref (channel_properties);

  return channel;
}
