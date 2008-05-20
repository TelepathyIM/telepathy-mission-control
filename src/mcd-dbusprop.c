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

#define MCD_INTERFACES_QUARK get_interfaces_quark()

static GQuark
get_interfaces_quark ()
{
    static GQuark interfaces_quark = 0;
    
    if (G_UNLIKELY (!interfaces_quark))
	interfaces_quark = g_quark_from_static_string ("interfaces");
    return interfaces_quark;
}

static const McdDBusProp *
get_interface_properties (TpSvcDBusProperties *object, const gchar *interface)
{
    McdInterfaceData *iface_data;

    iface_data = g_type_get_qdata (G_OBJECT_TYPE (object),
				   MCD_INTERFACES_QUARK);
    while (iface_data->get_type)
    {
	if (iface_data->interface &&
	    strcmp (iface_data->interface, interface) == 0)
	    return iface_data->properties;
	iface_data++;
    }
    return NULL;
}

void
dbusprop_set (TpSvcDBusProperties *self,
	      const gchar *interface_name,
	      const gchar *property_name,
	      const GValue *value,
	      DBusGMethodInvocation *context)
{
    const McdDBusProp *prop_array, *property;
    GError *error = NULL;

    g_debug ("%s: %s, %s", G_STRFUNC, interface_name, property_name);

    prop_array = get_interface_properties (self, interface_name);
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
    const McdDBusProp *prop_array, *property;

    prop_array = get_interface_properties (self, interface_name);
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
    const McdDBusProp *prop_array, *property;
    GHashTable *properties;
    GError *error = NULL;

    g_debug ("%s: %s", G_STRFUNC, interface_name);

    prop_array = get_interface_properties (self, interface_name);
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
mcd_dbus_init_interfaces (GType g_define_type_id,
			  const McdInterfaceData *iface_data)
{
    g_type_set_qdata (g_define_type_id, MCD_INTERFACES_QUARK,
		      (gpointer)iface_data);

    while (iface_data->get_type)
    {
	GType type;

	type = iface_data->get_type();
	G_IMPLEMENT_INTERFACE (type, iface_data->iface_init);
	iface_data++;
    }
}

void
mcd_dbus_init_interfaces_instances (gpointer self)
{
    McdInterfaceData *iface_data;

    iface_data = g_type_get_qdata (G_OBJECT_TYPE (self), MCD_INTERFACES_QUARK);

    while (iface_data->get_type)
    {
	if (iface_data->instance_init)
	    iface_data->instance_init (self);
	iface_data++;
    }
}

void
mcd_dbus_get_interfaces (TpSvcDBusProperties *self, const gchar *name,
			 GValue *value)
{
    McdInterfaceData *iface_data, *id;
    gint i;
    gchar **names;

    g_debug ("%s called", G_STRFUNC);
    iface_data = g_type_get_qdata (G_OBJECT_TYPE (self), MCD_INTERFACES_QUARK);

    /* count the interfaces */
    i = 0;
    for (id = iface_data; id->get_type; id++)
	i++;

    names = g_new (gchar *, i + 1);
    i = 0;
    for (id = iface_data; id->get_type; id++)
    {
	names[i] = g_strdup (id->interface);
	i++;
    }
    names[i] = NULL;
    g_value_init (value, G_TYPE_STRV);
    g_value_take_boxed (value, names);
}

