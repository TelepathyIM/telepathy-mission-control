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
 * SECTION:mcd-master
 * @title: McdMaster
 * @short_description: Server master class
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-master.h
 * 
 * This class implements actual mission-control. It keeps track of
 * individual account presence and connection states in a McdPresenceFrame
 * member object, which is available as a property.
 * 
 * The McdPresenceFrame object could be easily utilized for
 * any presence releated events and actions, either within this class or
 * any other class subclassing it or using it.
 *
 * It is basically a container for all McdManager objects and
 * takes care of their management. It also takes care of sleep and awake
 * cycles (e.g. translates to auto away somewhere down the hierarchy).
 *
 * McdMaster is a subclass of McdConroller, which essentially means it
 * is subject to all device control.
 */

#include <glib/gi18n.h>
#include <gconf/gconf-client.h>
#include <gmodule.h>
#include <string.h>
#include <libmissioncontrol/mc-manager.h>
#include <libmissioncontrol/mc-account.h>
#include <libmissioncontrol/mc-profile.h>
#include <libmissioncontrol/mc-account-monitor.h>
#include <libmissioncontrol/mission-control.h>

#include "mcd-master.h"
#include "mcd-presence-frame.h"
#include "mcd-proxy.h"
#include "mcd-manager.h"
#include "mcd-dispatcher.h"
#include "mcd-account-manager.h"
#include "mcd-plugin.h"

#define MCD_MASTER_PRIV(master) (G_TYPE_INSTANCE_GET_PRIVATE ((master), \
				  MCD_TYPE_MASTER, \
				  McdMasterPrivate))

G_DEFINE_TYPE (McdMaster, mcd_master, MCD_TYPE_CONTROLLER);

typedef struct _McdMasterPrivate
{
    McdPresenceFrame *presence_frame;
    McdAccountManager *account_manager;
    McdDispatcher *dispatcher;
    McdProxy *proxy;
    McPresence awake_presence;
    gchar *awake_presence_message;
    McPresence default_presence;

    /* We create this for our member objects */
    TpDBusDaemon *dbus_daemon;
    
    /* Monitor for account enabling/disabling events */
    McAccountMonitor *account_monitor;

    /* if this flag is set, presence should go offline when all conversations
     * are closed */
    gboolean offline_on_idle;
    GHashTable *clients_needing_presence;

    GHashTable *extra_parameters;

    GPtrArray *plugins;

    gboolean is_disposed;
} McdMasterPrivate;

enum
{
    PROP_0,
    PROP_PRESENCE_FRAME,
    PROP_DBUS_CONNECTION,
    PROP_DISPATCHER,
    PROP_DEFAULT_PRESENCE,
};

static McdMaster *default_master = NULL;

static void
mcd_master_unload_plugins (McdMaster *master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GModule *module;
    gint i;

    for (i = 0; i < priv->plugins->len; i++)
    {
	module = g_ptr_array_index (priv->plugins, i);
	g_module_close (module);
    }
    g_ptr_array_free (priv->plugins, TRUE);
    priv->plugins = NULL;
}

static void
mcd_master_load_plugins (McdMaster *master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GDir *dir = NULL;
    GError *error = NULL;
    const gchar *name;

    dir = g_dir_open (MCD_DEFAULT_FILTER_PLUGIN_DIR, 0, &error);
    if (!dir)
    {
	g_debug ("Could not open plugin directory: %s", error->message);
	g_error_free (error);
	return;
    }

    priv->plugins = g_ptr_array_new ();
    while ((name = g_dir_read_name (dir)))
    {
	GModule *module;
	gchar *path;

	if (name[0] == '.' || !g_str_has_suffix (name, ".so")) continue;

	path = g_build_filename (MCD_DEFAULT_FILTER_PLUGIN_DIR, name, NULL);
	module = g_module_open (path, 0);
	g_free (path);
	if (module)
	{
	    McdPluginInitFunc init_func;
	    if (g_module_symbol (module, MCD_PLUGIN_INIT_FUNC,
				 (gpointer)&init_func))
	    {
		init_func ((McdPlugin *)master);
		g_ptr_array_add (priv->plugins, module);
	    }
	    else
		g_debug ("Error looking up symbol " MCD_PLUGIN_INIT_FUNC
			 " from plugin %s: %s", name, g_module_error ());
	}
	else
	{
	    g_debug ("Error opening plugin: %s: %s", name, g_module_error ());
	}
    }
    g_dir_close (dir);
}

static gboolean
exists_supporting_invisible (McdMasterPrivate *priv)
{
    McPresence *presences, *presence;
    gboolean found = FALSE;

    presences =
       	mc_account_monitor_get_supported_presences (priv->account_monitor);
    for (presence = presences; *presence; presence++)
	if (*presence == MC_PRESENCE_HIDDEN)
	{
	    found = TRUE;
	    break;
	}
    g_free (presences);
    return found;
}

static McPresence
_get_default_presence (McdMasterPrivate *priv)
{
    McPresence presence = priv->default_presence;

    if (presence == MC_PRESENCE_OFFLINE)
    {
	/* Map offline to hidden if supported */
	presence = exists_supporting_invisible (priv)?
	    MC_PRESENCE_HIDDEN : MC_PRESENCE_AWAY;
    }

    else if ((presence == MC_PRESENCE_HIDDEN) &&
	     (exists_supporting_invisible (priv) == FALSE))
    {
	/* Default presence was set to hidden/invisible but none of the
	 * accounts support it. Therefore use MC_PRESENCE_AWAY. */
	g_debug ("Default presence setting is hidden but none of the "
		 "accounts support it. Falling back to away.");
	presence = MC_PRESENCE_AWAY;
    }

    return presence;
}

static DBusHandlerResult
dbus_filter_func (DBusConnection *connection,
		  DBusMessage    *message,
		  gpointer        data)
{
    DBusHandlerResult result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    McdMasterPrivate *priv = (McdMasterPrivate *)data;

    if (dbus_message_is_signal (message,
				"org.freedesktop.DBus",
				"NameOwnerChanged")) {
	const gchar *name = NULL;
	const gchar *prev_owner = NULL;
	const gchar *new_owner = NULL;
	DBusError error = {0};

	dbus_error_init (&error);

	if (!dbus_message_get_args (message,
				    &error,
				    DBUS_TYPE_STRING,
				    &name,
				    DBUS_TYPE_STRING,
				    &prev_owner,
				    DBUS_TYPE_STRING,
				    &new_owner,
				    DBUS_TYPE_INVALID)) {

	    g_debug ("%s: error: %s", G_STRFUNC, error.message);
	    dbus_error_free (&error);

	    return result;
	}

	if (name && prev_owner && prev_owner[0] != '\0')
	{
	    if (g_hash_table_lookup (priv->clients_needing_presence, prev_owner))
	    {
		g_debug ("Process %s which requested default presence is dead", prev_owner);
		g_hash_table_remove (priv->clients_needing_presence, prev_owner);
		if (g_hash_table_size (priv->clients_needing_presence) == 0 &&
		    priv->offline_on_idle)
		{
		    mcd_presence_frame_request_presence (priv->presence_frame,
							 MC_PRESENCE_OFFLINE,
							 "No active processes");
		}
	    }
	}
    }

    return result;
}

static void
_mcd_master_connect (McdMission * mission)
{
    MCD_MISSION_CLASS (mcd_master_parent_class)->connect (mission);
    /*if (mission->main_presence.presence_enum != MC_PRESENCE_OFFLINE)
     * mcd_connect_all_accounts(mission); */

}

static void
_mcd_master_disconnect (McdMission * mission)
{
    g_debug ("%s", G_STRFUNC);

    MCD_MISSION_CLASS (mcd_master_parent_class)->disconnect (mission);
}

static void
_mcd_master_finalize (GObject * object)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (object);

    g_free (priv->awake_presence_message);

    G_OBJECT_CLASS (mcd_master_parent_class)->finalize (object);
}

static void
_mcd_master_get_property (GObject * obj, guint prop_id,
			  GValue * val, GParamSpec * pspec)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (obj);

    switch (prop_id)
    {
    case PROP_PRESENCE_FRAME:
	g_value_set_object (val, priv->presence_frame);
	break;
    case PROP_DISPATCHER:
	g_value_set_object (val, priv->dispatcher);
	break;
    case PROP_DBUS_CONNECTION:
	g_value_set_pointer (val,
			     TP_PROXY (priv->dbus_daemon)->dbus_connection);
	break;
    case PROP_DEFAULT_PRESENCE:
	g_value_set_uint (val, priv->default_presence);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_master_set_property (GObject *obj, guint prop_id,
			  const GValue *val, GParamSpec *pspec)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (obj);

    switch (prop_id)
    { 
    case PROP_DEFAULT_PRESENCE:
	priv->default_presence = g_value_get_uint (val);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_master_set_flags (McdMission * mission, McdSystemFlags flags)
{
    McdSystemFlags idle_flag_old, idle_flag_new;
    McdMasterPrivate *priv;

    g_return_if_fail (MCD_IS_MASTER (mission));
    priv = MCD_MASTER_PRIV (MCD_MASTER (mission));

    idle_flag_old = MCD_MISSION_GET_FLAGS_MASKED (mission, MCD_SYSTEM_IDLE);
    idle_flag_new = flags & MCD_SYSTEM_IDLE;
    
    if (idle_flag_old != idle_flag_new)
    {
	if (idle_flag_new)
	{
	    /* Save the current presence first */
	    priv->awake_presence =
		mcd_presence_frame_get_actual_presence (priv->presence_frame);
	    if (priv->awake_presence != MC_PRESENCE_AVAILABLE)
		return;
	    g_free (priv->awake_presence_message);
	    priv->awake_presence_message = g_strdup
		(mcd_presence_frame_get_actual_presence_message
		 (priv->presence_frame));

	    mcd_presence_frame_request_presence (priv->presence_frame,
						 MC_PRESENCE_AWAY, NULL);
	}
	else
	{    
	    mcd_presence_frame_request_presence (priv->presence_frame,
						 priv->awake_presence,
						 priv->awake_presence_message);
	}
    }
    MCD_MISSION_CLASS (mcd_master_parent_class)->set_flags (mission, flags);
}

static void
_mcd_master_dispose (GObject * object)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (object);
    
    if (priv->is_disposed)
    {
	return;
    }
    priv->is_disposed = TRUE;

    g_hash_table_destroy (priv->clients_needing_presence);

    if (priv->plugins)
    {
	mcd_master_unload_plugins (MCD_MASTER (object));
    }

    if (priv->account_manager)
    {
	g_object_unref (priv->account_manager);
	priv->account_manager = NULL;
    }

    if (priv->dbus_daemon)
    {
	DBusGConnection *dbus_connection;

	dbus_connection = TP_PROXY (priv->dbus_daemon)->dbus_connection;
	dbus_connection_remove_filter (dbus_g_connection_get_connection
				       (dbus_connection),
				       dbus_filter_func, priv);
	g_object_unref (priv->dbus_daemon);
	priv->dbus_daemon = NULL;
    }

    /* Don't unref() the dispatcher and the presence-frame: they will be
     * unref()ed by the McdProxy */
    priv->dispatcher = NULL;
    priv->presence_frame = NULL;
    g_object_unref (priv->proxy);

    G_OBJECT_CLASS (mcd_master_parent_class)->dispose (object);
}

static void
mcd_master_class_init (McdMasterClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    McdMissionClass *mission_class = MCD_MISSION_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdMasterPrivate));

    object_class->finalize = _mcd_master_finalize;
    object_class->get_property = _mcd_master_get_property;
    object_class->set_property = _mcd_master_set_property;
    object_class->dispose = _mcd_master_dispose;

    mission_class->connect = _mcd_master_connect;
    mission_class->disconnect = _mcd_master_disconnect;
    mission_class->set_flags = _mcd_master_set_flags;

    /* Properties */
    g_object_class_install_property (object_class,
				     PROP_PRESENCE_FRAME,
				     g_param_spec_object ("presence-frame",
							  _("Presence Frame Object"),
							  _("Presence frame Object used by connections to update presence"),
							  MCD_TYPE_PRESENCE_FRAME,
							  G_PARAM_READABLE));
    g_object_class_install_property (object_class,
				     PROP_DISPATCHER,
				     g_param_spec_object ("dispatcher",
							  _("Dispatcher Object"),
							  _("Dispatcher Object used to dispatch channels"),
							  MCD_TYPE_DISPATCHER,
							  G_PARAM_READABLE));
    g_object_class_install_property (object_class,
				     PROP_DBUS_CONNECTION,
				     g_param_spec_pointer ("dbus-connection",
							  _("D-Bus Connection"),
							  _("Connection to the D-Bus"),
							  G_PARAM_READABLE));
    g_object_class_install_property (object_class, PROP_DEFAULT_PRESENCE,
				     g_param_spec_uint ("default-presence",
						       _("Default presence"),
						       _("Default presence when connecting"),
						       0,
						       LAST_MC_PRESENCE,
						       0,
						       G_PARAM_READWRITE));
}

static void
install_dbus_filter (McdMasterPrivate *priv)
{
    DBusGConnection *dbus_connection;
    DBusConnection *dbus_conn;
    DBusError error;

    /* set up the NameOwnerChange filter */

    dbus_connection = TP_PROXY (priv->dbus_daemon)->dbus_connection;
    dbus_conn = dbus_g_connection_get_connection (dbus_connection);
    dbus_error_init (&error);
    dbus_connection_add_filter (dbus_conn,
				dbus_filter_func,
				priv, NULL);
    dbus_bus_add_match (dbus_conn,
			"type='signal'," "interface='org.freedesktop.DBus',"
			"member='NameOwnerChanged'", &error);
    if (dbus_error_is_set (&error))
    {
	g_warning ("Match rule adding failed");
	dbus_error_free (&error);
    }
}

static void
_g_value_free (gpointer data)
{
  GValue *value = (GValue *) data;
  g_value_unset (value);
  g_free (value);
}

static void
mcd_master_init (McdMaster * master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    DBusGConnection *dbus_connection;
    GError *error = NULL;

    if (!default_master)
	default_master = master;

    /* Initialize DBus connection */
    dbus_connection = dbus_g_bus_get (DBUS_BUS_STARTER, &error);
    if (dbus_connection == NULL)
    {
	g_printerr ("Failed to open connection to bus: %s", error->message);
	g_error_free (error);
	return;
    }
    priv->dbus_daemon = tp_dbus_daemon_new (dbus_connection);

    install_dbus_filter (priv);

    priv->presence_frame = mcd_presence_frame_new ();
    priv->dispatcher = mcd_dispatcher_new (priv->dbus_daemon, master);
    g_assert (MCD_IS_DISPATCHER (priv->dispatcher));
    /* propagate the signals to dispatcher and presence_frame, too */
    priv->proxy = mcd_proxy_new (MCD_MISSION (master));
    mcd_operation_take_mission (MCD_OPERATION (priv->proxy),
			       	MCD_MISSION (priv->presence_frame));
    mcd_operation_take_mission (MCD_OPERATION (priv->proxy),
			       	MCD_MISSION (priv->dispatcher));

    priv->clients_needing_presence = g_hash_table_new_full (g_str_hash,
							    g_str_equal,
							    g_free, NULL);

    priv->extra_parameters = g_hash_table_new_full (g_str_hash, g_str_equal,
						    g_free, _g_value_free);

    priv->account_manager = mcd_account_manager_new (priv->dbus_daemon);
    mcd_presence_frame_set_account_manager (priv->presence_frame,
					    priv->account_manager);

    mcd_master_load_plugins (master);
}

McdMaster *
mcd_master_get_default (void)
{
    if (!default_master)
	default_master = MCD_MASTER (g_object_new (MCD_TYPE_MASTER, NULL));
    return default_master;
}

static void
mcd_master_set_offline_on_idle (McdMaster *master, gboolean offline_on_idle)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    g_debug ("%s: setting offline_on_idle to %d", G_STRFUNC, offline_on_idle);
    priv->offline_on_idle = offline_on_idle;
}

void
mcd_master_request_presence (McdMaster * master,
			     McPresence presence,
			     const gchar * presence_message)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    mcd_presence_frame_request_presence (priv->presence_frame, presence,
					 presence_message);
    if (presence >= MC_PRESENCE_AVAILABLE)
	mcd_master_set_offline_on_idle (master, FALSE);
}

McPresence
mcd_master_get_actual_presence (McdMaster * master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    return mcd_presence_frame_get_actual_presence (priv->presence_frame);
}

gchar *
mcd_master_get_actual_presence_message (McdMaster * master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    return g_strdup (
	mcd_presence_frame_get_actual_presence_message (priv->presence_frame));
}

McPresence
mcd_master_get_requested_presence (McdMaster * master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    return mcd_presence_frame_get_requested_presence (priv->presence_frame);
}

gchar *
mcd_master_get_requested_presence_message (McdMaster * master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    return g_strdup (mcd_presence_frame_get_requested_presence_message (
						    priv->presence_frame));
}

gboolean
mcd_master_set_default_presence (McdMaster * master, const gchar *client_id)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    McPresence presence;

    presence = _get_default_presence (priv);
    if (presence == MC_PRESENCE_UNSET)
	return FALSE;

    if (client_id)
    {
	if (g_hash_table_lookup (priv->clients_needing_presence, client_id) == NULL)
	{
	    g_debug ("New process requesting default presence (%s)", client_id);
	    g_hash_table_insert (priv->clients_needing_presence,
				 g_strdup (client_id), GINT_TO_POINTER(1));
	}
    }

    if (mcd_presence_frame_get_actual_presence (priv->presence_frame)
       	>= MC_PRESENCE_AVAILABLE ||
	!mcd_presence_frame_is_stable (priv->presence_frame) ||
	/* if we are not connected the presence frame will always be stable,
	 * but this doesn't mean we must accept this request; maybe another one
	 * is pending */
	(!mcd_mission_is_connected (MCD_MISSION (master)) &&
	 mcd_presence_frame_get_requested_presence (priv->presence_frame)
	 >= MC_PRESENCE_AVAILABLE))
    {
	g_debug ("%s: Default presence requested while connected or "
		 "already connecting", G_STRFUNC);
	return FALSE;
    }
    mcd_master_set_offline_on_idle (master, TRUE);
    mcd_presence_frame_request_presence (priv->presence_frame, presence, NULL);
    return TRUE;
}

TpConnectionStatus
mcd_master_get_account_status (McdMaster * master, gchar * account_name)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    TpConnectionStatus status;
    McdAccount *account;

    account = mcd_account_manager_lookup_account (priv->account_manager,
						  account_name);
    if (account)
    {
	status = mcd_account_get_connection_status (account);
    }
    else
	status = TP_CONNECTION_STATUS_DISCONNECTED;
    return status;
}

gboolean
mcd_master_get_online_connection_names (McdMaster * master,
					gchar *** connected_names)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GList *account_list, *account_node;
    GPtrArray *names = g_ptr_array_new ();

    account_list = mcd_presence_frame_get_accounts (priv->presence_frame);
    for (account_node = account_list; account_node != NULL;
	 account_node = account_node->next)
    {
	McdAccount *account = account_node->data;


	if (mcd_account_get_connection_status (account) ==
	    TP_CONNECTION_STATUS_CONNECTED)
	{
	    g_ptr_array_add (names,
			     g_strdup (mcd_account_get_unique_name (account)));
	}
    }

    if (names->len != 0)
    {
	int i;

	/* Copy the collected names to the array of strings */
	*connected_names =
	    (gchar **) g_malloc0 (sizeof (gchar *) * (names->len + 1));
	for (i = 0; i < names->len; i++)
	{
	    *(*connected_names + i) = g_ptr_array_index (names, i);
	}
	(*connected_names)[i] = NULL;
    }
    g_ptr_array_free (names, TRUE);
    return TRUE;
}

gboolean
mcd_master_get_account_connection_details (McdMaster * master,
					   const gchar * account_name,
					   gchar ** servname, gchar ** objpath)
{
    g_warning ("%s not implemented", G_STRFUNC);
    return FALSE;
}

gboolean
mcd_master_request_channel (McdMaster *master,
			    const struct mcd_channel_request *req,
			    GError ** error)
{
#if 0
    const GList *managers, *node;
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    g_return_val_if_fail (MCD_IS_MASTER (master), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
    
    /* Low memory ? */
    if (MCD_MISSION_GET_FLAGS_MASKED (MCD_MISSION (master),
				      MCD_SYSTEM_MEMORY_CONSERVED))
    {
	g_warning ("Device is in lowmem state, will not create a channel");
	if (error)
	    g_set_error (error, MC_ERROR, MC_LOWMEM_ERROR, "Low memory");
	return FALSE;
    }
    
    /* First find out the right manager */
    managers = mcd_operation_get_missions (MCD_OPERATION (master));
    
    /* If there are no accounts, error */
    if (managers == NULL)
    {
	if (error)
	{
	    g_set_error (error, MC_ERROR, MC_NO_ACCOUNTS_ERROR,
			 "No accounts configured");
	}
	g_warning ("No accounts configured");
	
	/* Nothing to do. Just exit */
	mcd_controller_shutdown (MCD_CONTROLLER (master),
				 "No accounts configured");
	return FALSE;
    }
 
    /* make sure we are online, or will be */
    if (mcd_presence_frame_get_actual_presence (priv->presence_frame) <= MC_PRESENCE_AVAILABLE &&
	mcd_presence_frame_is_stable (priv->presence_frame))
    {
	g_debug ("%s: requesting default presence", G_STRFUNC);
	mcd_master_set_default_presence (master, req->requestor_client_id);
    }

    node = managers;
    while (node)
    {
	if (mcd_manager_get_account_by_name (MCD_MANAGER (node->data),
					     req->account_name))
	{
	    /* FIXME: handle error correctly */
	    if (!mcd_manager_request_channel (MCD_MANAGER (node->data),
					      req, error))
	    {
		g_assert (error == NULL || *error != NULL);
		return FALSE;
	    }
	    g_assert (error == NULL || *error == NULL);
	    return TRUE;
	}
	node = node->next;
    }
    
    /* Manager not found */
    if (error)
    {
	g_set_error (error, MC_ERROR, MC_NO_MATCHING_CONNECTION_ERROR,
		     "No matching manager found for account %s",
		     req->account_name);
    }
    g_warning ("No matching manager found for account %s", req->account_name);
#else
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    McdAccount *account;

    account = mcd_account_manager_lookup_account (priv->account_manager,
						  req->account_name);
    if (!account)
    {
	if (error)
	{
	    g_set_error (error, MC_ERROR, MC_INVALID_ACCOUNT_ERROR,
			 "No such account %s", req->account_name);
	}
	return FALSE;
    }
    return mcd_account_request_channel_nmc4 (account, req, error);
#endif
}

gboolean
mcd_master_cancel_channel_request (McdMaster *master, guint operation_id,
				   const gchar *requestor_client_id,
				   GError **error)
{
    const GList *managers, *node;

    g_return_val_if_fail (MCD_IS_MASTER (master), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
    
    /* First find out the right manager */
    managers = mcd_operation_get_missions (MCD_OPERATION (master));
    if (!managers) return FALSE;

    for (node = managers; node; node = node->next)
    {
	if (mcd_manager_cancel_channel_request (MCD_MANAGER (node->data),
						operation_id,
						requestor_client_id,
						error))
	    return TRUE;
    }

    return FALSE;
}

gboolean
mcd_master_get_used_channels_count (McdMaster *master, guint chan_type,
				    guint * ret, GError ** error)
{
    McdMasterPrivate *priv;
    
    g_return_val_if_fail (ret != NULL, FALSE);
    
    priv = MCD_MASTER_PRIV (master);
    *ret = mcd_dispatcher_get_channel_type_usage (priv->dispatcher,
						  chan_type);
    return TRUE;
}

McdConnection *
mcd_master_get_connection (McdMaster *master, const gchar *object_path,
			   GError **error)
{
    McdConnection *connection;
    const GList *managers, *node;
    
    g_return_val_if_fail (MCD_IS_MASTER (master), NULL);
    
    managers = mcd_operation_get_missions (MCD_OPERATION (master));
    
    /* MC exits if there aren't any accounts */
    if (managers == NULL)
    {
	if (error)
	{
	    g_set_error (error, MC_ERROR, MC_NO_ACCOUNTS_ERROR,
			 "No accounts configured");
	}
	mcd_controller_shutdown (MCD_CONTROLLER (master),
				 "No accounts configured");
	return NULL;
    }
    
    node = managers;
    while (node)
    {
	connection = mcd_manager_get_connection (MCD_MANAGER (node->data),
						 object_path);
	if (connection)
	    return connection;
	node = node->next;
    }
    
    /* Manager not found */
    if (error)
    {
	g_set_error (error, MC_ERROR, MC_NO_MATCHING_CONNECTION_ERROR,
		     "No matching manager found for connection '%s'",
		     object_path);
    }
    return NULL;
}

gboolean
mcd_master_get_account_for_connection (McdMaster *master,
				       const gchar *object_path,
				       gchar **ret_unique_name,
				       GError **error)
{
    McdConnection *connection;
    McdAccount *account;

    connection = mcd_master_get_connection (master, object_path, error);
    if (connection &&
	(account = mcd_connection_get_account (connection)))
    {

	*ret_unique_name = g_strdup (mcd_account_get_unique_name (account));
	return TRUE;
    }
    return FALSE;
}

void
mcd_master_set_default_presence_setting (McdMaster *master,
					 McPresence presence)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    priv->default_presence = presence;
}

/**
 * mcd_master_add_connection_parameter:
 * @master: the #McdMaster.
 * @name: the name of the parameter to add.
 * @value: a #GValue.
 *
 * Set a global connection parameter to be passed to all connection managers
 * (which support this parameter).  If called twice for the same parameter, the
 * new value will replace the previous one.
 */
void
mcd_master_add_connection_parameter (McdMaster *master, const gchar *name,
				     const GValue *value)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GValue *val;

    g_return_if_fail (name != NULL);
    g_return_if_fail (value != NULL);

    val = g_malloc0 (sizeof (GValue));
    g_value_init (val, G_VALUE_TYPE (value));
    g_value_copy (value, val);
    g_hash_table_replace (priv->extra_parameters, g_strdup (name), val);
}

static void
copy_parameter (gpointer key, gpointer value, gpointer userdata)
{
    GHashTable *dest = (GHashTable *)userdata;

    g_hash_table_insert (dest, key, value);
}

/**
 * mcd_master_get_connection_parameters:
 * @master: the #McdMaster.
 *
 * Get the global connections parameters.
 *
 * Returns: the #GHashTable of the parameters. It has to be destroyed when no
 * longer needed.
 */
GHashTable *
mcd_master_get_connection_parameters (McdMaster *master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GHashTable *ret;
    
    ret = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_foreach (priv->extra_parameters, copy_parameter, ret);
    return ret;
}

/**
 * mcd_master_lookup_manager:
 * @master: the #McdMaster.
 * @unique_name: the name of the manager.
 *
 * Gets the manager whose name is @unique_name. If the manager object doesn't
 * exists yet, it is created.
 *
 * Returns: a #McdManager. Caller must call g_object_ref() on it to ensure it
 * will stay alive as long as needed.
 */
McdManager *
mcd_master_lookup_manager (McdMaster *master,
			   const gchar *unique_name)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    const GList *managers, *list;
    McdManager *manager;

    managers = mcd_operation_get_missions (MCD_OPERATION (master));
    for (list = managers; list; list = list->next)
    {
	manager = MCD_MANAGER (list->data);
	if (strcmp (unique_name,
		    mcd_manager_get_name (manager)) == 0)
	    return manager;
    }

    manager = mcd_manager_new (unique_name, priv->presence_frame,
			       priv->dispatcher, priv->dbus_daemon);
    if (G_UNLIKELY (!manager))
	g_warning ("Manager %s not created", unique_name);
    else
	mcd_operation_take_mission (MCD_OPERATION (master),
				    MCD_MISSION (manager));

    return manager;
}

/**
 * mcd_plugin_get_dispatcher:
 * @plugin: the #McdPlugin
 * 
 * Gets the McdDispatcher, to be used for registering channel filters. The
 * reference count of the returned object is not incremented, and the object is
 * guaranteed to stay alive during the whole lifetime of the plugin.
 *
 * Returns: the #McdDispatcher
 */
McdDispatcher *
mcd_plugin_get_dispatcher (McdPlugin *plugin)
{
    return MCD_MASTER_PRIV (plugin)->dispatcher;
}

