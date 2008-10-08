/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

#define _POSIX_C_SOURCE 200112L  /* for strtok_r() */
#include "config.h"
#include "mcd-manager.h"

#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <telepathy-glib/connection-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#include <libmcclient/mc-errors.h>

#include "mcd-connection.h"

#define MANAGER_SUFFIX ".manager"

#define MCD_MANAGER_PRIV(manager) (MCD_MANAGER (manager)->priv)

G_DEFINE_TYPE (McdManager, mcd_manager, MCD_TYPE_OPERATION);

struct _McdManagerPrivate
{
    gchar *name;
    TpDBusDaemon *dbus_daemon;
    McdPresenceFrame *presence_frame;
    McdDispatcher *dispatcher;

    /* bus name and object path of the ConnectionManager */
    gchar *bus_name;
    gchar *object_path;
    TpConnectionManager *tp_conn_mgr;

    GArray *protocols; /* array of McdProtocol structures */
    gboolean is_disposed;
    gboolean delay_presence_request;
};

enum
{
    PROP_0,
    PROP_NAME,
    PROP_PRESENCE_FRAME,
    PROP_DISPATCHER,
    PROP_DBUS_DAEMON,
};

static const gchar**
_mc_manager_get_dirs (void)
{
    GSList *dir_list = NULL, *slist;
    const gchar *dirname;
    static gchar **manager_dirs = NULL;
    guint n;

    if (manager_dirs) return (const gchar **)manager_dirs;

    dirname = g_getenv ("MC_MANAGER_DIR");
    if (dirname && g_file_test (dirname, G_FILE_TEST_IS_DIR))
	dir_list = g_slist_prepend (dir_list, (gchar *)dirname);

    if (MANAGERS_DIR[0] == '/')
    {
	if (g_file_test (MANAGERS_DIR, G_FILE_TEST_IS_DIR))
	    dir_list = g_slist_prepend (dir_list, MANAGERS_DIR);
    }
    else
    {
	const gchar * const *dirs;
	gchar *dir;

	dir = g_build_filename (g_get_user_data_dir(), MANAGERS_DIR, NULL);
	if (g_file_test (dir, G_FILE_TEST_IS_DIR))
	    dir_list = g_slist_prepend (dir_list, dir);
	else g_free (dir);

	dirs = g_get_system_data_dirs();
	for (dirname = *dirs; dirname; dirs++, dirname = *dirs)
	{
	    dir = g_build_filename (dirname, MANAGERS_DIR, NULL);
	    if (g_file_test (dir, G_FILE_TEST_IS_DIR))
		dir_list = g_slist_prepend (dir_list, dir);
	    else g_free (dir);
	}
    }

    /* build the string array */
    n = g_slist_length (dir_list);
    manager_dirs = g_new (gchar *, n + 1);
    manager_dirs[n--] = NULL;
    for (slist = dir_list; slist; slist = slist->next)
	manager_dirs[n--] = slist->data;
    g_slist_free (dir_list);
    return (const gchar **)manager_dirs;
}

static gchar *
_mcd_manager_filename (const gchar *unique_name)
{
    const gchar **manager_dirs;
    const gchar *dirname;
    gchar *filename, *filepath = NULL;

    manager_dirs = _mc_manager_get_dirs ();
    if (!manager_dirs) return NULL;

    filename = g_strconcat (unique_name, MANAGER_SUFFIX, NULL);
    for (dirname = *manager_dirs; dirname; manager_dirs++, dirname = *manager_dirs)
    {
	filepath = g_build_filename (dirname, filename, NULL);
	if (g_file_test (filepath, G_FILE_TEST_EXISTS)) break;
	g_free (filepath);
	filepath = NULL;
    }
    g_free (filename);
    return filepath;
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

static gboolean
on_presence_requested_idle (gpointer data)
{
    McdManager *manager = MCD_MANAGER (data);
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (manager);
    TpConnectionPresenceType requested_presence =
	mcd_presence_frame_get_requested_presence (priv->presence_frame);
    TpConnectionPresenceType actual_presence =
	mcd_presence_frame_get_actual_presence (priv->presence_frame);

    g_debug ("%s: %d, %d", G_STRFUNC, requested_presence,
	     actual_presence);
    if ((actual_presence == TP_CONNECTION_PRESENCE_TYPE_OFFLINE
	 || actual_presence == TP_CONNECTION_PRESENCE_TYPE_UNSET)
	&& (requested_presence != TP_CONNECTION_PRESENCE_TYPE_OFFLINE
	    && requested_presence != TP_CONNECTION_PRESENCE_TYPE_UNSET))
    {
	/* FIXME
	_mcd_manager_create_connections (manager);
	*/
    }

    return FALSE;
}

static void
on_presence_requested (McdPresenceFrame * presence_frame,
		       TpConnectionPresenceType presence,
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
    DBusGConnection *dbus_connection;
    
    if (already_nuked)
	return; /* We only nuke it once in process instance */
    already_nuked = TRUE;
    
    g_debug ("Nuking possible stale connections");
    
    priv = MCD_MANAGER_PRIV (manager);
    dbus_connection = TP_PROXY (priv->dbus_daemon)->dbus_connection;
    proxy = dbus_g_proxy_new_for_name(dbus_connection,
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
	    
	    proxy = dbus_g_proxy_new_for_name(dbus_connection,
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
	g_object_unref (priv->presence_frame);
    }
    priv->presence_frame = presence_frame;
    if (priv->presence_frame)
    {
	g_signal_connect (G_OBJECT (priv->presence_frame),
			  "presence-requested",
			  G_CALLBACK (on_presence_requested), manager);
    }
}

static void
_mcd_manager_finalize (GObject * object)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (object);
    McdProtocolParam *param;
    McdProtocol *protocol;
    guint i, j;

    for (i = 0; i < priv->protocols->len; i++)
    {
	protocol = &g_array_index (priv->protocols, McdProtocol, i);
	for (j = 0; j < protocol->params->len; j++)
	{
	    param = &g_array_index (protocol->params, McdProtocolParam, j);
	    g_free (param->name);
	    g_free (param->signature);
	}
	g_array_free (protocol->params, TRUE);
    }
    g_array_free (priv->protocols, TRUE);

    g_free (priv->name);
    g_free (priv->bus_name);
    g_free (priv->object_path);

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

    if (priv->dispatcher)
    {
	g_object_unref (priv->dispatcher);
	priv->dispatcher = NULL;
    }
    
    _mcd_manager_set_presence_frame (MCD_MANAGER (object), NULL);
    
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

static inline void
read_parameters (GArray *params, GKeyFile *keyfile, const gchar *group_name)
{
    gchar **keys, **i;

    keys = g_key_file_get_keys (keyfile, group_name, NULL, NULL);
    if (!keys)
    {
	g_warning ("%s: failed to get keys from file", G_STRFUNC);
	return;
    }

    for (i = keys; *i != NULL; i++)
    {
	McdProtocolParam param = { 0, 0, 0 };
	gchar *value = g_key_file_get_string (keyfile, group_name, *i, NULL);

	if (strncmp (*i, "param-", 6) == 0)
	{
	    gchar *ptr, *signature, *flag;

	    param.name = g_strdup (*i + 6);

	    signature = strtok_r (value, " \t", &ptr);
	    if (!signature)
	    {
		g_warning ("%s: param \"%s\" has no signature",
			   G_STRFUNC, param.name);
		g_free (value);
		continue;
	    }
	    param.signature = g_strdup (signature);

	    while ((flag = strtok_r (NULL, " \t", &ptr)) != NULL)
	    {
		if (strcmp (flag, "required") == 0)
		    param.flags |= MCD_PROTOCOL_PARAM_REQUIRED;
		else if (strcmp (flag, "register") == 0)
		    param.flags |= MCD_PROTOCOL_PARAM_REGISTER;
	    }
	    g_array_append_val (params, param);
	}

	g_free (value);
    }

    g_strfreev (keys);
}

static inline void
read_protocols (McdManager *manager, GKeyFile *keyfile)
{
    McdManagerPrivate *priv = manager->priv;
    gchar **groups = NULL, **i;

    groups = g_key_file_get_groups (keyfile, NULL);

    for (i = groups; *i != NULL; i++)
    {
	McdProtocol protocol;

	if (strncmp (*i, "Protocol ", 9) != 0) continue;
	protocol.name = g_strdup (*i + 9);
	protocol.params = g_array_new (FALSE, FALSE,
				       sizeof (McdProtocolParam));
	read_parameters (protocol.params, keyfile, *i);

	g_array_append_val (priv->protocols, protocol);
    }

    g_strfreev (groups);
}

static gboolean
mcd_manager_setup (McdManager *manager)
{
    McdManagerPrivate *priv = manager->priv;
    GError *error = NULL;
    GKeyFile *keyfile;
    gchar *bus_name = NULL;
    gchar *object_path = NULL;
    gchar *filename;

    filename = _mcd_manager_filename (priv->name);
    if (!filename)
    {
	g_warning ("No config file found for manager %s", priv->name);
	return FALSE;
    }

    keyfile = g_key_file_new ();

    if (!g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, &error))
    {
	g_warning ("%s: loading %s failed: %s", G_STRFUNC,
		   filename, error->message);
	g_error_free (error);
	g_free (filename);
	return FALSE;
    }
    g_free (filename);

    priv->bus_name = g_key_file_get_string (keyfile, "ConnectionManager",
					    "BusName", NULL);
    priv->object_path = g_key_file_get_string (keyfile, "ConnectionManager",
					       "ObjectPath", NULL);

    if (!priv->bus_name || !priv->object_path)
    {
	g_warning ("%s: failed to get bus name and object path from file",
		   G_STRFUNC);
	g_free (bus_name);
	g_free (object_path);
	return FALSE;
    }

    read_protocols (manager, keyfile);
    g_key_file_free (keyfile);

    return TRUE;
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

    _mcd_manager_nuke_connections (manager);

    return (GObject *) manager;
}

static void
_mcd_manager_set_property (GObject * obj, guint prop_id,
			   const GValue * val, GParamSpec * pspec)
{
    McdManagerPrivate *priv = MCD_MANAGER_PRIV (obj);
    McdPresenceFrame *presence_frame;
    McdDispatcher *dispatcher;

    switch (prop_id)
    {
    case PROP_NAME:
	g_assert (priv->name == NULL);
	priv->name = g_value_dup_string (val);
	break;
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
    case PROP_DBUS_DAEMON:
	if (priv->dbus_daemon)
	    g_object_unref (priv->dbus_daemon);
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
    case PROP_PRESENCE_FRAME:
	g_value_set_object (val, priv->presence_frame);
	break;
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
    g_object_class_install_property (object_class,
				     PROP_NAME,
				     g_param_spec_string ("name",
							  _("Name"),
							  _("Name"),
							  NULL,
							  G_PARAM_WRITABLE |
							  G_PARAM_CONSTRUCT_ONLY));
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
    g_object_class_install_property (object_class, PROP_DBUS_DAEMON,
				     g_param_spec_object ("dbus-daemon",
							  _("DBus daemon"),
							  _("DBus daemon"),
							  TP_TYPE_DBUS_DAEMON,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT));
}

static void
mcd_manager_init (McdManager *manager)
{
    McdManagerPrivate *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, MCD_TYPE_MANAGER,
					McdManagerPrivate);
    manager->priv = priv;

    priv->protocols = g_array_new (FALSE, FALSE, sizeof (McdProtocol));
}

/* Public methods */

McdManager *
mcd_manager_new (const gchar *unique_name,
		 McdPresenceFrame * pframe,
		 McdDispatcher *dispatcher,
		 TpDBusDaemon *dbus_daemon)
{
    McdManager *obj;
    obj = MCD_MANAGER (g_object_new (MCD_TYPE_MANAGER,
				     "name", unique_name,
				     "presence-frame", pframe,
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
const GArray *
mcd_manager_get_parameters (McdManager *manager, const gchar *protocol)
{
    McdManagerPrivate *priv = manager->priv;
    McdProtocol *proto;
    guint i;

    for (i = 0; i < priv->protocols->len; i++)
    {
	proto = &g_array_index (priv->protocols, McdProtocol, i);
	if (strcmp (proto->name, protocol) == 0)
	{
	    return proto->params;
	}
    }
    return NULL;
}

McdConnection *
mcd_manager_create_connection (McdManager *manager, McdAccount *account)
{
    McdManagerPrivate *priv = manager->priv;
    McdConnection *connection;

    if (!priv->tp_conn_mgr)
    {
	GError *error = NULL;
	gchar *manager_filename;

	manager_filename = _mcd_manager_filename (priv->name);
	priv->tp_conn_mgr =
	    tp_connection_manager_new (priv->dbus_daemon, priv->name,
				       manager_filename, &error);
	g_free (manager_filename);
	if (error)
	{
	    g_warning ("%s, cannot create manager %s: %s", G_STRFUNC,
		       priv->name, error->message);
	    g_error_free (error);
	    return NULL;
	}
	g_debug ("%s: Manager %s created", G_STRFUNC, priv->name);
    }

    connection = mcd_connection_new (priv->dbus_daemon,
				     TP_PROXY (priv->tp_conn_mgr)->bus_name,
				     priv->tp_conn_mgr, account,
				     priv->dispatcher);
    mcd_operation_take_mission (MCD_OPERATION (manager),
				MCD_MISSION (connection));
    g_debug ("%s: Created a connection %p for account: %s", G_STRFUNC,
	     connection, mcd_account_get_unique_name (account));

    return connection;
}

