/* Mission Control plugin API - representation of a ChannelRequest
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
 * SECTION:request
 * @title: McpRequest
 * @short_description: Request object, implemented by Mission Control
 * @see_also: #McpRequestPolicy
 * @include: mission-control-plugins/mission-control-plugins.h
 */

#include "config.h"

#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/implementation.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

GType
mcp_request_get_type (void)
{
  static gsize once = 0;
  static GType type = 0;

  if (g_once_init_enter (&once))
    {
      static const GTypeInfo info = {
          sizeof (McpRequestIface),
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

      type = g_type_register_static (G_TYPE_INTERFACE, "McpRequest",
          &info, 0);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);

      g_once_init_leave (&once, 1);
    }

  return type;
}

const gchar *
mcp_request_get_account_path (McpRequest *self)
{
  McpRequestIface *iface = MCP_REQUEST_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_account_path != NULL, NULL);
  return iface->get_account_path (self);
}

const gchar *
mcp_request_get_protocol (McpRequest *self)
{
  McpRequestIface *iface = MCP_REQUEST_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_protocol != NULL, NULL);
  return iface->get_protocol (self);
}

const gchar *
mcp_request_get_cm_name (McpRequest *self)
{
  McpRequestIface *iface = MCP_REQUEST_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_cm_name != NULL, NULL);
  return iface->get_cm_name (self);
}

gint64
mcp_request_get_user_action_time (McpRequest *self)
{
  McpRequestIface *iface = MCP_REQUEST_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, 0);
  g_return_val_if_fail (iface->get_user_action_time != NULL, 0);
  return iface->get_user_action_time (self);
}

guint
mcp_request_get_n_requests (McpRequest *self)
{
  McpRequestIface *iface = MCP_REQUEST_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, 0);
  g_return_val_if_fail (iface->get_n_requests != NULL, 0);
  return iface->get_n_requests (self);
}

GHashTable *
mcp_request_ref_nth_request (McpRequest *self,
    guint n)
{
  McpRequestIface *iface = MCP_REQUEST_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->ref_nth_request != NULL, NULL);
  return iface->ref_nth_request (self, n);
}

void
mcp_request_deny (McpRequest *self,
    GQuark domain,
    gint code,
    const gchar *message)
{
  McpRequestIface *iface = MCP_REQUEST_GET_IFACE (self);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (domain != 0);
  g_return_if_fail (message != NULL);
  g_return_if_fail (iface->deny != NULL);

  iface->deny (self, domain, code, message);
}

gboolean
mcp_request_find_request_by_type (McpRequest *self,
    guint start_from,
    GQuark channel_type,
    guint *ret_index,
    GHashTable **ret_ref_requested_properties)
{
  guint i = start_from;

  while (1)
    {
      GHashTable *req = mcp_request_ref_nth_request (self, i);

      if (req == NULL)
        return FALSE;

      if (channel_type == 0 ||
          channel_type == g_quark_try_string (
            tp_asv_get_string (req, TP_IFACE_CHANNEL ".ChannelType")))
        {
          if (ret_index != NULL)
            *ret_index = i;

          if (ret_ref_requested_properties != NULL)
            *ret_ref_requested_properties = req;
          else
            g_hash_table_unref (req);

          return TRUE;
        }

      g_hash_table_unref (req);
      i++;
    }
}

McpRequestDelay *
mcp_request_start_delay (McpRequest *self)
{
  McpRequestIface *iface = MCP_REQUEST_GET_IFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->start_delay != NULL, NULL);
  return iface->start_delay (self);
}

void
mcp_request_end_delay (McpRequest *self, McpRequestDelay *delay)
{
  McpRequestIface *iface = MCP_REQUEST_GET_IFACE (self);

  g_return_if_fail (iface != NULL);
  g_return_if_fail (delay != NULL);
  g_return_if_fail (iface->end_delay != NULL);
  iface->end_delay (self, delay);
}
