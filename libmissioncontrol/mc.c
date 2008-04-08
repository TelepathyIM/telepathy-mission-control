/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
 *
 * Contact: Naba Kumar  <naba.kumar@nokia.com>
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

#include <gmodule.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/proxy-subclass.h>


#include "mc.h"

#define LIBRARY_FILE G_STRINGIFY(LIBDIR) "/libmissioncontrol-config.so." G_STRINGIFY(LIBVERSION)

/**
 * mc_make_resident:
 *
 * This function is a workaround for problems with mc getting loaded twice
 * into the same process, such as when the control panel loads a plugin which
 * uses mc after it has already been loaded and unloaded. In order to
 * prevent g_type_register_static being called twice, this function can be
 * called to make mc be redident in memory for the lifetime of the process.
 */

void
mc_make_resident (void)
{
  GModule *module = g_module_open (LIBRARY_FILE, 0);
  if (NULL == module)
    {
      g_critical("%s: g_module_open() failed: %s", G_STRFUNC, g_module_error());
    }
  g_module_make_resident (module);
}

gboolean
mc_cli_dbus_properties_do_get (gpointer proxy,
    gint timeout_ms,
    const gchar *in_Interface_Name,
    const gchar *in_Property_Name,
    GValue **out_Value,
    GError **error)
{
  DBusGProxy *iface;
  GQuark interface = TP_IFACE_QUARK_DBUS_PROPERTIES;
  GValue *o_Value = g_new0 (GValue, 1);

  g_return_val_if_fail (TP_IS_PROXY (proxy), FALSE);

  iface = tp_proxy_borrow_interface_by_id
       ((TpProxy *) proxy, interface, error);

  if (iface == NULL)
    return FALSE;

  if(dbus_g_proxy_call_with_timeout (iface,
          "Get",
          timeout_ms,
	  error,
              G_TYPE_STRING, in_Interface_Name,
              G_TYPE_STRING, in_Property_Name,
          G_TYPE_INVALID,
              G_TYPE_VALUE, o_Value,
          G_TYPE_INVALID))
  {
      *out_Value = o_Value;
      return TRUE;
  }
  else
  {
      g_free (o_Value);
      return FALSE;
  }
}


gboolean
mc_cli_dbus_properties_do_get_all (gpointer proxy,
    gint timeout_ms,
    const gchar *in_Interface_Name,
    GHashTable **out_Properties,
    GError **error)
{
  DBusGProxy *iface;
  GQuark interface = TP_IFACE_QUARK_DBUS_PROPERTIES;
  GHashTable *o_Properties;

  g_return_val_if_fail (TP_IS_PROXY (proxy), FALSE);

  iface = tp_proxy_borrow_interface_by_id
       ((TpProxy *) proxy, interface, error);

  if (iface == NULL)
    return FALSE;

  if (dbus_g_proxy_call_with_timeout (iface,
          "GetAll",
          timeout_ms,
	  error,
              G_TYPE_STRING, in_Interface_Name,
          G_TYPE_INVALID,
	      (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)), &o_Properties,
          G_TYPE_INVALID))
  {
      *out_Properties = o_Properties;

      return TRUE;
  }
  else
      return FALSE;
}


gboolean
mc_cli_dbus_properties_do_set (gpointer proxy,
    gint timeout_ms,
    const gchar *in_Interface_Name,
    const gchar *in_Property_Name,
    const GValue *in_Value,
    GError **error)
{
  DBusGProxy *iface;
  GQuark interface = TP_IFACE_QUARK_DBUS_PROPERTIES;

  g_return_val_if_fail (TP_IS_PROXY (proxy), FALSE);

  iface = tp_proxy_borrow_interface_by_id
       ((TpProxy *) proxy, interface, error);

  if (iface == NULL)
    return FALSE;

  return dbus_g_proxy_call_with_timeout (iface,
          "Set",
          timeout_ms,
	  error,
              G_TYPE_STRING, in_Interface_Name,
              G_TYPE_STRING, in_Property_Name,
              G_TYPE_VALUE, in_Value,
          G_TYPE_INVALID);
}


