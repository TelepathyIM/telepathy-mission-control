/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mc-dispatch-operation.c - Telepathy Account Manager D-Bus interface
 * (client side)
 *
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

#define MC_INTERNAL

#include <string.h>
#include "mc-dispatch-operation.h"
#include "mc-account-manager.h"
#include "mc-gtypes.h"
#include "mc-interfaces.h"
#include "dbus-api.h"
#include "mc-signals-marshal.h"

#include <telepathy-glib/proxy-subclass.h>

#include "_gen/cli-dispatch-operation-body.h"
#include "_gen/signals-marshal.h"

/**
 * SECTION:mc-dispatch-operation
 * @title: McDispatchOperation
 * @short_description: proxy object for the Telepathy ChannelDispatchOperation
 * D-Bus API
 *
 * This module provides a client-side proxy object for the Telepathy
 * ChannelDispatchOperation D-Bus API.
 */

/**
 * McDispatchOperationClass:
 *
 * The class of a #McDispatchOperation.
 */
struct _McDispatchOperationClass {
    TpProxyClass parent_class;
    /*<private>*/
    gpointer priv;
};

typedef struct _McDispatchOperationProps {
    gchar *connection;
    gchar *account;
    gchar **possible_handlers;
    GList *channels;
} McDispatchOperationProps;

struct _McDispatchOperationPrivate {
    McDispatchOperationProps *props;
};

/**
 * McDispatchOperation:
 *
 * A proxy object for the Telepathy DispatchOperation D-Bus API. This is a
 * subclass of #TpProxy.
 */
struct _McDispatchOperation {
    TpProxy parent;
    /*<private>*/
    McDispatchOperationPrivate *priv;
};

G_DEFINE_TYPE (McDispatchOperation, mc_dispatch_operation, TP_TYPE_PROXY);

enum
{
    PROP_0,
    PROP_PROPERTIES,
};

/* if this turns out to work well, we can move this definition to a common
 * header and use it in every module */
typedef struct {
    gchar *name;
    gchar *dbus_signature;
    void (*update_property) (const gchar *name, const GValue *value,
                             gpointer props_struct);
} McProperty;

#define MAX_PROPERTY_NAME_LEN  64
#define MC_QUALIFIED_PROPERTY_NAME_LEN \
    (sizeof(MC_IFACE_CHANNEL_DISPATCH_OPERATION) + MAX_PROPERTY_NAME_LEN)

static void
mc_dispatch_operation_init (McDispatchOperation *operation)
{
    operation->priv =
        G_TYPE_INSTANCE_GET_PRIVATE(operation, MC_TYPE_DISPATCH_OPERATION,
                                    McDispatchOperationPrivate);
}

static void
mc_channel_details_free (McChannelDetails *details)
{
    g_free (details->object_path);
    g_hash_table_destroy (details->properties);
    g_slice_free (McChannelDetails, details);
}

static inline void
operation_props_free (McDispatchOperationProps *props)
{
    g_strfreev (props->possible_handlers);
    g_free (props->connection);
    g_free (props->account);
    g_list_foreach (props->channels, (GFunc)mc_channel_details_free, NULL);
    g_list_free (props->channels);
    g_slice_free (McDispatchOperationProps, props);
}

static inline GList *
create_channels_prop (const GValue *value)
{
    GList *list = NULL;
    GPtrArray *channels;
    gint i;

    channels = g_value_get_boxed (value);
    for (i = channels->len - 1; i >= 0; i--)
    {
        McChannelDetails *details;
        GValueArray *va;

        details = g_slice_new (McChannelDetails);
        va = g_ptr_array_index (channels, i);
        details->object_path = g_value_dup_boxed (va->values);
        details->properties = g_value_dup_boxed (va->values + 1);
        list = g_list_prepend (list, details);
    }
    return list;
}

static void
set_connection (const gchar *name, const GValue *value, gpointer props_struct)
{
    McDispatchOperationProps *props = props_struct;
    g_free (props->connection);
    props->connection = g_value_dup_boxed (value);
}

static void
set_account (const gchar *name, const GValue *value, gpointer props_struct)
{
    McDispatchOperationProps *props = props_struct;
    g_free (props->account);
    props->account = g_value_dup_boxed (value);
}

static void
set_channels (const gchar *name, const GValue *value, gpointer props_struct)
{
    McDispatchOperationProps *props = props_struct;
    g_list_foreach (props->channels, (GFunc)mc_channel_details_free, NULL);
    g_list_free (props->channels);
    props->channels = create_channels_prop (value);
}

static void
set_possible_handlers (const gchar *name, const GValue *value,
                       gpointer props_struct)
{
    McDispatchOperationProps *props = props_struct;
    g_strfreev (props->possible_handlers);
    props->possible_handlers = g_value_dup_boxed (value);
}

static const McProperty dispatch_operation_props[] =
{
    /* Make sure that the property names are not too long: adjust the
     * MAX_PROPERTY_NAME_LEN constant accordingly */
    { "Connection", "o", set_connection },
    { "Account", "o", set_account },
    { "Channels", "a(oa{sv})", set_channels },
    { "PossibleHandlers", "as", set_possible_handlers },
    { NULL, NULL, NULL }
};

static GType
gtype_from_dbus_signature (const gchar *signature)
{
    /* dbus-glib's functions that create the GTypes are implemented using a
     * lookup table, so that if the sub-component types are the same, the same
     * GType is returned.
     * So here it should be safe to use any of the functions that return the
     * desired type */
    if (strcmp (signature, "o") == 0)
        return DBUS_TYPE_G_OBJECT_PATH;
    if (strcmp (signature, "a(oa{sv})") == 0)
        return MC_ARRAY_TYPE_CHANNEL_DETAILS_LIST;
    if (strcmp (signature, "as") == 0)
        return G_TYPE_STRV;
    g_warning ("%s: Type %s not mapped", G_STRFUNC, signature);
    return G_TYPE_INVALID;
}

static void
create_operation_props (McDispatchOperation *operation, GHashTable *properties)
{
    McDispatchOperationProps *props;
    const McProperty *prop;
    gchar qualified_name[MC_QUALIFIED_PROPERTY_NAME_LEN], *name_ptr;

    props = g_slice_new0 (McDispatchOperationProps);
    operation->priv->props = props;

    strcpy (qualified_name, MC_IFACE_CHANNEL_DISPATCH_OPERATION);
    name_ptr =
        qualified_name + (sizeof (MC_IFACE_CHANNEL_DISPATCH_OPERATION) - 1);
    *name_ptr = '.';
    name_ptr++;

    for (prop = dispatch_operation_props; prop->name != NULL; prop++)
    {
        GValue *value;
        GType type;

        g_return_if_fail (strlen (prop->name) < MAX_PROPERTY_NAME_LEN);
        strcpy (name_ptr, prop->name);

        value = g_hash_table_lookup (properties, qualified_name);
        if (!value) continue;

        type = gtype_from_dbus_signature (prop->dbus_signature);
        if (G_LIKELY (G_VALUE_HOLDS (value, type)))
        {
            prop->update_property (prop->name, value, props);
        }
        else
        {
            g_warning ("%s: %s is a %s, expecting %s",
                       G_STRFUNC, prop->name,
                       G_VALUE_TYPE_NAME (value), g_type_name (type));
        }
    }
}

static void
mc_dispatch_operation_set_property (GObject *obj, guint prop_id,
                                    const GValue *val, GParamSpec *pspec)
{
    McDispatchOperation *operation = MC_DISPATCH_OPERATION (obj);

    switch (prop_id)
    {
    case PROP_PROPERTIES:
        create_operation_props (operation, g_value_get_pointer (val));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    McDispatchOperation *operation = MC_DISPATCH_OPERATION (object);

    if (operation->priv->props)
        operation_props_free (operation->priv->props);

    G_OBJECT_CLASS (mc_dispatch_operation_parent_class)->finalize (object);
}

static void
mc_dispatch_operation_class_init (McDispatchOperationClass *klass)
{
    GType type = MC_TYPE_DISPATCH_OPERATION;
    GObjectClass *object_class = (GObjectClass *)klass;
    TpProxyClass *proxy_class = (TpProxyClass *) klass;

    g_type_class_add_private (object_class,
                              sizeof (McDispatchOperationPrivate));

    object_class->set_property = mc_dispatch_operation_set_property;
    object_class->finalize = finalize;

    /* the API is stateless, so we can keep the same proxy across restarts */
    proxy_class->must_have_unique_name = FALSE;

    _mc_ext_register_dbus_glib_marshallers ();

    proxy_class->interface = MC_IFACE_QUARK_CHANNEL_DISPATCH_OPERATION;
    tp_proxy_init_known_interfaces ();
    tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_dispatch_operation_add_signals);

    tp_proxy_subclass_add_error_mapping (type, TP_ERROR_PREFIX, TP_ERRORS,
                                         TP_TYPE_ERROR);

    g_object_class_install_property (object_class, PROP_PROPERTIES,
        g_param_spec_pointer ("properties", "properties", "properties",
                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
}

/**
 * mc_dispatch_operation_new_ready:
 * @dbus: a D-Bus daemon; may not be %NULL
 * @channels: a #GPtrArray of #GValueArray, each containing the object path
 *  of a channel and a #GHashTable of its immutable properties
 * @object_path: the D-Bus object path of the ChannelDispatchOperation.
 * @properties: a #GHashTable of properties.
 *
 * Creates a proxy for the DBus DispatchOperation Telepathy object and sets
 * its properties, so that D-Bus introspection won't be needed.
 *
 * Returns: a new #McDispatchOperation object.
 */
McDispatchOperation *
mc_dispatch_operation_new_ready (TpDBusDaemon *dbus, const GPtrArray *channels,
                                 const gchar *object_path,
                                 GHashTable *properties)
{
    McDispatchOperation *op;
    /* The implementation of McDispatchOperation assumes the previous
     * AddDispatchOperation API, where Channels was an immutable property of
     * the CDO. For the moment we work around that in this function rather
     * than redesigning the object. */
    GHashTable *properties_and_channels = g_hash_table_new (g_str_hash,
                                                            g_str_equal);
    GValue cv = { 0 };

    g_value_init (&cv, MC_ARRAY_TYPE_CHANNEL_DETAILS_LIST);
    g_value_set_boxed (&cv, channels);

    /* borrow all keys and values from @properties, then insert @cv too. This
     * is safe to do without copying, because the GObject properties mechanism
     * will copy the hash table anyway (and in fact so does this object) */
    tp_g_hash_table_update (properties_and_channels, properties, NULL, NULL);
    g_hash_table_insert (properties_and_channels,
                         MC_IFACE_CHANNEL_DISPATCH_OPERATION ".Channels",
                         &cv);

    op = g_object_new (MC_TYPE_DISPATCH_OPERATION,
                       "dbus-daemon", dbus,
                       "bus-name", MC_CHANNEL_DISPATCHER_DBUS_SERVICE,
                       "object-path", object_path,
                       "properties", properties_and_channels,
                       NULL);
    g_hash_table_destroy (properties_and_channels);

    return op;
}

/**
 * mc_dispatch_operation_get_connection_path:
 * @operation: the #McDispatchOperation.
 *
 * Returns: the D-Bus object path of the connection.
 */
const gchar *
mc_dispatch_operation_get_connection_path (McDispatchOperation *operation)
{
    g_return_val_if_fail (MC_IS_DISPATCH_OPERATION (operation), NULL);
    if (G_UNLIKELY (!operation->priv->props)) return NULL;
    return operation->priv->props->connection;
}

/**
 * mc_dispatch_operation_get_account_path:
 * @operation: the #McDispatchOperation.
 *
 * Returns: the D-Bus object path of the account.
 */
const gchar *
mc_dispatch_operation_get_account_path (McDispatchOperation *operation)
{
    g_return_val_if_fail (MC_IS_DISPATCH_OPERATION (operation), NULL);
    if (G_UNLIKELY (!operation->priv->props)) return NULL;
    return operation->priv->props->account;
}

/**
 * mc_dispatch_operation_get_possible_handlers:
 * @operation: the #McDispatchOperation.
 *
 * Returns: a non-modifyable array of strings representing the DBus well-known
 * names of the possible channel handlers.
 */
const gchar * const *
mc_dispatch_operation_get_possible_handlers (McDispatchOperation *operation)
{
    g_return_val_if_fail (MC_IS_DISPATCH_OPERATION (operation), NULL);
    if (G_UNLIKELY (!operation->priv->props)) return NULL;
    return (const gchar * const *)operation->priv->props->possible_handlers;
}

/**
 * mc_dispatch_operation_get_channels:
 * @operation: the #McDispatchOperation.
 *
 * Returns: a #GList of #McChannelDetails structures. Do not free.
 */
GList *
mc_dispatch_operation_get_channels (McDispatchOperation *operation)
{
    g_return_val_if_fail (MC_IS_DISPATCH_OPERATION (operation), NULL);
    if (G_UNLIKELY (!operation->priv->props)) return NULL;
    return operation->priv->props->channels;
}

