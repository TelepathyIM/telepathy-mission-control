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

#define MCD_MASTER_PRIV(master) (G_TYPE_INSTANCE_GET_PRIVATE ((master), \
				  MCD_TYPE_MASTER, \
				  McdMasterPrivate))

G_DEFINE_TYPE (McdMaster, mcd_master, MCD_TYPE_CONTROLLER);

typedef struct _McdMasterPrivate
{
    McdPresenceFrame *presence_frame;
    McdDispatcher *dispatcher;
    McdProxy *proxy;
    McPresence awake_presence;
    const gchar *awake_presence_message;
    McPresence default_presence;

    /* We create this for our member objects */
    DBusGConnection *dbus_connection;
    
    /* Monitor for account enabling/disabling events */
    McAccountMonitor *account_monitor;

    /* if this flag is set, presence should go offline when all conversations
     * are closed */
    gboolean offline_on_idle;
    GHashTable *clients_needing_presence;

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

static void
_mcd_master_init_managers (McdMaster * master)
{
    GList *acct, *acct_head;
    GHashTable *mc_managers;
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    /* FIXME: Should get only _supported_ protocols */
    /* Only enabled accounts are read in */

    mc_managers = g_hash_table_new (g_direct_hash, g_direct_equal);

    /* Deal with all enabled accounts */
    acct_head = mc_accounts_list_by_enabled (TRUE);

    /* Let the presence frame know what accounts we have */
    mcd_presence_frame_set_accounts (priv->presence_frame, acct_head);

    for (acct = acct_head; acct; acct = g_list_next (acct))
    {
	McAccount *account;
	McProfile *profile;
	McProtocol *protocol;
	McManager *mc_manager;

	account = acct ? acct->data : NULL;
	profile = account ? mc_account_get_profile (account) : NULL;
	protocol = profile ? mc_profile_get_protocol (profile) : NULL;
	mc_manager = protocol ? mc_protocol_get_manager (protocol) : NULL;

	if (mc_manager)
	{
	    McdManager *manager;
	    manager = g_hash_table_lookup (mc_managers, mc_manager);

	    if (!manager)
	    {
		manager =
		    mcd_manager_new (mc_manager, priv->presence_frame,
				     priv->dispatcher, priv->dbus_connection);
		g_hash_table_insert (mc_managers, mc_manager, manager);
		mcd_operation_take_mission (MCD_OPERATION (master),
					    MCD_MISSION (manager));
	    }
	    mcd_manager_add_account (manager, account);

	    g_debug ("%s: Added account:\n\tName\t\"%s\"\n\tProfile\t\"%s\""
		     "\n\tProto\t\"%s\"\n\tManager\t\"%s\"",
		     G_STRFUNC,
		     mc_account_get_unique_name (account),
		     mc_profile_get_unique_name (profile),
		     mc_protocol_get_name (protocol),
		     mc_manager_get_unique_name (mc_manager));
	}
	else
	{
	    g_warning ("%s: Cannot add account:\n\tName\t\"%s\"\n\tProfile\t"
		       "\"%s\"\n\tProto\t\"%s\"\n\tManager\t\"%s\"",
		       G_STRFUNC,
		       account ? mc_account_get_unique_name (account) :
		       "NONE",
		       profile ? mc_profile_get_unique_name (profile) :
		       "NONE",
		       protocol ? mc_protocol_get_name (protocol) : "NONE",
		       mc_manager ?
		       mc_manager_get_unique_name (mc_manager) : "NONE");
	}

	if (profile)
	    g_object_unref (profile);
	if (protocol)
	    g_object_unref (protocol);
	if (mc_manager)
	    g_object_unref (mc_manager);
	/* if (account)
	    g_object_unref (account); */
    }				/*for */
    g_list_free (acct_head);
    g_hash_table_destroy (mc_managers);
}

static gint
_manager_has_account (McdManager * manager, McAccount * account)
{
    const GList *accounts;
    const GList *account_node;
    
    accounts = mcd_manager_get_accounts (manager);
    account_node = g_list_find ((GList *) accounts, account);

    if (account_node)
    {
	return 0;
    }

    else
    {
	return 1;
    }
}

static McdManager *
_mcd_master_find_manager (McdMaster * master, McAccount * account)
{
    const GList *managers;
    const GList *manager_node;

    managers = mcd_operation_get_missions (MCD_OPERATION (master));
    manager_node =
	g_list_find_custom ((GList*)managers, account,
			    (GCompareFunc) _manager_has_account);

    if (manager_node)
    {
	return MCD_MANAGER (manager_node->data);
    }

    else
    {
	return NULL;
    }
}

static gint
_is_manager_responsible (McdManager * manager, McAccount * account)
{
    gboolean can_handle = 
        mcd_manager_can_handle_account (manager, account);

    if (can_handle)
    {
	return 0;
    }
    else
    {
	return 1;
    }
}

static McdManager *
_mcd_master_find_potential_manager (McdMaster * master, McAccount * account)
{
    const GList *managers;
    const GList *manager_node;

    managers = mcd_operation_get_missions (MCD_OPERATION (master));
    manager_node =
	g_list_find_custom ((GList*)managers, account,
			    (GCompareFunc) _is_manager_responsible);

    if (manager_node)
    {
	return MCD_MANAGER (manager_node->data);
    }

    else
    {
	return NULL;
    }
}

/* Reads in account's settings if they aren't in the hash table
   already and (re)connects the account. */
static void
_mcd_master_on_account_enabled (McAccountMonitor * monitor,
			        gchar * account_name, gpointer user_data)
{
    McdMaster *master = MCD_MASTER (user_data);
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    McdManager *manager;
    McAccount *account;

    g_debug ("Account %s enabled", account_name);

    account = mc_account_lookup (account_name);
    manager = _mcd_master_find_potential_manager (master, account);

    if (manager == NULL)
    {
	McProfile *profile;
	McProtocol *protocol;
	McManager *mc_manager;

        g_debug ("%s: manager not found, creating a new one", G_STRFUNC);
	profile = account ? mc_account_get_profile (account) : NULL;
	protocol = profile ? mc_profile_get_protocol (profile) : NULL;
	mc_manager = protocol ? mc_protocol_get_manager (protocol) : NULL;

	if (mc_manager)
	{
            manager =
                mcd_manager_new (mc_manager, priv->presence_frame,
                                 priv->dispatcher, priv->dbus_connection);
            mcd_operation_take_mission (MCD_OPERATION (master),
                                 MCD_MISSION (manager));
        }
        else
        {
	    g_warning ("%s: Failed to get the manager for the account:"
                       "\n\tName\t\"%s\"\n\tProfile\t\"%s\"\n\tProto"
                       "\t\"%s\"\n\tManager\t\"%s\"",
		       G_STRFUNC,
		       account ? mc_account_get_unique_name (account) :
		       "NONE",
		       profile ? mc_profile_get_unique_name (profile) :
		       "NONE",
		       protocol ? mc_protocol_get_name (protocol) : "NONE",
		       mc_manager ?
		       mc_manager_get_unique_name (mc_manager) : "NONE");
        }
    
        if (profile)
            g_object_unref (profile);
        if (protocol)
            g_object_unref (protocol);
        if (mc_manager)
            g_object_unref (mc_manager);
    }

    if (manager != NULL)
    {
        g_debug ("adding account to manager and presence_frame");
        mcd_presence_frame_add_account (priv->presence_frame, account);
        mcd_manager_add_account (manager, account);
    }

    if (account)
        g_object_unref (account);
}

static void
_mcd_master_on_account_disabled (McAccountMonitor * monitor,
			        gchar * account_name, gpointer user_data)
{
    McdMaster *master = MCD_MASTER (user_data);
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    McdManager *manager;
    McAccount *account;

    g_debug ("Account %s disabled", account_name);

    account = mc_account_lookup (account_name);
    
    manager = _mcd_master_find_manager (master, account);

    if (manager != NULL)
    {
        g_debug ("removing account from manager");
        mcd_manager_remove_account (manager, account);
    }

    g_debug ("%s: removing account %s from presence_frame %p", 
             G_STRFUNC, 
             mc_account_get_unique_name (account),
             priv->presence_frame);
    mcd_presence_frame_remove_account (priv->presence_frame, account);

    if (account)
        g_object_unref (account);
}

static void
_mcd_master_on_account_changed (McAccountMonitor * monitor,
			        gchar * account_name, McdMaster *master)
{
    McdManager *manager;
    McAccount *account;

    g_debug ("Account %s changed", account_name);

    account = mc_account_lookup (account_name);
    if (!account) return;
    manager = _mcd_master_find_manager (master, account);

    if (manager)
    {
	McdConnection *connection;
       
	connection = mcd_manager_get_account_connection (manager, account);
	if (connection)
	    mcd_connection_account_changed (connection);
    }

    g_object_unref (account);
}

static void
_mcd_master_on_param_changed (McAccountMonitor *monitor, gchar *account_name,
			      gchar *param, McdMaster *master)
{
    McdManager *manager;
    McAccount *account;

    g_debug ("Account %s changed param %s", account_name, param);

    account = mc_account_lookup (account_name);
    if (!account) return;
    manager = _mcd_master_find_manager (master, account);

    if (manager)
    {
	McdConnection *connection;
       
	connection = mcd_manager_get_account_connection (manager, account);
	if (connection)
	    mcd_connection_restart (connection);
    }

    g_object_unref (account);
}

static void
_mcd_master_init_account_monitoring (McdMaster * master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    
    priv->account_monitor = mc_account_monitor_new ();
    g_signal_connect (priv->account_monitor,
		      "account-enabled",
		      (GCallback) _mcd_master_on_account_enabled, master);
    g_signal_connect (priv->account_monitor,
		      "account-disabled",
		      (GCallback) _mcd_master_on_account_disabled, master);
    g_signal_connect (priv->account_monitor,
		      "account-changed",
		      (GCallback) _mcd_master_on_account_changed, master);
    g_signal_connect (priv->account_monitor,
		      "param-changed",
		      (GCallback) _mcd_master_on_param_changed, master);
}

static void
_mcd_master_dispose_account_monitoring (McdMaster * master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    
    g_signal_handlers_disconnect_by_func (priv->account_monitor,
		      (GCallback) _mcd_master_on_account_enabled, master);
    g_signal_handlers_disconnect_by_func (priv->account_monitor,
		      (GCallback) _mcd_master_on_account_disabled, master);
    g_signal_handlers_disconnect_by_func (priv->account_monitor,
		      (GCallback) _mcd_master_on_account_changed, master);
    g_signal_handlers_disconnect_by_func (priv->account_monitor,
		      (GCallback) _mcd_master_on_param_changed, master);
    g_object_unref (priv->account_monitor);
    priv->account_monitor = NULL;
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
    McdMaster *master;
    master = MCD_MASTER (object);
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
	g_value_set_pointer (val, priv->dbus_connection);
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
	    priv->awake_presence_message =
		mcd_presence_frame_get_actual_presence_message (priv->presence_frame);

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

    if (priv->dbus_connection)
    {
	dbus_connection_remove_filter (dbus_g_connection_get_connection
				       (priv->dbus_connection),
				       dbus_filter_func, priv);

	/* Flush all outgoing DBUS messages and signals */
	dbus_g_connection_flush (priv->dbus_connection);
	dbus_g_connection_unref (priv->dbus_connection);
	priv->dbus_connection = NULL;
    }

    /* Don't unref() the dispatcher and the presence-frame: they will be
     * unref()ed by the McdProxy */
    priv->dispatcher = NULL;
    priv->presence_frame = NULL;
    g_object_unref (priv->proxy);

    if (priv->account_monitor)
	_mcd_master_dispose_account_monitoring (MCD_MASTER (object));

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
    DBusConnection *dbus_conn;
    DBusError error;

    /* set up the NameOwnerChange filter */
    dbus_conn = dbus_g_connection_get_connection (priv->dbus_connection);
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
mcd_master_init (McdMaster * master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GError *error = NULL;
    /* Initialize DBus connection */
    priv->dbus_connection = dbus_g_bus_get (DBUS_BUS_STARTER, &error);
    if (priv->dbus_connection == NULL)
    {
	g_printerr ("Failed to open connection to bus: %s", error->message);
	g_error_free (error);
	return;
    }

    install_dbus_filter (priv);

    priv->presence_frame = mcd_presence_frame_new ();
    priv->dispatcher = mcd_dispatcher_new (priv->dbus_connection, master);
    g_assert (MCD_IS_DISPATCHER (priv->dispatcher));
    /* propagate the signals to dispatcher and presence_frame, too */
    priv->proxy = mcd_proxy_new (MCD_MISSION (master));
    mcd_operation_take_mission (MCD_OPERATION (priv->proxy),
			       	MCD_MISSION (priv->presence_frame));
    mcd_operation_take_mission (MCD_OPERATION (priv->proxy),
			       	MCD_MISSION (priv->dispatcher));

    _mcd_master_init_managers (master);
    
    /* Listen for account enable/disable events */
    _mcd_master_init_account_monitoring (master);

    priv->clients_needing_presence = g_hash_table_new_full (g_str_hash,
							    g_str_equal,
							    g_free, NULL);
}

McdMaster *
mcd_master_new (void)
{
    McdMaster *obj;
    obj = MCD_MASTER (g_object_new (MCD_TYPE_MASTER, NULL));
    return obj;
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

TelepathyConnectionStatus
mcd_master_get_account_status (McdMaster * master, gchar * account_name)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    TelepathyConnectionStatus status;
    McAccount *account;

    account = mc_account_lookup (account_name);
    if (account)
    {
	status = mcd_presence_frame_get_account_status (priv->presence_frame,
						      account);
	g_object_unref (account);
    }
    else
	status = TP_CONN_STATUS_DISCONNECTED;
    return status;
}

gboolean
mcd_master_get_online_connection_names (McdMaster * master,
					gchar *** connected_names)
{
    GList *accounts;
    gboolean ret;

    accounts = mc_accounts_list_by_enabled (TRUE);

    /* MC exits if there aren't any accounts */
    if (accounts)
    {
	McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
	GPtrArray *names = g_ptr_array_new ();
	GList *account_node;

	/* Iterate through all connected accounts */
	for (account_node = accounts; account_node;
	     account_node = g_list_next (account_node))
	{
	    McAccount *account = account_node->data;
	    TelepathyConnectionStatus status;

	    status =
		mcd_presence_frame_get_account_status (priv->presence_frame,
						       account);
	    /* Ensure that only accounts that are actually conntected are added to
	     * the pointer array. */

	    if (status == TP_CONN_STATUS_CONNECTED)
	    {
		g_ptr_array_add (names,
				 g_strdup (mc_account_get_unique_name
					   (account)));
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

	    ret = TRUE;
	}

	else
	{
	    ret = FALSE;
	}

	g_ptr_array_free (names, TRUE);
	g_list_free (accounts);
    }

    else
    {
	ret = FALSE;
    }

    return ret;
}

gboolean
mcd_master_get_account_connection_details (McdMaster * master,
					   const gchar * account_name,
					   gchar ** servname, gchar ** objpath)
{
    McAccount *account;
    McdManager *manager;
    McdConnection *connection;
    gboolean ret = FALSE;

    account = mc_account_lookup (account_name);
    if (account)
    {
	manager = _mcd_master_find_manager (master, account);
	connection =
	    manager ? mcd_manager_get_account_connection (manager, account) : NULL;
	g_object_unref (account);

	if (connection)
	    ret =
		mcd_connection_get_telepathy_details (connection, servname,
						      objpath);
    }

    return ret;
}

gboolean
mcd_master_request_channel (McdMaster *master,
			    const struct mcd_channel_request *req,
			    GError ** error)
{
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
	mcd_master_set_default_presence (master, NULL);
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
    return FALSE;
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
mcd_master_cancel_last_presence_request (McdMaster * master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    return mcd_presence_frame_cancel_last_request (priv->presence_frame);
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

    connection = mcd_master_get_connection (master, object_path, error);
    if (connection)
    {
	McAccount *account;
	
	g_object_get (G_OBJECT (connection), "account", &account, NULL);
	*ret_unique_name = g_strdup (mc_account_get_unique_name (account));
	g_object_unref (G_OBJECT (account));
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

