/*
 * This file is part of mission-control
 *
 * Copyright © 2011 Nokia Corporation.
 * Copyright © 2011 Collabora Ltd.
 *
 * Contact: Vivek Dasmohapatra  <vivek@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "config.h"

#include "mcd-connection-service-points.h"
#include "mcd-connection-priv.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

static void
service_handles_fetched_cb (TpConnection *tp_conn,
    TpHandleType handle_type,
    guint n_handles,
    const TpHandle *handles,
    const gchar * const *ids,
    const GError *error,
    gpointer user_data G_GNUC_UNUSED,
    GObject *weak)
{
  guint i;
  McdConnection *connection = MCD_CONNECTION (weak);
  TpIntset *e_handles = tp_intset_new ();

  if (error != NULL)
    return;

  for (i = 0; i < n_handles; i++)
    tp_intset_add (e_handles, handles[i]);

  _mcd_connection_take_emergency_handles (connection, e_handles);
}

static void
parse_services_list (McdConnection *connection,
    const GPtrArray *services)
{
  guint i;
  GSList *e_numbers = NULL;

  for (i = 0; i < services->len; i++)
    {
      GValueArray *sp_info;
      GValueArray *sp;
      gchar **numbers;
      guint type;

      sp_info = g_ptr_array_index (services, i);
      sp = g_value_get_boxed (sp_info->values);
      type = g_value_get_uint (sp->values);

      if (type == TP_SERVICE_POINT_TYPE_EMERGENCY)
        {
          numbers = g_value_dup_boxed (sp_info->values + 1);
          e_numbers = g_slist_prepend (e_numbers, numbers);
        }
    }

  if (e_numbers != NULL)
    {
      GSList *service;
      TpConnection *tp_conn = mcd_connection_get_tp_connection (connection);

      _mcd_connection_clear_emergency_data (connection);

      for (service = e_numbers; service != NULL; service =g_slist_next (service))
        {
          if (service->data != NULL)
            tp_connection_request_handles (tp_conn, -1, TP_HANDLE_TYPE_CONTACT,
                (const gchar *const *) service->data,
                service_handles_fetched_cb, NULL, NULL, G_OBJECT (connection));
        }

      _mcd_connection_take_emergency_numbers (connection, e_numbers);
    }
}

static void
service_points_changed_cb (TpConnection *proxy,
    const GPtrArray *service_points,
    gpointer user_data G_GNUC_UNUSED,
    GObject *object)
{
  McdConnection *connection = MCD_CONNECTION (object);

  parse_services_list (connection, service_points);
}

static void
service_points_fetched_cb (TpProxy *proxy,
    const GValue *value,
    const GError *error,
    gpointer user_data G_GNUC_UNUSED,
    GObject *object)
{
  McdConnection *connection = MCD_CONNECTION (object);

  if (error)
    {
      g_warning ("%s: got error: %s", G_STRFUNC, error->message);
      return;
    }

    parse_services_list (connection, g_value_get_boxed (value));
}

static void
service_point_interface_check (TpConnection *tp_conn,
    const gchar **interfaces,
    const GError *error,
    gpointer data,
    GObject *connection)
{
  const gchar *interface;
  gboolean found = FALSE;
  gboolean watch = GPOINTER_TO_UINT (data);
  guint i = 0;

  if (interfaces == NULL)
    return;

  for (interface = interfaces[0];
       !found && !tp_str_empty (interface);
       interface = interfaces[++i])
    {
      if (!tp_strdiff (interface, TP_IFACE_CONNECTION_INTERFACE_SERVICE_POINT))
        found = TRUE;
    }

  if (!found)
    return;

  /* so we know if/when the service points change (eg the SIM might not be
   * accessible yet, in which case the call below won't return any entries)
   * check the flag though as we only want to do this once per connection:
   */
  if (watch)
    tp_cli_connection_interface_service_point_connect_to_service_points_changed
      (tp_conn, service_points_changed_cb, NULL, NULL, connection, NULL);

  /* fetch the current list to initialise our state */
  tp_cli_dbus_properties_call_get (tp_conn, -1,
      TP_IFACE_CONNECTION_INTERFACE_SERVICE_POINT,
      "KnownServicePoints", service_points_fetched_cb,
      NULL, NULL, connection);
}

void
mcd_connection_service_point_setup (McdConnection *connection, gboolean watch)
{
  TpConnection *tp_conn = mcd_connection_get_tp_connection (connection);

  if (G_UNLIKELY (!tp_conn))
    return;

  /* see if the connection supports the service point interface */
  tp_cli_connection_call_get_interfaces (tp_conn, -1,
      service_point_interface_check,
      GUINT_TO_POINTER (watch), NULL, G_OBJECT (connection));
}
