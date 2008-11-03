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

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/connection.h>
#include <libmcclient/mc-errors.h>

#include "mcd-connection.h"
#include "mcd-channel.h"
#include "mcd-provisioning-factory.h"
#include "mcd-misc.h"

#define MAX_REF_PRESENCE 4
#define LAST_MC_PRESENCE (TP_CONNECTION_PRESENCE_TYPE_BUSY + 1)

#define MCD_CONNECTION_PRIV(mcdconn) (MCD_CONNECTION (mcdconn)->priv)

G_DEFINE_TYPE (McdConnection, mcd_connection, MCD_TYPE_OPERATION);

/* Private */
struct _McdConnectionPrivate
{
    /* DBUS connection */
    TpDBusDaemon *dbus_daemon;

    /* DBus bus name */
    gchar *bus_name;

    /* Channel dispatcher */
    McdDispatcher *dispatcher;

    /* Account */
    McdAccount *account;

    /* Associated profile */
    /* McProfile *profile; */

    /* Telepathy connection manager */
    TpConnectionManager *tp_conn_mgr;

    /* Telepathy connection */
    TpConnection *tp_conn;
    TpProxySignalConnection *new_channel_sc;
    guint self_handle;

    /* Capabilities timer */
    guint capabilities_timer;

    guint reconnect_timer; 	/* timer for reconnection */
    guint reconnect_interval;
    gboolean reconnection_requested;

    /* Supported presences */
    GArray *recognized_presence_info_array;
    struct presence_info *presence_to_set[LAST_MC_PRESENCE - 1];

    TpConnectionStatusReason abort_reason;
    guint got_capabilities : 1;
    guint setting_avatar : 1;
    guint has_presence_if : 1;
    guint has_avatars_if : 1;
    guint has_alias_if : 1;
    guint has_capabilities_if : 1;
    guint has_requests_if : 1;

    /* FALSE until the connection is ready for dispatching */
    guint can_dispatch : 1;

    gchar *alias;

    gboolean is_disposed;
    
};

struct presence_info
{
    gchar *presence_str;
    gboolean allow_message;
};

typedef struct {
    TpConnectionPresenceType presence;
    gchar *status;
    gchar *message;
} McdPresenceData;

struct param_data
{
    GSList *pr_params;
    GHashTable *dest;
};

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
    PROP_BUS_NAME,
    PROP_TP_MANAGER,
    PROP_TP_CONNECTION,
    PROP_ACCOUNT,
    PROP_DISPATCHER,
};

/* This table lists the Telepathy well-known statuses and the corresponding
 * TpConnectionPresenceType values; the order in which the items appear is only
 * important for those statuses which map to the same TpConnectionPresenceType
 * value: for them, the first ones will be preferred. */
static const struct _presence_mapping {
    gchar *presence_str;
    TpConnectionPresenceType mc_presence;
} presence_mapping[] = {
    { "offline", TP_CONNECTION_PRESENCE_TYPE_OFFLINE },
    { "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE },
    { "away", TP_CONNECTION_PRESENCE_TYPE_AWAY },
    { "xa", TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY },
    { "hidden", TP_CONNECTION_PRESENCE_TYPE_HIDDEN },
    { "dnd", TP_CONNECTION_PRESENCE_TYPE_BUSY },
    { "brb", TP_CONNECTION_PRESENCE_TYPE_AWAY },
    { "busy", TP_CONNECTION_PRESENCE_TYPE_BUSY },
    { NULL, 0 },
};

static const TpConnectionPresenceType fallback_presence
    [LAST_MC_PRESENCE - 1][MAX_REF_PRESENCE] = {
    { 0 },	/* TP_CONNECTION_PRESENCE_TYPE_OFFLINE */
    { 0 },	/* TP_CONNECTION_PRESENCE_TYPE_AVAILABLE */
    { TP_CONNECTION_PRESENCE_TYPE_AVAILABLE,
       	0 },	/* TP_CONNECTION_PRESENCE_TYPE_AWAY */
    { TP_CONNECTION_PRESENCE_TYPE_AWAY, TP_CONNECTION_PRESENCE_TYPE_AVAILABLE,
       	0 },	/* TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY */
    { TP_CONNECTION_PRESENCE_TYPE_BUSY,
       	TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY,
       	TP_CONNECTION_PRESENCE_TYPE_AVAILABLE,
       	0 },	/* TP_CONNECTION_PRESENCE_TYPE_HIDDEN */
    { 0 }	/* TP_CONNECTION_PRESENCE_TYPE_BUSY */
};

struct request_id {
    guint requestor_serial;
    const gchar *requestor_client_id;
};

struct capabilities_wait_data {
    GError *error; /* error originally received when channel request failed */
    TpProxySignalConnection *signal_connection;
};

static void request_channel_cb (TpConnection *proxy, const gchar *channel_path,
				const GError *error, gpointer user_data,
				GObject *weak_object);
static GError * map_tp_error_to_mc_error (McdChannel *channel, const GError *tp_error);
static void _mcd_connection_setup (McdConnection * connection);
static void _mcd_connection_release_tp_connection (McdConnection *connection);

static TpConnectionPresenceType presence_str_to_enum (const gchar *presence_str)
{
    const struct _presence_mapping *mapping;
    for (mapping = presence_mapping; mapping->presence_str; mapping++)
	if (strcmp (presence_str, mapping->presence_str) == 0)
	    return mapping->mc_presence;
    return TP_CONNECTION_PRESENCE_TYPE_UNSET;
}

/* Free dynamic members and presence_info itself */
static void
_mcd_connection_free_presence_info (McdConnection * conn)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (conn);

    if (priv->recognized_presence_info_array != NULL)
    {
	struct presence_info *pi;
	guint i;

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
	    g_debug ("Fallback for TpConnectionPresenceType %d set to %s",
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
    if (j == TP_CONNECTION_PRESENCE_TYPE_UNSET)
    {
	/* Didn't find match by comparing strings so map using the telepathy enum. */
	telepathy_enum = g_value_get_uint (g_value_array_get_nth (status, 0));
	switch (telepathy_enum)
	{
	case TP_CONNECTION_PRESENCE_TYPE_OFFLINE:
	    j = TP_CONNECTION_PRESENCE_TYPE_OFFLINE;
	    break;
	case TP_CONNECTION_PRESENCE_TYPE_AVAILABLE:
	    j = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;
	    break;
	case TP_CONNECTION_PRESENCE_TYPE_AWAY:
	    j = TP_CONNECTION_PRESENCE_TYPE_AWAY;
	    break;
	case TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY:
	    j = TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY;
	    break;
	case TP_CONNECTION_PRESENCE_TYPE_HIDDEN:
	    j = TP_CONNECTION_PRESENCE_TYPE_HIDDEN;
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
	    guint i;
	    /* see if this presence is supported by the connection */
	    for (i = 0; i < priv->recognized_presence_info_array->len; i++)
	    {
		pi = &g_array_index (priv->recognized_presence_info_array,
				     struct presence_info, i);
		if (strcmp (pi->presence_str, mapping->presence_str) == 0)
		{
		    g_debug ("Using %s status for TpConnectionPresenceType %d",
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
mcd_presence_data_free (gpointer userdata)
{
    McdPresenceData *pd = userdata;

    g_free (pd->status);
    g_free (pd->message);
    g_free (pd);
}

static void
presence_set_status_cb (TpConnection *proxy, const GError *error,
			gpointer user_data, GObject *weak_object)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (weak_object);
    McdPresenceData *pd = user_data;

    if (error)
    {
	g_warning ("%s: Setting presence of %s to %d failed: %s",
		   G_STRFUNC, mcd_account_get_unique_name (priv->account),
		   pd->presence, error->message);
    }
    else
    {
	mcd_account_set_current_presence (priv->account, pd->presence,
					  pd->status, pd->message);
    }
}

static void
_mcd_connection_set_presence (McdConnection * connection,
			      TpConnectionPresenceType presence,
			      const gchar *status, const gchar *message)
{
    const gchar *presence_str;
    GHashTable *presence_ht;
    GHashTable *params_ht;
    struct presence_info *supported_presence_info;
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    GValue msg_gval = { 0, };

    if (!priv->tp_conn)
    {
	g_warning ("%s: tp_conn is NULL!", G_STRFUNC);
	_mcd_connection_setup (connection);
	return;
    }
    g_return_if_fail (TP_IS_CONNECTION (priv->tp_conn));
    g_return_if_fail (priv->bus_name != NULL);

    if (!priv->has_presence_if) return;

    supported_presence_info = priv->presence_to_set[presence - 1];

    if (supported_presence_info == NULL)
    {
	g_debug ("No matching supported presence found. "
		 "Account presence has not been changed.");
	return;
    }

    presence_str = g_strdup (supported_presence_info->presence_str);
    presence = presence_str_to_enum (supported_presence_info->presence_str);

    /* FIXME: what should we do when this is NULL? */
    if (presence_str != NULL)
    {
	McdPresenceData *pd;

	presence_ht = g_hash_table_new_full (g_str_hash, g_str_equal,
					     g_free, NULL);
	params_ht = g_hash_table_new (g_str_hash, g_str_equal);

	/* 
	 * Note that we silently ignore the message if Connection Manager
	 * doesn't support it for this presence state!
	 */
	if (supported_presence_info->allow_message && message)
	{
	    g_value_init (&msg_gval, G_TYPE_STRING);
	    g_value_set_string (&msg_gval, message);
	    g_hash_table_insert (params_ht, "message", &msg_gval);
	}

	g_hash_table_insert (presence_ht, (gpointer) presence_str, params_ht);

	pd = g_malloc (sizeof (McdPresenceData));
	pd->presence = presence;
	pd->status = g_strdup (status);
	pd->message = g_strdup (message);
	tp_cli_connection_interface_presence_call_set_status (priv->tp_conn, -1,
							      presence_ht,
							      presence_set_status_cb,
							      pd, mcd_presence_data_free,
							      (GObject *)connection);

	if (supported_presence_info->allow_message && message)
	    g_value_unset (&msg_gval);

	g_hash_table_destroy (presence_ht);
	g_hash_table_destroy (params_ht);
    }
}


static void
presence_get_statuses_cb (TpConnection *proxy, GHashTable *status_hash,
			  const GError *error, gpointer user_data,
			  GObject *weak_object)
{
    McdConnectionPrivate *priv = user_data;
    McdConnection *connection = MCD_CONNECTION (weak_object);
    TpConnectionPresenceType presence;
    const gchar *status, *message;
    guint i;

    if (error)
    {
	g_warning ("%s: Get statuses failed for account %s: %s", G_STRFUNC,
		   mcd_account_get_unique_name (priv->account),
		   error->message);
	return;
    }

    /* so pack the available presences into connection info */
    /* Initialize presence info array and pointers for setting presences */
    for (i = 0; i < LAST_MC_PRESENCE - 1; i++)
	priv->presence_to_set[i] = NULL;
    priv->recognized_presence_info_array =
	g_array_new (FALSE, FALSE, sizeof (struct presence_info));
    g_hash_table_foreach (status_hash, recognize_presence, priv);

    enable_well_known_presences (priv);

    /* Set the fallback presence values */
    for (i = 0; i < LAST_MC_PRESENCE - 1; i++)
    {
	if (priv->presence_to_set[i] == NULL)
	    _mcd_connection_set_fallback_presences (connection, i);
    }

    /* Now the presence info is ready. We can set the presence */
    mcd_account_get_requested_presence (priv->account, &presence,
				       	&status, &message);
    _mcd_connection_set_presence (connection, presence, status, message);
}

static void
_mcd_connection_setup_presence (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    tp_cli_connection_interface_presence_call_get_statuses (priv->tp_conn, -1,
							    presence_get_statuses_cb,
							    priv, NULL,
							    (GObject *)connection);
}

static void
disconnect_cb (TpConnection *proxy, const GError *error, gpointer user_data,
	       GObject *weak_object)
{
    if (error)
	g_warning ("Disconnect failed: %s", error->message);
    g_object_unref (proxy);
}

static void
_mcd_connection_call_disconnect (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;
    guint status;
    
    if (!priv->tp_conn) return;

    g_object_get (G_OBJECT (priv->tp_conn),
		  "status", &status,
		  NULL);
    if (status == TP_CONNECTION_STATUS_DISCONNECTED) return;
    tp_cli_connection_call_disconnect (priv->tp_conn, -1,
				       disconnect_cb,
				       priv, NULL,
				       (GObject *)connection);

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
on_presence_requested (McdAccount *account,
		       TpConnectionPresenceType presence,
		       const gchar *status, const gchar *message,
		       gpointer user_data)
{
    McdConnection *connection = MCD_CONNECTION (user_data);
    McdConnectionPrivate *priv = connection->priv;

    g_debug ("Presence requested: %d", presence);
    if (presence == TP_CONNECTION_PRESENCE_TYPE_UNSET) return;

    if (presence == TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
    {
	/* Connection Proxy */
	priv->abort_reason = TP_CONNECTION_STATUS_REASON_REQUESTED;
	mcd_mission_disconnect (MCD_MISSION (connection));
	_mcd_connection_call_disconnect (connection);
    }
    else
    {
	if (mcd_connection_get_connection_status (connection) == TP_CONNECTION_STATUS_CONNECTED)
	    _mcd_connection_set_presence (connection, presence, status, message);
    }
}

static void
on_new_channel (TpConnection *proxy, const gchar *chan_obj_path,
	       	const gchar *chan_type, guint handle_type, guint handle,
		gboolean suppress_handler, gpointer user_data,
	       	GObject *weak_object)
{
    McdConnection *connection = MCD_CONNECTION (weak_object);
    McdConnectionPrivate *priv = user_data;
    McdChannel *channel;

    /* ignore all our own requests (they have always suppress_handler = 1) as
     * well as other requests for which our intervention has not been requested
     * */
    if (suppress_handler) return;

    /* It's an incoming channel, so we create a new McdChannel for it */
    channel = mcd_channel_new_from_path (proxy,
                                         chan_obj_path,
                                         chan_type, handle, handle_type);
    if (G_UNLIKELY (!channel)) return;

    mcd_operation_take_mission (MCD_OPERATION (connection),
				MCD_MISSION (channel));

    if (priv->can_dispatch)
    {
        /* Dispatch the incoming channel */
        mcd_dispatcher_send (priv->dispatcher, channel);
    }
    else
        mcd_channel_set_status (channel, MCD_CHANNEL_UNDISPATCHED);
}

static void
_foreach_channel_remove (McdMission * mission, McdOperation * operation)
{
    g_assert (MCD_IS_MISSION (mission));
    g_assert (MCD_IS_OPERATION (operation));

    mcd_operation_remove_mission (operation, mission);
}

static void
on_capabilities_changed (TpConnection *proxy, const GPtrArray *caps,
			 gpointer user_data, GObject *weak_object)
{
    McdConnection *connection = user_data;
    McdConnectionPrivate *priv = connection->priv;
    McdChannel *channel = MCD_CHANNEL (weak_object);
    gboolean found = FALSE;
    GType type;
    gchar *chan_type;
    guint chan_handle, chan_handle_type;
    TpProxyPendingCall *call;
    guint i;

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
	goto done;
    chan_handle_type = mcd_channel_get_handle_type (channel);
    g_debug ("%s: requesting channel again (type = %s, handle_type = %u, handle = %u)",
	     G_STRFUNC, chan_type, chan_handle_type, chan_handle);
    call = tp_cli_connection_call_request_channel (priv->tp_conn, -1,
						   chan_type,
						   chan_handle_type,
						   chan_handle, TRUE,
						   request_channel_cb,
						   connection, NULL,
						   (GObject *)channel);
    g_object_set_data ((GObject *)channel, "tp_chan_call", call);
done:
    g_free (chan_type);
}

static gboolean
on_channel_capabilities_timeout (McdChannel *channel,
				 McdConnection *connection)
{
    struct capabilities_wait_data *cwd;
    GError *mc_error;

    cwd = g_object_get_data (G_OBJECT (channel), "error_on_creation");
    if (!cwd) return FALSE;

    /* We reach this point if this channel was waiting for capabilities; we
     * abort it and return the original error */
    g_debug ("%s: channel %p timed out, returning error!", G_STRFUNC, channel);

    mc_error = map_tp_error_to_mc_error (channel, cwd->error);
    _mcd_channel_set_error (channel, mc_error);
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
    const GList *list, *list_curr;

    g_debug ("%s: got_capabilities is %d", G_STRFUNC, priv->got_capabilities);
    priv->got_capabilities = TRUE;
    list = mcd_operation_get_missions ((McdOperation *)connection);
    while (list)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);

	list_curr = list;
	list = list->next;
        if (mcd_channel_get_status (channel) == MCD_CHANNEL_REQUEST &&
            on_channel_capabilities_timeout (channel, connection))
	{
            mcd_mission_abort ((McdMission *)channel);
	}
    }
    priv->capabilities_timer = 0;
    return FALSE;
}

static void
capabilities_advertise_cb (TpConnection *proxy, const GPtrArray *out0,
			   const GError *error, gpointer user_data,
			   GObject *weak_object)
{
    if (error)
    {
	g_warning ("%s: AdvertiseCapabilities failed: %s", G_STRFUNC, error->message);
    }
    
}

static void
_mcd_connection_setup_capabilities (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    GPtrArray *capabilities;
    const gchar *removed = NULL;
    const gchar *protocol_name;
    GType type;
    guint i;

    if (!priv->has_capabilities_if)
    {
	g_debug ("%s: connection does not support capabilities interface", G_STRFUNC);
	priv->got_capabilities = TRUE;
	return;
    }
    protocol_name = mcd_account_get_protocol_name (priv->account);
    capabilities = mcd_dispatcher_get_channel_capabilities (priv->dispatcher,
							    protocol_name);
    g_debug ("%s: advertising capabilities", G_STRFUNC);
    tp_cli_connection_interface_capabilities_call_advertise_capabilities (priv->tp_conn, -1,
									  capabilities,
									  &removed,
									  capabilities_advertise_cb,
									  priv, NULL,
									  (GObject *) connection);
    if (priv->capabilities_timer)
    {
	g_warning ("This connection still has dangling capabilities timer on");
	g_source_remove (priv->capabilities_timer);
    }
    priv->capabilities_timer =
	g_timeout_add (1000 * 10, (GSourceFunc)on_capabilities_timeout, connection);

    /* free the connection capabilities */
    type = dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING,
				   G_TYPE_UINT, G_TYPE_INVALID);
    for (i = 0; i < capabilities->len; i++)
	g_boxed_free (type, g_ptr_array_index (capabilities, i));
    g_ptr_array_free (capabilities, TRUE);
}

static void
inspect_handles_cb (TpConnection *proxy, const gchar **names,
		    const GError *error, gpointer user_data,
		    GObject *weak_object)
{
    McdConnectionPrivate *priv = user_data;

    if (error)
    {
	g_warning ("%s: InspectHandles failed: %s", G_STRFUNC, error->message);
	return;
    }
    if (names && names[0] != NULL)
    {
	mcd_account_set_normalized_name (priv->account, names[0]);
    }
}

static void
_mcd_connection_get_normalized_name (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;
    GArray *handles;

    handles = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
    g_array_append_val (handles, priv->self_handle);
    tp_cli_connection_call_inspect_handles (priv->tp_conn, -1,
					    TP_HANDLE_TYPE_CONTACT,
					    handles,
					    inspect_handles_cb, priv, NULL,
					    (GObject *)connection);
    g_array_free (handles, TRUE); 
}

static void
get_self_handle_cb (TpConnection *proxy, guint self_handle,
		    const GError *error, gpointer user_data,
		    GObject *weak_object)
{
    McdConnection *connection = MCD_CONNECTION (weak_object);
    McdConnectionPrivate *priv = user_data;

    if (!error)
    {
	priv->self_handle = self_handle;
	_mcd_connection_get_normalized_name (connection);
    }
    else
	g_warning ("GetSelfHandle failed for connection %p: %s",
		   connection, error->message);
}

static void
_mcd_connection_get_self_handle (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;

    tp_cli_connection_call_get_self_handle (priv->tp_conn, -1,
					    get_self_handle_cb,
					    priv, NULL,
					    (GObject *)connection);
}

static void
avatars_set_avatar_cb (TpConnection *proxy, const gchar *token,
		       const GError *error, gpointer user_data,
		       GObject *weak_object)
{
    McdConnectionPrivate *priv = user_data;

    priv->setting_avatar = FALSE;
    if (error)
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
	return;
    }
    g_debug ("%s: received token: %s", G_STRFUNC, token);
    mcd_account_set_avatar_token (priv->account, token);
}

static void
avatars_clear_avatar_cb (TpConnection *proxy, const GError *error,
			 gpointer user_data, GObject *weak_object)
{
    if (!error)
    {
	g_debug ("%s: Clear avatar succeeded", G_STRFUNC);
    }
    else
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
    }
}

static void
on_avatar_retrieved (TpConnection *proxy, guint contact_id, const gchar *token,
		     const GArray *avatar, const gchar *mime_type,
		     gpointer user_data, GObject *weak_object)
{
    McdConnectionPrivate *priv = user_data;
    gchar *prev_token = NULL;

    if (contact_id != priv->self_handle) return;

    /* if we are setting the avatar, we must ignore this signal */
    if (priv->setting_avatar) return;

    g_debug ("%s: Avatar retrieved for contact %d, token: %s", G_STRFUNC, contact_id, token);
    prev_token = mcd_account_get_avatar_token (priv->account);

    if (!prev_token || strcmp (token, prev_token) != 0)
    {
	g_debug ("%s: received mime-type: %s", G_STRFUNC, mime_type);
	mcd_account_set_avatar (priv->account, avatar, mime_type, token, NULL);
    }
    g_free (prev_token);
}

static void
avatars_request_avatars_cb (TpConnection *proxy, const GError *error,
			    gpointer user_data, GObject *weak_object)
{
    if (error)
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
    }
}

static void
on_avatar_updated (TpConnection *proxy, guint contact_id, const gchar *token,
		   gpointer user_data, GObject *weak_object)
{
    McdConnectionPrivate *priv = user_data;
    McdConnection *connection = MCD_CONNECTION (weak_object);
    gchar *prev_token;

    if (contact_id != priv->self_handle) return;

    /* if we are setting the avatar, we must ignore this signal */
    if (priv->setting_avatar) return;

    g_debug ("%s: contact %d, token: %s", G_STRFUNC, contact_id, token);
    if (!(prev_token = mcd_account_get_avatar_token (priv->account)))
	return;

    if (!prev_token || strcmp (token, prev_token) != 0)
    {
    	GArray handles;
	g_debug ("%s: avatar has changed", G_STRFUNC);
	/* the avatar has changed, let's retrieve the new one */
	handles.len = 1;
	handles.data = (gchar *)&contact_id;
	tp_cli_connection_interface_avatars_call_request_avatars (priv->tp_conn, -1,
								  &handles,
								  avatars_request_avatars_cb,
								  priv, NULL,
								  (GObject *)connection);
    }
    g_free (prev_token);
}

static void
_mcd_connection_set_avatar (McdConnection *connection, const GArray *avatar,
			    const gchar *mime_type)
{
    McdConnectionPrivate *priv = connection->priv;

    g_debug ("%s called", G_STRFUNC);
    if (avatar->len > 0 && avatar->len < G_MAXUINT)
    {
	tp_cli_connection_interface_avatars_call_set_avatar (priv->tp_conn, -1,
							     avatar, mime_type,
							     avatars_set_avatar_cb,
							     priv, NULL,
							     (GObject *)connection);
	priv->setting_avatar = TRUE;
    }
    else
	tp_cli_connection_interface_avatars_call_clear_avatar (priv->tp_conn, -1,
							       avatars_clear_avatar_cb,
							       NULL,
							       g_free,
							       (GObject *)connection);
}

static void
avatars_request_tokens_cb (TpConnection *proxy, GHashTable *tokens,
			   const GError *error, gpointer user_data,
			   GObject *weak_object)
{
    McdConnectionPrivate *priv = (McdConnectionPrivate *)user_data;
    McdConnection *connection = MCD_CONNECTION (weak_object);
    GArray *avatar = NULL;
    const gchar *token;
    gchar *mime_type;

    if (error)
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
	return;
    }

    token = g_hash_table_lookup (tokens, GUINT_TO_POINTER (priv->self_handle));
    if (token)
	return;

    mcd_account_get_avatar (priv->account, &avatar, &mime_type);

    g_debug ("No avatar set, setting our own");
    _mcd_connection_set_avatar (connection, avatar, mime_type);

    g_array_free (avatar, TRUE);
    g_free (mime_type);
}

static void
on_account_avatar_changed (McdAccount *account, const GArray *avatar,
			   const gchar *mime_type, McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    if (!priv->has_avatars_if) return;
    _mcd_connection_set_avatar (connection, avatar, mime_type);
}

static void
_mcd_connection_setup_avatar (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;
    gchar *mime_type, *token;
    GArray *avatar;

    if (!priv->has_avatars_if)
	return;

    tp_cli_connection_interface_avatars_connect_to_avatar_updated (priv->tp_conn,
								   on_avatar_updated,
								   priv, NULL,
								   (GObject *)connection,
								   NULL);
    tp_cli_connection_interface_avatars_connect_to_avatar_retrieved (priv->tp_conn,
								     on_avatar_retrieved,
								     priv, NULL,
								     (GObject *)connection,
								     NULL);
    priv->setting_avatar = FALSE;

    mcd_account_get_avatar (priv->account, &avatar, &mime_type);

    if (avatar)
    {
	token = mcd_account_get_avatar_token (priv->account);
	g_free (token);
	if (!token)
	    _mcd_connection_set_avatar (connection, avatar, mime_type);
	else
	{
	    GArray handles;

	    g_debug ("checking for server token");
	    /* Set the avatar only if no other one was set */
	    handles.len = 1;
	    handles.data = (gchar *)&priv->self_handle;
	    tp_cli_connection_interface_avatars_call_get_known_avatar_tokens (priv->tp_conn, -1,
									      &handles,
									      avatars_request_tokens_cb,
									      priv, NULL,
									      (GObject *)connection);
	}
	g_array_free (avatar, TRUE);
    }
    g_free (mime_type);
}

static void
on_aliases_changed (TpConnection *proxy, const GPtrArray *aliases,
		    gpointer user_data, GObject *weak_object)
{
    McdConnectionPrivate *priv = user_data;
    GType type;
    gchar *alias;
    guint contact;
    guint i;

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
		mcd_account_set_alias (priv->account, alias);
	    }
	    break;
	}
	g_free (alias);
    }
}

static void
aliasing_set_aliases_cb (TpConnection *proxy, const GError *error,
			 gpointer user_data, GObject *weak_object)
{
    if (error)
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
    }
}

static void
_mcd_connection_set_alias (McdConnection *connection,
			   const gchar *alias)
{
    McdConnectionPrivate *priv = connection->priv;
    GHashTable *aliases;

    g_debug ("%s: setting alias '%s'", G_STRFUNC, alias);

    aliases = g_hash_table_new (NULL, NULL);
    g_hash_table_insert (aliases, GINT_TO_POINTER(priv->self_handle),
			 (gchar *)alias);
    tp_cli_connection_interface_aliasing_call_set_aliases (priv->tp_conn, -1,
							   aliases,
							   aliasing_set_aliases_cb,
							   priv, NULL,
							   (GObject *)connection);
    g_hash_table_destroy (aliases);
}

static void
on_account_alias_changed (McdAccount *account, const gchar *alias,
			  McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    if (!priv->has_alias_if) return;
    _mcd_connection_set_alias (connection, alias);
}

static void
_mcd_connection_setup_alias (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;
    gchar *alias;

    tp_cli_connection_interface_aliasing_connect_to_aliases_changed (priv->tp_conn,
								     on_aliases_changed,
								     priv, NULL,
								     (GObject *)connection,
								     NULL);
    alias = mcd_account_get_alias (priv->account);
    if (alias && (!priv->alias || strcmp (priv->alias, alias) != 0))
	_mcd_connection_set_alias (connection, alias);
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
on_connection_status_changed (TpConnection *tp_conn, GParamSpec *pspec,
			      McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    TpConnectionStatus conn_status;
    TpConnectionStatusReason conn_reason;

    g_object_get (G_OBJECT (tp_conn),
		  "status", &conn_status,
		  "status-reason", &conn_reason,
		  NULL);
    g_debug ("%s: status_changed called from tp (%d)", G_STRFUNC, conn_status);

    switch (conn_status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
	mcd_account_set_connection_status (priv->account,
					   conn_status, conn_reason);
	priv->abort_reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;
	priv->reconnection_requested = FALSE;
	break;
    case TP_CONNECTION_STATUS_CONNECTED:
	{
	    mcd_account_set_connection_status (priv->account,
					       conn_status, conn_reason);
	    _mcd_connection_get_self_handle (connection);
	    priv->reconnect_interval = 30 * 1000; /* reset it to 30 seconds */
	}
	break;
    case TP_CONNECTION_STATUS_DISCONNECTED:
	/* Connection could die during account status updated if its
	 * manager is the only one holding the reference to it (manager will
	 * remove the connection from itself). To ensure we get a chance to
	 * emit abort signal (there could be others holding a ref to it), we
	 * will hold a temporary ref to it.
	 */
	priv->abort_reason = conn_reason;
	
	if (conn_reason != TP_CONNECTION_STATUS_REASON_REQUESTED &&
	    conn_reason != TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED)
	{
	    mcd_account_request_presence (priv->account,
					  TP_CONNECTION_PRESENCE_TYPE_UNSET,
					  NULL, NULL);
	}
	break;
    default:
	g_warning ("Unknown telepathy connection status");
    }
}

static void proxy_destroyed (DBusGProxy *tp_conn, guint domain, gint code,
			     gchar *message, McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    g_debug ("Proxy destroyed (%s)!", message);

    _mcd_connection_release_tp_connection (connection);

    /* Destroy any pending timer */
    if (priv->capabilities_timer)
    {
	g_source_remove (priv->capabilities_timer);
	priv->capabilities_timer = 0;
    }

    if (priv->reconnection_requested)
    {
	g_debug ("Preparing for reconnection");
	/* we were disconnected by a network error or by a gabble crash (in
	 * the latter case, we get NoneSpecified as a reason): don't abort
	 * the connection but try to reconnect later */
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
	mcd_account_set_connection_status (priv->account,
					   TP_CONNECTION_STATUS_CONNECTING,
					   TP_CONNECTION_STATUS_REASON_REQUESTED);
	priv->reconnection_requested = FALSE;
    }
    else
    {
	g_object_ref (connection);
	/* Notify connection abort */
	mcd_mission_abort (MCD_MISSION (connection));
	g_object_unref (connection);
    }
}

static void
connect_cb (TpConnection *tp_conn, const GError *error,
		      gpointer user_data, GObject *weak_object)
{
    McdConnection *connection = MCD_CONNECTION (weak_object);

    g_debug ("%s called for connection %p", G_STRFUNC, connection);

    if (error)
    {
	g_warning ("%s: tp_conn_connect failed: %s",
		   G_STRFUNC, error->message);
    }
}

static void
request_unrequested_channels (McdConnection *connection)
{
    const GList *channels;

    channels = mcd_operation_get_missions ((McdOperation *)connection);

    g_debug ("%s called", G_STRFUNC);
    /* go through the channels that were requested while the connection was not
     * ready, and process them */
    while (channels)
    {
	McdChannel *channel = MCD_CHANNEL (channels->data);

        if (mcd_channel_get_status (channel) == MCD_CHANNEL_REQUEST)
        {
            g_debug ("Requesting channel %p", channel);
            mcd_connection_request_channel (connection, channel);
        }
        channels = channels->next;
    }
}

static void
dispatch_undispatched_channels (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;
    const GList *channels;

    priv->can_dispatch = TRUE;
    channels = mcd_operation_get_missions ((McdOperation *)connection);

    g_debug ("%s called", G_STRFUNC);
    while (channels)
    {
	McdChannel *channel = MCD_CHANNEL (channels->data);

        if (mcd_channel_get_status (channel) == MCD_CHANNEL_UNDISPATCHED)
        {
            g_debug ("Dispatching channel %p", channel);
            /* dispatch the channel */
            mcd_dispatcher_send (priv->dispatcher, channel);
        }
        channels = channels->next;
    }
}

static void
on_new_channels (TpConnection *proxy, const GPtrArray *channels,
                 gpointer user_data, GObject *weak_object)
{
    McdConnection *connection = MCD_CONNECTION (weak_object);
    McdConnectionPrivate *priv = user_data;
    McdChannel *channel;
    GList *channel_list = NULL;
    gboolean requested = FALSE;
    guint i;

    /* we can completely ignore the channels that arrive while can_dispatch is
     * FALSE: the on_new_channel handler is already recording them */
    if (!priv->can_dispatch) return;

    for (i = 0; i < channels->len; i++)
    {
        GValueArray *va;
        const gchar *object_path, *channel_type;
        GHashTable *props;
        GValue *value;
        guint handle_type, handle;

        va = g_ptr_array_index (channels, i);
        object_path = g_value_get_boxed (va->values);
        props = g_value_get_boxed (va->values + 1);

        /* Don't do anything for requested channels */
        value = g_hash_table_lookup (props, TP_IFACE_CHANNEL ".Requested");
        if (value && g_value_get_boolean (value))
        {
            requested = TRUE;
            /* FIXME: once the CMs emit this signal _after_ having returned
             * from CreateChannel(), we can handle requested channels here,
             * too. */
            continue;
        }

        /* get channel type, handle type, handle */
        value = g_hash_table_lookup (props, TP_IFACE_CHANNEL ".ChannelType");
        channel_type = value ? g_value_get_string (value) : NULL;

        value = g_hash_table_lookup (props,
                                     TP_IFACE_CHANNEL ".TargetHandleType");
        handle_type = value ? g_value_get_uint (value) : 0;

        value = g_hash_table_lookup (props, TP_IFACE_CHANNEL ".TargetHandle");
        handle = value ? g_value_get_uint (value) : 0;

        g_debug ("%s: type = %s, handle_type = %u, handle = %u", G_STRFUNC,
                 channel_type, handle_type, handle);
        channel = mcd_channel_new_from_path (proxy, object_path, channel_type,
                                             handle, handle_type);
        if (G_UNLIKELY (!channel)) continue;

        /* properties need to be copied */
        props = g_value_dup_boxed (va->values + 1);
        _mcd_channel_set_immutable_properties (channel, props);
        mcd_operation_take_mission (MCD_OPERATION (connection),
                                    MCD_MISSION (channel));

        channel_list = g_list_prepend (channel_list, channel);
    }

    /* FIXME: once the CMs emit this signal _after_ having returned from
     * CreateChannel(), we can handle requested channels here, too. */
    if (requested) return;

    _mcd_dispatcher_send_channels (priv->dispatcher, channel_list, requested);
}

static void get_all_requests_cb (TpProxy *proxy, GHashTable *properties,
                                 const GError *error, gpointer user_data,
                                 GObject *weak_object)
{
    McdConnection *connection = MCD_CONNECTION (weak_object);
    McdConnectionPrivate *priv = user_data;
    const GList *mcd_channels, *list;
    GPtrArray *channels;
    GValue *value;
    guint i;

    value = g_hash_table_lookup (properties, "Channels");
    if (!G_VALUE_HOLDS (value, TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST))
    {
        g_warning ("%s: property Channels has type %s, expecting %s",
                   G_STRFUNC, G_VALUE_TYPE_NAME (value),
                   g_type_name (TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST));
        return;
    }

    mcd_channels = mcd_operation_get_missions ((McdOperation *)connection);
    channels = g_value_get_boxed (value);
    for (i = 0; i < channels->len; i++)
    {
        GValueArray *va;
        const gchar *object_path;
        GHashTable *channel_props;

        va = g_ptr_array_index (channels, i);
        object_path = g_value_get_boxed (va->values);
        channel_props = g_value_dup_boxed (va->values + 1);
        /* find the McdChannel */
        for (list = mcd_channels; list != NULL; list = list->next)
        {
            McdChannel *channel = MCD_CHANNEL (list->data);
            const gchar *channel_path;

            if (mcd_channel_get_status (channel) != MCD_CHANNEL_UNDISPATCHED)
                continue;
            channel_path = mcd_channel_get_object_path (channel);
            if (channel_path && strcmp (channel_path, object_path) == 0)
            {
                _mcd_channel_set_immutable_properties (channel, channel_props);
                /* channel is ready for dispatching */
                mcd_dispatcher_send (priv->dispatcher, channel);
                break;
            }
        }
    }

    tp_proxy_signal_connection_disconnect (priv->new_channel_sc);
    priv->can_dispatch = TRUE;
}

static void
mcd_connection_setup_requests (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;

    /*
     * 1. connect to the NewChannels
     * 2. get existing channels
     * 3. disconnect from NewChannel
     * 4. dispatch the UNDISPATCHED
     */
    tp_cli_connection_interface_requests_connect_to_new_channels
        (priv->tp_conn, on_new_channels, priv, NULL,
         (GObject *)connection, NULL);

    tp_cli_dbus_properties_call_get_all (priv->tp_conn, -1,
        TP_IFACE_CONNECTION_INTERFACE_REQUESTS,
        get_all_requests_cb, priv, NULL, (GObject *)connection);
}

static void
on_connection_ready (TpConnection *tp_conn, const GError *error,
		     gpointer user_data)
{
    McdConnection *connection, **connection_ptr = user_data;
    McdConnectionPrivate *priv;

    connection = *connection_ptr;
    if (connection)
	g_object_remove_weak_pointer ((GObject *)connection,
				      (gpointer)connection_ptr);
    g_slice_free (McdConnection *, connection_ptr);
    if (error)
    {
	g_debug ("%s got error: %s", G_STRFUNC, error->message);
	return;
    }

    if (!connection) return;

    g_debug ("%s: connection is ready", G_STRFUNC);
    priv = MCD_CONNECTION_PRIV (connection);

    priv->has_presence_if = tp_proxy_has_interface_by_id (tp_conn,
							  TP_IFACE_QUARK_CONNECTION_INTERFACE_PRESENCE);
    priv->has_avatars_if = tp_proxy_has_interface_by_id (tp_conn,
							 TP_IFACE_QUARK_CONNECTION_INTERFACE_AVATARS);
    priv->has_alias_if = tp_proxy_has_interface_by_id (tp_conn,
						       TP_IFACE_QUARK_CONNECTION_INTERFACE_ALIASING);
    priv->has_capabilities_if = tp_proxy_has_interface_by_id (tp_conn,
							      TP_IFACE_QUARK_CONNECTION_INTERFACE_CAPABILITIES);
    priv->has_requests_if = tp_proxy_has_interface_by_id (tp_conn,
        TP_IFACE_QUARK_CONNECTION_INTERFACE_REQUESTS);

    if (priv->has_presence_if)
	_mcd_connection_setup_presence (connection);

    if (priv->has_capabilities_if)
	_mcd_connection_setup_capabilities (connection);

    if (priv->has_avatars_if)
	_mcd_connection_setup_avatar (connection);

    if (priv->has_alias_if)
	_mcd_connection_setup_alias (connection);

    if (priv->has_requests_if)
        mcd_connection_setup_requests (connection);
    else
        dispatch_undispatched_channels (connection);

    /* and request all channels */
    request_unrequested_channels (connection);
}

static void
request_connection_cb (TpConnectionManager *proxy, const gchar *bus_name,
		       const gchar *obj_path, const GError *tperror,
		       gpointer user_data, GObject *weak_object)
{
    McdConnection *connection = MCD_CONNECTION (weak_object);
    McdConnection **connection_ptr;
    McdConnectionPrivate *priv = user_data;
    GError *error = NULL;

    if (tperror)
    {
	g_warning ("%s: RequestConnection failed: %s",
		   G_STRFUNC, tperror->message);
	mcd_account_set_connection_status (priv->account,
					   TP_CONNECTION_STATUS_DISCONNECTED,
					   TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
	return;
    }

    priv->tp_conn = tp_connection_new (priv->dbus_daemon, bus_name,
				       obj_path, &error);
    if (!priv->tp_conn)
    {
	g_warning ("%s: tp_connection_new failed: %s",
		   G_STRFUNC, error->message);
	mcd_account_set_connection_status (priv->account,
					   TP_CONNECTION_STATUS_DISCONNECTED,
					   TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
	g_error_free (error);
	return;
    }
    _mcd_account_tp_connection_changed (priv->account);

    /* Setup signals */
    g_signal_connect (priv->tp_conn, "invalidated",
		      G_CALLBACK (proxy_destroyed), connection);
    g_signal_connect (priv->tp_conn, "notify::status",
		      G_CALLBACK (on_connection_status_changed),
		      connection);
    /* HACK for cancelling the _call_when_ready() callback when our object gets
     * destroyed */
    connection_ptr = g_slice_alloc (sizeof (McdConnection *));
    *connection_ptr = connection;
    g_object_add_weak_pointer ((GObject *)connection,
			       (gpointer)connection_ptr);
    tp_connection_call_when_ready (priv->tp_conn, on_connection_ready,
				   connection_ptr);
    priv->new_channel_sc =
        tp_cli_connection_connect_to_new_channel (priv->tp_conn,
                                                  on_new_channel,
                                                  priv, NULL,
                                                  (GObject *)connection, NULL);

    /* FIXME we don't know the status of the connection yet, but calling
     * Connect shouldn't cause any harm
     * https://bugs.freedesktop.org/show_bug.cgi?id=14620 */
    tp_cli_connection_call_connect (priv->tp_conn, -1, connect_cb, priv, NULL,
				    (GObject *)connection);
}

static void
_mcd_connection_connect (McdConnection *connection, GHashTable *params)
{
    McdConnectionPrivate *priv = connection->priv;
    const gchar *protocol_name;
    const gchar *account_name;

    protocol_name = mcd_account_get_protocol_name (priv->account);
    account_name = mcd_account_get_unique_name (priv->account);

    g_debug ("%s: Trying connect account: %s",
	     G_STRFUNC, (gchar *) account_name);

    /* TODO: add extra parameters? */
    tp_cli_connection_manager_call_request_connection (priv->tp_conn_mgr, -1,
						       protocol_name, params,
						       request_connection_cb,
						       priv, NULL,
						       (GObject *)connection);
}

static void
mcd_connection_get_params_and_connect (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    GHashTable *params = NULL;
    const gchar *account_name;

    g_debug ("%s called for %p", G_STRFUNC, connection);
    mcd_account_set_connection_status (priv->account,
				       TP_CONNECTION_STATUS_CONNECTING,
				       TP_CONNECTION_STATUS_REASON_REQUESTED);

    account_name = mcd_account_get_unique_name (priv->account);

    g_debug ("%s: Trying connect account: %s",
	     G_STRFUNC, (gchar *) account_name);

    params = g_object_get_data ((GObject *)connection, "params");
    _mcd_connection_connect (connection, params);
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
	TP_CONNECTION_STATUS_DISCONNECTED)
     * but since we set the account status to CONNECTING as soon as we got
     * disconnected by a network error, we must accept that status, too */
    if (mcd_connection_get_connection_status (connection) !=
	TP_CONNECTION_STATUS_CONNECTED)
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
    mcd_account_set_current_presence (priv->account,
				      TP_CONNECTION_PRESENCE_TYPE_OFFLINE,
				      "offline", NULL);
    mcd_account_set_connection_status (priv->account,
				       TP_CONNECTION_STATUS_DISCONNECTED, 
				       priv->abort_reason);
    if (priv->tp_conn)
    {
	/* Disconnect signals */
	g_signal_handlers_disconnect_by_func (priv->tp_conn,
					      G_CALLBACK (on_connection_status_changed),
					      connection);
	g_signal_handlers_disconnect_by_func (G_OBJECT (priv->tp_conn),
					      G_CALLBACK (proxy_destroyed),
					      connection);

	_mcd_connection_call_disconnect (connection);
	/* g_object_unref (priv->tp_conn) is done in the disconnect_cb */
	priv->tp_conn = NULL;
	_mcd_account_tp_connection_changed (priv->account);
    }

    /* the interface proxies obtained from this connection must be deleted, too
     */
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

    _mcd_connection_release_tp_connection (connection);
    
    if (priv->account)
    {
	g_signal_handlers_disconnect_by_func (priv->account,
					      G_CALLBACK
					      (on_presence_requested), object);
	g_signal_handlers_disconnect_by_func (priv->account,
					      G_CALLBACK (on_account_avatar_changed),
					      object);
	g_signal_handlers_disconnect_by_func (priv->account,
					      G_CALLBACK (on_account_alias_changed),
					      object);
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

    if (priv->dbus_daemon)
    {
	g_object_unref (priv->dbus_daemon);
	priv->dbus_daemon = NULL;
    }

    G_OBJECT_CLASS (mcd_connection_parent_class)->dispose (object);
}

static void
_mcd_connection_set_property (GObject * obj, guint prop_id,
			      const GValue * val, GParamSpec * pspec)
{
    McdDispatcher *dispatcher;
    McdAccount *account;
    TpConnectionManager *tp_conn_mgr;
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (obj);

    switch (prop_id)
    {
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
    case PROP_BUS_NAME:
	g_return_if_fail (g_value_get_string (val) != NULL);
	g_free (priv->bus_name);
	priv->bus_name = g_strdup (g_value_get_string (val));
	break;
    case PROP_TP_MANAGER:
	tp_conn_mgr = g_value_get_object (val);
	g_object_ref (tp_conn_mgr);
	if (priv->tp_conn_mgr)
	    g_object_unref (priv->tp_conn_mgr);
	priv->tp_conn_mgr = tp_conn_mgr;
	break;
    case PROP_ACCOUNT:
	account = g_value_get_object (val);
	g_return_if_fail (MCD_IS_ACCOUNT (account));
	g_object_ref (account);
	priv->account = account;
	g_signal_connect (priv->account,
			  "requested-presence-changed",
			  G_CALLBACK (on_presence_requested), obj);
	g_signal_connect (priv->account,
			  "mcd-avatar-changed",
			  G_CALLBACK (on_account_avatar_changed), obj);
	g_signal_connect (priv->account,
			  "alias-changed",
			  G_CALLBACK (on_account_alias_changed), obj);
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
    case PROP_DBUS_DAEMON:
	g_value_set_object (val, priv->dbus_daemon);
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
				     PROP_DISPATCHER,
				     g_param_spec_object ("dispatcher",
							  _("Dispatcher Object"),
							  _("Dispatcher to dispatch channels"),
							  MCD_TYPE_DISPATCHER,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class, PROP_DBUS_DAEMON,
				     g_param_spec_object ("dbus-daemon",
							  _("DBus daemon"),
							  _("DBus daemon"),
							  TP_TYPE_DBUS_DAEMON,
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
							  TP_TYPE_CONNECTION_MANAGER,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class, PROP_TP_CONNECTION,
				     g_param_spec_object ("tp-connection",
							  _
							  ("Telepathy Connection Object"),
							  _
							  ("Telepathy Connection Object which this connection uses"),
							  TP_TYPE_CONNECTION,
							  G_PARAM_READABLE));
    g_object_class_install_property (object_class, PROP_ACCOUNT,
				     g_param_spec_object ("account",
							  _("Account Object"),
							  _
							  ("Account that will be used to create this connection"),
							  MCD_TYPE_ACCOUNT,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));
}

static void
mcd_connection_init (McdConnection * connection)
{
    McdConnectionPrivate *priv;
   
    priv = G_TYPE_INSTANCE_GET_PRIVATE (connection, MCD_TYPE_CONNECTION,
					McdConnectionPrivate);
    connection->priv = priv;

    priv->abort_reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;

    priv->reconnect_interval = 30 * 1000; /* 30 seconds */
}

/* Public methods */

/* Creates a new connection object. Increases a refcount of account.
 * Uses mcd_get_manager function to get TpConnManager
 */
McdConnection *
mcd_connection_new (TpDBusDaemon *dbus_daemon,
		    const gchar * bus_name,
		    TpConnectionManager * tp_conn_mgr,
		    McdAccount * account,
		    McdDispatcher *dispatcher)
{
    McdConnection *mcdconn = NULL;
    g_return_val_if_fail (dbus_daemon != NULL, NULL);
    g_return_val_if_fail (bus_name != NULL, NULL);
    g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (tp_conn_mgr), NULL);
    g_return_val_if_fail (MCD_IS_ACCOUNT (account), NULL);

    mcdconn = g_object_new (MCD_TYPE_CONNECTION,
			    "dbus-daemon", dbus_daemon,
			    "bus-name", bus_name,
			    "tp-manager", tp_conn_mgr,
			    "dispatcher", dispatcher,
			    "account", account, NULL);
    return mcdconn;
}

/* Constant getters. These should probably be removed */

McdAccount *
mcd_connection_get_account (McdConnection * id)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (id);
    return priv->account;
}

TpConnectionStatus
mcd_connection_get_connection_status (McdConnection * id)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (id);
    return mcd_account_get_connection_status (priv->account);
}

TpConnectionStatusReason
mcd_connection_get_connection_status_reason (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;
    TpConnectionStatusReason conn_reason;

    if (priv->tp_conn)
	g_object_get (G_OBJECT (priv->tp_conn),
		      "status-reason", &conn_reason,
		      NULL);
    else
	conn_reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;
    return conn_reason;
}

gboolean
mcd_connection_get_telepathy_details (McdConnection * id,
				      gchar ** ret_servname,
				      gchar ** ret_objpath)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (id);

    g_return_val_if_fail (priv->tp_conn != NULL, FALSE);
    g_return_val_if_fail (TP_IS_CONNECTION (priv->tp_conn), FALSE);

    /* Query the properties required for creation of identical TpConn object */
    *ret_objpath =
	g_strdup (TP_PROXY (priv->tp_conn)->object_path);
    *ret_servname =
	g_strdup (TP_PROXY (priv->tp_conn)->bus_name);

    return TRUE;
}

static GError *
map_tp_error_to_mc_error (McdChannel *channel, const GError *error)
{
    MCError mc_error_code = MC_CHANNEL_REQUEST_GENERIC_ERROR;
    
    g_warning ("Telepathy Error = %s", error->message);
    
    /* TODO : Are there still more specific errors we need
     * to distinguish?
     */
    if (mcd_channel_get_channel_type_quark (channel) ==
	TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA &&
	error->code == TP_ERROR_NOT_AVAILABLE)
    {
	mc_error_code = MC_CONTACT_DOES_NOT_SUPPORT_VOICE_ERROR;
    }
    else if (error->code == TP_ERROR_CHANNEL_BANNED)
    {
	mc_error_code = MC_CHANNEL_BANNED_ERROR;
    }
    else if (error->code == TP_ERROR_CHANNEL_FULL)
    {
	mc_error_code = MC_CHANNEL_FULL_ERROR;
    }
    else if (error->code == TP_ERROR_CHANNEL_INVITE_ONLY)
    {
	mc_error_code = MC_CHANNEL_INVITE_ONLY_ERROR;
    }
    else if (error->code == TP_ERROR_INVALID_HANDLE)
    {
	mc_error_code = MC_INVALID_HANDLE_ERROR;
    }
    return g_error_new (MC_ERROR, mc_error_code, "Telepathy Error: %s",
			error->message);
}

static void
remove_capabilities_refs (gpointer data)
{
    struct capabilities_wait_data *cwd = data;

    g_debug ("\n\n\n%s called\n\n\n", G_STRFUNC);
    tp_proxy_signal_connection_disconnect (cwd->signal_connection);
    g_error_free (cwd->error);
    g_free (cwd);
}

static void
request_channel_cb (TpConnection *proxy, const gchar *channel_path,
		    const GError *tp_error, gpointer user_data,
		    GObject *weak_object)
{
    McdChannel *channel = MCD_CHANNEL (weak_object);
    McdConnection *connection = user_data;
    McdConnectionPrivate *priv = connection->priv;
    GError *error_on_creation;
    struct capabilities_wait_data *cwd;
    GQuark chan_type;
    TpHandleType chan_handle_type;
    guint chan_handle;
    /* We handle only the dbus errors */
    
    /* ChannelRequestor *chan_req = (ChannelRequestor *)user_data; */
    g_object_steal_data (G_OBJECT (channel), "tp_chan_call");

    chan_handle = mcd_channel_get_handle (channel);
    chan_handle_type = mcd_channel_get_handle_type (channel);
    chan_type = mcd_channel_get_channel_type_quark (channel);

    cwd = g_object_get_data (G_OBJECT (channel), "error_on_creation");
    if (cwd)
    {
	error_on_creation = cwd->error;
	g_object_set_data (G_OBJECT (channel), "error_on_creation", NULL);
    }
    else
	error_on_creation = NULL;

    
    if (tp_error != NULL)
    {
	g_debug ("%s: Got error: %s", G_STRFUNC, tp_error->message);
	if (error_on_creation != NULL)
	{
	    /* replace the error, so that the initial one is reported */
	    tp_error = error_on_creation;
	}

	if (priv->got_capabilities || error_on_creation)
	{
	    /* Faild dispatch */
	    GError *mc_error = map_tp_error_to_mc_error (channel, tp_error);
            _mcd_channel_set_error (channel, mc_error);
            mcd_mission_abort ((McdMission *)channel);
	}
	else
	{
	    /* the channel request has failed probably because we are just
	     * connected and we didn't recive the contact capabilities yet. In
	     * this case, wait for this contact's capabilities to arrive */
	    g_debug ("%s: listening for remote capabilities on channel handle %d, type %d",
		     G_STRFUNC, chan_handle, mcd_channel_get_handle_type (channel));
	    /* Store the error, we might need it later */
	    cwd = g_malloc (sizeof (struct capabilities_wait_data));
	    cwd->error = g_error_copy (tp_error);
	    cwd->signal_connection =
		tp_cli_connection_interface_capabilities_connect_to_capabilities_changed (priv->tp_conn,
											  on_capabilities_changed,
											  connection, NULL,
											  (GObject *)channel,
											  NULL);
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
        _mcd_channel_set_error (channel, mc_error);
        mcd_mission_abort ((McdMission *)channel);
	return;
    }

    /* TODO: construct the a{sv} of immutable properties */
    /* Everything here is well and fine. We can create the channel proxy. */
    if (!mcd_channel_set_object_path (channel, priv->tp_conn, channel_path))
    {
        mcd_mission_abort ((McdMission *)channel);
        return;
    }

    /* Dispatch the incoming channel */
    mcd_dispatcher_send (priv->dispatcher, channel);
}

static void
request_handles_cb (TpConnection *proxy, const GArray *handles,
		    const GError *error, gpointer user_data,
		    GObject *weak_object)
{
    McdChannel *channel, *existing_channel;
    McdConnection *connection = user_data;
    McdConnectionPrivate *priv = connection->priv;
    guint chan_handle, chan_handle_type;
    GQuark chan_type;
    const GList *channels;

    channel = MCD_CHANNEL (weak_object);
    
    if (error != NULL || g_array_index (handles, guint, 0) == 0)
    {
	GError *mc_error;
	const gchar *msg;

	msg = error ? error->message : "got handle 0";
	g_warning ("Could not map string handle to a valid handle!: %s",
		   msg);
	
	/* Fail dispatch */
	mc_error = g_error_new (MC_ERROR, MC_INVALID_HANDLE_ERROR,
		     "Could not map string handle to a valid handle!: %s",
		     msg);
        _mcd_channel_set_error (channel, mc_error);
	
	/* No abort, because we are the only one holding the only reference
	 * to this temporary channel
	 */
	g_object_unref (channel);
	return;
    }
    
    chan_type = mcd_channel_get_channel_type_quark (channel),
    chan_handle_type = mcd_channel_get_handle_type (channel),
    chan_handle = g_array_index (handles, guint, 0);
    
    g_debug ("Got handle %u", chan_handle);
    
    /* Check if a telepathy channel has already been created; this could happen
     * in the case we had a chat window open, the UI crashed and now the same
     * channel is requested. */
    channels = mcd_operation_get_missions (MCD_OPERATION (connection));
    while (channels)
    {
	/* for calls, we probably don't want this. TODO: investigate better */
	if (chan_type == TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA) break;

	existing_channel = MCD_CHANNEL (channels->data);
	g_debug ("Chan: %d, handle type %d, channel type %s",
		 mcd_channel_get_handle (existing_channel),
		 mcd_channel_get_handle_type (existing_channel),
		 mcd_channel_get_channel_type (existing_channel));
	if (chan_handle == mcd_channel_get_handle (existing_channel) &&
	    chan_handle_type == mcd_channel_get_handle_type (existing_channel) &&
	    chan_type == mcd_channel_get_channel_type_quark (existing_channel))
	{
	    g_debug ("%s: Channel already existing, returning old one", G_STRFUNC);
            /* FIXME: this situation is weird. We should have checked for the
             * existance of the channel _before_ getting here, already when
             * creating the request */
	    /* we no longer need the new channel */
	    g_object_unref (channel);
	    /* notify the dispatcher again */
	    mcd_dispatcher_send (priv->dispatcher, existing_channel);
	    return;
	}
	channels = channels->next;
    }

    /* Update our newly acquired information */
    mcd_channel_set_handle (channel, chan_handle);

    g_return_if_fail (chan_handle != 0);
    mcd_connection_request_channel (connection, channel);
}

static void
create_channel_cb (TpConnection *proxy, const gchar *channel_path,
                   GHashTable *properties, const GError *error,
                   gpointer user_data, GObject *weak_object)
{
    McdChannel *channel = MCD_CHANNEL (weak_object);
    McdConnection *connection = user_data;
    McdConnectionPrivate *priv = connection->priv;

    if (error != NULL)
    {
        GError *mc_error;

        /* No special handling of "no capabilities" error: being confident that
         * https://bugs.freedesktop.org/show_bug.cgi?id=15769 will be fixed
         * soon :-) */
        g_debug ("%s: Got error: %s", G_STRFUNC, error->message);
        mc_error = map_tp_error_to_mc_error (channel, error);
        _mcd_channel_set_error (channel, mc_error);
        mcd_mission_abort ((McdMission *)channel);
        return;
    }

    _mcd_channel_set_immutable_properties (channel,
                                           _mcd_deepcopy_asv (properties));
    /* Everything here is well and fine. We can create the channel. */
    if (!mcd_channel_set_object_path (channel, priv->tp_conn, channel_path))
    {
        mcd_mission_abort ((McdMission *)channel);
        return;
    }

    /* Dispatch the incoming channel */
    mcd_dispatcher_send (priv->dispatcher, channel);
}

static gboolean
request_channel_new_iface (McdConnection *connection, McdChannel *channel)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    GHashTable *properties;

    properties = _mcd_channel_get_requested_properties (channel);
    tp_cli_connection_interface_requests_call_create_channel
        (priv->tp_conn, -1, properties, create_channel_cb, connection, NULL,
         (GObject *)channel);
    return TRUE;
}

static gboolean
request_channel_old_iface (McdConnection *connection, McdChannel *channel)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    guint channel_handle, channel_handle_type;

    channel_handle_type = mcd_channel_get_handle_type (channel);
    channel_handle = mcd_channel_get_handle (channel);

    if (channel_handle != 0 || channel_handle_type == 0)
    {
	TpProxyPendingCall *call;
        const gchar *channel_type;

        channel_type = mcd_channel_get_channel_type (channel);
	call = tp_cli_connection_call_request_channel (priv->tp_conn, -1,
                                                       channel_type,
                                                       channel_handle_type,
                                                       channel_handle, TRUE,
						       request_channel_cb,
						       connection, NULL,
						       (GObject *)channel);
	g_object_set_data ((GObject *)channel, "tp_chan_call", call);
    }
    else
    {
	/* if channel handle is 0, this means that the channel was requested by
	 * a string handle; in that case, we must first request a channel
	 * handle for it */
        const gchar *name_array[2], *target_id;

        target_id = _mcd_channel_get_target_id (channel);
        g_return_val_if_fail (target_id != NULL, FALSE);
        g_return_val_if_fail (channel_handle_type != 0, FALSE);

        name_array[0] = target_id;
	name_array[1] = NULL;

	/* Channel is temporary and will be added as a child mission
	 * only when we successfully resolve the handle. */
	tp_cli_connection_call_request_handles (priv->tp_conn, -1,
                                                channel_handle_type,
						name_array,
						request_handles_cb,
						connection, NULL,
						(GObject *)channel);
    }
    return TRUE;
}

gboolean
mcd_connection_request_channel (McdConnection *connection,
                                McdChannel *channel)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    g_return_val_if_fail (priv->tp_conn != NULL, FALSE);
    g_return_val_if_fail (TP_IS_CONNECTION (priv->tp_conn), FALSE);
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), FALSE);

    if (!mcd_mission_get_parent ((McdMission *)channel))
        mcd_operation_take_mission (MCD_OPERATION (connection),
                                    MCD_MISSION (channel));

    if (!tp_connection_is_ready (priv->tp_conn))
    {
        /* don't request any channel until the connection is ready (because we
         * don't know if the CM implements the Requests interface). The channel
         * will be processed once the connection is ready */
        return TRUE;
    }

    if (priv->has_requests_if)
        return request_channel_new_iface (connection, channel);
    else
        return request_channel_old_iface (connection, channel);
}

gboolean
mcd_connection_cancel_channel_request (McdConnection *connection,
				       guint operation_id,
				       const gchar *requestor_client_id,
				       GError **error)
{
    struct request_id req_id;
    const GList *channels, *node;
    McdChannel *channel;

    /* first, see if the channel is in the list of the pending channels */
    req_id.requestor_serial = operation_id;
    req_id.requestor_client_id = requestor_client_id;

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
    g_debug ("%s called, but it's a stub", G_STRFUNC);
    return FALSE;
}

void
mcd_connection_close (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    priv->abort_reason = TP_CONNECTION_STATUS_REASON_REQUESTED;
    mcd_mission_abort (MCD_MISSION (connection));
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
    _mcd_connection_call_disconnect (connection);
}

/**
 * mcd_connection_connect:
 * @connection: the #McdConnection.
 * @params: a #GHashTable of connection parameters.
 *
 * Activate @connection. The connection takes ownership of @params.
 */
void
mcd_connection_connect (McdConnection *connection, GHashTable *params)
{
    /* TODO: we should probably not save the parameters, but instead restart
     * the full account connection process when we want to reconnect the
     * connection */
    g_object_set_data_full ((GObject *)connection, "params", params,
			    (GDestroyNotify)g_hash_table_destroy);
    _mcd_connection_setup (connection);
}

const gchar *
mcd_connection_get_object_path (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;

    if (priv->tp_conn)
	return TP_PROXY (priv->tp_conn)->object_path;
    else
	return NULL;
}

