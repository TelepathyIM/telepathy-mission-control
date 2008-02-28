/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
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

#include <string.h>
#include <glib/gi18n.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/connection-manager.h>
#include <libmissioncontrol/mission-control.h>

#include "mcd-connection.h"
#include "mcd-manager.h"

#define MCD_MANAGER_PRIV(manager) (G_TYPE_INSTANCE_GET_PRIVATE ((manager), \
				   MCD_TYPE_MANAGER, \
				   McdManagerPrivate))

G_DEFINE_TYPE (McdManager, mcd_manager, MCD_TYPE_OPERATION);

typedef struct _McdManagerPrivate
{
    DBusGConnection *dbus_connection;
    TpDBusDaemon *dbus_daemon;
    McManager *mc_manager;
    McdPresenceFrame *presence_frame;
    McdDispatcher *dispatcher;
    TpConnectionManager *tp_conn_mgr;
    GList *accounts;
    gboolean is_disposed;
    gboolean delay_presence_request;

    /* Table of channels to create upon connection */
    GHashTable *requested_channels;
} McdManagerPrivate;

enum
{
    PROP_0,
    PROP_PRESENCE_FRAME,
    PROP_DISPATCHER,
    PROP_MC_MANAGER,
    PROP_DBUS_CONNECTION,
    PROP_ACCOUNTS
};

enum _McdManagerSignalType
{
    ACCOUNT_ADDED,
    ACCOUNT_REMOVED,
    LAST_SIGNAL
};

static guint mcd_manager_signals[LAST_SIGNAL] = { 0 };

static void abort_requested_channel (gchar *key,
				     struct mcd_channel_request *req,
				     McdManager *manager);

static void
_mcd_manager_create_connection (McdManager * manager, McAccount * account)
{
    McdConnection *connection;
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);
    
    g_return_if_fail (mcd_manager_get_account_connection (manager, account) == NULL);
    if (!priv->tp_conn_mgr)
    {
	GError *error = NULL;
	const gchar *unique_name;

	g_return_if_fail (MC_IS_MANAGER (priv->mc_manager));

	unique_name = mc_manager_get_unique_name (priv->mc_manager);
	priv->tp_conn_mgr =
	    tp_connection_manager_new (priv->dbus_daemon, unique_name,
				       mc_manager_get_filename (priv->mc_manager),
				       &error);
	if (error)
	{
	    g_warning ("%s, cannot create manager %s: %s", G_STRFUNC,
		       unique_name, error->message);
	    g_error_free (error);
	    return;
	}
	g_debug ("%s: Manager %s created", G_STRFUNC, unique_name);
    }
    
    connection = mcd_connection_new (priv->dbus_connection,
            mc_manager_get_bus_name (priv->
                mc_manager),
            priv->tp_conn_mgr, account,
            priv->presence_frame,
            priv->dispatcher);
    mcd_operation_take_mission (MCD_OPERATION (manager),
            MCD_MISSION (connection));
    mcd_connection_connect (connection);
    g_debug ("%s: Created a connection %p for account: %s", G_STRFUNC,
            connection, mc_account_get_unique_name (account));
}

void
_mcd_manager_create_connections (McdManager * manager)
{
    GList *node;
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);

    for (node = priv->accounts; node; node = node->next)
    {
	/* Create a connection object for each account */
	McAccount *account = MC_ACCOUNT (node->data);
	
	/* Only create a new connection if there already is not one */
	if (!mcd_manager_get_account_connection (manager, account))
	{
	    _mcd_manager_create_connection (manager, account);
	}
    }
}

static gint
_find_connection (gconstpointer data, gconstpointer user_data)
{
    McdConnection *connection = MCD_CONNECTION (data);
    McAccount *account = MC_ACCOUNT (user_data);
    McAccount *connection_account = NULL;
    gint ret;

    g_object_get (G_OBJECT (connection), "account", &connection_account, NULL);

    if (connection_account == account)
    {
	ret = 0;
    }
    else
    {
	ret = 1;
    }

    g_object_unref (G_OBJECT (connection_account));
    return ret;
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
requested_channel_process (gchar *key, struct mcd_channel_request *req,
			   McdManager *manager)
{
    GError *error = NULL;

    g_debug ("%s: creating channel %s - %s - %s", G_STRFUNC, req->account_name, req->channel_type, req->channel_handle_string);

    if (!mcd_manager_request_channel (manager, req, &error))
    {
	g_assert (error != NULL);
	g_debug ("%s: channel request failed (%s)", G_STRFUNC, error->message);
	g_error_free (error);
	return;
    }
    g_assert (error == NULL);
}

static void
on_presence_stable (McdPresenceFrame *presence_frame,
		    gboolean is_stable, McdManager *manager)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);

    g_debug ("%s called", G_STRFUNC); 
    if (priv->requested_channels)
    {
	/* don't do anything until the presence frame is stable */
	g_debug ("presence frame is %sstable", mcd_presence_frame_is_stable (presence_frame) ? "" : "not ");
	if (!is_stable)
	    return;
	if (mcd_presence_frame_get_actual_presence (presence_frame) >=
	    MC_PRESENCE_AVAILABLE)
	{
	    g_hash_table_foreach (priv->requested_channels,
				  (GHFunc)requested_channel_process,
				  manager);
	}
	else
	{
	    /* We couldn't connect; signal an error to the channel requestors
	     */
	    g_hash_table_foreach (priv->requested_channels,
				  (GHFunc)abort_requested_channel,
				  manager);
	}
	g_hash_table_destroy (priv->requested_channels);
	priv->requested_channels = NULL;
    }
}

static gboolean
on_presence_requested_idle (gpointer data)
{
    McdManager *manager = MCD_MANAGER (data);
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);
    McPresence requested_presence =
	mcd_presence_frame_get_requested_presence (priv->presence_frame);
    McPresence actual_presence =
	mcd_presence_frame_get_actual_presence (priv->presence_frame);

    g_debug ("%s: %d, %d", G_STRFUNC, requested_presence,
	     actual_presence);
    if ((actual_presence == MC_PRESENCE_OFFLINE
	 || actual_presence == MC_PRESENCE_UNSET)
	&& (requested_presence != MC_PRESENCE_OFFLINE
	    && requested_presence != MC_PRESENCE_UNSET))
    {
	_mcd_manager_create_connections (manager);
    }

    return FALSE;
}

static void
abort_requested_channel (gchar *key, struct mcd_channel_request *req,
			 McdManager *manager)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);
    McdChannel *channel;
    GError *error;

    g_debug ("%s: aborting channel %s - %s - %s", G_STRFUNC,
	     req->account_name, req->channel_type, req->channel_handle_string);
    error = g_error_new (MC_ERROR, MC_NETWORK_ERROR,
			 "Connection cancelled");
    /* we must create a channel object, just for delivering the error */
    channel = mcd_channel_new (NULL,
			       NULL,
			       req->channel_type,
			       0,
			       req->channel_handle_type,
			       TRUE, /* outgoing */
			       req->requestor_serial,
			       req->requestor_client_id);
    g_signal_emit_by_name (priv->dispatcher, "dispatch-failed",
			   channel, error);
    g_error_free (error);
    /* this will actually destroy the channel object */
    g_object_unref (channel);
}

static void
abort_requested_channels (McdManager *manager)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);

    g_debug ("%s called %p", G_STRFUNC, priv->requested_channels);
    g_hash_table_foreach (priv->requested_channels,
			  (GHFunc)abort_requested_channel,
			  manager);
    g_hash_table_destroy (priv->requested_channels);
    priv->requested_channels = NULL;
}

static void
on_presence_requested (McdPresenceFrame * presence_frame,
		       McPresence presence,
		       const gchar * presence_message, gpointer data)
{
    McdManagerPrivate *priv;

    g_debug ("%s: Current connectivity status is %d", G_STRFUNC,
	     mcd_mission_is_connected (MCD_MISSION (data)));

    if (mcd_mission_is_connected (MCD_MISSION (data)))
    {
	on_presence_requested_idle(data);
    }
    else
    {
	priv = MCD_MANAGER_PRIV(data);
	g_debug ("%s: Delaying call to on_presence_requested_idle", G_STRFUNC);
	priv->delay_presence_request = TRUE;

	/* if we are offline and the user cancels the connection request, we
	 * must clean the requested channels and return an error to the UI for
	 * each of them. */
	if (presence == MC_PRESENCE_OFFLINE && priv->requested_channels != NULL)
	    abort_requested_channels (MCD_MANAGER (data));
    }
}

/* FIXME: Until we have a proper serialization and deserialization, we will
 * stick with killing all connections that were present before
 * mission-control got control of telepathy managers
 */
/* Search the bus for already connected accounts and disconnect them. */
static void
_mcd_manager_nuke_connections (McdManager *manager)
{
    McdManagerPrivate *priv;
    char **names, **name;
    DBusGProxy *proxy;
    GError *error = NULL;
    static gboolean already_nuked = FALSE;
    
    if (already_nuked)
	return; /* We only nuke it once in process instance */
    already_nuked = TRUE;
    
    g_debug ("Nuking possible stale connections");
    
    priv = MCD_MANAGER_PRIV (manager);
    proxy = dbus_g_proxy_new_for_name(priv->dbus_connection,
				      DBUS_SERVICE_DBUS,
				      DBUS_PATH_DBUS,
				      DBUS_INTERFACE_DBUS);
    
    if (!proxy) 
    {
	g_warning ("Error creating proxy");
	return;
    }
    
    if (!dbus_g_proxy_call(proxy, "ListNames", &error, G_TYPE_INVALID,
			   G_TYPE_STRV, &names, G_TYPE_INVALID))
    {
	g_warning ("ListNames() failed: %s", error->message);
	g_error_free (error);
	g_object_unref (proxy);
	return;
    }

    g_object_unref(proxy);
    
    for (name = names; *name; name++)
    {
	if (strncmp(*name, "org.freedesktop.Telepathy.Connection.",
		    strlen("org.freedesktop.Telepathy.Connection.")) == 0)
	{
	    gchar *path = g_strdelimit(g_strdup_printf("/%s", *name), ".", '/');
	    
	    g_debug ("Trying to disconnect (%s), path=%s", *name, path);
	    
	    proxy = dbus_g_proxy_new_for_name(priv->dbus_connection,
					      *name, path,
					      TP_IFACE_CONNECTION);
	    
	    g_free(path);
	    
	    if (proxy)
	    {
		if (!dbus_g_proxy_call(proxy, "Disconnect", &error,
				       G_TYPE_INVALID, G_TYPE_INVALID))
		{
		    g_warning ("Disconnect() failed: %s", error->message);
		    g_error_free(error);
		    error = NULL;
		}
		
		g_object_unref(proxy);
	    }
	    else
	    {
		g_warning ("Error creating proxy");
	    }
	}
    }
    g_strfreev(names);
}

static void
requested_channel_free (struct mcd_channel_request *req)
{
    g_free ((gchar *)req->account_name);
    g_free ((gchar *)req->channel_type);
    g_free ((gchar *)req->channel_handle_string);
    g_free ((gchar *)req->requestor_client_id);
    g_free (req);
}

static void
request_channel_delayed (McdManager *manager,
			 const struct mcd_channel_request *req)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);
    struct mcd_channel_request *req_cp;
    gchar *key;

    g_debug ("%s: account %s, type %s, handle %s", G_STRFUNC, req->account_name, req->channel_type, req->channel_handle_string);
    if (!priv->requested_channels)
	priv->requested_channels =
	    g_hash_table_new_full (g_direct_hash, g_direct_equal,
				   NULL, (GDestroyNotify)
				   requested_channel_free);

    if (req->channel_handle_string)
	key = g_strdup_printf("%s\n%s\n%s", req->account_name, req->channel_type,
			      req->channel_handle_string);
    else
	key = g_strdup_printf("%s\n%s\n%u", req->account_name, req->channel_type,
			      req->channel_handle);
    req_cp = g_malloc (sizeof (struct mcd_channel_request));
    memcpy(req_cp, req, sizeof (struct mcd_channel_request));
    req_cp->account_name = g_strdup (req->account_name);
    req_cp->channel_type = g_strdup (req->channel_type);
    req_cp->channel_handle_string = g_strdup (req->channel_handle_string);
    req_cp->requestor_client_id = g_strdup (req->requestor_client_id);
    g_hash_table_insert (priv->requested_channels, key, req_cp);
    g_free (key);
}

static void
_mcd_manager_set_presence_frame (McdManager *manager, McdPresenceFrame *presence_frame)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);
    if (presence_frame)
    {
	g_return_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame));
	g_object_ref (presence_frame);
    }

    if (priv->presence_frame)
    {
	g_signal_handlers_disconnect_by_func (G_OBJECT
					      (priv->presence_frame),
					      G_CALLBACK
					      (on_presence_requested), manager);
	g_signal_handlers_disconnect_by_func (priv->presence_frame,
					      G_CALLBACK
					      (on_presence_stable),
					      manager);
	g_object_unref (priv->presence_frame);
    }
    priv->presence_frame = presence_frame;
    if (priv->presence_frame)
    {
	g_signal_connect (G_OBJECT (priv->presence_frame),
			  "presence-requested",
			  G_CALLBACK (on_presence_requested), manager);
	g_signal_connect (priv->presence_frame, "presence-stable",
			  G_CALLBACK (on_presence_stable), manager);
    }
}

static void
_mcd_manager_finalize (GObject * object)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (object);

    if (priv->requested_channels)
    {
	g_hash_table_destroy (priv->requested_channels);
	priv->requested_channels = NULL;
    }
    G_OBJECT_CLASS (mcd_manager_parent_class)->finalize (object);
}

static void
_mcd_manager_dispose (GObject * object)
{
    GList *node;
    McdManagerPrivate *priv;
    priv = MCD_MANAGER_PRIV (object);

    if (priv->is_disposed)
    {
	return;
    }

    priv->is_disposed = TRUE;

    if (priv->accounts)
    {
	for (node = priv->accounts; node; node = node->next)
	    g_object_unref (G_OBJECT (node->data));
	g_list_free (priv->accounts);
	priv->accounts = NULL;
    }
    
    if (priv->dispatcher)
    {
	g_object_unref (priv->dispatcher);
	priv->dispatcher = NULL;
    }
    
    _mcd_manager_set_presence_frame (MCD_MANAGER (object), NULL);
    
    if (priv->dbus_connection)
    {
	dbus_g_connection_unref (priv->dbus_connection);
	priv->dbus_connection = NULL;
    }
    
    if (priv->mc_manager)
    {
	g_object_unref (priv->mc_manager);
	priv->mc_manager = NULL;
    }
    
    if (priv->tp_conn_mgr)
    {
	g_object_unref (priv->tp_conn_mgr);
	priv->tp_conn_mgr = NULL;
    }

    if (priv->dbus_daemon)
	g_object_unref (priv->dbus_daemon);

    G_OBJECT_CLASS (mcd_manager_parent_class)->dispose (object);
}

static void
_mcd_manager_connect (McdMission * mission)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (mission);

    g_debug ("%s: delay_presence_request = %d", G_STRFUNC, priv->delay_presence_request);
    if (priv->delay_presence_request)
    {
	priv->delay_presence_request = FALSE;
	g_idle_add (on_presence_requested_idle, mission);
	g_debug ("%s: Added idle func on_presence_requested_idle", G_STRFUNC);
    }
    MCD_MISSION_CLASS (mcd_manager_parent_class)->connect (mission);
}

static void
_mcd_manager_disconnect (McdMission * mission)
{
    GList *connections;

    g_debug ("%s(%p)", G_STRFUNC, mission);
    MCD_MISSION_CLASS (mcd_manager_parent_class)->disconnect (mission);

    /* We now call mcd_mission_abort() on all child connections; but since this
     * could modify the list of the children, we cannot just use
     * mcd_operation_foreach(). Instead, make a copy of the list and work on
     * that. */
    g_debug("manager tree before abort:");
    mcd_debug_print_tree(mission);
    connections = g_list_copy ((GList *)mcd_operation_get_missions
			       (MCD_OPERATION (mission)));
    g_list_foreach (connections, (GFunc) mcd_mission_abort, NULL);
    g_list_free (connections);
    g_debug("manager tree after abort:");
    mcd_debug_print_tree(mission);
}

static void
_mcd_manager_set_property (GObject * obj, guint prop_id,
			   const GValue * val, GParamSpec * pspec)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (obj);
    McdPresenceFrame *presence_frame;
    McdDispatcher *dispatcher;
    McManager *mc_manager;
    DBusGConnection *dbus_connection;

    switch (prop_id)
    {
    case PROP_PRESENCE_FRAME:
	presence_frame = g_value_get_object (val);
	_mcd_manager_set_presence_frame (MCD_MANAGER (obj), presence_frame);
	break;
    case PROP_DISPATCHER:
	dispatcher = g_value_get_object (val);
	if (dispatcher)
	{
	    g_return_if_fail (MCD_IS_DISPATCHER (dispatcher));
	    g_object_ref (dispatcher);
	}
	if (priv->dispatcher)
	{
	    g_object_unref (priv->dispatcher);
	}
	priv->dispatcher = dispatcher;
	break;
    case PROP_MC_MANAGER:
	mc_manager = g_value_get_object (val);
	g_return_if_fail (MC_IS_MANAGER (mc_manager));
	g_object_ref (mc_manager);
	if (priv->mc_manager)
	    g_object_unref (priv->mc_manager);
	priv->mc_manager = mc_manager;
	break;
    case PROP_DBUS_CONNECTION:
	dbus_connection = g_value_get_pointer (val);
	dbus_g_connection_ref (dbus_connection);
	if (priv->dbus_connection)
	    dbus_g_connection_unref (priv->dbus_connection);
	priv->dbus_connection = dbus_connection;
	if (priv->dbus_daemon)
	    g_object_unref (priv->dbus_daemon);
	priv->dbus_daemon = tp_dbus_daemon_new (dbus_connection);
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
    case PROP_PRESENCE_FRAME:
	g_value_set_object (val, priv->presence_frame);
	break;
    case PROP_DISPATCHER:
	g_value_set_object (val, priv->dispatcher);
	break;
    case PROP_MC_MANAGER:
	g_value_set_object (val, priv->mc_manager);
	break;
    case PROP_DBUS_CONNECTION:
	g_value_set_pointer (val, priv->dbus_connection);
	break;
    case PROP_ACCOUNTS:
        g_debug ("%s: accounts getting over-written", G_STRFUNC);
	g_value_set_pointer (val, priv->accounts);
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

    object_class->finalize = _mcd_manager_finalize;
    object_class->dispose = _mcd_manager_dispose;
    object_class->set_property = _mcd_manager_set_property;
    object_class->get_property = _mcd_manager_get_property;

    mission_class->connect = _mcd_manager_connect;
    mission_class->disconnect = _mcd_manager_disconnect;

    /* signals */
    mcd_manager_signals[ACCOUNT_ADDED] =
	g_signal_new ("account-added",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdManagerClass, account_added_signal),
		      NULL, NULL,
		      g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE, 1, G_TYPE_OBJECT);
    mcd_manager_signals[ACCOUNT_REMOVED] =
	g_signal_new ("account-removed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdManagerClass, account_removed_signal),
		      NULL, NULL,
		      g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE, 1, G_TYPE_OBJECT);

    /* Properties */
    g_object_class_install_property (object_class,
				     PROP_PRESENCE_FRAME,
				     g_param_spec_object ("presence-frame",
							  _
							  ("Presence Frame Object"),
							  _
							  ("Presence frame Object used by connections to update presence"),
							  MCD_TYPE_PRESENCE_FRAME,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
				     PROP_DISPATCHER,
				     g_param_spec_object ("dispatcher",
							  _
							  ("Dispatcher Object"),
							  _
							  ("Channel dispatcher object"),
							  MCD_TYPE_DISPATCHER,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class, PROP_MC_MANAGER,
				     g_param_spec_object ("mc-manager",
							  _
							  ("McManager Object"),
							  _
							  ("McManager Object which this manager uses"),
							  MC_TYPE_MANAGER,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class, PROP_DBUS_CONNECTION,
				     g_param_spec_pointer ("dbus-connection",
							   _("DBus Connection"),
							   _
							   ("DBus connection to use by us"),
							   G_PARAM_READWRITE |
							   G_PARAM_CONSTRUCT));
    g_object_class_install_property (object_class, PROP_ACCOUNTS,
				     g_param_spec_pointer ("accounts",
							   _("Accounts"),
							   _
							   ("List of accounts associated with this manager"),
							   G_PARAM_READABLE));
}

static void
mcd_manager_init (McdManager * manager)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);

    priv->dbus_connection = NULL;
}

/* Public methods */

McdManager *
mcd_manager_new (McManager * mc_manager,
		 McdPresenceFrame * pframe,
		 McdDispatcher *dispatcher,
		 DBusGConnection * dbus_connection)
{
    McdManager *obj;
    obj = MCD_MANAGER (g_object_new (MCD_TYPE_MANAGER,
				     "mc-manager", mc_manager,
				     "presence-frame", pframe,
				     "dispatcher", dispatcher,
				     "dbus-connection", dbus_connection, NULL));
    _mcd_manager_nuke_connections (obj);
    return obj;
}

gboolean
mcd_manager_can_handle_account (McdManager * manager, McAccount *account)
{
    McdManagerPrivate *priv;
    McProfile *profile;
    McProtocol *protocol;
    McManager *mc_manager;
    gboolean ret;

    g_return_val_if_fail (MCD_IS_MANAGER (manager), FALSE);
    g_return_val_if_fail (account != NULL, FALSE);
    
    priv = MCD_MANAGER_PRIV (manager);

    profile = account ? mc_account_get_profile (account) : NULL;
    protocol = profile ? mc_profile_get_protocol (profile) : NULL;
    mc_manager = protocol ? mc_protocol_get_manager (protocol) : NULL;

    if (priv->mc_manager == mc_manager)
    {
        ret = TRUE;
    }
    else 
    {
        ret = FALSE;
    }

    if (profile)
        g_object_unref (profile);
    if (protocol)
        g_object_unref (protocol);
    if (mc_manager)
        g_object_unref (mc_manager);
   
    return ret;
}

McAccount *
mcd_manager_get_account_by_name (McdManager * manager,
				 const gchar * account_name)
{
    GList *node;
    McdManagerPrivate *priv;

    g_return_val_if_fail (MCD_IS_MANAGER (manager), FALSE);
    g_return_val_if_fail (account_name != NULL, FALSE);
    
    priv = MCD_MANAGER_PRIV (manager);
    
    node = priv->accounts;
    while (node)
    {
	if (strcmp (mc_account_get_unique_name (MC_ACCOUNT (node->data)),
		    account_name) == 0)
	    return MC_ACCOUNT (node->data);
	node = node->next;
    }
    return NULL;
}

gboolean
mcd_manager_add_account (McdManager * manager, McAccount * account)
{
    McdManagerPrivate *priv;
    McdConnection *connection;
    McPresence requested_presence;

    g_return_val_if_fail (MCD_IS_MANAGER (manager), FALSE);
    g_return_val_if_fail (MC_IS_ACCOUNT (account), FALSE);

    priv = MCD_MANAGER_PRIV (manager);

    /* Make sure this account can be handled by this manager */
    g_return_val_if_fail (mcd_manager_can_handle_account (manager, account),
			  FALSE);

    /* Check if the account is already added */
    if (g_list_find (priv->accounts, account))
	return FALSE;

    g_object_ref (account);
    g_debug ("%s: %u accounts in total", G_STRFUNC, g_list_length (priv->accounts));
    g_debug ("%s: adding account %p", G_STRFUNC, account);
    priv->accounts = g_list_prepend (priv->accounts, account);
    g_debug ("%s: %u accounts in total", G_STRFUNC, g_list_length (priv->accounts));
    
    requested_presence =
	mcd_presence_frame_get_requested_presence (priv->presence_frame);

    connection = mcd_manager_get_account_connection (manager, account);
    if (!connection)
    {
        /* if presence is not offline or unset, we must create the 
         * connection for this new account */
        if ((requested_presence != MC_PRESENCE_OFFLINE &&
             requested_presence != MC_PRESENCE_UNSET))
        {
            _mcd_manager_create_connection (manager, account);
        }
    }

    g_signal_emit_by_name (manager, "account-added", account);
    return TRUE;
}

gboolean
mcd_manager_remove_account (McdManager * manager, McAccount * account)
{
    McdManagerPrivate *priv;
    McdConnection *connection;

    g_return_val_if_fail (MCD_IS_MANAGER (manager), FALSE);
    g_return_val_if_fail (MC_IS_ACCOUNT (account), FALSE);

    priv = MCD_MANAGER_PRIV (manager);

    if (!g_list_find (priv->accounts, account))
	return FALSE;

    connection = mcd_manager_get_account_connection (manager, account);
    if (connection != NULL)
    {
        mcd_connection_close (connection);
    }
    
    g_debug ("%s: %u accounts in total", G_STRFUNC, g_list_length (priv->accounts));
    g_debug ("%s: removing account %p", G_STRFUNC, account);
    priv->accounts = g_list_remove (priv->accounts, account);
    g_debug ("%s: %u accounts in total", G_STRFUNC, g_list_length (priv->accounts));
    g_signal_emit_by_name (manager, "account-removed", account);

    /* Account is unrefed after signal emission to prevent it being dead
     * when the signal was emitted.
     */
    g_object_unref (account);
    
    if (priv->accounts == NULL)
    {
        /* If we don't have any accounts, we don't have the right to live
         * anymore. */
        g_debug ("%s: commiting suicide", G_STRFUNC);
        mcd_mission_abort (MCD_MISSION (manager));
    }

    return TRUE;
}

const GList *
mcd_manager_get_accounts (McdManager * manager)
{
    return MCD_MANAGER_PRIV (manager)->accounts;
}

McdConnection *
mcd_manager_get_account_connection (McdManager * manager,
				    McAccount * account)
{
    const GList *connections;
    const GList *node;

    connections = mcd_operation_get_missions (MCD_OPERATION (manager));
    node = g_list_find_custom ((GList*)connections, account, _find_connection);

    if (node != NULL)
    {
	return MCD_CONNECTION (node->data);
    }

    else
    {
	return NULL;
    }
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
mcd_manager_request_channel (McdManager *manager,
			     const struct mcd_channel_request *req,
			     GError ** error)
{
    McAccount *account;
    McdConnection *connection;
    
    account = mcd_manager_get_account_by_name (manager, req->account_name);
    if (!account)
    {
	/* ERROR here */
	if (error)
	{
	    g_set_error (error, MC_ERROR, MC_NO_MATCHING_CONNECTION_ERROR,
			 "No matching account found for account name '%s'",
			 req->account_name);
	    g_warning ("No matching account found for account name '%s'",
		       req->account_name);
	}
	return FALSE;
    }
    
    connection = mcd_manager_get_account_connection (manager, account);
    if (!connection)
    {
	McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);

	g_debug ("%s: mcd-manager has connectivity status = %d", G_STRFUNC, mcd_mission_is_connected (MCD_MISSION (manager)));
	if (!mcd_mission_is_connected (MCD_MISSION (manager)) ||
	    (mcd_presence_frame_get_actual_presence (priv->presence_frame) <= MC_PRESENCE_AVAILABLE &&
	     !mcd_presence_frame_is_stable (priv->presence_frame))
	    )
	{
	    request_channel_delayed (manager, req);
	    return TRUE;
	}
	/* ERROR here */
	if (error)
	{
	    g_set_error (error, MC_ERROR, MC_NO_MATCHING_CONNECTION_ERROR,
			 "No matching connection found for account name '%s'",
			 req->account_name);
	    g_warning ("%s: No matching connection found for account name '%s'",
		       G_STRFUNC, req->account_name);
	}
	return FALSE;
    }
    else if (mcd_connection_get_connection_status (connection) !=
	     TP_CONNECTION_STATUS_CONNECTED)
    {
	g_debug ("%s: connection is not connected", G_STRFUNC);
	request_channel_delayed (manager, req);
	return TRUE;
    }

    if (!mcd_connection_request_channel (connection, req, error))
    {
	g_assert (error == NULL || *error != NULL);
	return FALSE;
    }
    g_assert (error == NULL || *error == NULL);
    return TRUE;
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
 * mcd_manager_reconnect_account:
 * @manager: the #McdManager.
 * @account: the #McAccount to reconnect.
 *
 * Reconnect the account; if the account is currently online, first it will be
 * disconnected.
 */
void
mcd_manager_reconnect_account (McdManager *manager, McAccount *account)
{
    McdConnection *connection;
   
    g_debug ("%s called", G_STRFUNC);
    connection = mcd_manager_get_account_connection (manager, account);
    if (connection)
	mcd_connection_restart (connection);
    else
    {
	/* create a connection for the account */
	g_debug ("try to create a connection");
	_mcd_manager_create_connection (manager, account);
    }
}

