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
 * SECTION:mcd-connection
 * @title: McdConnection
 * @short_description: Connection class representing Telepathy connection class
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-connection.h
 * 
 * FIXME
 */

#include <string.h>
#include <sys/types.h>
#include <sched.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

#include <libmissioncontrol/mc-manager.h>
#include <libmissioncontrol/mc-account.h>
#include <libmissioncontrol/mc-protocol.h>
#include <libmissioncontrol/mc-profile.h>
#include <libtelepathy/tp-connmgr.h>
#include <libtelepathy/tp-interfaces.h>
#include <libtelepathy/tp-constants.h>
#include <libtelepathy/tp-chan.h>
#include <libtelepathy/tp-conn.h>
#include <libtelepathy/tp-conn-gen.h>
#include <libtelepathy/tp-conn-iface-presence-gen.h>
#include <libtelepathy/tp-conn-iface-capabilities-gen.h>
#include <libtelepathy/tp-conn-iface-avatars-gen.h>
#include <libtelepathy/tp-conn-iface-aliasing-gen.h>
#include <libtelepathy/tp-helpers.h>

#include "mcd-connection.h"
#include "mcd-channel.h"
#include "mcd-provisioning-factory.h"

#define MAX_REF_PRESENCE 4

#define MCD_CONNECTION_PRIV(mcdconn) (G_TYPE_INSTANCE_GET_PRIVATE ((mcdconn), \
				      MCD_TYPE_CONNECTION, \
				      McdConnectionPrivate))

G_DEFINE_TYPE (McdConnection, mcd_connection, MCD_TYPE_OPERATION);

/* Private */
typedef struct
{
    /* DBUS connection */
    DBusGConnection *dbus_connection;

    /* DBus bus name */
    gchar *bus_name;

    /* Presence frame */
    McdPresenceFrame *presence_frame;

    /* Channel dispatcher */
    McdDispatcher *dispatcher;

    McdProvisioning *provisioning;

    /* Account */
    McAccount *account;

    /* Associated profile */
    /* McProfile *profile; */

    /* Telepathy connection manager */
    TpConnMgr *tp_conn_mgr;

    /* Telepathy connection */
    TpConn *tp_conn;
    guint self_handle;

    /* Presence proxy */
    DBusGProxy *presence_proxy;

    DBusGProxy *avatars_proxy;
    DBusGProxy *alias_proxy;

    /* Capabilities proxy */
    DBusGProxy *capabilities_proxy;
    
    /* Capabilities timer */
    guint capabilities_timer;

    guint reconnect_timer; 	/* timer for reconnection */
    guint reconnect_interval;
    gboolean reconnection_requested;

    /* Supported presences */
    GArray *recognized_presence_info_array;
    struct presence_info *presence_to_set[LAST_MC_PRESENCE - 1];

    /* List of pending channels which has been requested to telepathy,
     * but telepathy hasn't yet responded with the channel object
     */
    GHashTable *pending_channels;
    
    TelepathyConnectionStatusReason abort_reason;
    gboolean got_capabilities;

    gchar *alias;

    gboolean is_disposed;
    
} McdConnectionPrivate;

struct presence_info
{
    gchar *presence_str;
    gboolean allow_message;
};

enum
{
    PROP_0,
    PROP_DBUS_CONNECTION,
    PROP_BUS_NAME,
    PROP_TP_MANAGER,
    PROP_TP_CONNECTION,
    PROP_ACCOUNT,
    PROP_PRESENCE_FRAME,
    PROP_DISPATCHER,
};

/* This table lists the Telepathy well-known statuses and the corresponding
 * McPresence values; the order in which the items appear is only important for
 * those statuses which map to the same McPresence value: for them, the first
 * ones will be preferred. */
static const struct _presence_mapping {
    gchar *presence_str;
    McPresence mc_presence;
} presence_mapping[] = {
    { "offline", MC_PRESENCE_OFFLINE },
    { "available", MC_PRESENCE_AVAILABLE },
    { "away", MC_PRESENCE_AWAY },
    { "xa", MC_PRESENCE_EXTENDED_AWAY },
    { "hidden", MC_PRESENCE_HIDDEN },
    { "dnd", MC_PRESENCE_DO_NOT_DISTURB },
    { "brb", MC_PRESENCE_AWAY },
    { "busy", MC_PRESENCE_DO_NOT_DISTURB },
    { NULL, 0 },
};

static const McPresence fallback_presence
    [LAST_MC_PRESENCE - 1][MAX_REF_PRESENCE] = {
    { 0 },	/* MC_PRESENCE_OFFLINE */
    { 0 },	/* MC_PRESENCE_AVAILABLE */
    { MC_PRESENCE_AVAILABLE, 0 },	/* MC_PRESENCE_AWAY */
    { MC_PRESENCE_AWAY, MC_PRESENCE_AVAILABLE, 0 },	/* MC_PRESENCE_EXTENDED_AWAY */
    { MC_PRESENCE_DO_NOT_DISTURB, MC_PRESENCE_EXTENDED_AWAY, MC_PRESENCE_AVAILABLE, 0 }, /* MC_PRESENCE_HIDDEN */
    { 0 }	/* MC_PRESENCE_DO_NOT_DISTURB */
};

struct request_id {
    guint requestor_serial;
    const gchar *requestor_client_id;
};

struct capabilities_wait_data {
    GError *error; /* error originally received when channel request failed */
    McdChannel *channel;
    McdConnectionPrivate *priv;
};

static void mcd_async_request_chan_callback (DBusGProxy *proxy,
					     gchar *channel_path,
					     GError *error,
					     gpointer user_data);
static GError * map_tp_error_to_mc_error (McdChannel *channel, GError *tp_error);
static void _mcd_connection_setup (McdConnection * connection);
static void _mcd_connection_release_tp_connection (McdConnection *connection);

static McPresence presence_str_to_enum (const gchar *presence_str)
{
    const struct _presence_mapping *mapping;
    for (mapping = presence_mapping; mapping->presence_str; mapping++)
	if (strcmp (presence_str, mapping->presence_str) == 0)
	    return mapping->mc_presence;
    return MC_PRESENCE_UNSET;
}

/* Free dynamic members and presence_info itself */
static void
_mcd_connection_free_presence_info (McdConnection * conn)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (conn);

    if (priv->recognized_presence_info_array != NULL)
    {
	struct presence_info *pi;
	gint i;

	for (i = 0; i < priv->recognized_presence_info_array->len; i++)
	{
	    pi = &g_array_index (priv->recognized_presence_info_array,
				 struct presence_info, i);
	    g_free (pi->presence_str);
	}
	g_array_free (priv->recognized_presence_info_array, TRUE);
	priv->recognized_presence_info_array = NULL;
    }
}

/* Fill empty presence_to_set elements with fallback presence values */
static void
_mcd_connection_set_fallback_presences (McdConnection * connection, gint i)
{
    gint j;
    McdConnectionPrivate *priv;

    g_return_if_fail (MCD_IS_CONNECTION (connection));

    priv = MCD_CONNECTION_PRIV (connection);

    for (j = 0; j < MAX_REF_PRESENCE; j++)
    {
	struct presence_info *presence;

	if (fallback_presence[i][j] == 0) break;
	presence = priv->presence_to_set[fallback_presence[i][j] - 1];
	if (presence != NULL)
	{
	    priv->presence_to_set[i] = presence;
	    g_debug ("Fallback for McPresence %d set to %s",
		     i + 1, presence->presence_str);
	    return;
	}
    }
}

/* Used for initializing recognized_presence_info_array. */
static void
recognize_presence (gpointer key, gpointer value, gpointer user_data)
{
    guint telepathy_enum;
    GHashTable *ht;
    struct presence_info pi;
    GValueArray *status;
    McdConnectionPrivate *priv = (McdConnectionPrivate *) user_data;
    gint j;

    status = (GValueArray *) value;

    /* Pull out the arguments of Telepathy GetStatuses */
    ht = (GHashTable *) g_value_get_boxed (g_value_array_get_nth (status, 3));
    pi.allow_message = g_hash_table_lookup (ht, "message") ? TRUE : FALSE;

    /* Look up which MC_PRESENCE this presence string corresponds */
    pi.presence_str = g_strdup ((const gchar *) key);

    j = presence_str_to_enum (pi.presence_str);
    if (j == MC_PRESENCE_UNSET)
    {
	/* Didn't find match by comparing strings so map using the telepathy enum. */
	telepathy_enum = g_value_get_uint (g_value_array_get_nth (status, 0));
	switch (telepathy_enum)
	{
	case TP_CONN_PRESENCE_TYPE_OFFLINE:
	    j = MC_PRESENCE_OFFLINE;
	    break;
	case TP_CONN_PRESENCE_TYPE_AVAILABLE:
	    j = MC_PRESENCE_AVAILABLE;
	    break;
	case TP_CONN_PRESENCE_TYPE_AWAY:
	    j = MC_PRESENCE_AWAY;
	    break;
	case TP_CONN_PRESENCE_TYPE_EXTENDED_AWAY:
	    j = MC_PRESENCE_EXTENDED_AWAY;
	    break;
	case TP_CONN_PRESENCE_TYPE_HIDDEN:
	    j = MC_PRESENCE_HIDDEN;
	    break;
	default:
	    g_debug ("Unknown Telepathy presence type. Presence %s "
		     "with Telepathy enum %d ignored.", pi.presence_str,
		     telepathy_enum);
	    g_free (pi.presence_str);
	    return;
	    break;
	}
    }
    g_array_append_val (priv->recognized_presence_info_array, pi);
}

static void
enable_well_known_presences (McdConnectionPrivate *priv)
{
    const struct _presence_mapping *mapping;
    struct presence_info *pi;

    /* Loop the presence_mappinges; if one of the basic McPresences is not set,
     * check if an mapping is supported by the connection and, if so, use it */
    for (mapping = presence_mapping; mapping->presence_str; mapping++)
    {
	if (priv->presence_to_set[mapping->mc_presence - 1] == NULL)
	{
	    gint i;
	    /* see if this presence is supported by the connection */
	    for (i = 0; i < priv->recognized_presence_info_array->len; i++)
	    {
		pi = &g_array_index (priv->recognized_presence_info_array,
				     struct presence_info, i);
		if (strcmp (pi->presence_str, mapping->presence_str) == 0)
		{
		    g_debug ("Using %s status for McPresence %d",
			     mapping->presence_str, mapping->mc_presence);
		    /* Presence values used when setting the presence status */
		    priv->presence_to_set[mapping->mc_presence - 1] = pi;
		    break;
		}
	    }
	}
    }
}

static void
_mcd_connection_set_presence (McdConnection * connection,
			      McPresence presence,
			      const gchar * presence_message)
{
    const gchar *presence_str;
    GHashTable *presence_ht;
    GHashTable *params_ht;
    struct presence_info *supported_presence_info;
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    GError *error = NULL;
    GValue msg_gval = { 0, };

    if (!priv->tp_conn)
    {
	_mcd_connection_setup (connection);
	return;
    }
    g_return_if_fail (TELEPATHY_IS_CONN (priv->tp_conn));
    g_return_if_fail (priv->bus_name != NULL);

    if (priv->presence_proxy == NULL)
    {
	GHashTable *status_hash;
	guint i;
	GError *error = NULL;

	/* Gets a reference to it */
	priv->presence_proxy =
	    tp_conn_get_interface (priv->tp_conn,
				   TELEPATHY_CONN_IFACE_PRESENCE_QUARK);
	if (priv->presence_proxy == NULL)
	{
	    g_warning ("%s: Account %s has no presence interface", G_STRFUNC,
		       mc_account_get_unique_name (priv->account));
	    return;
	}
	g_object_add_weak_pointer (G_OBJECT (priv->presence_proxy), (gpointer)&priv->presence_proxy);

	if (tp_conn_iface_presence_get_statuses (priv->presence_proxy,
						 &status_hash, &error) == FALSE)
	{
	    g_warning ("%s: Get statuses failed for account %s: %s", G_STRFUNC,
		       mc_account_get_unique_name (priv->account),
		       error->message);
	    g_error_free (error);
	    return;
	}

	/* Everything went well so pack the available presences into
	 * connection info
	 */
	/* Initialize presence info array and pointers for setting presences */
	for (i = 0; i < LAST_MC_PRESENCE - 1; i++)
	    priv->presence_to_set[i] = NULL;
	priv->recognized_presence_info_array =
	    g_array_new (FALSE, FALSE, sizeof (struct presence_info));
	g_hash_table_foreach (status_hash, recognize_presence, priv);
	g_hash_table_destroy (status_hash);

	enable_well_known_presences (priv);

	/* Set the fallback presence values */
	for (i = 0; i < LAST_MC_PRESENCE - 1; i++)
	{
	    if (priv->presence_to_set[i] == NULL)
		_mcd_connection_set_fallback_presences (connection, i);
	}
    }

    supported_presence_info = priv->presence_to_set[presence - 1];

    if (supported_presence_info == NULL)
    {
	g_debug ("No matching supported presence found. "
		 "Account presence has not been changed.");
	return;
    }

    presence_str = g_strdup (supported_presence_info->presence_str);
    presence = presence_str_to_enum (supported_presence_info->presence_str);

    /* Add the presence by libtelepathy */
    /* FIXME: what should we do when this is NULL? */
    if (presence_str != NULL)
    {
	presence_ht = g_hash_table_new_full (g_str_hash, g_str_equal,
					     g_free, NULL);
	params_ht = g_hash_table_new (g_str_hash, g_str_equal);

	/* 
	 * Note that we silently ignore the message if Connection Manager
	 * doesn't support it for this presence state!
	 */
	if (supported_presence_info->allow_message && presence_message)
	{
	    g_value_init (&msg_gval, G_TYPE_STRING);
	    g_value_set_string (&msg_gval, presence_message);
	    g_hash_table_insert (params_ht, "message", &msg_gval);
	}

	g_hash_table_insert (presence_ht, (gpointer) presence_str, params_ht);

	if (tp_conn_iface_presence_set_status (priv->presence_proxy,
					       presence_ht, &error) == FALSE)
	{
	    g_warning ("%s: Setting presence of %s to %s (%d) failed: %s",
		       G_STRFUNC, mc_account_get_unique_name (priv->account),
		       presence_str, presence, error->message);
	}
	else
	{
	    mcd_presence_frame_set_account_presence (priv->presence_frame,
						     priv->account,
						     presence,
						     presence_message);
	}

	if (supported_presence_info->allow_message && presence_message)
	    g_value_unset (&msg_gval);

	g_hash_table_destroy (presence_ht);
	g_hash_table_destroy (params_ht);
    }
}


/* This handler should update the presence of the tp_connection. 
 * Note, that the only presence transition not served by this function 
 * is getting to non-offline state since when presence is offline this object 
 * does not exist.
 *
 * So, here we just submit the request to the tp_connection object. The return off 
 * the operation is handled by (yet to be written) handler
 */
static void
on_presence_requested (McdPresenceFrame * presence_frame,
		       McPresence presence,
		       const gchar * presence_message, gpointer user_data)
{
    McdConnection *connection = MCD_CONNECTION (user_data);
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    GError *error = NULL;

    g_debug ("Presence requested: %d", presence);
    if (presence == TP_CONN_PRESENCE_TYPE_OFFLINE ||
	presence == TP_CONN_PRESENCE_TYPE_UNSET)
    {
	/* Connection Proxy */
	priv->abort_reason = TP_CONN_STATUS_REASON_REQUESTED;
	mcd_mission_disconnect (MCD_MISSION (connection));
	if (priv->tp_conn)
	    tp_conn_disconnect (DBUS_G_PROXY (priv->tp_conn), &error);
    }
    else
    {
	_mcd_connection_set_presence (connection, presence, presence_message);
    }
}

static void
_mcd_connection_new_channel_cb (DBusGProxy * tp_conn_proxy,
				const gchar * chan_obj_path,
				const gchar * chan_type,
				guint handle_type,
				guint handle,
				gboolean suppress_handler,
				McdConnection * connection)
{
    TpChan *tp_chan;
    McdChannel *channel;
    McdConnectionPrivate *priv;
    
    priv = MCD_CONNECTION_PRIV (connection);

    /* ignore all our own requests (they have always suppress_handler = 1) as
     * well as other requests for which our intervention has not been requested
     * */
    if (suppress_handler) return;
    
    /* g_return_if_fail (TELEPATHY_IS_CHAN (tp_chan)); */
    tp_chan = tp_chan_new (priv->dbus_connection, priv->bus_name,
			   chan_obj_path, chan_type, handle_type, handle);

    /* It's an incoming channel, so we create a new McdChannel for it */
    channel = mcd_channel_new (tp_chan,
			       chan_obj_path,
			       chan_type,
			       handle,
			       handle_type,
			       FALSE, /* incoming */
			       0, 0); /* There is no requestor, obviously */
    
    mcd_operation_take_mission (MCD_OPERATION (connection),
				MCD_MISSION (channel));

    /* Channel about to be dispatched */
    mcd_channel_set_status (channel, MCD_CHANNEL_DISPATCHING);
    
    /* Dispatch the incoming channel */
    mcd_dispatcher_send (priv->dispatcher, channel);
    
    g_object_unref (tp_chan);
}

static void
_foreach_channel_remove (McdMission * mission, McdOperation * operation)
{
    g_assert (MCD_IS_MISSION (mission));
    g_assert (MCD_IS_OPERATION (operation));

    mcd_operation_remove_mission (operation, mission);
}

static void
on_capabilities_changed (DBusGProxy *tp_conn_proxy,
			 GPtrArray *caps, McdChannel *channel)
{
    McdConnection *connection;
    McdConnectionPrivate *priv;
    gboolean found = FALSE;
    GType type;
    gchar *chan_type;
    guint chan_handle, chan_handle_type;
    DBusGProxyCall *call;
    gint i;

    connection = g_object_get_data (G_OBJECT (channel), "temporary_connection");
    priv = MCD_CONNECTION_PRIV (connection);

    g_debug ("%s: got capabilities for channel %p handle %d, type %s",
	     G_STRFUNC, channel, mcd_channel_get_handle (channel), mcd_channel_get_channel_type (channel));
    type = dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING,
				   G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
				   G_TYPE_UINT, G_TYPE_INVALID);
    for (i = 0; i < caps->len; i++)
    {
	GValue cap = { 0 };

	g_value_init (&cap, type);
	g_value_set_static_boxed (&cap, g_ptr_array_index(caps, i));
	dbus_g_type_struct_get (&cap, 0, &chan_handle, 1, &chan_type, G_MAXUINT);
	if (chan_handle == mcd_channel_get_handle (channel) &&
	    strcmp (chan_type, mcd_channel_get_channel_type (channel)) == 0)
	{
	    found = TRUE;
	    break;
	}
	g_free (chan_type);
    }

    if (!found) return;
    /* Return also if the "tp_chan_call" data is set (which means that a
     * request for this channel has already been made) */
    if (g_object_get_data (G_OBJECT (channel), "tp_chan_call") != NULL)
	return;
    chan_handle_type = mcd_channel_get_handle_type (channel);
    g_debug ("%s: requesting channel again (type = %s, handle_type = %u, handle = %u)",
	     G_STRFUNC, chan_type, chan_handle_type, chan_handle);
    call = tp_conn_request_channel_async(DBUS_G_PROXY(priv->tp_conn),
					 chan_type,
					 chan_handle_type,
					 chan_handle, TRUE,
					 mcd_async_request_chan_callback,
					 channel);
    g_object_set_data (G_OBJECT (channel), "tp_chan_call", call);
    g_free (chan_type);
}

static gboolean
on_channel_capabilities_timeout (guint channel_handle, McdChannel *channel,
				 McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    struct capabilities_wait_data *cwd;
    GError *mc_error;

    cwd = g_object_get_data (G_OBJECT (channel), "error_on_creation");
    if (!cwd) return FALSE;

    /* We reach this point if this channel was waiting for capabilities; we
     * abort it and return the original error */
    g_debug ("%s: channel %p timed out, returning error!", G_STRFUNC, channel);

    mc_error = map_tp_error_to_mc_error (channel, cwd->error);
    g_signal_emit_by_name (G_OBJECT(priv->dispatcher), "dispatch-failed",
			   channel, mc_error);
    g_error_free (mc_error);
    g_object_set_data (G_OBJECT (channel), "error_on_creation", NULL);

    /* No abort on channel, because we are the only one holding the only
     * reference to this temporary channel.
     */
    return TRUE;
}

static gboolean
on_capabilities_timeout (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    g_debug ("%s: got_capabilities is %d", G_STRFUNC, priv->got_capabilities);
    priv->got_capabilities = TRUE;
    g_hash_table_foreach_remove (priv->pending_channels,
				 (GHRFunc)on_channel_capabilities_timeout,
				 connection);
    priv->capabilities_timer = 0;
    return FALSE;
}

static void
_mcd_connection_setup_capabilities (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    GPtrArray *capabilities;
    GError *error = NULL;
    const gchar *remove = NULL;

    priv->capabilities_proxy = tp_conn_get_interface (priv->tp_conn,
				    TELEPATHY_CONN_IFACE_CAPABILITIES_QUARK);
    if (!priv->capabilities_proxy)
    {
	g_debug ("%s: connection does not support capabilities interface", G_STRFUNC);
	priv->got_capabilities = TRUE;
	return;
    }
    g_object_add_weak_pointer (G_OBJECT (priv->capabilities_proxy), (gpointer)&priv->capabilities_proxy);
    capabilities = mcd_dispatcher_get_channel_capabilities (priv->dispatcher);
    g_debug ("%s: advertising capabilities", G_STRFUNC);
    tp_conn_iface_capabilities_advertise_capabilities (priv->capabilities_proxy,
						       capabilities,
						       &remove, NULL,
						       &error);
    if (error)
    {
	g_warning ("%s: AdvertiseCapabilities failed: %s", G_STRFUNC, error->message);
	g_error_free(error);
    }
    
    if (priv->capabilities_timer)
    {
	g_warning ("This connection still has dangling capabilities timer on");
	g_source_remove (priv->capabilities_timer);
    }
    priv->capabilities_timer =
	g_timeout_add (1000 * 5, (GSourceFunc)on_capabilities_timeout, connection);
}

static void
_mcd_connection_get_self_handle (McdConnectionPrivate *priv)
{
    GError *error = NULL;
    tp_conn_get_self_handle (DBUS_G_PROXY (priv->tp_conn),
			     &priv->self_handle, &error);
    if (error)
    {
	g_warning ("%s: tp_conn_get_self_handle failed: %s",
		   G_STRFUNC, error->message);
	g_error_free (error);
    }
}

static void
_mcd_connection_get_normalized_name (McdConnectionPrivate *priv)
{
    GArray *handles;
    gchar **names = NULL;
    GError *error = NULL;

    handles = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
    g_array_append_val (handles, priv->self_handle);
    tp_conn_inspect_handles (DBUS_G_PROXY (priv->tp_conn),
			     TP_CONN_HANDLE_TYPE_CONTACT, handles,
			     &names, &error);
    g_array_free (handles, TRUE); 
    if (error)
    {
	g_warning ("%s: tp_conn_inspect_handles failed: %s",
		   G_STRFUNC, error->message);
	g_error_free (error);
	return;
    }
    if (names)
    {
	g_return_if_fail (names[0] != 0);
	mc_account_set_normalized_name (priv->account, names[0]);
	g_strfreev (names);
    }
}

static void
set_avatar_cb (DBusGProxy *proxy, char *token, GError *error, gpointer userdata)
{
    McdConnectionPrivate *priv = (McdConnectionPrivate *)userdata;
    if (error)
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
	g_error_free (error);
	return;
    }
    g_debug ("%s: received token: %s", G_STRFUNC, token);
    mc_account_set_avatar_token (priv->account, token);
    g_free (token);
}

static void
clear_avatar_cb (DBusGProxy *proxy, GError *error, gpointer userdata)
{
    gchar *filename = userdata;
    if (!error)
    {
	g_debug ("%s: Clear avatar succeeded, removing %s", G_STRFUNC, filename);
	g_remove (filename);
    }
    else
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
	g_error_free (error);
    }
    g_free (filename);
}

static void
_mcd_connection_setup_avatar (McdConnectionPrivate *priv)
{
    gchar *filename, *mime_type, *token;
    GError *error = NULL;

    priv->avatars_proxy = tp_conn_get_interface (priv->tp_conn,
				    TELEPATHY_CONN_IFACE_AVATARS_QUARK);
    if (!priv->avatars_proxy)
    {
	g_debug ("%s: connection does not support avatar interface", G_STRFUNC);
	return;
    }
    g_object_ref (priv->avatars_proxy);
    if (!mc_account_get_avatar (priv->account, &filename, &mime_type, &token))
    {
	g_debug ("%s: mc_account_get_avatar() returned FALSE", G_STRFUNC);
	return;
    }

    /* if the token is set, we have nothing to do */
    if (!token && filename && g_file_test (filename, G_FILE_TEST_EXISTS))
    {
	gchar *data = NULL;
	size_t length;
	if (g_file_get_contents (filename, &data, &length, &error))
	{
	    if (length > 0 && length < G_MAXUINT) 
	    {
		GArray avatar;
		avatar.data = data;
		avatar.len = (guint)length;
		tp_conn_iface_avatars_set_avatar_async (priv->avatars_proxy,
						       	&avatar, mime_type,
						       	set_avatar_cb, priv);
	    }
	    else
		tp_conn_iface_avatars_clear_avatar_async(priv->avatars_proxy,
							 clear_avatar_cb,
							 g_strdup (filename));

	}
	else
	{
	    g_debug ("%s: error reading %s: %s", G_STRFUNC, filename, error->message);
	    g_error_free (error);
	}
	g_free(data);
    }
    
    g_free (filename);
    g_free (mime_type);
    g_free (token);
}

static void
on_aliases_changed (DBusGProxy *tp_conn_proxy, GPtrArray *aliases,
		    McdConnectionPrivate *priv)
{
    GType type;
    gchar *alias;
    guint contact;
    gint i;

    g_debug ("%s called", G_STRFUNC);
    type = dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING,
				   G_TYPE_INVALID);
    for (i = 0; i < aliases->len; i++)
    {
	GValue data = { 0 };

	g_value_init (&data, type);
	g_value_set_static_boxed (&data, g_ptr_array_index(aliases, i));
	dbus_g_type_struct_get (&data, 0, &contact, 1, &alias, G_MAXUINT);
	g_debug("Got alias for contact %u: %s", contact, alias);
	if (contact == priv->self_handle)
	{
	    g_debug("This is our alias");
	    if (!priv->alias || strcmp (priv->alias, alias) != 0)
	    {
		g_free (priv->alias);
		priv->alias = alias;
		mc_account_set_alias (priv->account, alias);
	    }
	    break;
	}
	g_free (alias);
    }
}

static void
set_alias_cb (DBusGProxy *proxy, GError *error, gpointer userdata)
{
    if (error)
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
	g_error_free (error);
    }
}

static void
_mcd_connection_setup_alias (McdConnectionPrivate *priv)
{
    GHashTable *aliases;
    gchar *alias;

    if (!priv->alias_proxy)
    {
	priv->alias_proxy = tp_conn_get_interface (priv->tp_conn,
					TELEPATHY_CONN_IFACE_ALIASING_QUARK);
	if (!priv->alias_proxy)
	{
	    g_debug ("%s: connection does not support aliasing interface", G_STRFUNC);
	    return;
	}
	dbus_g_proxy_connect_signal (priv->alias_proxy,
				     "AliasesChanged",
				     G_CALLBACK (on_aliases_changed),
				     priv, NULL);
	g_object_ref (priv->alias_proxy);
    }
    alias = mc_account_get_alias (priv->account);
    if (!priv->alias || strcmp (priv->alias, alias) != 0)
    {
	g_debug ("%s: setting alias '%s'", G_STRFUNC, alias);

	aliases = g_hash_table_new (NULL, NULL);
	g_hash_table_insert (aliases, GINT_TO_POINTER(priv->self_handle), alias);
	tp_conn_iface_aliasing_set_aliases_async (priv->alias_proxy, aliases,
						  set_alias_cb, priv);
	g_hash_table_destroy (aliases);
    }
    g_free (alias);
}

static gboolean
mcd_connection_reconnect (McdConnection *connection)
{
    g_debug ("%s: %p", G_STRFUNC, connection);
    _mcd_connection_setup (connection);
    return FALSE;
}

static void
_mcd_connection_status_changed_cb (DBusGProxy * tp_conn_proxy,
				   TelepathyConnectionStatus conn_status,
				   TelepathyConnectionStatusReason
				   conn_reason, McdConnection * connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    g_debug ("%s: status_changed called from tp (%d)", G_STRFUNC, conn_status);

    switch (conn_status)
    {
    case TP_CONN_STATUS_CONNECTING:
	mcd_presence_frame_set_account_status (priv->presence_frame,
					       priv->account,
					       conn_status, conn_reason);
	priv->abort_reason = TP_CONN_STATUS_REASON_NONE_SPECIFIED;
	break;
    case TP_CONN_STATUS_CONNECTED:
	{
	    const gchar *presence_message;
	    McPresence requested_presence;

	    mcd_presence_frame_set_account_status (priv->presence_frame,
						   priv->account,
						   conn_status, conn_reason);
	    requested_presence =
		mcd_presence_frame_get_requested_presence (priv->presence_frame);
	    presence_message =
		mcd_presence_frame_get_requested_presence_message (priv->presence_frame);
	    _mcd_connection_set_presence (connection, requested_presence,
					  presence_message);
	    _mcd_connection_get_self_handle (priv);
	    _mcd_connection_setup_capabilities (connection);
	    _mcd_connection_setup_avatar (priv);
	    _mcd_connection_setup_alias (priv);
	    _mcd_connection_get_normalized_name (priv);
	    priv->reconnect_interval = 30 * 1000; /* reset it to 30 seconds */
	}
	break;
    case TP_CONN_STATUS_DISCONNECTED:
	/* Connection could die during account status updated if its
	 * manager is the only one holding the reference to it (manager will
	 * remove the connection from itself). To ensure we get a chance to
	 * emit abort signal (there could be others holding a ref to it), we
	 * will hold a temporary ref to it.
	 */
	priv->abort_reason = conn_reason;
	
	/* Destroy any pending timer */
	if (priv->capabilities_timer)
	    g_source_remove (priv->capabilities_timer);
	priv->capabilities_timer = 0;
	
	/* Notify connection abort */
	if (conn_reason == TP_CONN_STATUS_REASON_NETWORK_ERROR ||
	    conn_reason == TP_CONN_STATUS_REASON_NONE_SPECIFIED ||
	    priv->reconnection_requested)
	{
	    /* we were disconnected by a network error or by a gabble crash (in
	     * the latter case, we get NoneSpecified as a reason): don't abort
	     * the connection but try to reconnect later */
	    _mcd_connection_release_tp_connection (connection);
	    priv->reconnect_timer = g_timeout_add (priv->reconnect_interval,
					(GSourceFunc)mcd_connection_reconnect,
					connection);
	    priv->reconnect_interval *= 2;
	    if (priv->reconnect_interval >= 30 * 60 * 1000)
		/* no more than 30 minutes! */
		priv->reconnect_interval = 30 * 60 * 1000;
	    /* FIXME HACK: since we want presence-applet to immediately start
	     * displaying a blinking icon, we must set the account status to
	     * CONNECTING now */
	    mcd_presence_frame_set_account_status (priv->presence_frame,
						   priv->account,
						   TP_CONN_STATUS_CONNECTING,
						   TP_CONN_STATUS_REASON_REQUESTED);
	    priv->reconnection_requested = FALSE;
	    return;
	}

	g_object_ref (connection);
	/* Notify connection abort */
	mcd_mission_abort (MCD_MISSION (connection));
	g_object_unref (connection);
	break;
    default:
	g_warning ("Unknown telepathy connection status");
    }
}

static void proxy_destroyed (DBusGProxy *tp_conn, McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    g_debug ("Proxy destroyed!");
    priv->tp_conn = NULL;
    _mcd_connection_status_changed_cb (tp_conn,
				       TP_CONN_STATUS_DISCONNECTED,
				       TP_CONN_STATUS_REASON_NONE_SPECIFIED,
				       connection);
    g_object_unref (tp_conn);
}

static void
mcd_connection_connect (McdConnection *connection, GHashTable *params)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    TelepathyConnectionStatus conn_status;
    McProfile *profile;
    const gchar *protocol_name;
    const gchar *account_name;
    gchar *conn_bus_name, *conn_obj_path;
    GError *error = NULL;
    gboolean ret;

    profile = mc_account_get_profile (priv->account);
    protocol_name = mc_profile_get_protocol_name (profile);
    account_name = mc_account_get_unique_name (priv->account);

    g_debug ("%s: Trying connect account: %s",
	     G_STRFUNC, (gchar *) account_name);

    ret = tp_connmgr_request_connection (DBUS_G_PROXY (priv->tp_conn_mgr),
					 protocol_name, params,
					 &conn_bus_name, &conn_obj_path,
					 &error);
    g_object_unref (profile);
    if (!ret)
    {
	g_warning ("%s: tp_connmgr_request_connection failed: %s",
		   G_STRFUNC, error->message);
	mcd_presence_frame_set_account_status (priv->presence_frame,
					       priv->account,
					       TP_CONN_STATUS_DISCONNECTED,
					       TP_CONN_STATUS_REASON_NETWORK_ERROR);
	g_error_free (error);
	return;
    }

    priv->tp_conn = tp_conn_new_without_connect (priv->dbus_connection,
						 conn_bus_name,
						 conn_obj_path,
						 &conn_status, &error);
    g_free (conn_bus_name);
    g_free (conn_obj_path);
    if (!priv->tp_conn)
    {
	g_warning ("%s: tp_conn_new_without_connect failed: %s",
		   G_STRFUNC, error->message);
	mcd_presence_frame_set_account_status (priv->presence_frame,
					       priv->account,
					       TP_CONN_STATUS_DISCONNECTED,
					       TP_CONN_STATUS_REASON_NETWORK_ERROR);
	g_error_free (error);
	return;
    }

    /* Setup signals */
    g_signal_connect (priv->tp_conn, "destroy",
		      G_CALLBACK (proxy_destroyed), connection);
    dbus_g_proxy_connect_signal (DBUS_G_PROXY (priv->tp_conn), "NewChannel",
				 G_CALLBACK
				 (_mcd_connection_new_channel_cb),
				 connection, NULL);
    dbus_g_proxy_connect_signal (DBUS_G_PROXY (priv->tp_conn),
				 "StatusChanged",
				 G_CALLBACK
				 (_mcd_connection_status_changed_cb),
				 connection, NULL);

    /* if this was an already existing connection, it might be that it was
     * already connected/connecting, in which case we done. */
    if (conn_status != TP_CONN_STATUS_DISCONNECTED)
	mcd_presence_frame_set_account_status (priv->presence_frame,
					       priv->account,
					       conn_status,
					       TP_CONN_STATUS_REASON_NONE_SPECIFIED);
    else
    {
	/* Try to connect the connection */
	if (!tp_conn_connect (DBUS_G_PROXY (priv->tp_conn), &error))
	{
	    g_warning ("%s: tp_conn_connect failed: %s",
		       G_STRFUNC, error->message);
	    mcd_presence_frame_set_account_status (priv->presence_frame,
						   priv->account,
						   conn_status,
						   TP_CONN_STATUS_REASON_NETWORK_ERROR);
	    g_error_free (error);
	    return;
	}
    }
}

static void
provisioning_cb (McdProvisioning *prov, GHashTable *parameters, GError *error,
		 gpointer user_data)
{
    McdConnection *connection = MCD_CONNECTION (user_data);
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    g_debug ("%s called", G_STRFUNC);
    priv->provisioning = NULL;
    if (error)
    {
	g_warning ("%s failed: %s", G_STRFUNC, error->message);
	g_error_free (error);
	mcd_presence_frame_set_account_status (priv->presence_frame,
					       priv->account,
					       TP_CONN_STATUS_DISCONNECTED,
					       TP_CONN_STATUS_REASON_AUTHENTICATION_FAILED);
	return;
    }
    mcd_connection_connect (connection, parameters);
    g_hash_table_destroy (parameters);
}

static void
mcd_connection_get_params_and_connect (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    GHashTable *params = NULL;
    McAccountSettingState state;
    gchar *url = NULL;
    McProfile *profile;
    const gchar *protocol_name;
    const gchar *account_name;
    gboolean requesting_provisioning = FALSE;

    profile = mc_account_get_profile (priv->account);
    if (!profile)
    {
	mcd_presence_frame_set_account_status (priv->presence_frame,
					       priv->account,
					       TP_CONN_STATUS_DISCONNECTED,
					       TP_CONN_STATUS_REASON_AUTHENTICATION_FAILED);
	return;
    }
    protocol_name = mc_profile_get_protocol_name (profile);
    account_name = mc_account_get_unique_name (priv->account);

    g_debug ("%s: Trying connect account: %s",
	     G_STRFUNC, (gchar *) account_name);

    state = mc_account_get_param_string (priv->account, "prov-url", &url);
    if (state != MC_ACCOUNT_SETTING_ABSENT && url != NULL)
    {
	gchar *service = NULL, *username = NULL, *password = NULL;
	/* get parameters from provisioning service */
	mc_account_get_param_string (priv->account, "prov-service", &service);
	mc_account_get_param_string (priv->account, "prov-username", &username);
	mc_account_get_param_string (priv->account, "prov-password", &password);
	if (service)
	{
	    McdProvisioningFactory *factory;
	    McdProvisioning *prov;

	    factory = mcd_provisioning_factory_get ();
	    g_assert (factory != NULL);
	    prov = mcd_provisioning_factory_lookup (factory, service);
	    if (prov)
	    {
		g_debug ("%s: requesting parameters from provisioning service %s",
			 G_STRFUNC, service);
		/* if there was already a request, cancel it */
		if (priv->provisioning)
		    mcd_provisioning_cancel_request (priv->provisioning,
						     provisioning_cb,
						     connection);
		mcd_provisioning_request_parameters (prov, url,
						     username, password,
						     provisioning_cb,
						     connection);
		requesting_provisioning = TRUE;
		priv->provisioning = prov;
	    }
	    else
		g_debug ("%s: provisioning service %s not found",
			 G_STRFUNC, service);
	}
    }
    if (!requesting_provisioning)
    {
	params = mc_account_get_params (priv->account);
	mcd_connection_connect (connection, params);
	g_hash_table_destroy (params);
    }
    g_object_unref (profile);
}

static void
_mcd_connection_setup (McdConnection * connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    g_return_if_fail (priv->bus_name);
    g_return_if_fail (priv->tp_conn_mgr);
    g_return_if_fail (priv->account);

    if (priv->reconnect_timer)
    {
	g_source_remove (priv->reconnect_timer);
	priv->reconnect_timer = 0;
    }

    /* FIXME HACK: the correct test is

    if (mcd_connection_get_connection_status (connection) ==
	TP_CONN_STATUS_DISCONNECTED)
     * but since we set the account status to CONNECTING as soon as we got
     * disconnected by a network error, we must accept that status, too */
    if (mcd_connection_get_connection_status (connection) !=
	TP_CONN_STATUS_CONNECTED)
    {
	mcd_connection_get_params_and_connect (connection);
    }
    else
    {
	g_debug ("%s: Not connecting because not disconnected (%i)",
		 G_STRFUNC, mcd_connection_get_connection_status (connection));
	return;
    }
}

static void
_mcd_connection_finalize (GObject * object)
{
    McdConnection *connection = MCD_CONNECTION (object);
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    g_free (priv->bus_name);
    
    _mcd_connection_free_presence_info (connection);

    G_OBJECT_CLASS (mcd_connection_parent_class)->finalize (object);
}

static void
_mcd_connection_release_tp_connection (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    g_debug ("%s(%p) called", G_STRFUNC, connection);
    mcd_presence_frame_set_account_status (priv->presence_frame,
					   priv->account,
					   TP_CONN_STATUS_DISCONNECTED, 
					   priv->abort_reason);
    if (priv->tp_conn)
    {
	/* Disconnect signals */
	dbus_g_proxy_disconnect_signal (DBUS_G_PROXY (priv->tp_conn),
					"StatusChanged",
					G_CALLBACK
					(_mcd_connection_status_changed_cb),
					connection);
	dbus_g_proxy_disconnect_signal (DBUS_G_PROXY (priv->tp_conn),
					"NewChannel",
					G_CALLBACK
					(_mcd_connection_new_channel_cb),
					connection);
	g_signal_handlers_disconnect_by_func (G_OBJECT (priv->tp_conn),
					      G_CALLBACK (proxy_destroyed),
					      connection);

        
	tp_conn_disconnect (DBUS_G_PROXY (priv->tp_conn), NULL);
	g_object_unref (priv->tp_conn);
	priv->tp_conn = NULL;
    }

    /* the interface proxies obtained from this connection must be deleted, too
     */
    if (priv->presence_proxy)
    {
	g_object_remove_weak_pointer (G_OBJECT(priv->presence_proxy),
				      (gpointer)&priv->presence_proxy);
	priv->presence_proxy = NULL;
    }
    if (priv->capabilities_proxy)
    {
	g_object_remove_weak_pointer (G_OBJECT(priv->capabilities_proxy),
				      (gpointer)&priv->capabilities_proxy);
	priv->capabilities_proxy = NULL;
    }
    if (priv->avatars_proxy)
    {
	g_object_unref (priv->avatars_proxy);
	priv->avatars_proxy = NULL;
    }
    if (priv->alias_proxy)
    {
	dbus_g_proxy_disconnect_signal (priv->alias_proxy,
					"AliasesChanged",
					G_CALLBACK (on_aliases_changed),
					priv);
	g_object_unref (priv->alias_proxy);
	priv->alias_proxy = NULL;
    }
    g_free (priv->alias);
    priv->alias = NULL;
    _mcd_connection_free_presence_info (connection);
}

static void
_mcd_connection_dispose (GObject * object)
{
    McdConnection *connection = MCD_CONNECTION (object);
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    g_debug ("%s called for object %p", G_STRFUNC, object);

    if (priv->is_disposed)
    {
	return;
    }

    priv->is_disposed = TRUE;

    /* Remove any pending source: timer and idle */
    g_source_remove_by_user_data (connection);
    priv->capabilities_timer = 0;
    
    mcd_operation_foreach (MCD_OPERATION (connection),
			   (GFunc) _foreach_channel_remove, connection);

    /* Unref pending channels */
    g_hash_table_destroy (priv->pending_channels);
    
    _mcd_connection_release_tp_connection (connection);
    
    if (priv->presence_frame != NULL)
    {
	g_signal_handlers_disconnect_by_func (G_OBJECT
					      (priv->presence_frame),
					      G_CALLBACK
					      (on_presence_requested), object);
	g_object_unref (priv->presence_frame);
	priv->presence_frame = NULL;
    }

    if (priv->account)
    {
	g_object_unref (priv->account);
	priv->account = NULL;
    }

    if (priv->tp_conn_mgr)
    {
	g_object_unref (priv->tp_conn_mgr);
	priv->tp_conn_mgr = NULL;
    }

    if (priv->dispatcher)
    {
	g_object_unref (priv->dispatcher);
	priv->dispatcher = NULL;
    }

    if (priv->provisioning)
    {
	mcd_provisioning_cancel_request (priv->provisioning, provisioning_cb,
					 connection);
	priv->provisioning = NULL;
    }
    G_OBJECT_CLASS (mcd_connection_parent_class)->dispose (object);
}

static void
_mcd_connection_set_property (GObject * obj, guint prop_id,
			      const GValue * val, GParamSpec * pspec)
{
    McdPresenceFrame *presence_frame;
    McdDispatcher *dispatcher;
    DBusGConnection *dbus_connection;
    McAccount *account;
    TpConnMgr *tp_conn_mgr;
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (obj);

    switch (prop_id)
    {
    case PROP_PRESENCE_FRAME:
	presence_frame = g_value_get_object (val);
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
						  (on_presence_requested), obj);
	    g_object_unref (priv->presence_frame);
	}
	priv->presence_frame = presence_frame;
	if (priv->presence_frame)
	{
	    g_signal_connect_after (G_OBJECT (priv->presence_frame),
				    "presence-requested",
				    G_CALLBACK (on_presence_requested), obj);
	}
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
    case PROP_DBUS_CONNECTION:
	dbus_connection = g_value_get_pointer (val);
	dbus_g_connection_ref (dbus_connection);
	if (priv->dbus_connection)
	    dbus_g_connection_unref (priv->dbus_connection);
	priv->dbus_connection = dbus_connection;
	break;
    case PROP_BUS_NAME:
	g_return_if_fail (g_value_get_string (val) != NULL);
	g_free (priv->bus_name);
	priv->bus_name = g_strdup (g_value_get_string (val));
	break;
    case PROP_TP_MANAGER:
	tp_conn_mgr = g_value_get_object (val);
	g_return_if_fail (TELEPATHY_IS_CONNMGR (tp_conn_mgr));
	g_object_ref (tp_conn_mgr);
	if (priv->tp_conn_mgr)
	    g_object_unref (priv->tp_conn_mgr);
	priv->tp_conn_mgr = tp_conn_mgr;
	break;
    case PROP_ACCOUNT:
	account = g_value_get_object (val);
	g_return_if_fail (MC_IS_ACCOUNT (account));
	g_object_ref (account);
	if (priv->account)
	    g_object_unref (priv->account);
	priv->account = account;
	_mcd_connection_setup (MCD_CONNECTION (obj));
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_connection_get_property (GObject * obj, guint prop_id,
			      GValue * val, GParamSpec * pspec)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (obj);

    switch (prop_id)
    {
    case PROP_DBUS_CONNECTION:
	g_value_set_pointer (val, priv->dbus_connection);
	break;
    case PROP_BUS_NAME:
	g_value_set_string (val, priv->bus_name);
	break;
    case PROP_ACCOUNT:
	g_value_set_object (val, priv->account);
	break;
    case PROP_TP_MANAGER:
	g_value_set_object (val, priv->tp_conn_mgr);
	break;
    case PROP_TP_CONNECTION:
	g_value_set_object (val, priv->tp_conn);
	break;
    case PROP_PRESENCE_FRAME:
	g_value_set_object (val, priv->presence_frame);
	break;
    case PROP_DISPATCHER:
	g_value_set_object (val, priv->dispatcher);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
mcd_connection_class_init (McdConnectionClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdConnectionPrivate));

    object_class->finalize = _mcd_connection_finalize;
    object_class->dispose = _mcd_connection_dispose;
    object_class->set_property = _mcd_connection_set_property;
    object_class->get_property = _mcd_connection_get_property;

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
							  _("Dispatcher Object"),
							  _("Dispatcher to dispatch channels"),
							  MCD_TYPE_DISPATCHER,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class, PROP_DBUS_CONNECTION,
				     g_param_spec_pointer ("dbus-connection",
							   _("DBus Connection"),
							   _
							   ("DBus connection to use by us"),
							   G_PARAM_READWRITE |
							   G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class, PROP_BUS_NAME,
				     g_param_spec_string ("bus-name",
							  _("DBus Bus name"),
							  _
							  ("DBus Bus name to use by us"),
							  NULL,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class, PROP_TP_MANAGER,
				     g_param_spec_object ("tp-manager",
							  _
							  ("Telepathy Manager Object"),
							  _
							  ("Telepathy Manager Object which this connection uses"),
							  TELEPATHY_CONNMGR_TYPE,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class, PROP_TP_CONNECTION,
				     g_param_spec_object ("tp-connection",
							  _
							  ("Telepathy Connection Object"),
							  _
							  ("Telepathy Connection Object which this connection uses"),
							  TELEPATHY_CONN_TYPE,
							  G_PARAM_READABLE));
    g_object_class_install_property (object_class, PROP_ACCOUNT,
				     g_param_spec_object ("account",
							  _("Account Object"),
							  _
							  ("Account that will be used to create this connection"),
							  MC_TYPE_ACCOUNT,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));
}

static void
release_channel_object (gpointer object)
{
    g_object_unref (object);
}

static void
mcd_connection_init (McdConnection * connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    priv->pending_channels = g_hash_table_new_full (g_direct_hash,
						    g_direct_equal,
						    NULL,
						    release_channel_object);
    priv->abort_reason = TP_CONN_STATUS_REASON_NONE_SPECIFIED;

    priv->reconnect_interval = 30 * 1000; /* 30 seconds */
}

/* Public methods */

/* Creates a new connection object. Increases a refcount of account.
 * Uses mcd_get_manager function to get TpConnManager
 */
McdConnection *
mcd_connection_new (DBusGConnection * dbus_connection,
		    const gchar * bus_name,
		    TpConnMgr * tp_conn_mgr,
		    McAccount * account,
		    McdPresenceFrame * presence_frame,
		    McdDispatcher *dispatcher)
{
    McdConnection *mcdconn = NULL;
    g_return_val_if_fail (dbus_connection != NULL, NULL);
    g_return_val_if_fail (bus_name != NULL, NULL);
    g_return_val_if_fail (TELEPATHY_IS_CONNMGR (tp_conn_mgr), NULL);
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    g_return_val_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame), NULL);

    mcdconn = g_object_new (MCD_TYPE_CONNECTION,
			    "dbus-connection", dbus_connection,
			    "bus-name", bus_name,
			    "tp-manager", tp_conn_mgr,
			    "presence-frame", presence_frame,
			    "dispatcher", dispatcher,
			    "account", account, NULL);
    return mcdconn;
}

/* Constant getters. These should probably be removed */

McAccount *
mcd_connection_get_account (McdConnection * id)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (id);
    return priv->account;
}

TelepathyConnectionStatus
mcd_connection_get_connection_status (McdConnection * id)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (id);
    return mcd_presence_frame_get_account_status (priv->presence_frame,
						  priv->account);
}

gboolean
mcd_connection_get_telepathy_details (McdConnection * id,
				      gchar ** ret_servname,
				      gchar ** ret_objpath)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (id);

    g_return_val_if_fail (priv->tp_conn != NULL, FALSE);
    g_return_val_if_fail (TELEPATHY_IS_CONN (priv->tp_conn), FALSE);

    /* Query the properties required for creation of identical TpConn object */
    *ret_objpath =
	g_strdup (dbus_g_proxy_get_path (DBUS_G_PROXY (priv->tp_conn)));
    *ret_servname =
	g_strdup (dbus_g_proxy_get_bus_name (DBUS_G_PROXY (priv->tp_conn)));

    return TRUE;
}

static GError *
map_tp_error_to_mc_error (McdChannel *channel, GError *tp_error)
{
    MCError mc_error_code = MC_CHANNEL_REQUEST_GENERIC_ERROR;
    
    g_warning ("Telepathy Error = %s", tp_error->message);
    
    /* TODO : Are there still more specific errors we need
     * to distinguish?
     */
    if (mcd_channel_get_channel_type_quark (channel) ==
	TELEPATHY_CHAN_IFACE_STREAMED_QUARK &&
	dbus_g_error_has_name(tp_error, "org.freedesktop.Telepathy.Error.NotAvailable"))
    {
	mc_error_code = MC_CONTACT_DOES_NOT_SUPPORT_VOICE_ERROR;
    }
    else if (dbus_g_error_has_name(tp_error, "org.freedesktop.Telepathy.Error.ChannelBanned"))
    {
	mc_error_code = MC_CHANNEL_BANNED_ERROR;
    }
    else if (dbus_g_error_has_name(tp_error, "org.freedesktop.Telepathy.Error.ChannelFull"))
    {
	mc_error_code = MC_CHANNEL_FULL_ERROR;
    }
    else if (dbus_g_error_has_name(tp_error, "org.freedesktop.Telepathy.Error.ChannelInviteOnly"))
    {
	mc_error_code = MC_CHANNEL_INVITE_ONLY_ERROR;
    }
    return g_error_new (MC_ERROR, mc_error_code, "Telepathy Error: %s",
			tp_error->message);
}

static void
remove_capabilities_refs (gpointer data)
{
    struct capabilities_wait_data *cwd = data;

    g_debug ("\n\n\n%s called\n\n\n", G_STRFUNC);
    dbus_g_proxy_disconnect_signal (cwd->priv->capabilities_proxy,
				    "CapabilitiesChanged",
				    G_CALLBACK (on_capabilities_changed),
				    cwd->channel);
    g_error_free (cwd->error);
    g_free (cwd);
}

static void
mcd_async_request_chan_callback (DBusGProxy *proxy,
				 gchar *channel_path,
				 GError *error,
				 gpointer user_data)
{
    McdChannel *channel;
    McdConnection *connection;
    McdConnectionPrivate *priv;
    GError *error_on_creation;
    struct capabilities_wait_data *cwd;
    gchar *chan_type;
    TelepathyHandleType chan_handle_type;
    guint chan_handle;
    TpChan *tp_chan;
    /* We handle only the dbus errors */
    
    /* ChannelRequestor *chan_req = (ChannelRequestor *)user_data; */
    channel = MCD_CHANNEL (user_data); /* the not-yet-added channel */
    connection = g_object_get_data (G_OBJECT (channel), "temporary_connection");
    g_object_steal_data (G_OBJECT (channel), "tp_chan_call");
    priv = MCD_CONNECTION_PRIV (connection);

    g_object_get (channel,
		  "channel-handle", &chan_handle,
		  "channel-handle-type", &chan_handle_type,
		  NULL);


    cwd = g_object_get_data (G_OBJECT (channel), "error_on_creation");
    if (cwd)
    {
	error_on_creation = cwd->error;
	g_object_set_data (G_OBJECT (channel), "error_on_creation", NULL);
    }
    else
	error_on_creation = NULL;

    
    if (error != NULL)
    {
	g_debug ("%s: Got error: %s", G_STRFUNC, error->message);
	if (error_on_creation != NULL)
	{
	    /* replace the error, so that the initial one is reported */
	    g_error_free (error);
	    error = error_on_creation;
	}

	if (priv->got_capabilities || error_on_creation)
	{
	    /* Faild dispatch */
	    GError *mc_error = map_tp_error_to_mc_error (channel, error);
	    g_signal_emit_by_name (G_OBJECT(priv->dispatcher), "dispatch-failed",
				   channel, mc_error);
	    g_error_free (mc_error);
	    g_error_free (error);
	    
	    /* No abort on channel, because we are the only one holding the only
	     * reference to this temporary channel.
	     * This should also unref the channel object
	     */
	    g_hash_table_remove (priv->pending_channels,
				 GINT_TO_POINTER (chan_handle));
	}
	else
	{
	    /* the channel request has failed probably because we are just
	     * connected and we didn't recive the contact capabilities yet. In
	     * this case, wait for this contact's capabilities to arrive */
	    g_debug ("%s: listening for remote capabilities on channel handle %d, type %d",
		     G_STRFUNC, chan_handle, mcd_channel_get_handle_type (channel));
	    dbus_g_proxy_connect_signal (priv->capabilities_proxy,
					 "CapabilitiesChanged",
					 G_CALLBACK (on_capabilities_changed),
					 channel, NULL);
	    /* Store the error, we might need it later */
	    cwd = g_malloc (sizeof (struct capabilities_wait_data));
	    g_assert (cwd != NULL);
	    cwd->error = error;
	    cwd->channel = channel;
	    cwd->priv = priv;
	    g_object_set_data_full (G_OBJECT (channel), "error_on_creation", cwd,
				    remove_capabilities_refs);
	}
	return;
    }
    
    if (channel_path == NULL)
    {
	GError *mc_error;
	g_warning ("Returned channel_path from telepathy is NULL");
	
	mc_error = g_error_new (MC_ERROR,
				MC_CHANNEL_REQUEST_GENERIC_ERROR,
				"Returned channel_path from telepathy is NULL");
	g_signal_emit_by_name (G_OBJECT(priv->dispatcher),
			       "dispatch-failed", channel, mc_error);
	g_error_free (mc_error);
	
	/* No abort on channel, because we are the only one holding the only
	 * reference to this temporary channel.
	 * This should also unref the channel object
	 */
	g_hash_table_remove (priv->pending_channels,
			     GINT_TO_POINTER (chan_handle));
	return;
    }
    
    /* Everything here is well and fine. We can create the channel. */
    channel = g_hash_table_lookup (priv->pending_channels,
				   GUINT_TO_POINTER (chan_handle));
    if (!channel)
    {
	g_warning ("%s: channel not found among the pending ones", G_STRFUNC);
	return;
    }

    g_object_get (channel,
		  "channel-type", &chan_type,
		  NULL);
    tp_chan = tp_chan_new (priv->dbus_connection, priv->bus_name,
			   channel_path, chan_type, chan_handle_type, chan_handle);
    g_object_set (channel,
		  "channel-object-path", channel_path,
		  "tp-channel", tp_chan,
		  NULL);

    /* The channel is no longer pending. 'stealing' because want the
     * channel ownership.
     */
    g_hash_table_steal (priv->pending_channels,
			GINT_TO_POINTER (chan_handle));
    mcd_operation_take_mission (MCD_OPERATION (connection),
				MCD_MISSION (channel));

    /* Channel about to be dispatched */
    mcd_channel_set_status (channel, MCD_CHANNEL_DISPATCHING);
    
    /* Dispatch the incoming channel */
    mcd_dispatcher_send (priv->dispatcher, channel);
    
    g_free (chan_type);
    g_free (channel_path);
    g_object_unref (tp_chan);
}

static void
mcd_async_request_handle_callback(DBusGProxy *proxy, GArray *handles,
				  GError *error, gpointer user_data)
{
    McdChannel *channel, *existing_channel;
    McdConnection *connection;
    McdConnectionPrivate *priv;
    guint chan_handle, chan_handle_type;
    const gchar *chan_type;
    const GList *channels;
    DBusGProxyCall *call;
    
    channel = MCD_CHANNEL (user_data);
    connection = g_object_get_data (G_OBJECT (channel), "temporary_connection");
    priv = MCD_CONNECTION_PRIV (connection);
    
    if (error != NULL)
    {
	GError *mc_error;
	g_warning ("Could not map string handle to a valid handle!: %s",
		   error->message);
	
	/* Fail dispatch */
	mc_error = g_error_new (MC_ERROR, MC_INVALID_HANDLE_ERROR,
		     "Could not map string handle to a valid handle!: %s",
				error->message);
	g_signal_emit_by_name (priv->dispatcher, "dispatch-failed",
			       channel, mc_error);
	g_error_free (mc_error);
	g_error_free(error);
	
	/* No abort, because we are the only one holding the only reference
	 * to this temporary channel
	 */
	g_object_unref (channel);
	return;
    }
    
    chan_type = mcd_channel_get_channel_type (channel),
    chan_handle_type = mcd_channel_get_handle_type (channel),
    chan_handle = g_array_index (handles, guint, 0);
    g_array_free(handles, TRUE);
    
    g_debug ("Got handle %u", chan_handle);
    
    /* Is the handle we got valid? */
    if (chan_handle == 0)
    {
	GError *mc_error;
	g_warning ("Could not map the string  to a valid handle!");
	
	/* Fail dispatch */
	mc_error = g_error_new (MC_ERROR, MC_INVALID_HANDLE_ERROR,
			     "Could not map string handle to a valid handle!");
	g_signal_emit_by_name (priv->dispatcher, "dispatch-failed",
			       channel, mc_error);
	g_error_free (mc_error);
	
	/* No abort, because we are the only one holding the only reference
	 * to this temporary channel
	 */
	g_object_unref (channel);
	return;
    }
    
    /* Check if a telepathy channel has already been created; this could happen
     * in the case we had a chat window open, the UI crashed and now the same
     * channel is requested. */
    channels = mcd_operation_get_missions (MCD_OPERATION (connection));
    while (channels)
    {
	existing_channel = MCD_CHANNEL (channels->data);
	g_debug ("Chan: %d, handle type %d, channel type %s",
		 mcd_channel_get_handle (existing_channel),
		 mcd_channel_get_handle_type (existing_channel),
		 mcd_channel_get_channel_type (existing_channel));
	if (chan_handle == mcd_channel_get_handle (existing_channel) &&
	    strcmp(chan_type,
		   mcd_channel_get_channel_type (existing_channel)) == 0)
	{
	    guint requestor_serial;
	    gchar *requestor_client_id;

	    g_debug ("%s: Channel already existing, returning old one", G_STRFUNC);
	    /* we retrieve the information we need from the newly created
	     * channel object and set them to the "old" channel */
	    g_object_get (channel,
			  "requestor-serial", &requestor_serial,
			  "requestor-client-id", &requestor_client_id,
			  NULL);
	    g_object_set (existing_channel,
			  "requestor-serial", requestor_serial,
			  "requestor-client-id", requestor_client_id,
			  NULL);
	    g_free (requestor_client_id);
	    /* we no longer need the new channel */
	    g_object_unref (channel);
	    /* notify the dispatcher again */
	    mcd_dispatcher_send (priv->dispatcher, existing_channel);
	    return;
	}
	channels = channels->next;
    }

    /* Update our newly acquired information */
    g_object_set (channel, "channel-handle", chan_handle, NULL);
 
    /* The channel is temporary and stays in priv->pending_channels until
     * a telepathy channel for it is created
     */
    g_hash_table_insert (priv->pending_channels,
			 GINT_TO_POINTER (chan_handle), channel);
    
    /* Now, request the corresponding telepathy channel. */
    call = tp_conn_request_channel_async(DBUS_G_PROXY(proxy),
					 mcd_channel_get_channel_type (channel),
					 mcd_channel_get_handle_type (channel),
					 chan_handle, TRUE,
					 mcd_async_request_chan_callback,
					 channel);
    g_object_set_data (G_OBJECT (channel), "tp_chan_call", call);
}

gboolean
mcd_connection_request_channel (McdConnection *connection,
				const struct mcd_channel_request *req,
			       	GError ** error)
{
    McdChannel *channel;
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    
    g_return_val_if_fail (priv->tp_conn != NULL, FALSE);
    g_return_val_if_fail (TELEPATHY_IS_CONN (priv->tp_conn), FALSE);
    
    /* The channel is temporary */
    channel = mcd_channel_new (NULL,
			       NULL,
			       req->channel_type,
			       req->channel_handle,
			       req->channel_handle_type,
			       TRUE, /* outgoing */
			       req->requestor_serial,
			       req->requestor_client_id);
    
    /* We do not add the channel in connection until tp_channel is created */
    g_object_set_data (G_OBJECT (channel), "temporary_connection", connection);
    
    if (req->channel_handle)
    {
	DBusGProxyCall *call;
	/* the channel stays in priv->pending_channels until a telepathy
	 * channel for it is created */
	g_hash_table_insert (priv->pending_channels,
			     GINT_TO_POINTER (req->channel_handle), channel);

	call = tp_conn_request_channel_async(DBUS_G_PROXY(priv->tp_conn),
					     req->channel_type,
					     req->channel_handle_type,
					     req->channel_handle, TRUE,
					     mcd_async_request_chan_callback,
					     channel);
	g_object_set_data (G_OBJECT (channel), "tp_chan_call", call);
    }
    else
    {
	/* if channel handle is 0, this means that the channel was requested by
	 * a string handle; in that case, we must first request a channel
	 * handle for it */
	const gchar *name_array[2];
	g_assert (req->channel_handle_string != NULL);

	name_array[0] = req->channel_handle_string;
	name_array[1] = NULL;

	/* Channel is temporary and will enter priv->pending_channels list
	 * only when we successfully resolve the handle. */
	tp_conn_request_handles_async (DBUS_G_PROXY(priv->tp_conn),
				       req->channel_handle_type,
				       name_array,
				       mcd_async_request_handle_callback,
				       channel);
    }
    return TRUE;
}

static gboolean
channel_matches_request (gpointer key, McdChannel *channel,
			 struct request_id *req_id)
{
    guint requestor_serial;
    gchar *requestor_client_id;
    gboolean found;

    g_object_get (channel,
		  "requestor-serial", &requestor_serial,
		  "requestor-client-id", &requestor_client_id,
		  NULL);
    if (requestor_serial == req_id->requestor_serial &&
	strcmp (requestor_client_id, req_id->requestor_client_id) == 0)
	found = TRUE;
    else
	found = FALSE;
    g_free (requestor_client_id);
    return found;
}

gboolean
mcd_connection_cancel_channel_request (McdConnection *connection,
				       guint operation_id,
				       const gchar *requestor_client_id,
				       GError **error)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    struct request_id req_id;
    const GList *channels, *node;
    McdChannel *channel;

    /* first, see if the channel is in the list of the pending channels */
    req_id.requestor_serial = operation_id;
    req_id.requestor_client_id = requestor_client_id;
    channel = g_hash_table_find (priv->pending_channels,
				 (GHRFunc)channel_matches_request,
				 &req_id);
    if (channel)
    {
	guint chan_handle;
	DBusGProxyCall *call;

	g_debug ("%s: requested channel found in the pending_channels list (%p)", G_STRFUNC, channel);
	g_object_get (channel,
		      "channel-handle", &chan_handle,
		      NULL);
	/* check for pending dbus calls to be cancelled */
	call = g_object_get_data (G_OBJECT (channel), "tp_chan_call");
	dbus_g_proxy_cancel_call (DBUS_G_PROXY(priv->tp_conn), call);
	/* No abort on channel, because we are the only one holding the only
	 * reference to this temporary channel.
	 * This should also unref the channel object.
	 * No other actions needed; if a NewChannel signal will be emitted for
	 * this channel, it will be ignored since it has the suppress_handler
	 * flag set.
	 */
	g_hash_table_remove (priv->pending_channels,
			     GINT_TO_POINTER (chan_handle));
	return TRUE;
    }

    /* the channel might already have a TpChan created for it, and either be in
     * the dispatcher or be already handled. Aborting in any case. */
    channels = mcd_operation_get_missions (MCD_OPERATION (connection));
    if (!channels) return FALSE;

    for (node = channels; node; node = node->next)
    {
	guint chan_requestor_serial;
	gchar *chan_requestor_client_id;

	channel = MCD_CHANNEL (node->data);
	g_object_get (channel,
		      "requestor-serial", &chan_requestor_serial,
		      "requestor-client-id", &chan_requestor_client_id,
		      NULL);
	if (chan_requestor_serial == operation_id &&
	    strcmp (chan_requestor_client_id, requestor_client_id) == 0)
	{
	    g_debug ("%s: requested channel found (%p)", G_STRFUNC, channel);
	    mcd_mission_abort (MCD_MISSION (channel));
	    g_free (chan_requestor_client_id);
	    return TRUE;
	}
	g_free (chan_requestor_client_id);
    }
    g_debug ("%s: requested channel not found!", G_STRFUNC);
    return FALSE;
}

static void
request_avatar_cb (DBusGProxy *proxy, GArray *avatar, char *mime_type,
		   GError *error, gpointer userdata)
{
    McdConnectionPrivate *priv = (McdConnectionPrivate *)userdata;
    gchar *filename;
    if (error)
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
	g_error_free (error);
	return;
    }
    g_debug ("%s: received mime-type: %s", G_STRFUNC, mime_type);
    if (mc_account_get_avatar (priv->account, &filename, NULL, NULL))
    {
	g_file_set_contents (filename, avatar->data, avatar->len, NULL);
	mc_account_set_avatar_mime_type (priv->account, mime_type);
	mc_account_reset_avatar_id (priv->account);
	g_free (filename);
    }
    g_free (mime_type);
}

/**
 * mcd_connection_remote_avatar_changed:
 * @connection: the #McdConnection.
 * @contact_id: the own contact id in Telepathy.
 * @token: the new avatar token.
 *
 * This function is to be called when Telepathy signals that our own avatar has
 * been updated. It takes care of checking if the remote avatar has to be
 * retrieved and stored in the account.
 *
 * Returns: %TRUE if the local avatar has been updated.
 */
gboolean mcd_connection_remote_avatar_changed (McdConnection *connection,
					       guint contact_id,
					       const gchar *token)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    gchar *prev_token;
    gboolean changed = FALSE;

    if (!priv->avatars_proxy)
    {
	g_warning ("%s: avatar proxy is gone", G_STRFUNC);
	return FALSE;
    }

    if (!mc_account_get_avatar (priv->account, NULL, NULL, &prev_token))
	return FALSE;

    if (!prev_token || strcmp (token, prev_token) != 0)
    {
	g_debug ("%s: avatar has changed", G_STRFUNC);
	/* the avatar has changed, let's retrieve the new one */
	tp_conn_iface_avatars_request_avatar_async (priv->avatars_proxy, contact_id,
						request_avatar_cb,
						priv);
	mc_account_set_avatar_token (priv->account, token);
	changed = TRUE;
    }
    g_free (prev_token);
    return changed;
}

void
mcd_connection_close (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    priv->abort_reason = TP_CONN_STATUS_REASON_REQUESTED;
    mcd_mission_abort (MCD_MISSION (connection));
}

/**
 * mcd_connection_account_changed:
 * @connection: the #McdConnection.
 *
 * This function must be called when the account this connection refers to is
 * modified.
 * Note: ideally, this should be a private method and the connection should
 * monitor the account itself; but since the monitoring is already done by
 * McdMaster for all accounts, it seemed more convenient to let it call this
 * function whenever needed.
 */
void
mcd_connection_account_changed (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    if (priv->tp_conn) 
    {
	/* setup the avatar (if it has not been changed, this function does
	 * nothing) */
	_mcd_connection_setup_avatar (priv);
	_mcd_connection_setup_alias (priv);
    }
}

/**
 * mcd_connection_restart:
 * @connection: the #McdConnection.
 *
 * Disconnect the connection and reconnect it. This can be useful when some
 * account parameter changes.
 */
void
mcd_connection_restart (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    g_debug ("%s called", G_STRFUNC);
    priv->reconnection_requested = TRUE;
    priv->reconnect_interval = 500; /* half a second */
    mcd_mission_disconnect (MCD_MISSION (connection));
    if (priv->tp_conn)
	tp_conn_disconnect (DBUS_G_PROXY (priv->tp_conn), NULL);
}

