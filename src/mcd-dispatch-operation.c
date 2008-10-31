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

        g_value_init (&channel_val, MC_STRUCT_TYPE_CHANNEL_DETAILS);
        g_value_take_boxed (&channel_val,
            dbus_g_type_specialized_construct (MC_STRUCT_TYPE_CHANNEL_DETAILS));
        dbus_g_type_struct_set (&channel_val,
                                0, mcd_channel_get_object_path (channel),
                                1, properties,
                                G_MAXUINT);

        g_ptr_array_add (channel_array, g_value_get_boxed (&channel_val));
    }

    g_value_init (value, MC_ARRAY_TYPE_CHANNEL_DETAILS_LIST);
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
dispatch_operation_iface_init (McSvcChannelDispatchOperationClass *iface,
                               gpointer iface_data)
{
#define IMPLEMENT(x) mc_svc_dispatch_operation_implement_##x (\
    iface, dispatch_operation_##x)
#undef IMPLEMENT
}

static void
create_object_path (McdDispatchOperationPrivate *priv)
{
    priv->object_path =
        g_strdup_printf (MC_DISPATCH_OPERATION_DBUS_OBJECT_BASE "do%u",
                         (guint)time (0));
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

    g_free (priv->object_path);

    G_OBJECT_CLASS (mcd_dispatch_operation_parent_class)->finalize (object);
}

static void
mcd_dispatch_operation_dispose (GObject *object)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (object);

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

