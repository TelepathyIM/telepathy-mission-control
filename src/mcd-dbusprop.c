/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008 Nokia Corporation. 
 *
 * Contact: Alberto Mardegan  <alberto.mardegan@nokia.com>
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

#include <string.h>
#include <telepathy-glib/errors.h>
#include "mcd-dbusprop.h"

void
dbusprop_set (TpSvcDBusProperties *self,
	      const gchar *interface_name,
	      const gchar *property_name,
	      const GValue *value,
	      DBusGMethodInvocation *context)
{
    McdDBusProp *prop_array, *property;
    GError *error = NULL;

    g_debug ("%s: %s, %s", G_STRFUNC, interface_name, property_name);

    /* FIXME: use some prefix */
    prop_array = g_object_get_data (G_OBJECT (self), interface_name);
    if (!prop_array)
    {
	g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "invalid interface: %s", interface_name);
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }

    /* look for our property */
    for (property = prop_array; property->name != NULL; property++)
	if (strcmp (property->name, property_name) == 0)
	    break;
    if (!property->name)
    {
	g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "invalid property: %s", property_name);
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }

    if (!property->setprop)
    {
	g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "property %s cannot be written", property_name);
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }
    /* we pass property->name, because we know it's a static value and there
     * will be no need to care about its lifetime */
    property->setprop (self, property->name, value);
    tp_svc_dbus_properties_return_from_set (context);
}

void
mcd_dbusprop_get_property (TpSvcDBusProperties *self,
			   const gchar *interface_name,
			   const gchar *property_name,
			   GValue *value,
			   GError **error)
{
    McdDBusProp *prop_array, *property;

    /* FIXME: use some prefix */
    prop_array = g_object_get_data (G_OBJECT (self), interface_name);
    if (!prop_array)
    {
	g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "invalid interface: %s", interface_name);
	return;
    }

    /* look for our property */
    for (property = prop_array; property->name != NULL; property++)
	if (strcmp (property->name, property_name) == 0)
	    break;
    if (!property->name)
    {
	g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "invalid property: %s", property_name);
	return;
    }

    if (!property->getprop)
    {
	g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "property %s cannot be read", property_name);
	return;
    }
    property->getprop (self, property_name, value);
}

void
dbusprop_get (TpSvcDBusProperties *self,
	      const gchar *interface_name,
	      const gchar *property_name,
	      DBusGMethodInvocation *context)
{
    GValue value = { 0 };
    GError *error = NULL;

    g_debug ("%s: %s, %s", G_STRFUNC, interface_name, property_name);

    mcd_dbusprop_get_property (self, interface_name, property_name,
			       &value, &error);
    if (error)
    {
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }

    tp_svc_dbus_properties_return_from_get (context, &value);
    g_value_unset (&value);
}

static void
free_prop_val (gpointer val)
{
    GValue *value = val;

    g_value_unset (value);
    g_free (value);
}

void
dbusprop_get_all (TpSvcDBusProperties *self,
		  const gchar *interface_name,
		  DBusGMethodInvocation *context)
{
    McdDBusProp *prop_array, *property;
    GHashTable *properties;
    GError *error = NULL;

    g_debug ("%s: %s", G_STRFUNC, interface_name);

    /* FIXME: use some prefix */
    prop_array = g_object_get_data (G_OBJECT (self), interface_name);
    if (!prop_array)
    {
	g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "invalid interface: %s", interface_name);
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }

    properties = g_hash_table_new_full (g_str_hash, g_str_equal,
					NULL, free_prop_val);
    for (property = prop_array; property->name != NULL; property++)
    {
	GValue *value;

	if (!property->getprop) continue;

	value = g_malloc0 (sizeof (GValue));
	property->getprop (self, property->name, value);
	g_hash_table_insert (properties, (gchar *)property->name, value);
    }
    tp_svc_dbus_properties_return_from_get_all (context, properties);
    g_hash_table_destroy (properties);
}

void
dbusprop_add_interface (TpSvcDBusProperties *self,
			const gchar *interface_name,
			const McdDBusProp *properties)
{
    g_debug ("%s: %s", G_STRFUNC, interface_name);
    g_object_set_data (G_OBJECT (self), interface_name, (gpointer)properties);
}

