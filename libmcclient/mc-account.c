/*
 * mc-account.c - Telepathy Account D-Bus interface (client side)
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
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

#include <stdio.h>
#include <string.h>
#include "mc-account.h"
#include "dbus-api.h"

#include <telepathy-glib/proxy-subclass.h>

#include "_gen/cli-Account-body.h"

/**
 * SECTION:mc-account
 * @title: McAccount
 * @short_description: proxy object for the Telepathy Account D-Bus API
 *
 * This module provides a client-side proxy object for the Telepathy
 * Account D-Bus API.
 *
 * Since: FIXME
 */

/**
 * McAccountClass:
 *
 * The class of a #McAccount.
 *
 * Since: FIXME
 */
struct _McAccountClass {
    TpProxyClass parent_class;
    /*<private>*/
    gpointer priv;
};

struct _McAccountPrivate {
    GData *properties;
    GArray *property_values;
};

typedef struct
{
    GArray *values;
    GHashTable *properties;
} McPropAddData;

/**
 * McAccount:
 *
 * A proxy object for the Telepathy Account D-Bus API. This is a subclass of
 * #TpProxy.
 *
 * Since: FIXME
 */

G_DEFINE_TYPE (McAccount, mc_account, TP_TYPE_PROXY);

enum
{
    PROP_0,
    PROP_OBJECT_PATH,
};

enum
{
    PROPERTIES_READY,
    NUM_SIGNALS
};

static guint signals[NUM_SIGNALS];

static inline gboolean
parse_object_path (McAccount *account)
{
    gchar manager[64], protocol[64], unique_name[256];
    gchar *object_path = account->parent.object_path;
    gint n;

    if (G_UNLIKELY (!object_path)) return FALSE;
    n = sscanf (object_path, MC_ACCOUNT_DBUS_OBJECT_BASE "%[^/]/%[^/]/%s",
		manager, protocol, unique_name);
    if (n != 3) return FALSE;

    account->manager_name = g_strdup (manager);
    account->protocol_name = g_strdup (protocol);
    account->unique_name = object_path +
       	(sizeof (MC_ACCOUNT_DBUS_OBJECT_BASE) - 1);
    return TRUE;
}

static void
mc_account_init (McAccount *account)
{
    McAccountPrivate *priv;

    priv = account->priv =
       	G_TYPE_INSTANCE_GET_PRIVATE(account, MC_TYPE_ACCOUNT,
				    McAccountPrivate);

    g_datalist_init (&priv->properties);
    priv->property_values = g_array_new (FALSE, FALSE, sizeof (GValue));
}

static void
finalize (GObject *object)
{
    McAccount *account = MC_ACCOUNT (object);
    McAccountPrivate *priv = account->priv;

    g_free (account->manager_name);
    g_free (account->protocol_name);

    g_datalist_clear (&priv->properties);
    if (priv->property_values)
	g_array_free (priv->property_values, TRUE);

    G_OBJECT_CLASS (mc_account_parent_class)->finalize (object);
}

static void
mc_account_class_init (McAccountClass *klass)
{
    GType type = MC_TYPE_ACCOUNT;
    GObjectClass *object_class = (GObjectClass *)klass;
    TpProxyClass *proxy_class = (TpProxyClass *)klass;

    g_type_class_add_private (object_class, sizeof (McAccountPrivate));
    object_class->finalize = finalize;

    /* the API is stateless, so we can keep the same proxy across restarts */
    proxy_class->must_have_unique_name = FALSE;

    proxy_class->interface = MC_IFACE_QUARK_ACCOUNT;
    tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_Account_add_signals);

    tp_proxy_subclass_add_error_mapping (type, TP_ERROR_PREFIX, TP_ERRORS,
					 TP_TYPE_ERROR);

    signals[PROPERTIES_READY] =
	g_signal_new ("props-ready",
		      G_TYPE_FROM_CLASS (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL,
		      g_cclosure_marshal_VOID__UINT,
		      G_TYPE_NONE, 1,
		      G_TYPE_UINT);
}

/**
 * mc_account_new:
 * @dbus: a D-Bus daemon; may not be %NULL
 *
 * <!-- -->
 *
 * Returns: a new NMC 4.x proxy
 *
 * Since: FIXME
 */
McAccount *
mc_account_new (TpDBusDaemon *dbus, const gchar *object_path)
{
    McAccount *account;

    account = g_object_new (MC_TYPE_ACCOUNT,
			    "dbus-daemon", dbus,
			    "bus-name", MC_ACCOUNT_MANAGER_DBUS_SERVICE,
			    "object-path", object_path,
			    NULL);
    if (G_LIKELY (account))
	parse_object_path (account);
    return account;
}

static void
add_property (gpointer key, gpointer ht_value, gpointer userdata)
{
    const gchar *name = key;
    GValue *value = ht_value;
    McPropAddData *pad = userdata;
    GValue *val;
    GType type;

    val = g_hash_table_lookup (pad->properties, name);
    if (val)
    {
	g_value_unset (val);
	/* steal the value from the returned hash-table */
	memcpy (val, value, sizeof (GValue));
    }
    else
    {
	g_array_append_vals (pad->values, value, 1);
	val = &g_array_index (pad->values, GValue, pad->values->len - 1);
	g_hash_table_insert (pad->properties, g_strdup (name), val);
    }
    /* clear the returned GValue, so that it will not be freed */
    type = G_VALUE_TYPE (value);
    memset (value, 0, sizeof (GValue));
    g_value_init (value, type);
}

static void
properties_get_all_cb (TpProxy *proxy, GHashTable *props,
		   const GError *error, gpointer user_data,
		   GObject *weak_object)
{
    McAccount *account = MC_ACCOUNT (proxy);
    McAccountPrivate *priv = account->priv;
    GQuark interface = GPOINTER_TO_INT (user_data);
    GHashTable *properties;
    McPropAddData pad;

    if (!error)
    {
	properties = g_datalist_id_get_data (&priv->properties, interface);
	if (!properties)
	{
	    properties = g_hash_table_new_full (g_str_hash, g_str_equal,
						g_free,
					       	(GDestroyNotify)g_value_unset);
	    g_datalist_id_set_data_full (&priv->properties, interface,
					 properties,
					 (GDestroyNotify)g_hash_table_destroy);
	    g_array_set_size (priv->property_values,
			      priv->property_values->len +
			      g_hash_table_size (props));
	}
	pad.values = priv->property_values;
	pad.properties = properties;
	g_hash_table_foreach (props, add_property, &pad);
    }
    else
    {
	g_warning ("%s: getting %s property failed (%s)",
		   G_STRFUNC, g_quark_to_string (interface), error->message);
    }
    g_signal_emit (account, signals[PROPERTIES_READY], interface, interface);
}

static TpProxySignalConnection *
mc_cli_account_connect_to_account_property_changed_iface (gpointer proxy,
    GQuark interface,
    mc_cli_account_signal_callback_account_property_changed callback,
    gpointer user_data,
    GDestroyNotify destroy,
    GObject *weak_object,
    GError **error)
{
  GType expected_types[2] = {
      (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)),
      G_TYPE_INVALID };

  g_return_val_if_fail (TP_IS_PROXY (proxy), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  return tp_proxy_signal_connection_v0_new ((TpProxy *) proxy,
      interface, "AccountPropertyChanged",
      expected_types,
      G_CALLBACK (_mc_cli_account_collect_args_of_account_property_changed),
      _mc_cli_account_invoke_callback_for_account_property_changed,
      G_CALLBACK (callback), user_data, destroy,
      weak_object, error);
}

static void
on_account_property_changed (TpProxy *proxy, GHashTable *props,
			     gpointer user_data, GObject *weak_object)
{
    McAccount *account = MC_ACCOUNT (proxy);
    McAccountPrivate *priv = account->priv;
    GQuark interface = GPOINTER_TO_INT (user_data);
    GHashTable *properties;
    McPropAddData pad;

    properties = g_datalist_id_get_data (&priv->properties, interface);
    if (properties)
    {
	pad.values = priv->property_values;
	pad.properties = properties;
	g_hash_table_foreach (props, add_property, &pad);
    }
}

gboolean
mc_account_watch_interface (McAccount *account, GQuark interface)
{
    McAccountPrivate *priv = account->priv;
    const gchar *iface_name;

    if (g_datalist_id_get_data (&priv->properties, interface))
    {
	/* if we already have the properties, there is nothing left to do but
	 * emit the props-ready signal */
	g_signal_emit (account, signals[PROPERTIES_READY], interface,
		       interface);
	return TRUE;
    }
    iface_name = g_quark_to_string (interface);
    if (!tp_cli_dbus_properties_call_get_all (account, -1,
					      iface_name,
					      properties_get_all_cb,
					      GINT_TO_POINTER (interface),
					      NULL, NULL))
    {
	g_warning ("%s: getting %s interface failed", G_STRFUNC, iface_name);
	return FALSE;
    }
    /* connect to the various "AccountPropertyChanged" signals from the
     * interfaces. Note that the signal might not exist on the interface */
    mc_cli_account_connect_to_account_property_changed_iface (account,
	interface,
	on_account_property_changed, GINT_TO_POINTER (interface),
	NULL, NULL, NULL);
    return TRUE;
}

const GValue *
mc_account_get_property (McAccount *account, GQuark interface,
			 const gchar *name)
{
    McAccountPrivate *priv = account->priv;
    GHashTable *properties;

    properties = g_datalist_id_get_data (&priv->properties, interface);
    return properties ? g_hash_table_lookup (properties, name) : NULL;
}

void
mc_account_get (McAccount *account, GQuark interface,
	       	const gchar *first_prop, ...)
{
    const gchar *name;
    va_list var_args;

    va_start (var_args, first_prop);
    for (name = first_prop; name; name = va_arg (var_args, gchar *))
    {
	const GValue *value;
	GType type;
	gpointer ptr;

	type = va_arg (var_args, GType);
	ptr = va_arg (var_args, gpointer);
	value = mc_account_get_property (account, interface, name);
	if (value)
	{
	    if (type != G_VALUE_TYPE (value))
	    {
		g_warning ("%s: prop %s has type %s, user expected %s",
			   G_STRFUNC, name, G_VALUE_TYPE_NAME (value),
			   g_type_name (type));
		continue;
	    }
		
	    switch (type)
	    {
	    case G_TYPE_BOOLEAN:
		*((gboolean *)ptr) = g_value_get_boolean (value);
		break;
	    case G_TYPE_INT:
		*((gint *)ptr) = g_value_get_int (value);
		break;
	    case G_TYPE_UINT:
		*((guint *)ptr) = g_value_get_uint (value);
		break;
	    case G_TYPE_FLOAT:
		*((gfloat *)ptr) = g_value_get_float (value);
		break;
	    case G_TYPE_DOUBLE:
		*((gdouble *)ptr) = g_value_get_double (value);
		break;
	    case G_TYPE_STRING:
		*((const gchar **)ptr) = g_value_get_string (value);
		break;
	    default:
		g_warning ("%s: unsupported type %s",
			   G_STRFUNC, g_type_name (type));
	    }
	}
    }
    
    va_end (var_args);
}

