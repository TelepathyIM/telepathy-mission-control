/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
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

/**
 * SECTION:mcd-manager
 * @title: McdManager
 * @short_description: Manager class representing Telepathy connection manager
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-manager.h
 * 
 * FIXME
 */

#include "config.h"
#include "mcd-manager.h"
#include "mcd-manager-priv.h"
#include "mcd-misc.h"
#include "mcd-slacker.h"

#include <stdio.h>
#include <string.h>

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-connection.h"

#define MANAGER_SUFFIX ".manager"

#define MCD_MANAGER_PRIV(manager) (MCD_MANAGER (manager)->priv)

G_DEFINE_TYPE (McdManager, mcd_manager, MCD_TYPE_OPERATION);

struct _McdManagerPrivate
{
    gchar *name;
    TpDBusDaemon *dbus_daemon;
    TpSimpleClientFactory *client_factory;
    McdDispatcher *dispatcher;

    TpConnectionManager *tp_conn_mgr;

    McdSlacker *slacker;

    guint is_disposed : 1;
    guint ready : 1;
};

enum
{
    PROP_0,
    PROP_NAME,
    PROP_DISPATCHER,
    PROP_CLIENT_FACTORY
};

static GQuark readiness_quark = 0;

static void
on_manager_ready (GObject *source_object,
                  GAsyncResult *result, gpointer user_data)
{
    TpConnectionManager *tp_conn_mgr = TP_CONNECTION_MANAGER (source_object);
    McdManager *manager = MCD_MANAGER (user_data);
    McdManagerPrivate *priv;
    GError *error = NULL;

    tp_proxy_prepare_finish (tp_conn_mgr, result, &error);

    priv = manager->priv;
    DEBUG ("manager %s is ready", priv->name);
    priv->ready = TRUE;
    _mcd_object_ready (manager, readiness_quark, error);
    g_clear_error (&error);
}

static void
_mcd_manager_finalize (GObject * object)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (object);

    g_free (priv->name);

    G_OBJECT_CLASS (mcd_manager_parent_class)->finalize (object);
}

static void
_mcd_manager_dispose (GObject * object)
{
    McdManagerPrivate *priv;

    priv = MCD_MANAGER_PRIV (object);

    if (priv->is_disposed)
    {
	return;
    }

    priv->is_disposed = TRUE;

    tp_clear_object (&priv->dispatcher);
    tp_clear_object (&priv->tp_conn_mgr);
    tp_clear_object (&priv->client_factory);
    tp_clear_object (&priv->dbus_daemon);
    tp_clear_object (&priv->slacker);

    G_OBJECT_CLASS (mcd_manager_parent_class)->dispose (object);
}

static void
_mcd_manager_connect (McdMission * mission)
{
    MCD_MISSION_CLASS (mcd_manager_parent_class)->connect (mission);
}

static void
_mcd_manager_disconnect (McdMission * mission)
{
    GList *connections;

    DEBUG ("%p", mission);
    MCD_MISSION_CLASS (mcd_manager_parent_class)->disconnect (mission);

    /* We now call mcd_mission_abort() on all child connections; but since this
     * could modify the list of the children, we cannot just use
     * mcd_operation_foreach(). Instead, make a copy of the list and work on
     * that. */
    DEBUG("manager tree before abort:");
    mcd_debug_print_tree(mission);
    connections = g_list_copy ((GList *)mcd_operation_get_missions
			       (MCD_OPERATION (mission)));
    g_list_foreach (connections, (GFunc) mcd_mission_abort, NULL);
    g_list_free (connections);
    DEBUG("manager tree after abort:");
    mcd_debug_print_tree(mission);
}

static gboolean
mcd_manager_setup (McdManager *manager)
{
    McdManagerPrivate *priv = manager->priv;
    GError *error = NULL;

    priv->slacker = mcd_slacker_new ();

    priv->tp_conn_mgr =
        tp_connection_manager_new (priv->dbus_daemon, priv->name,
                                   NULL, &error);
    if (error)
    {
        g_warning ("%s, cannot create manager %s: %s", G_STRFUNC,
                   priv->name, error->message);
        goto error;
    }

    tp_proxy_prepare_async (priv->tp_conn_mgr, NULL, on_manager_ready, manager);

    DEBUG ("Manager %s created", priv->name);
    return TRUE;

error:
    tp_clear_object (&priv->tp_conn_mgr);
    g_clear_error (&error);

    return FALSE;
}

static GObject *
_mcd_manager_constructor (GType type, guint n_params,
			  GObjectConstructParam *params)
{
    GObjectClass *object_class = (GObjectClass *)mcd_manager_parent_class;
    McdManager *manager;

    manager =  MCD_MANAGER (object_class->constructor (type, n_params, params));

    g_return_val_if_fail (manager != NULL, NULL);

    if (!mcd_manager_setup (manager))
    {
	g_object_unref (manager);
	return NULL;
    }

    return (GObject *) manager;
}

static void
_mcd_manager_set_property (GObject * obj, guint prop_id,
			   const GValue * val, GParamSpec * pspec)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (obj);
    McdDispatcher *dispatcher;

    switch (prop_id)
    {
    case PROP_NAME:
	g_assert (priv->name == NULL);
	priv->name = g_value_dup_string (val);
	break;
    case PROP_DISPATCHER:
	dispatcher = g_value_get_object (val);
	if (dispatcher)
	{
	    g_return_if_fail (MCD_IS_DISPATCHER (dispatcher));
	    g_object_ref (dispatcher);
	}
	tp_clear_object (&priv->dispatcher);
	priv->dispatcher = dispatcher;
	break;

    case PROP_CLIENT_FACTORY:
        g_assert (priv->client_factory == NULL); /* construct-only */
        priv->client_factory = g_value_dup_object (val);
        priv->dbus_daemon = g_object_ref (
            tp_simple_client_factory_get_dbus_daemon (priv->client_factory));
        break;

    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_manager_get_property (GObject * obj, guint prop_id,
			   GValue * val, GParamSpec * pspec)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (obj);

    switch (prop_id)
    {
    case PROP_DISPATCHER:
	g_value_set_object (val, priv->dispatcher);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
mcd_manager_class_init (McdManagerClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    McdMissionClass *mission_class = MCD_MISSION_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (McdManagerPrivate));

    object_class->constructor = _mcd_manager_constructor;
    object_class->finalize = _mcd_manager_finalize;
    object_class->dispose = _mcd_manager_dispose;
    object_class->set_property = _mcd_manager_set_property;
    object_class->get_property = _mcd_manager_get_property;

    mission_class->connect = _mcd_manager_connect;
    mission_class->disconnect = _mcd_manager_disconnect;

    /* Properties */
    g_object_class_install_property
        (object_class, PROP_NAME,
         g_param_spec_string ("name", "Name", "Name",
                              NULL,
                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property
        (object_class, PROP_DISPATCHER,
         g_param_spec_object ("dispatcher",
                              "Dispatcher",
                              "Dispatcher",
                              MCD_TYPE_DISPATCHER,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (object_class, PROP_CLIENT_FACTORY,
        g_param_spec_object ("client-factory", "Client factory",
            "Client factory", TP_TYPE_SIMPLE_CLIENT_FACTORY,
            G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

    readiness_quark = g_quark_from_static_string ("mcd_manager_got_info");
}

static void
mcd_manager_init (McdManager *manager)
{
    McdManagerPrivate *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, MCD_TYPE_MANAGER,
					McdManagerPrivate);
    manager->priv = priv;
}

/* Public methods */

McdManager *
mcd_manager_new (const gchar *unique_name,
		 McdDispatcher *dispatcher,
                 TpSimpleClientFactory *client_factory)
{
    McdManager *obj;
    obj = MCD_MANAGER (g_object_new (MCD_TYPE_MANAGER,
				     "name", unique_name,
				     "dispatcher", dispatcher,
                                     "client-factory", client_factory,
                                     NULL));
    return obj;
}

/**
 * mcd_manager_get_unique_name:
 * @manager: the #McdManager.
 *
 * Gets the unique name of the @manager.
 *
 * Returns: a const string with the unique name.
 */
const gchar *
mcd_manager_get_name (McdManager *manager)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);
    return priv->name;
}

TpProtocol *
_mcd_manager_dup_protocol (McdManager *manager,
                           const gchar *protocol)
{
    TpProtocol *p;
    g_return_val_if_fail (MCD_IS_MANAGER (manager), NULL);
    g_return_val_if_fail (protocol != NULL, NULL);

    p = tp_connection_manager_get_protocol_object (manager->priv->tp_conn_mgr,
                                                   protocol);

    if (p == NULL)
        return NULL;
    else
        return g_object_ref (p);
}

const TpConnectionManagerParam *
mcd_manager_get_protocol_param (McdManager *manager, const gchar *protocol,
                                const gchar *param)
{
    McdManagerPrivate *priv;
    TpProtocol *cm_protocol;

    g_return_val_if_fail (MCD_IS_MANAGER (manager), NULL);
    g_return_val_if_fail (protocol != NULL, NULL);
    g_return_val_if_fail (param != NULL, NULL);

    priv = manager->priv;

    cm_protocol = tp_connection_manager_get_protocol_object (priv->tp_conn_mgr,
                                                             protocol);

    if (cm_protocol == NULL)
        return NULL;

    return tp_protocol_get_param (cm_protocol, param);
}

McdConnection *
mcd_manager_create_connection (McdManager *manager, McdAccount *account)
{
    McdConnection *connection;

    g_return_val_if_fail (MCD_IS_MANAGER (manager), NULL);
    g_return_val_if_fail (manager->priv->tp_conn_mgr != NULL, NULL);

    connection = g_object_new (MCD_TYPE_CONNECTION,
                               "client-factory", manager->priv->client_factory,
                               "tp-manager", manager->priv->tp_conn_mgr,
                               "dispatcher", manager->priv->dispatcher,
                               "account", account,
                               "slacker", manager->priv->slacker,
                               NULL);

    mcd_operation_take_mission (MCD_OPERATION (manager),
				MCD_MISSION (connection));
    DEBUG ("Created a connection %p for account: %s",
           connection, mcd_account_get_unique_name (account));

    return connection;
}

/**
 * mcd_manager_get_tp_proxy:
 * @manager: the #McdManager.
 *
 * Returns: the #TpConnectionManager proxy, or %NULL.
 */
TpConnectionManager *
mcd_manager_get_tp_proxy (McdManager *manager)
{
    g_return_val_if_fail (MCD_IS_MANAGER (manager), NULL);
    return manager->priv->tp_conn_mgr;
}

/**
 * mcd_manager_call_when_ready:
 * @manager: the #McdManager.
 * @callbacks: the #McdManagerReadyCb to invoke.
 * @user_data: user data to be passed to the callback.
 *
 * Invoke @callback when @manager is ready, i.e. when its introspection has
 * completed and all the manager protocols and parameter descriptions are
 * available.
 */
void
mcd_manager_call_when_ready (McdManager *manager, McdManagerReadyCb callback,
                             gpointer user_data)
{
    g_return_if_fail (MCD_IS_MANAGER (manager));
    g_return_if_fail (callback != NULL);

    if (manager->priv->ready)
        callback (manager, NULL, user_data);
    else
        _mcd_object_call_when_ready (manager, readiness_quark,
                                     (McdReadyCb)callback, user_data);
}

