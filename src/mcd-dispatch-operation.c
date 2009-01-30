/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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

#include "config.h"
#include "mcd-dispatch-operation.h"

#include <stdio.h>
#include <string.h>

#include <dbus/dbus.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>

#include "mcd-dbusprop.h"
#include "_gen/interfaces.h"
#include "_gen/gtypes.h"

#define MCD_CLIENT_BASE_NAME "org.freedesktop.Telepathy.Client."
#define MCD_CLIENT_BASE_NAME_LEN (sizeof (MCD_CLIENT_BASE_NAME) - 1)

#define MCD_DISPATCH_OPERATION_PRIV(operation) (MCD_DISPATCH_OPERATION (operation)->priv)

static void
dispatch_operation_iface_init (McSvcChannelDispatchOperationClass *iface,
                               gpointer iface_data);
static void properties_iface_init (TpSvcDBusPropertiesClass *iface,
                                   gpointer iface_data);

static const McdDBusProp dispatch_operation_properties[];

static const McdInterfaceData dispatch_operation_interfaces[] = {
    MCD_IMPLEMENT_IFACE (mc_svc_channel_dispatch_operation_get_type,
                         dispatch_operation,
                         MC_IFACE_CHANNEL_DISPATCH_OPERATION),
    { G_TYPE_INVALID, }
};

G_DEFINE_TYPE_WITH_CODE (McdDispatchOperation, mcd_dispatch_operation,
                         G_TYPE_OBJECT,
    MCD_DBUS_INIT_INTERFACES (dispatch_operation_interfaces);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES, properties_iface_init);
    )

struct _McdDispatchOperationPrivate
{
    gchar *unique_name;
    gchar *object_path;
    GHashTable *properties;

    /* Results */
    guint finished : 1;
    guint claimed : 1;
    gchar *handler;

    /* DBUS connection */
    TpDBusDaemon *dbus_daemon;

    McdConnection *connection;

    GList *channels;
};

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
    PROP_CHANNELS,
};

static void
get_connection (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (self);
    const gchar *object_path;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    g_value_init (value, DBUS_TYPE_G_OBJECT_PATH);
    if (priv->connection &&
        (object_path = mcd_connection_get_object_path (priv->connection)))
        g_value_set_boxed (value, object_path);
    else
        g_value_set_static_boxed (value, "/");
}

static void
get_account (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (self);
    McdAccount *account;
    const gchar *object_path;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    g_value_init (value, DBUS_TYPE_G_OBJECT_PATH);
    if (priv->connection &&
        (account = mcd_connection_get_account (priv->connection)) &&
        (object_path = mcd_account_get_object_path (account)))
        g_value_set_boxed (value, object_path);
    else
        g_value_set_static_boxed (value, "/");
}

static void
get_channels (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (self);
    GPtrArray *channel_array;
    GList *list;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);

    channel_array = g_ptr_array_sized_new (g_list_length (priv->channels));
    for (list = priv->channels; list != NULL; list = list->next)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);
        GHashTable *properties;
        GValue channel_val = { 0, };

        properties = _mcd_channel_get_immutable_properties (channel);

        g_value_init (&channel_val, TP_STRUCT_TYPE_CHANNEL_DETAILS);
        g_value_take_boxed (&channel_val,
            dbus_g_type_specialized_construct (TP_STRUCT_TYPE_CHANNEL_DETAILS));
        dbus_g_type_struct_set (&channel_val,
                                0, mcd_channel_get_object_path (channel),
                                1, properties,
                                G_MAXUINT);

        g_ptr_array_add (channel_array, g_value_get_boxed (&channel_val));
    }

    g_value_init (value, TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST);
    g_value_take_boxed (value, channel_array);
}

static void
get_possible_handlers (TpSvcDBusProperties *self, const gchar *name,
                       GValue *value)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (self);

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    g_value_init (value, G_TYPE_STRV);
    g_warning ("%s not implemented", G_STRFUNC);
    g_value_set_static_boxed (value, NULL);
}


static const McdDBusProp dispatch_operation_properties[] = {
    { "Interfaces", NULL, mcd_dbus_get_interfaces },
    { "Connection", NULL, get_connection },
    { "Account", NULL, get_account },
    { "Channels", NULL, get_channels },
    { "PossibleHandlers", NULL, get_possible_handlers },
    { 0 },
};

static void
properties_iface_init (TpSvcDBusPropertiesClass *iface, gpointer iface_data)
{
#define IMPLEMENT(x) tp_svc_dbus_properties_implement_##x (\
    iface, dbusprop_##x)
    IMPLEMENT(set);
    IMPLEMENT(get);
    IMPLEMENT(get_all);
#undef IMPLEMENT
}

static void
mcd_dispatch_operation_finish (McdDispatchOperation *operation)
{
    McdDispatchOperationPrivate *priv = operation->priv;

    priv->finished = TRUE;
    mc_svc_channel_dispatch_operation_emit_finished (operation);
}

static void
dispatch_operation_handle_with (McSvcChannelDispatchOperation *self,
                                const gchar *handler_path,
                                DBusGMethodInvocation *context)
{
    GError *error = NULL;

    mcd_dispatch_operation_handle_with (MCD_DISPATCH_OPERATION (self),
                                        handler_path, &error);
    if (error)
    {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
    else
        mc_svc_channel_dispatch_operation_return_from_handle_with (context);
}

static void
dispatch_operation_claim (McSvcChannelDispatchOperation *self,
                          DBusGMethodInvocation *context)
{
    McdDispatchOperationPrivate *priv;

    priv = MCD_DISPATCH_OPERATION_PRIV (self);
    if (priv->finished)
    {
        GError *error = g_error_new (TP_ERRORS, TP_ERROR_NOT_YOURS,
                                     "CDO already finished");
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    priv->claimed = TRUE;
    mc_svc_channel_dispatch_operation_return_from_claim (context);

    mcd_dispatch_operation_finish (MCD_DISPATCH_OPERATION (self));
}

static void
dispatch_operation_iface_init (McSvcChannelDispatchOperationClass *iface,
                               gpointer iface_data)
{
#define IMPLEMENT(x) mc_svc_channel_dispatch_operation_implement_##x (\
    iface, dispatch_operation_##x)
    IMPLEMENT(handle_with);
    IMPLEMENT(claim);
#undef IMPLEMENT
}

static void
create_object_path (McdDispatchOperationPrivate *priv)
{
    static guint cpt = 0;
    priv->object_path =
        g_strdup_printf (MC_DISPATCH_OPERATION_DBUS_OBJECT_BASE "do%u",
                         cpt++);
    priv->unique_name = priv->object_path +
        (sizeof (MC_DISPATCH_OPERATION_DBUS_OBJECT_BASE) - 1);
}

static GObject *
mcd_dispatch_operation_constructor (GType type, guint n_params,
                                    GObjectConstructParam *params)
{
    GObjectClass *object_class =
        (GObjectClass *)mcd_dispatch_operation_parent_class;
    GObject *object;
    McdDispatchOperation *operation;
    McdDispatchOperationPrivate *priv;
    DBusGConnection *dbus_connection;

    object = object_class->constructor (type, n_params, params);
    operation = MCD_DISPATCH_OPERATION (object);

    g_return_val_if_fail (operation != NULL, NULL);
    priv = operation->priv;

    if (!priv->dbus_daemon) goto error;

    dbus_connection = TP_PROXY (priv->dbus_daemon)->dbus_connection;
    create_object_path (priv);

    if (G_LIKELY (dbus_connection))
        dbus_g_connection_register_g_object (dbus_connection,
                                             priv->object_path, object);

    return object;
error:
    g_object_unref (object);
    return NULL;
}

static void
mcd_dispatch_operation_set_property (GObject *obj, guint prop_id,
                                     const GValue *val, GParamSpec *pspec)
{
    McdDispatchOperation *operation = MCD_DISPATCH_OPERATION (obj);
    McdDispatchOperationPrivate *priv = operation->priv;
    GList *list;

    switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
        if (priv->dbus_daemon)
            g_object_unref (priv->dbus_daemon);
        priv->dbus_daemon = TP_DBUS_DAEMON (g_value_dup_object (val));
        break;
    case PROP_CHANNELS:
        g_assert (priv->channels == NULL);
        priv->channels = g_value_get_pointer (val);
        if (G_LIKELY (priv->channels))
        {
            /* get the connection from the first channel */
            McdChannel *channel = MCD_CHANNEL (priv->channels->data);
            priv->connection = (McdConnection *)
                mcd_mission_get_parent (MCD_MISSION (channel));
            if (G_LIKELY (priv->connection))
                g_object_ref (priv->connection);

            /* reference the channels */
            for (list = priv->channels; list != NULL; list = list->next)
                g_object_ref (list->data);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
        break;
    }
}

static void
mcd_dispatch_operation_get_property (GObject *obj, guint prop_id,
                                     GValue *val, GParamSpec *pspec)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (obj);

    switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
        g_value_set_object (val, priv->dbus_daemon);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
        break;
    }
}

static void
mcd_dispatch_operation_finalize (GObject *object)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (object);

    if (priv->properties)
        g_hash_table_unref (priv->properties);

    g_free (priv->handler);
    g_free (priv->object_path);

    G_OBJECT_CLASS (mcd_dispatch_operation_parent_class)->finalize (object);
}

static void
mcd_dispatch_operation_dispose (GObject *object)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (object);
    GList *list;

    if (priv->channels)
    {
        for (list = priv->channels; list != NULL; list = list->next)
            g_object_unref (list->data);
        g_list_free (priv->channels);
        priv->channels = NULL;
    }

    if (priv->connection)
    {
        g_object_unref (priv->connection);
        priv->connection = NULL;
    }

    if (priv->dbus_daemon)
    {
        g_object_unref (priv->dbus_daemon);
        priv->dbus_daemon = NULL;
    }
    G_OBJECT_CLASS (mcd_dispatch_operation_parent_class)->dispose (object);
}

static void
mcd_dispatch_operation_class_init (McdDispatchOperationClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class,
                              sizeof (McdDispatchOperationPrivate));

    object_class->constructor = mcd_dispatch_operation_constructor;
    object_class->dispose = mcd_dispatch_operation_dispose;
    object_class->finalize = mcd_dispatch_operation_finalize;
    object_class->set_property = mcd_dispatch_operation_set_property;
    object_class->get_property = mcd_dispatch_operation_get_property;

    g_object_class_install_property (object_class, PROP_DBUS_DAEMON,
        g_param_spec_object ("dbus-daemon", "DBus daemon", "DBus daemon",
							  TP_TYPE_DBUS_DAEMON,
							  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (object_class, PROP_CHANNELS,
        g_param_spec_pointer ("channels", "channels", "channels",
                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
}

static void
mcd_dispatch_operation_init (McdDispatchOperation *operation)
{
    McdDispatchOperationPrivate *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE ((operation),
                                        MCD_TYPE_DISPATCH_OPERATION,
                                        McdDispatchOperationPrivate);
    operation->priv = priv;

    /* initializes the interfaces */
    mcd_dbus_init_interfaces_instances (operation);
}

/*
 * _mcd_dispatch_operation_new:
 * @dbus_daemon: a #TpDBusDaemon.
 * @channels: a #GList of #McdChannel elements to dispatch.
 *
 * Creates a #McdDispatchOperation. The #GList @channels will be no longer
 * valid after this function has been called.
 */
McdDispatchOperation *
_mcd_dispatch_operation_new (TpDBusDaemon *dbus_daemon,
                             GList *channels)
{
    gpointer *obj;
    obj = g_object_new (MCD_TYPE_DISPATCH_OPERATION,
                        "dbus-daemon", dbus_daemon,
                        "channels", channels,
                        NULL);
    return MCD_DISPATCH_OPERATION (obj);
}

/**
 * mcd_dispatch_operation_get_path:
 * @operation: the #McdDispatchOperation.
 *
 * Returns: the D-Bus object path of @operation.
 */
const gchar *
mcd_dispatch_operation_get_path (McdDispatchOperation *operation)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (operation), NULL);
    return operation->priv->object_path;
}

/**
 * mcd_dispatch_operation_get_properties:
 * @operation: the #McdDispatchOperation.
 *
 * Gets the immutable properties of @operation.
 *
 * Returns: a #GHashTable with the operation properties. The reference count is
 * not incremented.
 */
GHashTable *
mcd_dispatch_operation_get_properties (McdDispatchOperation *operation)
{
    McdDispatchOperationPrivate *priv;

    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (operation), NULL);
    priv = operation->priv;
    if (!priv->properties)
    {
        const McdDBusProp *property;

        priv->properties =
            g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify)tp_g_value_slice_free);

        for (property = dispatch_operation_properties;
             property->name != NULL;
             property++)
        {
            GValue *value;
            gchar *name;

            if (!property->getprop) continue;

            value = g_slice_new0 (GValue);
            property->getprop ((TpSvcDBusProperties *)operation,
                               property->name, value);
            name = g_strconcat (MC_IFACE_CHANNEL_DISPATCH_OPERATION, ".",
                                property->name, NULL);
            g_hash_table_insert (priv->properties, name, value);
        }
    }
    return priv->properties;
}

/**
 * mcd_dispatch_operation_is_claimed:
 * @operation: the #McdDispatchOperation.
 *
 * Returns: %TRUE if the operation was claimed, %FALSE otherwise.
 */
gboolean
mcd_dispatch_operation_is_claimed (McdDispatchOperation *operation)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (operation), FALSE);
    return operation->priv->claimed;
}

/**
 * mcd_dispatch_operation_get_handler:
 * @operation: the #McdDispatchOperation.
 *
 * Returns: the well-known name of the choosen channel handler.
 */
const gchar *
mcd_dispatch_operation_get_handler (McdDispatchOperation *operation)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (operation), NULL);
    return operation->priv->handler;
}

void
mcd_dispatch_operation_handle_with (McdDispatchOperation *operation,
                                    const gchar *handler_path,
                                    GError **error)
{
    McdDispatchOperationPrivate *priv;

    g_return_if_fail (MCD_IS_DISPATCH_OPERATION (operation));
    priv = operation->priv;

    if (priv->finished)
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_YOURS,
                     "CDO already finished");
        return;
    }

    if (handler_path)
    {
        if (strncmp (handler_path, MCD_CLIENT_BASE_NAME,
                     MCD_CLIENT_BASE_NAME_LEN) != 0)
        {
            g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                         "Invalid handler name");
            return;
        }
        priv->handler = g_strdup (handler_path + MCD_CLIENT_BASE_NAME_LEN);
    }

    mcd_dispatch_operation_finish (operation);
}

