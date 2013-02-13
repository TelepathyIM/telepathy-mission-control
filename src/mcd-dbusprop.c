/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
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

#include "config.h"

#include <string.h>
#include <telepathy-glib/telepathy-glib.h>

#include "mcd-dbusprop.h"
#include "mcd-debug.h"

#define MCD_INTERFACES_QUARK get_interfaces_quark()

static GQuark
get_interfaces_quark (void)
{
    static GQuark interfaces_quark = 0;

    if (G_UNLIKELY (!interfaces_quark))
	interfaces_quark = g_quark_from_static_string ("interfaces");
    return interfaces_quark;
}

#define MCD_ACTIVE_OPTIONAL_INTERFACES_QUARK \
    get_active_optional_interfaces_quark()

static GQuark
get_active_optional_interfaces_quark (void)
{
    static GQuark active_optional_interfaces_quark = 0;

    if (G_UNLIKELY (active_optional_interfaces_quark == 0))
        active_optional_interfaces_quark =
            g_quark_from_static_string ("active-optional-interfaces");

    return active_optional_interfaces_quark;
}

/**
 * get_active_optional_interfaces:
 *
 * DBus Interfaces marked as optional will only be included in the object's
 * Interfaces property if they appear in this set.
 *
 * Returns: a #TpIntset of the active optional interfaces.
 */
static TpIntset *
get_active_optional_interfaces (TpSvcDBusProperties *object)
{
    TpIntset *aoi = g_object_get_qdata (G_OBJECT (object),
                                        MCD_ACTIVE_OPTIONAL_INTERFACES_QUARK);

    if (G_UNLIKELY (aoi == NULL))
    {
        aoi = tp_intset_new ();

        g_object_set_qdata_full (G_OBJECT (object),
                                 MCD_ACTIVE_OPTIONAL_INTERFACES_QUARK,
                                 aoi,
                                 (GDestroyNotify) tp_intset_destroy);
    }

    return aoi;
}

void
mcd_dbus_activate_optional_interface (TpSvcDBusProperties *object,
                                      GType interface)
{
    tp_intset_add (get_active_optional_interfaces (object), interface);
}

gboolean
mcd_dbus_is_active_optional_interface (TpSvcDBusProperties *object,
                                       GType interface)
{
    return tp_intset_is_member (get_active_optional_interfaces (object),
                                interface);
}

static const McdDBusProp *
get_interface_properties (TpSvcDBusProperties *object, const gchar *interface)
{
    McdInterfaceData *iface_data;
    GType type;

    /* we must look up the ancestors, in case the object implementing the
     * interface has been subclassed. */
    for (type = G_OBJECT_TYPE (object); type != 0; type = g_type_parent (type))
    {
	iface_data = g_type_get_qdata (type, MCD_INTERFACES_QUARK);
	if (!iface_data) continue;

	while (iface_data->get_type)
	{
	    if (iface_data->interface &&
		strcmp (iface_data->interface, interface) == 0)
		return iface_data->properties;
	    iface_data++;
	}
    }
    return NULL;
}

static const McdDBusProp *
get_mcddbusprop (TpSvcDBusProperties *self,
                 const gchar *interface_name,
                 const gchar *property_name,
                 GError **error)
{
    const McdDBusProp *prop_array, *property;

    DEBUG ("%s, %s", interface_name, property_name);

    prop_array = get_interface_properties (self, interface_name);
    if (!prop_array)
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "invalid interface: %s", interface_name);
        return NULL;
    }

    /* look for our property */
    for (property = prop_array; property->name != NULL; property++)
        if (strcmp (property->name, property_name) == 0)
            break;

    if (!property->name)
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "invalid property: %s", property_name);
        return NULL;
    }

    return property;
}

gboolean
mcd_dbusprop_set_property (TpSvcDBusProperties *self,
                           const gchar *interface_name,
                           const gchar *property_name,
                           const GValue *value,
                           GError **error)
{
    const McdDBusProp *property;

    property = get_mcddbusprop (self, interface_name, property_name, error);

    if (property == NULL)
      return FALSE;

    if (!property->setprop)
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "property %s cannot be written", property_name);
        return FALSE;
    }

    /* we pass property->name, because we know it's a static value and there
     * will be no need to care about its lifetime */
    return property->setprop (self, property->name, value,
                              MCD_DBUS_PROP_SET_FLAG_NONE, error);
}

void
dbusprop_set (TpSvcDBusProperties *self,
              const gchar *interface_name,
              const gchar *property_name,
              const GValue *value,
              DBusGMethodInvocation *context)
{
    GError *error = NULL;

    mcd_dbusprop_set_property (self, interface_name, property_name,
			       value, &error);
    if (error)
    {
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }

    tp_svc_dbus_properties_return_from_set (context);
}

gboolean
mcd_dbusprop_get_property (TpSvcDBusProperties *self,
                           const gchar *interface_name,
                           const gchar *property_name,
                           GValue *value,
                           GError **error)
{
    const McdDBusProp *property;

    property = get_mcddbusprop (self, interface_name, property_name, error);

    if (property == NULL)
      return FALSE;

    if (!property->getprop)
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "property %s cannot be read", property_name);
        return FALSE;
    }

    property->getprop (self, property_name, value);
    return TRUE;
}

void
dbusprop_get (TpSvcDBusProperties *self,
              const gchar *interface_name,
              const gchar *property_name,
              DBusGMethodInvocation *context)
{
    GValue value = G_VALUE_INIT;
    GError *error = NULL;

    DEBUG ("%s, %s", interface_name, property_name);

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

typedef struct
{
    TpSvcDBusProperties *self;
    DBusGMethodInvocation *context;
    GHashTable *properties;
    const McdDBusProp *property;
} GetAllData;

static void
get_all_iter (GetAllData *data)
{
    if (data->property->name != NULL)
    {
        if (data->property->getprop)
        {
            GValue *out;

            out = g_malloc0 (sizeof (GValue));
            data->property->getprop (data->self, data->property->name, out);
            g_hash_table_insert (data->properties, (gchar *) data->property->name,
                tp_g_value_slice_dup (out));
            g_value_unset (out);
            g_free (out);

            data->property++;
            get_all_iter (data);
        }
        else
        {
            data->property++;
            get_all_iter (data);
        }
    }
    else
    {
      tp_svc_dbus_properties_return_from_get_all (data->context,
                                                  data->properties);
      g_hash_table_unref (data->properties);
      g_slice_free (GetAllData, data);

    }
}

typedef struct
{
    TpSvcDBusProperties *tp_svc_props;
    gchar *interface;
    gchar *property;
} DBusPropAsyncData;

void
dbusprop_get_all (TpSvcDBusProperties *self,
                  const gchar *interface_name,
                  DBusGMethodInvocation *context)
{
    const McdDBusProp *prop_array;
    GError *error = NULL;
    GetAllData *data;

    DEBUG ("%s", interface_name);

    prop_array = get_interface_properties (self, interface_name);
    if (!prop_array)
    {
        g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "invalid interface: %s", interface_name);
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    data = g_slice_new0 (GetAllData);
    data->self = self;
    data->context = context;

    data->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              NULL, (GDestroyNotify) tp_g_value_slice_free);

    data->property = prop_array;

    get_all_iter (data);
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
    GPtrArray *a_ifaces;
    GType type;

    DEBUG ("called");

    a_ifaces = g_ptr_array_new ();

    for (type = G_OBJECT_TYPE (self); type != 0; type = g_type_parent (type))
    {
	iface_data = g_type_get_qdata (type, MCD_INTERFACES_QUARK);
	if (!iface_data) continue;

	for (id = iface_data; id->get_type; id++)
        {
            if (id->optional &&
                !mcd_dbus_is_active_optional_interface (self,
                                                        id->get_type ()))
            {
                DEBUG ("skipping inactive optional iface %s", id->interface);
                continue;
            }

	    g_ptr_array_add (a_ifaces, g_strdup (id->interface));
        }
    }
    g_ptr_array_add (a_ifaces, NULL);

    g_value_init (value, G_TYPE_STRV);
    g_value_take_boxed (value, g_ptr_array_free (a_ifaces, FALSE));
}

