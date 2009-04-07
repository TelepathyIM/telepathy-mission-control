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
#include <telepathy-glib/proxy-subclass.h>
#include <libmcclient/mc-errors.h>

#include "mcd-connection.h"
#include "mcd-account-connection.h"
#include "mcd-channel.h"
#include "mcd-provisioning-factory.h"
#include "mcd-misc.h"
#include "sp_timestamp.h"

#include "_gen/interfaces.h"
#include "_gen/gtypes.h"
#include "_gen/cli-Connection_Interface_Contact_Capabilities.h"
#include "_gen/cli-Connection_Interface_Contact_Capabilities-body.h"

#define INITIAL_RECONNECTION_TIME   1 /* 1 second */
#define MAX_REF_PRESENCE 4
#define LAST_MC_PRESENCE (TP_CONNECTION_PRESENCE_TYPE_BUSY + 1)

#define MCD_CONNECTION_PRIV(mcdconn) (MCD_CONNECTION (mcdconn)->priv)

G_DEFINE_TYPE (McdConnection, mcd_connection, MCD_TYPE_OPERATION);

/* Private */
struct _McdConnectionPrivate
{
    /* DBUS connection */
    TpDBusDaemon *dbus_daemon;

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

    /* Capabilities timer */
    guint capabilities_timer;

    guint reconnect_timer; 	/* timer for reconnection */
    guint reconnect_interval;

    /* Supported presences (values are McdPresenceInfo structs) */
    GHashTable *recognized_presences;

    TpConnectionStatusReason abort_reason;
    guint got_capabilities : 1;
    guint got_contact_capabilities : 1;
    guint setting_avatar : 1;
    guint has_presence_if : 1;
    guint has_avatars_if : 1;
    guint has_alias_if : 1;
    guint has_capabilities_if : 1;
    guint has_contact_capabilities_if : 1;
    guint has_requests_if : 1;

    /* FALSE until the connection is ready for dispatching */
    guint can_dispatch : 1;

    /* FALSE until we got the first PresencesChanged for the self handle */
    guint got_presences_changed : 1;

    guint auto_reconnect : 1;

    gchar *alias;

    gboolean is_disposed;
    
};

typedef struct
{
    gchar *object_path;
    gchar *channel_type;
    TpHandle handle;
    TpHandleType handle_type;
} McdTmpChannelData;

#define MCD_TMP_CHANNEL_DATA    "tmp_channel_data"

typedef struct
{
    TpConnectionPresenceType presence;
    guint may_set_on_self : 1;
    guint can_have_message : 1;
} McdPresenceInfo;

struct param_data
{
    GSList *pr_params;
    GHashTable *dest;
};

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
    PROP_TP_MANAGER,
    PROP_TP_CONNECTION,
    PROP_ACCOUNT,
    PROP_DISPATCHER,
};

struct request_id {
    guint requestor_serial;
    const gchar *requestor_client_id;
};

struct capabilities_wait_data {
    GError *error; /* error originally received when channel request failed */
    TpProxySignalConnection *signal_connection;
};

static const gchar *_available_fb[] = { NULL };
static const gchar *_away_fb[] = { "away", NULL };
static const gchar *_ext_away_fb[] = { "xa", "away", NULL };
static const gchar *_hidden_fb[] = { "hidden", "dnd", "busy", "away", NULL };
static const gchar *_busy_fb[] = { "busy", "dnd", "away", NULL };
static const gchar **presence_fallbacks[] = {
    _available_fb, _away_fb, _ext_away_fb, _hidden_fb, _busy_fb
};

static void request_channel_cb (TpConnection *proxy, const gchar *channel_path,
				const GError *error, gpointer user_data,
				GObject *weak_object);
static GError * map_tp_error_to_mc_error (McdChannel *channel, const GError *tp_error);
static void _mcd_connection_release_tp_connection (McdConnection *connection);
static gboolean request_channel_new_iface (McdConnection *connection,
                                           McdChannel *channel);
static gboolean request_channel_old_iface (McdConnection *connection,
                                           McdChannel *channel);

static void
mcd_tmp_channel_data_free (gpointer data)
{
    McdTmpChannelData *tcd = data;

    g_free (tcd->object_path);
    g_free (tcd->channel_type);
    g_slice_free (McdTmpChannelData, tcd);
}

static void
mcd_presence_info_free (McdPresenceInfo *pi)
{
    g_slice_free (McdPresenceInfo, pi);
}

static void
presence_set_status_cb (TpConnection *proxy, const GError *error,
			gpointer user_data, GObject *weak_object)
{
    McdConnectionPrivate *priv = user_data;

    if (error)
    {
        g_warning ("%s: Setting presence of %s failed: %s",
		   G_STRFUNC, mcd_account_get_unique_name (priv->account),
                   error->message);
    }
    /* We rely on the PresenceChanged signal to update our presence, but:
     * - it is not emitted if the presence doesn't change
     * - we miss a few emissions, while we wait for the readiness
     *
     * For this reasons, until we don't get the first PresenceChanged for our
     * self handle, just copy the account requested presence as current
     * presence.
     * FIXME: remove this code is things in things in SimplePresence interface
     * are changed.
     */
    if (!priv->got_presences_changed)
    {
        TpConnectionPresenceType presence;
        const gchar *status, *message;

        /* this is not really correct, as the requested presence might have
         * been changed -- but we hope it didn't */
        mcd_account_get_requested_presence (priv->account,
                                            &presence, &status, &message);
        mcd_account_set_current_presence (priv->account,
                                          presence, status, message);
    }
}

static gboolean
_check_presence (McdConnectionPrivate *priv, TpConnectionPresenceType presence,
                 const gchar **status)
{
    const gchar **fallbacks;

    if (presence == TP_CONNECTION_PRESENCE_TYPE_UNSET || *status == NULL)
        return FALSE;

    if (g_hash_table_lookup (priv->recognized_presences, *status))
        return TRUE;

    if (presence < TP_CONNECTION_PRESENCE_TYPE_AVAILABLE ||
        presence > TP_CONNECTION_PRESENCE_TYPE_BUSY)
        return FALSE;

    fallbacks =
        presence_fallbacks[presence - TP_CONNECTION_PRESENCE_TYPE_AVAILABLE];

    for (; *fallbacks != NULL; fallbacks++)
        if (g_hash_table_lookup (priv->recognized_presences, *fallbacks))
            break;

    /* assume that "available" is always supported -- otherwise, an error will
     * be returned by SetPresence, but it's not a big loss */
    if (*fallbacks == NULL)
        *fallbacks = "available";

    DEBUG ("account %s: presence %s not supported, setting %s",
           mcd_account_get_unique_name (priv->account),
           *status, *fallbacks);
    *status = *fallbacks;
    return TRUE;
}

static void
_mcd_connection_set_presence (McdConnection * connection,
                              TpConnectionPresenceType presence,
			      const gchar *status, const gchar *message)
{
    McdConnectionPrivate *priv = connection->priv;

    if (!priv->tp_conn)
    {
	g_warning ("%s: tp_conn is NULL!", G_STRFUNC);
	mcd_connection_connect (connection, NULL);
	return;
    }
    g_return_if_fail (TP_IS_CONNECTION (priv->tp_conn));

    if (!priv->has_presence_if) return;

    if (_check_presence (priv, presence, &status))
        tp_cli_connection_interface_simple_presence_call_set_presence
            (priv->tp_conn, -1, status, message, presence_set_status_cb,
             priv, NULL, (GObject *)connection);
}


static void
presence_get_statuses_cb (TpProxy *proxy, const GValue *v_statuses,
			  const GError *error, gpointer user_data,
			  GObject *weak_object)
{
    McdConnectionPrivate *priv = user_data;
    McdConnection *connection = MCD_CONNECTION (weak_object);
    TpConnectionPresenceType presence;
    const gchar *status, *message;
    GHashTable *statuses;
    GHashTableIter iter;
    gpointer ht_key, ht_value;

    if (error)
    {
        g_warning ("%s: Get statuses failed for account %s: %s", G_STRFUNC,
                   mcd_account_get_unique_name (priv->account),
                   error->message);
        return;
    }

    if (!priv->recognized_presences)
        priv->recognized_presences =
            g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify)mcd_presence_info_free);

    DEBUG ("account %s:", mcd_account_get_unique_name (priv->account));
    statuses = g_value_get_boxed (v_statuses);
    g_hash_table_iter_init (&iter, statuses);
    while (g_hash_table_iter_next (&iter, &ht_key, &ht_value))
    {
        GValueArray *va = ht_value;
        McdPresenceInfo *pi;

        status = ht_key;
        DEBUG ("  %s", status);

        pi = g_slice_new (McdPresenceInfo);
        pi->presence = g_value_get_uint (va->values);
        pi->may_set_on_self = g_value_get_boolean (va->values + 1);
        pi->can_have_message = g_value_get_boolean (va->values + 2);
        g_hash_table_insert (priv->recognized_presences,
                             g_strdup (status), pi);
    }

    /* Now the presence info is ready. We can set the presence */
    mcd_account_get_requested_presence (priv->account, &presence,
                                        &status, &message);
    _mcd_connection_set_presence (connection, presence, status, message);
}

static void
on_presences_changed (TpConnection *proxy, GHashTable *presences,
                      gpointer user_data, GObject *weak_object)
{
    McdConnectionPrivate *priv = user_data;
    GValueArray *va;
    TpHandle self_handle;

    self_handle = tp_connection_get_self_handle (proxy);
    va = g_hash_table_lookup (presences, GUINT_TO_POINTER (self_handle));
    if (va)
    {
        TpConnectionPresenceType presence;
        const gchar *status, *message;

        presence = g_value_get_uint (va->values);
        status = g_value_get_string (va->values + 1);
        message = g_value_get_string (va->values + 2);
        mcd_account_set_current_presence (priv->account,
                                          presence, status, message);
        priv->got_presences_changed = TRUE;
    }
}

static void
_mcd_connection_setup_presence (McdConnection *connection)
{
    McdConnectionPrivate *priv =  connection->priv;

    tp_cli_dbus_properties_call_get
        (priv->tp_conn, -1, TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
         "Statuses", presence_get_statuses_cb, priv, NULL,
         (GObject *)connection);
    tp_cli_connection_interface_simple_presence_connect_to_presences_changed
        (priv->tp_conn, on_presences_changed, priv, NULL,
         (GObject *)connection, NULL);
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
    TpConnection *tp_conn = connection->priv->tp_conn;

    if (!tp_conn || TP_PROXY (tp_conn)->invalidated != NULL) return;

    if (tp_connection_get_status (tp_conn, NULL) ==
        TP_CONNECTION_STATUS_DISCONNECTED) return;
    tp_cli_connection_call_disconnect (tp_conn, -1,
				       disconnect_cb,
				       NULL, NULL,
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

    DEBUG ("Presence requested: %d", presence);
    if (presence == TP_CONNECTION_PRESENCE_TYPE_UNSET) return;

    if (presence == TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
    {
	/* Connection Proxy */
	priv->abort_reason = TP_CONNECTION_STATUS_REASON_REQUESTED;
	mcd_mission_disconnect (MCD_MISSION (connection));
	_mcd_connection_call_disconnect (connection);

        /* if a reconnection attempt is scheduled, cancel it */
        if (priv->reconnect_timer)
        {
            g_source_remove (priv->reconnect_timer);
            priv->reconnect_timer = 0;
        }
    }
    else
    {
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

    DEBUG ("%s (t=%s, ht=%u, h=%u, suppress=%c)",
           chan_obj_path, chan_type, handle_type, handle,
           suppress_handler ? 'T' : 'F');

    /* ignore all our own requests (they have always suppress_handler = 1) as
     * well as other requests for which our intervention has not been requested
     * */
    if (suppress_handler) return;

    /* It's an incoming channel, so we create a new McdChannel for it */
    if (priv->can_dispatch)
    {
        channel = mcd_channel_new_from_path (proxy,
                                             chan_obj_path,
                                             chan_type, handle, handle_type);
        if (G_UNLIKELY (!channel)) return;
        mcd_operation_take_mission (MCD_OPERATION (connection),
                                    MCD_MISSION (channel));
        /* Dispatch the incoming channel */
        _mcd_dispatcher_take_channels (priv->dispatcher,
                                       g_list_prepend (NULL, channel),
                                       FALSE);
    }
    else
    {
        /* create a void channel, but no TpProxy yet. Bundle the channel data,
         * to be used later */
        McdTmpChannelData *tcd;

        channel = _mcd_channel_new_undispatched ();
        tcd = g_slice_new (McdTmpChannelData);
        tcd->object_path = g_strdup (chan_obj_path);
        tcd->channel_type = g_strdup (chan_type);
        tcd->handle = handle;
        tcd->handle_type = handle_type;
        g_object_set_data_full (G_OBJECT (channel), MCD_TMP_CHANNEL_DATA,
                                tcd, mcd_tmp_channel_data_free);
        mcd_operation_take_mission (MCD_OPERATION (connection),
                                    MCD_MISSION (channel));
    }
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

    DEBUG ("got capabilities for channel %p handle %d, type %s",
           channel, mcd_channel_get_handle (channel),
           mcd_channel_get_channel_type (channel));
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
    DEBUG ("requesting channel again (type = %s, handle_type = %u, handle = %u)",
           chan_type, chan_handle_type, chan_handle);
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
    DEBUG ("channel %p timed out, returning error!", channel);

    mc_error = map_tp_error_to_mc_error (channel, cwd->error);
    mcd_channel_take_error (channel, mc_error);
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

    DEBUG ("got_capabilities is %d", priv->got_capabilities);
    priv->got_capabilities = TRUE;
    list = mcd_operation_get_missions ((McdOperation *)connection);
    while (list)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);
        McdChannelStatus status;

	list_curr = list;
	list = list->next;
        status = mcd_channel_get_status (channel);
        if ((status == MCD_CHANNEL_STATUS_REQUEST ||
             status == MCD_CHANNEL_STATUS_REQUESTED) &&
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
        DEBUG ("connection does not support capabilities interface");
	priv->got_capabilities = TRUE;
	return;
    }
    protocol_name = mcd_account_get_protocol_name (priv->account);
    capabilities = mcd_dispatcher_get_channel_capabilities (priv->dispatcher,
							    protocol_name);
    DEBUG ("advertising capabilities");
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
        g_timeout_add_seconds (10, (GSourceFunc)on_capabilities_timeout,
                               connection);

    /* free the connection capabilities */
    type = dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING,
				   G_TYPE_UINT, G_TYPE_INVALID);
    for (i = 0; i < capabilities->len; i++)
	g_boxed_free (type, g_ptr_array_index (capabilities, i));
    g_ptr_array_free (capabilities, TRUE);
}

static void
_mcd_connection_setup_contact_capabilities (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    GPtrArray *contact_capabilities;

    if (!priv->has_contact_capabilities_if)
    {
        DEBUG ("connection does not support contact capabilities interface");
	priv->got_contact_capabilities = TRUE;
	return;
    }
    contact_capabilities = mcd_dispatcher_get_channel_enhanced_capabilities
      (priv->dispatcher);

    DEBUG ("advertising capabilities");

    mc_cli_connection_interface_contact_capabilities_call_set_self_capabilities
      (priv->tp_conn, -1, contact_capabilities, NULL, NULL, NULL, NULL);
    DEBUG ("SetSelfCapabilities: Called.");

    /* free the connection capabilities (FIXME) */
    g_ptr_array_free (contact_capabilities, TRUE);
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
    TpHandle self_handle;

    g_return_if_fail (priv->tp_conn != NULL);

    handles = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
    self_handle = tp_connection_get_self_handle (priv->tp_conn);
    g_array_append_val (handles, self_handle);
    tp_cli_connection_call_inspect_handles (priv->tp_conn, -1,
					    TP_HANDLE_TYPE_CONTACT,
					    handles,
					    inspect_handles_cb, priv, NULL,
					    (GObject *)connection);
    g_array_free (handles, TRUE); 
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
    DEBUG ("received token: %s", token);
    mcd_account_set_avatar_token (priv->account, token);
}

static void
avatars_clear_avatar_cb (TpConnection *proxy, const GError *error,
			 gpointer user_data, GObject *weak_object)
{
    if (!error)
    {
        DEBUG ("Clear avatar succeeded");
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

    if (contact_id != tp_connection_get_self_handle (proxy)) return;

    /* if we are setting the avatar, we must ignore this signal */
    if (priv->setting_avatar) return;

    DEBUG ("Avatar retrieved for contact %d, token: %s", contact_id, token);
    prev_token = mcd_account_get_avatar_token (priv->account);

    if (!prev_token || strcmp (token, prev_token) != 0)
    {
        DEBUG ("received mime-type: %s", mime_type);
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

    if (contact_id != tp_connection_get_self_handle (proxy)) return;

    /* if we are setting the avatar, we must ignore this signal */
    if (priv->setting_avatar) return;

    DEBUG ("contact %d, token: %s", contact_id, token);
    prev_token = mcd_account_get_avatar_token (priv->account);

    if (!prev_token || strcmp (token, prev_token) != 0)
    {
    	GArray handles;
        DEBUG ("avatar has changed");
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

    DEBUG ("called");
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
    TpHandle self_handle;

    if (error)
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
	return;
    }

    self_handle = tp_connection_get_self_handle (proxy);
    token = g_hash_table_lookup (tokens, GUINT_TO_POINTER (self_handle));
    if (token)
	return;

    mcd_account_get_avatar (priv->account, &avatar, &mime_type);
    if (avatar)
    {
        DEBUG ("No avatar set, setting our own");
        _mcd_connection_set_avatar (connection, avatar, mime_type);
        g_array_free (avatar, TRUE);
    }
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
            TpHandle self_handle;

            DEBUG ("checking for server token");
	    /* Set the avatar only if no other one was set */
            self_handle = tp_connection_get_self_handle (priv->tp_conn);
	    handles.len = 1;
            handles.data = (gchar *)&self_handle;
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

    DEBUG ("called");
    type = dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING,
				   G_TYPE_INVALID);
    for (i = 0; i < aliases->len; i++)
    {
	GValue data = { 0 };

	g_value_init (&data, type);
	g_value_set_static_boxed (&data, g_ptr_array_index(aliases, i));
	dbus_g_type_struct_get (&data, 0, &contact, 1, &alias, G_MAXUINT);
        DEBUG ("Got alias for contact %u: %s", contact, alias);
	if (contact == tp_connection_get_self_handle (proxy))
	{
            DEBUG ("This is our alias");
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
    TpHandle self_handle;

    DEBUG ("setting alias '%s'", alias);

    aliases = g_hash_table_new (NULL, NULL);
    self_handle = tp_connection_get_self_handle (priv->tp_conn);
    g_hash_table_insert (aliases, GINT_TO_POINTER(self_handle),
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
    DEBUG ("%p", connection);
    mcd_connection_connect (connection, NULL);
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
    DEBUG ("status_changed called from tp (%d)", conn_status);

    switch (conn_status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
	mcd_account_set_connection_status (priv->account,
					   conn_status, conn_reason);
	priv->abort_reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;
	break;
    case TP_CONNECTION_STATUS_CONNECTED:
	{
	    mcd_account_set_connection_status (priv->account,
					       conn_status, conn_reason);
	    priv->reconnect_interval = INITIAL_RECONNECTION_TIME;
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
    DEBUG ("Proxy destroyed (%s)!", message);

    _mcd_connection_release_tp_connection (connection);

    /* Destroy any pending timer */
    if (priv->capabilities_timer)
    {
	g_source_remove (priv->capabilities_timer);
	priv->capabilities_timer = 0;
    }

    if (priv->auto_reconnect &&
        (priv->abort_reason == TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED ||
         priv->abort_reason == TP_CONNECTION_STATUS_REASON_NETWORK_ERROR ||
         priv->abort_reason == TP_CONNECTION_STATUS_REASON_NAME_IN_USE))
    {
        /* we were disconnected by a network error or by a connection manager
         * crash (in the latter case, we get NoneSpecified as a reason): don't
         * abort the connection but try to reconnect later */
        if (priv->reconnect_timer == 0)
        {
            DEBUG ("Preparing for reconnection");
            priv->reconnect_timer = g_timeout_add_seconds
                (priv->reconnect_interval,
                 (GSourceFunc)mcd_connection_reconnect, connection);
            priv->reconnect_interval *= 2;
            if (priv->reconnect_interval >= 30 * 60)
                /* no more than 30 minutes! */
                priv->reconnect_interval = 30 * 60;
        }
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

    DEBUG ("called for connection %p", connection);

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

    DEBUG ("called");
    /* go through the channels that were requested while the connection was not
     * ready, and process them */
    while (channels)
    {
	McdChannel *channel = MCD_CHANNEL (channels->data);

        if (mcd_channel_get_status (channel) == MCD_CHANNEL_STATUS_REQUEST)
        {
            DEBUG ("Requesting channel %p", channel);
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

    DEBUG ("called");
    while (channels)
    {
	McdChannel *channel = MCD_CHANNEL (channels->data);

        if (mcd_channel_get_status (channel) == MCD_CHANNEL_STATUS_UNDISPATCHED)
        {
            /* undispatched channels have no TpProxy associated: create it now
             */
            McdTmpChannelData *tcd;

            tcd = g_object_get_data (G_OBJECT (channel), MCD_TMP_CHANNEL_DATA);
            if (G_UNLIKELY (!tcd))
            {
                g_warning ("Channel %p is undispatched without data", channel);
                continue;
            }

            _mcd_channel_create_proxy_old (channel, priv->tp_conn,
                                           tcd->object_path, tcd->channel_type,
                                           tcd->handle, tcd->handle_type);
            g_object_set_data (G_OBJECT (channel), MCD_TMP_CHANNEL_DATA, NULL);
            DEBUG ("Dispatching channel %p", channel);
            /* dispatch the channel */
            _mcd_dispatcher_take_channels (priv->dispatcher,
                                           g_list_prepend (NULL, channel),
                                           FALSE);
        }
        channels = channels->next;
    }
}

static McdChannel *
find_channel_by_path (McdConnection *connection, const gchar *object_path)
{
    const GList *list = NULL;

    list = mcd_operation_get_missions (MCD_OPERATION (connection));
    while (list)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);
        const gchar *req_object_path;

        req_object_path = mcd_channel_get_object_path (channel);
        if (req_object_path &&
            strcmp (object_path, req_object_path) == 0)
        {
            return channel;
        }
        list = list->next;
    }
    return NULL;
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

    if (DEBUGGING)
    {
        for (i = 0; i < channels->len; i++)
        {
            GValueArray *va = g_ptr_array_index (channels, i);
            const gchar *object_path = g_value_get_boxed (va->values);
            GHashTable *props = g_value_get_boxed (va->values + 1);
            GHashTableIter iter;
            gpointer k, v;

            DEBUG ("%s", object_path);

            g_hash_table_iter_init (&iter, props);

            while (g_hash_table_iter_next (&iter, &k, &v))
            {
                gchar *repr = g_strdup_value_contents (v);

                DEBUG("  \"%s\" => %s", (const gchar *) k, repr);
                g_free (repr);
            }
        }
    }

    /* we can completely ignore the channels that arrive while can_dispatch is
     * FALSE: the on_new_channel handler is already recording them */
    if (!priv->can_dispatch) return;

    /* first, check if we have to dispatch the channels at all */
    if (!MCD_CONNECTION_GET_CLASS (connection)->need_dispatch (connection,
                                                               channels))
        return;

    sp_timestamp ("NewChannels received");
    for (i = 0; i < channels->len; i++)
    {
        GValueArray *va;
        const gchar *object_path;
        GHashTable *props;
        GValue *value;

        va = g_ptr_array_index (channels, i);
        object_path = g_value_get_boxed (va->values);
        props = g_value_get_boxed (va->values + 1);

        /* Don't do anything for requested channels */
        value = g_hash_table_lookup (props, TP_IFACE_CHANNEL ".Requested");
        if (value && g_value_get_boolean (value))
            requested = TRUE;

        /* if the channel was a request, we already have an object for it;
         * otherwise, create a new one */
        channel = find_channel_by_path (connection, object_path);
        if (!channel)
        {
            channel = mcd_channel_new_from_properties (proxy, object_path,
                                                       props);
            if (G_UNLIKELY (!channel)) continue;

            mcd_operation_take_mission (MCD_OPERATION (connection),
                                        MCD_MISSION (channel));
        }

        channel_list = g_list_prepend (channel_list, channel);
    }

    _mcd_dispatcher_take_channels (priv->dispatcher, channel_list, requested);
}

static void
mcd_connection_recover_channel (McdConnection *connection,
                                const gchar *object_path,
                                const GHashTable *properties)
{
    McdConnectionPrivate *priv = connection->priv;
    McdChannel *channel;

    DEBUG ("called for %s", object_path);
    channel = mcd_channel_new_from_properties (priv->tp_conn, object_path,
                                               properties);
    if (G_UNLIKELY (!channel)) return;

    mcd_operation_take_mission (MCD_OPERATION (connection),
                                MCD_MISSION (channel));

    _mcd_dispatcher_recover_channel (priv->dispatcher, channel);
}

static void get_all_requests_cb (TpProxy *proxy, GHashTable *properties,
                                 const GError *error, gpointer user_data,
                                 GObject *weak_object)
{
    McdConnection *connection = MCD_CONNECTION (weak_object);
    McdConnectionPrivate *priv = user_data;
    GPtrArray *channels;
    GValue *value;
    guint i;

    if (error)
    {
        g_warning ("%s got error: %s", G_STRFUNC, error->message);
        return;
    }

    value = g_hash_table_lookup (properties, "Channels");

    if (value == NULL)
    {
        g_warning ("%s: no Channels property on %s",
                   G_STRFUNC, tp_proxy_get_object_path (proxy));
        return;
    }

    if (!G_VALUE_HOLDS (value, TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST))
    {
        g_warning ("%s: property Channels has type %s, expecting %s",
                   G_STRFUNC, G_VALUE_TYPE_NAME (value),
                   g_type_name (TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST));
        return;
    }

    channels = g_value_get_boxed (value);
    for (i = 0; i < channels->len; i++)
    {
        GValueArray *va;
        const gchar *object_path;
        GHashTable *channel_props;
        const GList *list;
        gboolean found = FALSE;

        va = g_ptr_array_index (channels, i);
        object_path = g_value_get_boxed (va->values);
        channel_props = g_value_get_boxed (va->values + 1);
        /* find the McdChannel */
        /* NOTE: we cannot move the mcd_operation_get_missions() call out of
         * the loop, because mcd_dispatcher_send() can cause the channel to be
         * destroyed, at which point our list would contain a finalized channel
         * (and a crash will happen) */
        list = mcd_operation_get_missions ((McdOperation *)connection);
        for (; list != NULL; list = list->next)
        {
            McdChannel *channel = MCD_CHANNEL (list->data);
            McdTmpChannelData *tcd;

            if (g_strcmp0 (object_path,
                           mcd_channel_get_object_path (channel)) == 0)
            {
                found = TRUE;
                break;
            }

            if (mcd_channel_get_status (channel) !=
                MCD_CHANNEL_STATUS_UNDISPATCHED)
                continue;

            tcd = g_object_get_data (G_OBJECT (channel), MCD_TMP_CHANNEL_DATA);
            if (tcd && strcmp (tcd->object_path, object_path) == 0)
            {
                _mcd_channel_create_proxy (channel, priv->tp_conn,
                                           object_path, channel_props);
                g_object_set_data (G_OBJECT (channel), MCD_TMP_CHANNEL_DATA,
                                   NULL);
                /* channel is ready for dispatching */
                _mcd_dispatcher_take_channels (priv->dispatcher,
                                               g_list_prepend (NULL, channel),
                                               FALSE);
                found = TRUE;
                break;
            }
        }

        if (!found)
        {
            /* We don't have a McdChannel for this channel, which most likely
             * means that it was already present on the connection before MC
             * started. Let's try to recover it */
            mcd_connection_recover_channel (connection,
                                            object_path, channel_props);
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
        DEBUG ("got error: %s", error->message);
	return;
    }

    if (!connection) return;

    DEBUG ("connection is ready");
    priv = MCD_CONNECTION_PRIV (connection);

    _mcd_connection_get_normalized_name (connection);

    priv->has_presence_if = tp_proxy_has_interface_by_id
        (tp_conn, TP_IFACE_QUARK_CONNECTION_INTERFACE_SIMPLE_PRESENCE);
    priv->has_avatars_if = tp_proxy_has_interface_by_id (tp_conn,
							 TP_IFACE_QUARK_CONNECTION_INTERFACE_AVATARS);
    priv->has_alias_if = tp_proxy_has_interface_by_id (tp_conn,
						       TP_IFACE_QUARK_CONNECTION_INTERFACE_ALIASING);
    priv->has_capabilities_if = tp_proxy_has_interface_by_id (tp_conn,
							      TP_IFACE_QUARK_CONNECTION_INTERFACE_CAPABILITIES);
    priv->has_contact_capabilities_if = tp_proxy_has_interface_by_id (tp_conn,
        MC_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_CAPABILITIES);
    priv->has_requests_if = tp_proxy_has_interface_by_id (tp_conn,
        TP_IFACE_QUARK_CONNECTION_INTERFACE_REQUESTS);

    if (priv->has_presence_if)
	_mcd_connection_setup_presence (connection);

    if (priv->has_capabilities_if)
	_mcd_connection_setup_capabilities (connection);

    if (priv->has_contact_capabilities_if)
	_mcd_connection_setup_contact_capabilities (connection);

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

    DEBUG ("created %s", obj_path);

    _mcd_connection_set_tp_connection (connection, bus_name, obj_path, &error);
    if (G_UNLIKELY (error))
    {
        g_warning ("%s: got error: %s", G_STRFUNC, error->message);
	g_error_free (error);
	return;
    }

    /* FIXME we don't know the status of the connection yet, but calling
     * Connect shouldn't cause any harm
     * https://bugs.freedesktop.org/show_bug.cgi?id=14620 */
    tp_cli_connection_call_connect (priv->tp_conn, -1, connect_cb, priv, NULL,
				    (GObject *)connection);
}

static void
_mcd_connection_connect_with_params (McdConnection *connection,
                                     GHashTable *params)
{
    McdConnectionPrivate *priv = connection->priv;
    const gchar *protocol_name;

    protocol_name = mcd_account_get_protocol_name (priv->account);

    DEBUG ("Trying connect account: %s",
           mcd_account_get_unique_name (priv->account));

    mcd_account_set_connection_status (priv->account,
                                       TP_CONNECTION_STATUS_CONNECTING,
                                       TP_CONNECTION_STATUS_REASON_REQUESTED);

    /* TODO: add extra parameters? */
    tp_cli_connection_manager_call_request_connection (priv->tp_conn_mgr, -1,
						       protocol_name, params,
						       request_connection_cb,
						       priv, NULL,
						       (GObject *)connection);
}

static void
_mcd_connection_finalize (GObject * object)
{
    McdConnection *connection = MCD_CONNECTION (object);
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    g_free (priv->alias);
    if (priv->recognized_presences)
        g_hash_table_destroy (priv->recognized_presences);

    G_OBJECT_CLASS (mcd_connection_parent_class)->finalize (object);
}

static void
_mcd_connection_release_tp_connection (McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    DEBUG ("%p", connection);
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

    if (priv->recognized_presences)
        g_hash_table_remove_all (priv->recognized_presences);
}

static void
on_account_removed (McdAccount *account, McdConnection *connection)
{
    DEBUG ("Account %s removed, aborting connection",
             mcd_account_get_unique_name (account));
    mcd_mission_abort (MCD_MISSION (connection));
}

static void
_mcd_connection_dispose (GObject * object)
{
    McdConnection *connection = MCD_CONNECTION (object);
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    DEBUG ("called for object %p", object);

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
        g_signal_handlers_disconnect_by_func (priv->account,
                                              G_CALLBACK (on_account_removed),
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
        g_signal_connect (priv->account, "removed",
                          G_CALLBACK (on_account_removed),
                          obj);
        _mcd_account_set_connection (account, MCD_CONNECTION (obj));
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

/*
 * mcd_connection_need_dispatch:
 * @connection: the #McdConnection.
 * @channels: array of #McdChannel elements.
 *
 * This functions must be called in response to a NewChannels signals, and is
 * responsible for deciding whether MC must handle the channels or not.
 */
static gboolean
mcd_connection_need_dispatch (McdConnection *connection,
                              const GPtrArray *channels)
{
    gboolean any_requested = FALSE, requested_by_us = FALSE;
    guint i;

    /* We must _not_ handle channels that have the Requested flag set but that
     * have no McdChannel object associated: these are the channels directly
     * requested to the CM by some other application, and we must ignore them
     */
    for (i = 0; i < channels->len; i++)
    {
        GValueArray *va;
        const gchar *object_path;
        GHashTable *props;
        gboolean requested;

        va = g_ptr_array_index (channels, i);
        object_path = g_value_get_boxed (va->values);
        props = g_value_get_boxed (va->values + 1);

        requested = tp_asv_get_boolean (props, TP_IFACE_CHANNEL ".Requested",
                                        NULL);
        if (requested)
        {
            any_requested = TRUE;

            if (find_channel_by_path (connection, object_path))
                requested_by_us = TRUE;
        }
    }

    /* handle only bundles which were not requested or that were requested
     * through MC */
    return !any_requested || requested_by_us;
}

static gboolean
_mcd_connection_request_channel (McdConnection *connection,
                                 McdChannel *channel)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    gboolean ret;

    g_return_val_if_fail (priv->tp_conn != NULL, FALSE);
    g_return_val_if_fail (TP_IS_CONNECTION (priv->tp_conn), FALSE);

    if (!tp_connection_is_ready (priv->tp_conn))
    {
        /* don't request any channel until the connection is ready (because we
         * don't know if the CM implements the Requests interface). The channel
         * will be processed once the connection is ready */
        return TRUE;
    }

    if (priv->has_requests_if)
        ret = request_channel_new_iface (connection, channel);
    else
        ret = request_channel_old_iface (connection, channel);

    if (ret)
        _mcd_channel_set_status (channel, MCD_CHANNEL_STATUS_REQUESTED);
    return ret;
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

    klass->need_dispatch = mcd_connection_need_dispatch;
    klass->request_channel = _mcd_connection_request_channel;

    tp_proxy_or_subclass_hook_on_interface_add
        (TP_TYPE_CONNECTION,
         mc_cli_Connection_Interface_Contact_Capabilities_add_signals);

    /* Properties */
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
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property
        (object_class, PROP_TP_MANAGER,
         g_param_spec_object ("tp-manager",
                              "Telepathy Manager",
                              "Telepathy Manager",
                              TP_TYPE_CONNECTION_MANAGER,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property
        (object_class, PROP_TP_CONNECTION,
         g_param_spec_object ("tp-connection",
                              "Telepathy Connection",
                              "Telepathy Connection",
                              TP_TYPE_CONNECTION,
                              G_PARAM_READABLE));
    g_object_class_install_property
        (object_class, PROP_ACCOUNT,
         g_param_spec_object ("account",
                              "Account",
                              "Account",
                              MCD_TYPE_ACCOUNT,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
mcd_connection_init (McdConnection * connection)
{
    McdConnectionPrivate *priv;
   
    priv = G_TYPE_INSTANCE_GET_PRIVATE (connection, MCD_TYPE_CONNECTION,
					McdConnectionPrivate);
    connection->priv = priv;

    priv->abort_reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;

    priv->reconnect_interval = INITIAL_RECONNECTION_TIME;
    priv->auto_reconnect = TRUE;
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
    g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (tp_conn_mgr), NULL);
    g_return_val_if_fail (MCD_IS_ACCOUNT (account), NULL);

    mcdconn = g_object_new (MCD_TYPE_CONNECTION,
			    "dbus-daemon", dbus_daemon,
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
    McError mc_error_code;

    DEBUG ("Telepathy Error = %s", error->message);

    /* Some TP errors might be a bit too generic for the UIs.
     * With some guesswork, we can add more precise error reporting here.
     */
    if (mcd_channel_get_channel_type_quark (channel) ==
	TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA &&
	error->code == TP_ERROR_NOT_AVAILABLE)
    {
	mc_error_code = MC_CONTACT_DOES_NOT_SUPPORT_VOICE_ERROR;
    }
    else
        return g_error_copy (error);

    return g_error_new (MC_ERROR, mc_error_code, "Telepathy Error: %s",
                        error->message);
}

static void
remove_capabilities_refs (gpointer data)
{
    struct capabilities_wait_data *cwd = data;

    DEBUG ("called");
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
        DEBUG ("got error: %s", tp_error->message);
	if (error_on_creation != NULL)
	{
	    /* replace the error, so that the initial one is reported */
	    tp_error = error_on_creation;
	}

	if (priv->got_capabilities || error_on_creation)
	{
	    /* Faild dispatch */
	    GError *mc_error = map_tp_error_to_mc_error (channel, tp_error);
            mcd_channel_take_error (channel, mc_error);
            mcd_mission_abort ((McdMission *)channel);
	}
	else
	{
	    /* the channel request has failed probably because we are just
	     * connected and we didn't recive the contact capabilities yet. In
	     * this case, wait for this contact's capabilities to arrive */
            DEBUG ("listening for remote capabilities on channel handle %d, type %d",
                   chan_handle, mcd_channel_get_handle_type (channel));
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
        mcd_channel_take_error (channel, mc_error);
        mcd_mission_abort ((McdMission *)channel);
	return;
    }

    /* TODO: construct the a{sv} of immutable properties */
    /* Everything here is well and fine. We can create the channel proxy. */
    if (!_mcd_channel_create_proxy (channel, priv->tp_conn,
                                    channel_path, NULL))
    {
        mcd_mission_abort ((McdMission *)channel);
        return;
    }

    /* Dispatch the incoming channel */
    _mcd_dispatcher_take_channels (priv->dispatcher,
                                   g_list_prepend (NULL, channel),
                                   TRUE);
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
        mcd_channel_take_error (channel, mc_error);
	
	/* No abort, because we are the only one holding the only reference
	 * to this temporary channel
	 */
	g_object_unref (channel);
	return;
    }
    
    chan_type = mcd_channel_get_channel_type_quark (channel),
    chan_handle_type = mcd_channel_get_handle_type (channel),
    chan_handle = g_array_index (handles, guint, 0);
    
    DEBUG ("Got handle %u", chan_handle);
    
    /* Check if a telepathy channel has already been created; this could happen
     * in the case we had a chat window open, the UI crashed and now the same
     * channel is requested. */
    channels = mcd_operation_get_missions (MCD_OPERATION (connection));
    while (channels)
    {
	/* for calls, we probably don't want this. TODO: investigate better */
	if (chan_type == TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA) break;

	existing_channel = MCD_CHANNEL (channels->data);
        DEBUG ("Chan: %d, handle type %d, channel type %s",
               mcd_channel_get_handle (existing_channel),
               mcd_channel_get_handle_type (existing_channel),
               mcd_channel_get_channel_type (existing_channel));
	if (chan_handle == mcd_channel_get_handle (existing_channel) &&
	    chan_handle_type == mcd_channel_get_handle_type (existing_channel) &&
	    chan_type == mcd_channel_get_channel_type_quark (existing_channel))
	{
            DEBUG ("Channel already existing, returning old one");
            /* FIXME: this situation is weird. We should have checked for the
             * existance of the channel _before_ getting here, already when
             * creating the request */
	    /* we no longer need the new channel */
	    g_object_unref (channel);
	    /* notify the dispatcher again */
            _mcd_dispatcher_take_channels (priv->dispatcher,
                                           g_list_prepend (NULL,
                                                           existing_channel),
                                           TRUE);
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
common_request_channel_cb (TpConnection *proxy, gboolean yours,
                           const gchar *channel_path, GHashTable *properties,
                           const GError *error,
                           McdConnection *connection, McdChannel *channel)
{
    McdConnectionPrivate *priv = connection->priv;

    if (error != NULL)
    {
        GError *mc_error;

        /* No special handling of "no capabilities" error: being confident that
         * https://bugs.freedesktop.org/show_bug.cgi?id=15769 will be fixed
         * soon :-) */
        DEBUG ("got error: %s", error->message);
        mc_error = map_tp_error_to_mc_error (channel, error);
        mcd_channel_take_error (channel, mc_error);
        mcd_mission_abort ((McdMission *)channel);
        return;
    }
    DEBUG ("%p, object %s", channel, channel_path);

    /* if this was a call to EnsureChannel, it can happen that the returned
     * channel was already created before; in that case we keep the McdChannel
     * alive only as a proxy for the status-changed signals from the "real"
     * McdChannel */
    if (_mcd_channel_get_request_use_existing (channel))
    {
        McdChannel *existing;
        existing = find_channel_by_path (connection, channel_path);
        if (existing)
        {
            _mcd_dispatcher_add_channel_request (priv->dispatcher, existing,
                                                 channel);
            return;
        }
    }

    /* Everything here is well and fine. We can create the channel. */
    if (!_mcd_channel_create_proxy (channel, priv->tp_conn,
                                    channel_path, properties))
    {
        mcd_mission_abort ((McdMission *)channel);
        return;
    }

    /* if the channel request was cancelled, abort the channel now */
    if (mcd_channel_get_status (channel) == MCD_CHANNEL_STATUS_FAILED)
    {
        DEBUG ("Channel %p was cancelled, aborting", channel);
        mcd_mission_abort (MCD_MISSION (channel));
    }

    /* No dispatching here: the channel will be dispatched upon receiving the
     * NewChannels signal */
}

static void
ensure_channel_cb (TpConnection *proxy, gboolean yours,
                   const gchar *channel_path, GHashTable *properties,
                   const GError *error,
                   gpointer user_data, GObject *weak_object)
{
    common_request_channel_cb (proxy, yours, channel_path, properties, error,
                               MCD_CONNECTION (user_data),
                               MCD_CHANNEL (weak_object));
}

static void
create_channel_cb (TpConnection *proxy, const gchar *channel_path,
                   GHashTable *properties, const GError *error,
                   gpointer user_data, GObject *weak_object)
{
    common_request_channel_cb (proxy, TRUE, channel_path, properties, error,
                               MCD_CONNECTION (user_data),
                               MCD_CHANNEL (weak_object));
}

static gboolean
request_channel_new_iface (McdConnection *connection, McdChannel *channel)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    GHashTable *properties;

    properties = _mcd_channel_get_requested_properties (channel);
    if (_mcd_channel_get_request_use_existing (channel))
    {
        tp_cli_connection_interface_requests_call_ensure_channel
            (priv->tp_conn, -1, properties, ensure_channel_cb,
             connection, NULL, (GObject *)channel);
    }
    else
    {
        tp_cli_connection_interface_requests_call_create_channel
            (priv->tp_conn, -1, properties, create_channel_cb,
             connection, NULL, (GObject *)channel);
    }
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
    g_return_val_if_fail (MCD_IS_CONNECTION (connection), FALSE);
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), FALSE);

    if (!mcd_mission_get_parent ((McdMission *)channel))
        mcd_operation_take_mission (MCD_OPERATION (connection),
                                    MCD_MISSION (channel));

    return MCD_CONNECTION_GET_CLASS (connection)->request_channel (connection,
                                                                   channel);
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
            DEBUG ("requested channel found (%p)", channel);
	    mcd_mission_abort (MCD_MISSION (channel));
	    g_free (chan_requestor_client_id);
	    return TRUE;
	}
	g_free (chan_requestor_client_id);
    }
    DEBUG ("requested channel not found!");
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
    DEBUG ("called, but it's a stub");
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
 * mcd_connection_connect:
 * @connection: the #McdConnection.
 * @params: a #GHashTable of connection parameters.
 *
 * Activate @connection. The connection takes ownership of @params.
 */
void
mcd_connection_connect (McdConnection *connection, GHashTable *params)
{
    McdConnectionPrivate *priv;

    g_return_if_fail (MCD_IS_CONNECTION (connection));
    priv = connection->priv;

    g_return_if_fail (priv->tp_conn_mgr);
    g_return_if_fail (priv->account);
    DEBUG ("called for %p, account %s", connection,
           mcd_account_get_unique_name (priv->account));

    if (priv->reconnect_timer)
    {
	g_source_remove (priv->reconnect_timer);
	priv->reconnect_timer = 0;
    }

    if (mcd_connection_get_connection_status (connection) ==
        TP_CONNECTION_STATUS_DISCONNECTED)
    {
        g_object_set_data_full ((GObject *)connection, "params", params,
                                (GDestroyNotify)g_hash_table_destroy);

        if (params)
            _mcd_connection_connect_with_params (connection, params);
        else
            mcd_account_connection_begin (priv->account);
    }
    else
    {
        DEBUG ("Not connecting because not disconnected (%i)",
               mcd_connection_get_connection_status (connection));
    }
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

const gchar *
mcd_connection_get_name (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;

    if (priv->tp_conn)
	return TP_PROXY (priv->tp_conn)->bus_name;
    else
	return NULL;
}

/**
 * mcd_connection_set_reconnect:
 * @connection: the #McdConnection.
 * @reconnect: %TRUE to activate auto-reconnection, %FALSE otherwise.
 *
 * Enable/disable the automatic reconnection behaviour on connection lost.
 * By default automatic reconnection is enabled.
 */
void
mcd_connection_set_reconnect (McdConnection *connection, gboolean reconnect)
{
    g_return_if_fail (MCD_IS_CONNECTION (connection));

    connection->priv->auto_reconnect = reconnect;
}

/**
 * _mcd_connection_update_property:
 * @connection: the #McdConnection.
 * @name: the qualified name of the property to be updated.
 * @value: the new value of the property.
 *
 * Sets the property @name to @value.
 */
void
_mcd_connection_update_property (McdConnection *connection, const gchar *name,
                                 const GValue *value)
{
    McdConnectionPrivate *priv;
    const gchar *dot, *member;
    gchar *interface;

    g_return_if_fail (MCD_IS_CONNECTION (connection));
    g_return_if_fail (name != NULL);
    priv = connection->priv;

    if (G_UNLIKELY (!priv->tp_conn)) return;

    dot = strrchr (name, '.');
    if (G_UNLIKELY (!dot)) return;

    interface = g_strndup (name, dot - name);
    member = dot + 1;
    tp_cli_dbus_properties_call_set (priv->tp_conn, -1,
                                     interface, member, value,
                                     NULL, NULL, NULL, NULL);
    g_free (interface);
}

void
_mcd_connection_set_tp_connection (McdConnection *connection,
                                   const gchar *bus_name,
                                   const gchar *obj_path, GError **error)
{
    McdConnection **connection_ptr;
    McdConnectionPrivate *priv;

    g_return_if_fail (MCD_IS_CONNECTION (connection));
    priv = connection->priv;
    priv->tp_conn = tp_connection_new (priv->dbus_daemon, bus_name,
                                       obj_path, error);
    if (!priv->tp_conn)
    {
        mcd_account_set_connection_status (priv->account,
                                           TP_CONNECTION_STATUS_DISCONNECTED,
                                           TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
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
}

/**
 * mcd_connection_get_tp_connection:
 * @connection: the #McdConnection.
 *
 * Returns: the #TpConnection being used, or %NULL if none.
 */
TpConnection *
mcd_connection_get_tp_connection (McdConnection *connection)
{
    g_return_val_if_fail (MCD_IS_CONNECTION (connection), NULL);
    return connection->priv->tp_conn;
}

