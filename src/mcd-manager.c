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

#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <telepathy-glib/telepathy-glib.h>

#include <libmcclient/mc-errors.h>

#include "mcd-connection.h"

#define MANAGER_SUFFIX ".manager"

#define MCD_MANAGER_PRIV(manager) (MCD_MANAGER (manager)->priv)

G_DEFINE_TYPE (McdManager, mcd_manager, MCD_TYPE_OPERATION);

struct _McdManagerPrivate
{
    gchar *name;
    TpDBusDaemon *dbus_daemon;
    McdDispatcher *dispatcher;

    TpConnectionManager *tp_conn_mgr;

    guint is_disposed : 1;
    guint ready : 1;
};

enum
{
    PROP_0,
    PROP_NAME,
    PROP_DISPATCHER,
    PROP_DBUS_DAEMON,
};

static GQuark readiness_quark = 0;

static void
on_manager_ready (TpConnectionManager *tp_conn_mgr, const GError *error,
                  gpointer user_data, GObject *weak_object)
{
    McdManager *manager = MCD_MANAGER (weak_object);
    McdManagerPrivate *priv;

    priv = manager->priv;
    DEBUG ("manager %s is ready", priv->name);
    priv->ready = TRUE;
    _mcd_object_ready (manager, readiness_quark, error);
}

static gint
_find_connection_by_path (gconstpointer data, gconstpointer user_data)
{
    TpConnection *tp_conn;
    McdConnection *connection = MCD_CONNECTION (data);
    const gchar *object_path = (const gchar *)user_data;
    const gchar *conn_object_path = NULL;
    gint ret;

    if (!data) return 1;

    g_object_get (G_OBJECT (connection), "tp-connection",
		  &tp_conn, NULL);
    if (!tp_conn)
	return 1;
    conn_object_path = TP_PROXY (tp_conn)->object_path;
    if (strcmp (conn_object_path, object_path) == 0)
    {
	ret = 0;
    }
    else
    {
	ret = 1;
    }
    
    g_object_unref (G_OBJECT (tp_conn));
    return ret;
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
    tp_clear_object (&priv->dbus_daemon);

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

    priv->tp_conn_mgr =
        tp_connection_manager_new (priv->dbus_daemon, priv->name,
                                   NULL, &error);
    if (error)
    {
        g_warning ("%s, cannot create manager %s: %s", G_STRFUNC,
                   priv->name, error->message);
        goto error;
    }

    tp_connection_manager_call_when_ready (priv->tp_conn_mgr, on_manager_ready,
                                           NULL, NULL, (GObject *)manager);

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
    McdManagerPrivate *priv;

    manager =  MCD_MANAGER (object_class->constructor (type, n_params, params));
    priv = manager->priv;

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
    case PROP_DBUS_DAEMON:
	tp_clear_object (&priv->dbus_daemon);
	priv->dbus_daemon = TP_DBUS_DAEMON (g_value_dup_object (val));
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
    case PROP_DBUS_DAEMON:
	g_value_set_object (val, priv->dbus_daemon);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static McdConnection *
create_connection (McdManager *manager, McdAccount *account)
{
    McdManagerPrivate *priv = manager->priv;

    return g_object_new (MCD_TYPE_CONNECTION,
                         "dbus-daemon", priv->dbus_daemon,
                         "tp-manager", priv->tp_conn_mgr,
                         "dispatcher", priv->dispatcher,
                         "account", account,
                         NULL);
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

    klass->create_connection = create_connection;

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
    g_object_class_install_property
        (object_class, PROP_DBUS_DAEMON,
         g_param_spec_object ("dbus-daemon", "DBus daemon", "DBus daemon",
                              TP_TYPE_DBUS_DAEMON,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

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
		 TpDBusDaemon *dbus_daemon)
{
    McdManager *obj;
    obj = MCD_MANAGER (g_object_new (MCD_TYPE_MANAGER,
				     "name", unique_name,
				     "dispatcher", dispatcher,
				     "dbus-daemon", dbus_daemon, NULL));
    return obj;
}

McdConnection *
mcd_manager_get_connection (McdManager * manager, const gchar *object_path)
{
    const GList *connections;
    const GList *node;

    connections = mcd_operation_get_missions (MCD_OPERATION (manager));
    node = g_list_find_custom ((GList*)connections, object_path,
			       _find_connection_by_path);

    if (node != NULL)
    {
	return MCD_CONNECTION (node->data);
    }

    else
    {
	return NULL;
    }
}

gboolean
mcd_manager_cancel_channel_request (McdManager *manager, guint operation_id,
				    const gchar *requestor_client_id,
				    GError **error)
{
    const GList *connections, *node;

    connections = mcd_operation_get_missions (MCD_OPERATION (manager));
    if (!connections) return FALSE;

    for (node = connections; node; node = node->next)
    {
	if (mcd_connection_cancel_channel_request (MCD_CONNECTION (node->data),
						   operation_id,
						   requestor_client_id,
						   error))
	    return TRUE;
    }
    return FALSE;
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

/**
 * mcd_manager_get_parameters:
 * @manager: the #McdManager.
 * @protocol: the protocol name.
 *
 * Retrieve the array of the parameters supported by the protocol.
 *
 * Returns: a #GArray of #McProtocolParam elements.
 */
const TpConnectionManagerParam *
mcd_manager_get_parameters (McdManager *manager, const gchar *protocol)
{
    McdManagerPrivate *priv;
    const TpConnectionManagerProtocol * const *protocols;
    guint i;

    g_return_val_if_fail (MCD_IS_MANAGER (manager), NULL);
    g_return_val_if_fail (protocol != NULL, NULL);

    priv = manager->priv;
    if (G_UNLIKELY (!priv->tp_conn_mgr))
        return NULL;

    protocols = priv->tp_conn_mgr->protocols;
    if (G_UNLIKELY (!protocols))
        return NULL;

    for (i = 0; protocols[i] != NULL; i++)
    {
        if (strcmp (protocols[i]->name, protocol) == 0)
            return protocols[i]->params;
    }
    return NULL;
}

TpConnectionManagerProtocol *
_mcd_manager_dup_protocol (McdManager *manager,
                           const gchar *protocol)
{
    const TpConnectionManagerProtocol *p;
    g_return_val_if_fail (MCD_IS_MANAGER (manager), NULL);
    g_return_val_if_fail (protocol != NULL, NULL);

    p = tp_connection_manager_get_protocol (manager->priv->tp_conn_mgr,
                                            protocol);

    if (p == NULL)
        return NULL;
    else
        return tp_connection_manager_protocol_copy (p);
}

const TpConnectionManagerParam *
mcd_manager_get_protocol_param (McdManager *manager, const gchar *protocol,
                                const gchar *param)
{
    McdManagerPrivate *priv;
    const TpConnectionManagerProtocol *cm_protocol;

    g_return_val_if_fail (MCD_IS_MANAGER (manager), NULL);
    g_return_val_if_fail (protocol != NULL, NULL);
    g_return_val_if_fail (param != NULL, NULL);

    priv = manager->priv;

    cm_protocol = tp_connection_manager_get_protocol (priv->tp_conn_mgr,
                                                      protocol);

    if (cm_protocol == NULL)
        return NULL;

    return tp_connection_manager_protocol_get_param (cm_protocol, param);
}

McdConnection *
mcd_manager_create_connection (McdManager *manager, McdAccount *account)
{
    McdConnection *connection;

    g_return_val_if_fail (MCD_IS_MANAGER (manager), NULL);
    g_return_val_if_fail (manager->priv->tp_conn_mgr != NULL, NULL);

    connection = MCD_MANAGER_GET_CLASS (manager)->create_connection
        (manager, account);
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
 * mcd_manager_get_dispatcher:
 * @manager: the #McdManager.
 *
 * Returns: the #McdDispatcher.
 */
McdDispatcher *
mcd_manager_get_dispatcher (McdManager *manager)
{
    g_return_val_if_fail (MCD_IS_MANAGER (manager), NULL);
    return manager->priv->dispatcher;
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

