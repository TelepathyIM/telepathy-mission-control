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

/**
 * SECTION:dispatch-operation
 * @title: McpDispatchOperation
 * @short_description: Dispatch operation object, implemented by Mission Control
 * @see_also: #McpDispatchOperationPolicy
 * @include: mission-control-plugins/mission-control-plugins.h
 *
 * This object represents a Telepathy ChannelDispatchOperation object, as used
 * by Approvers. A ChannelDispatchOperation represents a bundle of one or more
 * Telepathy Channels being dispatched to user interfaces or other clients.
 *
 * The virtual method mcd_dispatch_operation_policy_check() receives an object
 * provided by Mission Control that implements this interface. It can be
 * used to inspect the channels, delay dispatching of the bundle until the
 * plugin is ready to continue, or close the channels in various ways.
 *
 * Only Mission Control should implement this interface.
 */

#include "config.h"

#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/implementation.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

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

/**
 * mcp_dispatch_operation_get_account_path:
 * @self: a dispatch operation
 *
 * <!-- -->
 *
 * Returns: the D-Bus object path of the Account with which the channels are
 *  associated. The string is owned by @self and must not be freed; it is
 *  only valid as long as a reference to @self is held.
 */
const gchar *
mcp_dispatch_operation_get_account_path (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_account_path != NULL, NULL);
  return iface->get_account_path (self);
}

/**
 * mcp_dispatch_operation_get_connection_path:
 * @self: a dispatch operation
 *
 * <!-- -->
 *
 * Returns: the D-Bus object path of the Connection with which the channels are
 *  associated. The string is owned by @self and must not be freed; it is
 *  only valid as long as a reference to @self is held.
 */
const gchar *
mcp_dispatch_operation_get_connection_path (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_connection_path != NULL, NULL);
  return iface->get_connection_path (self);
}

/**
 * mcp_dispatch_operation_get_protocol:
 * @self: a dispatch operation
 *
 * <!-- -->
 *
 * Returns: the Telepathy identifier for the protocol, such as 'jabber' or
 *  'icq' (the Protocol type in telepathy-spec). The string is owned by @self
 *  and must not be freed; it is only valid as long as a reference to @self
 *  is held.
 */
const gchar *
mcp_dispatch_operation_get_protocol (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_protocol != NULL, NULL);
  return iface->get_protocol (self);
}

/**
 * mcp_dispatch_operation_get_cm_name:
 * @self: a dispatch operation
 *
 * <!-- -->
 *
 * Returns: the short name of the Telepathy connection manager, such as
 *  'gabble' or 'haze' (the Connection_Manager_Name type in telepathy-spec).
 *  The string is owned by @self and must not be freed; it is only valid as
 *  long as a reference to @self is held.
 */
const gchar *
mcp_dispatch_operation_get_cm_name (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_cm_name != NULL, NULL);
  return iface->get_cm_name (self);
}

/**
 * mcp_dispatch_operation_get_n_channels:
 * @self: a dispatch operation
 *
 * <!-- -->
 *
 * Returns: the number of channels in this dispatch operation.
 */
guint
mcp_dispatch_operation_get_n_channels (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, 0);
  g_return_val_if_fail (iface->get_n_channels != NULL, 0);
  return iface->get_n_channels (self);
}

/**
 * mcp_dispatch_operation_get_nth_channel_path:
 * @self: a dispatch operation
 * @n: index of the channel to inspect
 *
 * <!-- -->
 *
 * Returns: the D-Bus object path of the @n'th channel (starting from 0), or
 *  %NULL if @n is greater than or equal to
 *  mcp_dispatch_operation_get_n_channels().
 *  The string is owned by @self and must not be freed; it is only valid as
 *  long as a reference to @self is held.
 */
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

/**
 * mcp_dispatch_operation_ref_nth_channel_properties:
 * @self: a dispatch operation
 * @n: index of the channel to inspect
 *
 * Return the immutable properties of the @n'th channel (starting from 0), or
 * %NULL if @n is greater than or equal to
 * mcp_dispatch_operation_get_n_channels().
 *
 * The keys of the hash table are strings and the values are in #GValue
 * structures, using the same representation as dbus-glib, tp_asv_get_string()
 * etc. Do not add or remove entries in this hash table.
 *
 * Returns: a reference to a hash table, which must be released with
 *  g_hash_table_unref() by the caller
 */
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

/**
 * mcp_dispatch_operation_start_delay:
 * @self: a dispatch operation
 *
 * Start to delay the dispatch operation, for instance while waiting for
 * an asynchronous operation to finish. The returned token must be passed to
 * mcp_dispatch_operation_end_delay() exactly once, at which point dispatching
 * will continue and the token becomes invalid.
 *
 * This is similar to an Observer delaying the return from ObserveChannels,
 * except that there is no time limit - a dispatch operation policy plugin
 * can delay the dispatch operation indefinitely.
 *
 * Returns: a token which can be used to end the delay
 */
McpDispatchOperationDelay *
mcp_dispatch_operation_start_delay (McpDispatchOperation *self)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->start_delay != NULL, NULL);
  return iface->start_delay (self);
}

/**
 * mcp_dispatch_operation_end_delay:
 * @self: a dispatch operation
 * @delay: a token obtained by calling mcp_dispatch_operation_start_delay()
 *  on @self previously
 *
 * Stop delaying the dispatch operation, allowing dispatching to proceed.
 */
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

/**
 * mcp_dispatch_operation_leave_channels:
 * @self: a dispatch operation
 * @wait_for_observers: if %FALSE, leave the channels immediately; if %TRUE
 *  (usually recommended), wait for Observers to reply first
 * @reason: the reason code to give
 * @message: a human-readable message provided by the user, or either the
 *  empty string or %NULL if no message has been provided
 *
 * Leave all channels in this bundle by using RemoveMembersWithReason if the
 * channel has the Group interface, or Close if not.
 *
 * This method was intended for StreamedMedia channels, which (ab)used the
 * Group interface for call control. StreamedMedia channels have been
 * superseded by Call channels, which have a proper "hang up" method which
 * should be used instead.
 *
 * Deprecated: 5.15.1: Use tp_call_channel_hangup_async() to
 *  hang up Call channels, mcp_dispatch_operation_close_channels() to close
 *  generic channels, or mcp_dispatch_operation_destroy_channels() to
 *  terminate the channel destructively.
 */
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

/**
 * mcp_dispatch_operation_close_channels:
 * @self: a dispatch operation
 * @wait_for_observers: if %FALSE, close the channels immediately; if %TRUE
 *  (usually recommended), wait for Observers to reply first
 *
 * Close all channels in this bundle by using the Close D-Bus method.
 *
 * Plugins that terminate an audio or audio/video call should
 * use tp_call_channel_hangup_async() instead.
 */
void
mcp_dispatch_operation_close_channels (McpDispatchOperation *self,
    gboolean wait_for_observers)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->close_channels != NULL);
  iface->close_channels (self, wait_for_observers);
}

/**
 * mcp_dispatch_operation_destroy_channels:
 * @self: a dispatch operation
 * @wait_for_observers: if %FALSE, close the channels immediately; if %TRUE
 *  (usually recommended), wait for Observers to reply first
 *
 * Close all channels in this bundle destructively, by using the Destroy D-Bus
 * method if implemented, or the Close D-Bus method if not.
 *
 * Plugins that terminate an audio or audio/video call should
 * use tp_call_channel_hangup_async() instead.
 */
void
mcp_dispatch_operation_destroy_channels (McpDispatchOperation *self,
    gboolean wait_for_observers)
{
  McpDispatchOperationIface *iface = MCP_DISPATCH_OPERATION_GET_IFACE (self);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->destroy_channels != NULL);
  iface->destroy_channels (self, wait_for_observers);
}

/**
 * mcp_dispatch_operation_find_channel_by_type:
 * @self: a dispatch operation
 * @start_from: index at which to start searching, usually 0
 * @handle_type: the handle type to match
 * @channel_type: the channel type to match
 * @ret_index: if not %NULL, used to return the index of the first matching
 *  channel, suitable for use with
 *  mcp_dispatch_operation_get_nth_channel_path() etc.
 * @ret_dup_path: if not %NULL, used to return the object path of the first
 *  matching channel, which must be freed with g_free()
 * @ret_ref_immutable_properties: if not %NULL, used to return a reference to
 *  immutable properties, as if via
 *  mcp_dispatch_operation_ref_nth_channel_properties(), which must be
 *  released with g_hash_table_unref()
 * @ret_ref_channel: if not %NULL, used to return a #TpChannel, which is not
 *  guaranteed to be ready immediately, and must be released with
 *  g_object_unref()
 *
 * Attempt to find a channel matching the given handle type and channel type
 * in the bundle. This is an easy way to test whether the bundle contains any
 * channels of interest to a particular plugin.
 *
 * Returns: %TRUE if a matching channel was found, or %FALSE (without touching
 *  @ret_index, @ret_dup_path, @ret_ref_immutable_properties or
 *  @ret_ref_channel) if not
 */
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
              /* FIXME: in next, this method should take a TpClientFactory
               * argument, and pass it on here */
              TpConnection *connection =
                mcp_dispatch_operation_ref_connection (self);

              *ret_ref_channel = tp_simple_client_factory_ensure_channel (
                  tp_proxy_get_factory (connection), connection,
                  channel_path, properties, NULL);

              g_object_unref (connection);
            }

          return TRUE;
        }

      g_hash_table_unref (properties);
    }

  return FALSE;
}

/**
 * mcp_dispatch_operation_ref_connection:
 * @self: a dispatch operation
 *
 * Return a #TpConnection object. It is not guaranteed to be ready immediately;
 * use tp_connection_call_when_ready().
 *
 * Returns: a reference to a #TpConnection, which must be released with
 *  g_object_unref() by the caller
 */
TpConnection *
mcp_dispatch_operation_ref_connection (McpDispatchOperation *self)
{
  TpDBusDaemon *dbus = tp_dbus_daemon_dup (NULL);
  TpConnection *connection = NULL;
  const gchar *conn_path;

  conn_path = mcp_dispatch_operation_get_connection_path (self);

  if (conn_path != NULL && dbus != NULL)
    {
      /* FIXME: in next, this method should take a TpClientFactory argument
       * instead of making a new one here */
      TpSimpleClientFactory *factory = tp_simple_client_factory_new (dbus);

      connection = tp_simple_client_factory_ensure_connection (factory,
          conn_path, NULL, NULL);
      g_object_unref (factory);
    }

  g_object_unref (dbus);
  return connection;
}

/**
 * mcp_dispatch_operation_ref_nth_channel:
 * @self: a dispatch operation
 * @n: index of the channel to inspect
 *
 * Return a #TpChannel object. It is not guaranteed to be ready immediately;
 * use tp_channel_call_when_ready().
 *
 * Returns: a reference to a #TpChannel, which must be released with
 *  g_object_unref() by the caller, or %NULL if @n is too large
 */
TpChannel *
mcp_dispatch_operation_ref_nth_channel (McpDispatchOperation *self,
    guint n)
{
  /* FIXME: in next, this method should take a TpClientFactory argument,
   * and pass it on here */
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

  channel = tp_simple_client_factory_ensure_channel (
      tp_proxy_get_factory (connection),
      connection, channel_path, channel_properties, NULL);

finally:
  tp_clear_object (&connection);
  tp_clear_pointer (&channel_properties, g_hash_table_unref);

  return channel;
}
