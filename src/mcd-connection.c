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
 * SECTION:mcd-connection
 * @title: McdConnection
 * @short_description: Connection class representing Telepathy connection class
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-connection.h
 * 
 * FIXME
 */

#include "mcd-connection.h"

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
#include <telepathy-glib/util.h>

#include <libmcclient/mc-errors.h>
#include <libmcclient/mc-gtypes.h>
#include <libmcclient/mc-interfaces.h>

#include "mcd-account-priv.h"
#include "mcd-channel-priv.h"
#include "mcd-connection-priv.h"
#include "mcd-dispatcher-priv.h"
#include "mcd-channel.h"
#include "mcd-provisioning-factory.h"
#include "mcd-misc.h"
#include "sp_timestamp.h"

#include "mcd-signals-marshal.h"
#include "_gen/cli-Connection_Interface_Contact_Capabilities.h"
#include "_gen/cli-Connection_Interface_Contact_Capabilities-body.h"

#define INITIAL_RECONNECTION_TIME   1 /* 1 second */

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

    guint reconnect_timer; 	/* timer for reconnection */
    guint reconnect_interval;

    /* Supported presences (values are McdPresenceInfo structs) */
    GHashTable *recognized_presences;

    TpConnectionStatusReason abort_reason;
    guint got_contact_capabilities : 1;
    guint setting_avatar : 1;
    guint has_presence_if : 1;
    guint has_avatars_if : 1;
    guint has_alias_if : 1;
    guint has_capabilities_if : 1;
    guint has_contact_capabilities_if : 1;
    guint has_requests_if : 1;

    /* FALSE until the dispatcher has said it's ready for us */
    guint dispatching_started : 1;
    /* FALSE until channels announced by NewChannel/NewChannels need to be
     * dispatched */
    guint dispatched_initial_channels : 1;

    /* FALSE until we got the first PresencesChanged for the self handle */
    guint got_presences_changed : 1;

    gchar *alias;

    gboolean is_disposed;
    
};

typedef struct
{
    TpConnectionPresenceType presence;
    guint may_set_on_self : 1;
    guint can_have_message : 1;
} McdPresenceInfo;

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
    PROP_TP_MANAGER,
    PROP_TP_CONNECTION,
    PROP_ACCOUNT,
    PROP_DISPATCHER,
};

enum
{
    READY,
    SELF_PRESENCE_CHANGED,
    SELF_NICKNAME_CHANGED,
    CONNECTION_STATUS_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

static const gchar * const _available_fb[] = { NULL };
static const gchar * const _away_fb[] = { "away", NULL };
static const gchar * const _ext_away_fb[] = { "xa", "away", NULL };
static const gchar * const _hidden_fb[] = { "hidden", "dnd", "busy", "away", NULL };
static const gchar * const _busy_fb[] = { "busy", "dnd", "away", NULL };
static const gchar * const *presence_fallbacks[] = {
    _available_fb, _away_fb, _ext_away_fb, _hidden_fb, _busy_fb
};

static GError * map_tp_error_to_mc_error (McdChannel *channel, const GError *tp_error);
static void _mcd_connection_release_tp_connection (McdConnection *connection);
static gboolean request_channel_new_iface (McdConnection *connection,
                                           McdChannel *channel);

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
}

static gboolean
_check_presence (McdConnectionPrivate *priv, TpConnectionPresenceType presence,
                 const gchar **status)
{
    const gchar * const *fallbacks;

    if (priv->recognized_presences == NULL ||
        g_hash_table_size (priv->recognized_presences) == 0)
    {
        DEBUG ("account %s: recognized presences unknown, not setting "
               "presence yet", mcd_account_get_unique_name (priv->account));
        return FALSE;
    }

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

    if (*fallbacks != NULL)
    {
        DEBUG ("account %s: presence %s not supported, setting %s",
               mcd_account_get_unique_name (priv->account), *status,
               *fallbacks);
        *status = *fallbacks;
    }
    else
    {
        DEBUG ("account %s: presence %s not supported and no fallback is "
               "supported either, trying \"available\" and hoping for the "
               "best...", mcd_account_get_unique_name (priv->account),
               *status);
        *status = "available";
    }

    return TRUE;
}

static void
_mcd_connection_attempt (McdConnection *connection)
{
    g_return_if_fail (connection->priv->tp_conn_mgr != NULL);
    g_return_if_fail (connection->priv->account != NULL);

    DEBUG ("called for %p, account %s", connection,
           mcd_account_get_unique_name (connection->priv->account));

    if (connection->priv->reconnect_timer != 0)
    {
        g_source_remove (connection->priv->reconnect_timer);
        connection->priv->reconnect_timer = 0;
    }

    if (mcd_account_get_connection_status (connection->priv->account) ==
        TP_CONNECTION_STATUS_DISCONNECTED)
    {
        _mcd_account_connection_begin (connection->priv->account);
    }
    else
    {
        /* Can this happen? We just don't know. */
        DEBUG ("Not connecting because not disconnected (%i)",
               mcd_account_get_connection_status (connection->priv->account));
    }
}

static void
_mcd_connection_set_presence (McdConnection * connection,
                              TpConnectionPresenceType presence,
			      const gchar *status, const gchar *message)
{
    McdConnectionPrivate *priv = connection->priv;
    const gchar *adj_status = status;

    if (!priv->tp_conn)
    {
        DEBUG ("tp_conn is NULL");
        _mcd_connection_attempt (connection);
        return;
    }
    g_return_if_fail (TP_IS_CONNECTION (priv->tp_conn));

    if (!priv->has_presence_if)
    {
        DEBUG ("Presence not supported on this connection");
        return;
    }

    if (_check_presence (priv, presence, &adj_status))
    {
        DEBUG ("Setting status '%s' of type %u ('%s' was requested)",
               adj_status, presence, status);

        tp_cli_connection_interface_simple_presence_call_set_presence
            (priv->tp_conn, -1, adj_status, message, presence_set_status_cb,
             priv, NULL, (GObject *)connection);
    }
    else
    {
        DEBUG ("Unable to set status '%s', or anything suitable for type %u",
               status, presence);
    }
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
    else if (G_VALUE_TYPE (v_statuses) != TP_HASH_TYPE_SIMPLE_STATUS_SPEC_MAP)
    {
        g_warning ("%s: Get(Statuses) returned the wrong type: %s",
                   mcd_account_get_unique_name (priv->account),
                   G_VALUE_TYPE_NAME (v_statuses));
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
        g_signal_emit (weak_object, signals[SELF_PRESENCE_CHANGED], 0,
                       presence, status, message);
        priv->got_presences_changed = TRUE;
    }
}

static void
mcd_connection_initial_presence_cb (TpConnection *proxy,
                                    GHashTable *presences,
                                    const GError *error,
                                    gpointer user_data,
                                    GObject *weak_object)
{
    if (error != NULL)
    {
        DEBUG ("GetPresences([SelfHandle]) failed: %s", error->message);
        return;
    }

    on_presences_changed (proxy, presences, user_data, weak_object);
}

static void
_mcd_connection_setup_presence (McdConnection *connection)
{
    McdConnectionPrivate *priv =  connection->priv;
    GArray *self_handle_array;
    guint self_handle;

    tp_cli_connection_interface_simple_presence_connect_to_presences_changed
        (priv->tp_conn, on_presences_changed, priv, NULL,
         (GObject *)connection, NULL);

    self_handle_array = g_array_new (FALSE, FALSE, sizeof (guint));
    self_handle = tp_connection_get_self_handle (priv->tp_conn);
    g_array_append_val (self_handle_array, self_handle);
    tp_cli_connection_interface_simple_presence_call_get_presences
        (priv->tp_conn, -1, self_handle_array,
         mcd_connection_initial_presence_cb, priv, NULL,
         (GObject *) connection);
    g_array_free (self_handle_array, TRUE);

    tp_cli_dbus_properties_call_get
        (priv->tp_conn, -1, TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
         "Statuses", presence_get_statuses_cb, priv, NULL,
         (GObject *)connection);
}

static void
disconnect_cb (TpConnection *proxy, const GError *error, gpointer user_data,
	       GObject *weak_object)
{
    if (error)
	g_warning ("Disconnect failed: %s", error->message);
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

/* Update the presence of the tp_connection.
 *
 * Note, that the only presence transition not served by this function 
 * is getting to non-offline state since when presence is offline this object 
 * does not exist.
 *
 * So, here we just submit the request to the tp_connection object. The return off 
 * the operation is handled by (yet to be written) handler
 */
void
_mcd_connection_request_presence (McdConnection *self,
                                  TpConnectionPresenceType presence,
                                  const gchar *status, const gchar *message)
{
    g_return_if_fail (MCD_IS_CONNECTION (self));

    DEBUG ("Presence requested: %d", presence);
    if (presence == TP_CONNECTION_PRESENCE_TYPE_UNSET) return;

    if (presence == TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
    {
        /* Connection Proxy */
        self->priv->abort_reason = TP_CONNECTION_STATUS_REASON_REQUESTED;
        mcd_mission_disconnect (MCD_MISSION (self));
        _mcd_connection_call_disconnect (self);

        /* if a reconnection attempt is scheduled, cancel it */
        if (self->priv->reconnect_timer)
        {
            g_source_remove (self->priv->reconnect_timer);
            self->priv->reconnect_timer = 0;
        }
    }
    else
    {
        _mcd_connection_set_presence (self, presence, status, message);
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
    if (priv->dispatched_initial_channels)
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
}

static void
_foreach_channel_remove (McdMission * mission, McdOperation * operation)
{
    g_assert (MCD_IS_MISSION (mission));
    g_assert (MCD_IS_OPERATION (operation));

    mcd_operation_remove_mission (operation, mission);
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
	return;
    }
    protocol_name = mcd_account_get_protocol_name (priv->account);
    capabilities = _mcd_dispatcher_get_channel_capabilities (priv->dispatcher,
                                                             protocol_name);
    DEBUG ("advertising capabilities");
    tp_cli_connection_interface_capabilities_call_advertise_capabilities (priv->tp_conn, -1,
									  capabilities,
									  &removed,
									  capabilities_advertise_cb,
									  priv, NULL,
									  (GObject *) connection);

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
    contact_capabilities = _mcd_dispatcher_get_channel_enhanced_capabilities
      (priv->dispatcher);

    DEBUG ("advertising capabilities");

    mc_cli_connection_interface_contact_capabilities_call_set_self_capabilities
      (priv->tp_conn, -1, contact_capabilities, NULL, NULL, NULL, NULL);
    DEBUG ("SetSelfCapabilities: Called.");

    /* free the connection capabilities (FIXME) */
    g_ptr_array_free (contact_capabilities, TRUE);
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
    _mcd_account_set_avatar_token (priv->account, token);
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
    prev_token = _mcd_account_get_avatar_token (priv->account);

    if (!prev_token || strcmp (token, prev_token) != 0)
    {
        DEBUG ("received mime-type: %s", mime_type);
        _mcd_account_set_avatar (priv->account, avatar, mime_type, token,
                                 NULL);
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
    prev_token = _mcd_account_get_avatar_token (priv->account);

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

void
_mcd_connection_set_avatar (McdConnection *connection,
                            const GArray *avatar,
                            const gchar *mime_type)
{
    McdConnectionPrivate *priv = connection->priv;

    if (!priv->has_avatars_if)
        return;

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
    {
        /* act as though the avatar had changed to this */
        on_avatar_updated (proxy, self_handle, token, priv, weak_object);
        return;
    }

    _mcd_account_get_avatar (priv->account, &avatar, &mime_type);
    if (avatar)
    {
        DEBUG ("No avatar set, setting our own");
        _mcd_connection_set_avatar (connection, avatar, mime_type);
        g_array_free (avatar, TRUE);
    }
    g_free (mime_type);
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

    _mcd_account_get_avatar (priv->account, &avatar, &mime_type);

    if (avatar)
    {
	token = _mcd_account_get_avatar_token (priv->account);
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
    guint self_handle;
    guint i;

    DEBUG ("called");

    self_handle = tp_connection_get_self_handle (proxy);

    for (i = 0; i < aliases->len; i++)
    {
        GValueArray *structure = g_ptr_array_index (aliases, i);

        if (g_value_get_uint (structure->values) == self_handle)
        {
            const gchar *alias = g_value_get_string (structure->values + 1);

            DEBUG ("Our alias on %s changed to %s",
                   tp_proxy_get_object_path (proxy), alias);

            if (priv->alias == NULL || tp_strdiff (priv->alias, alias))
            {
                g_free (priv->alias);
                priv->alias = g_strdup (alias);
                g_signal_emit (weak_object, signals[SELF_NICKNAME_CHANGED],
                               0, alias);
            }
            break;
        }
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

void
_mcd_connection_set_nickname (McdConnection *connection,
                              const gchar *nickname)
{
    McdConnectionPrivate *priv = connection->priv;
    GHashTable *aliases;
    TpHandle self_handle;

    if (!priv->has_alias_if)
        return;

    DEBUG ("setting nickname '%s' using Aliasing", nickname);

    aliases = g_hash_table_new (NULL, NULL);
    self_handle = tp_connection_get_self_handle (priv->tp_conn);
    g_hash_table_insert (aliases, GUINT_TO_POINTER (self_handle),
                         (gchar *) nickname);
    tp_cli_connection_interface_aliasing_call_set_aliases (priv->tp_conn, -1,
							   aliases,
							   aliasing_set_aliases_cb,
							   priv, NULL,
							   (GObject *)connection);
    g_hash_table_destroy (aliases);
}

static void
_mcd_connection_setup_alias (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;

    tp_cli_connection_interface_aliasing_connect_to_aliases_changed (priv->tp_conn,
								     on_aliases_changed,
								     priv, NULL,
								     (GObject *)connection,
								     NULL);
}

static gboolean
mcd_connection_reconnect (McdConnection *connection)
{
    DEBUG ("%p", connection);
    _mcd_connection_attempt (connection);
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
        g_signal_emit (connection, signals[CONNECTION_STATUS_CHANGED], 0,
                       conn_status, conn_reason);
        priv->abort_reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;
        break;

    case TP_CONNECTION_STATUS_CONNECTED:
        {
            g_signal_emit (connection, signals[CONNECTION_STATUS_CHANGED], 0,
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
	break;

    default:
	g_warning ("Unknown telepathy connection status");
    }
}

static void proxy_destroyed (TpConnection *tp_conn, guint domain, gint code,
			     gchar *message, McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    DEBUG ("Proxy destroyed (%s)!", message);

    _mcd_connection_release_tp_connection (connection);

    if (priv->abort_reason == TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED ||
        priv->abort_reason == TP_CONNECTION_STATUS_REASON_NETWORK_ERROR)
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

    /* we can completely ignore the channels that arrive while this is
     * FALSE: they'll also be in Channels in the GetAll(Requests) result */
    if (!priv->dispatched_initial_channels) return;

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

static void
mcd_connection_found_channel (McdConnection *self,
                              const gchar *object_path,
                              GHashTable *channel_props)
{
    const GList *list;
    gboolean found = FALSE;

    /* find the McdChannel */
    /* NOTE: we cannot move the mcd_operation_get_missions() call out of
     * the loop, because mcd_dispatcher_send() can cause the channel to be
     * destroyed, at which point our list would contain a finalized channel
     * (and a crash will happen) */
    list = mcd_operation_get_missions ((McdOperation *) self);
    for (; list != NULL; list = list->next)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);

        if (g_strcmp0 (object_path,
                       mcd_channel_get_object_path (channel)) == 0)
        {
            found = TRUE;
            break;
        }

        if (mcd_channel_get_status (channel) !=
            MCD_CHANNEL_STATUS_UNDISPATCHED)
            continue;
    }

    if (!found)
    {
        /* We don't have a McdChannel for this channel, which most likely
         * means that it was already present on the connection before MC
         * started. Let's try to recover it */
        mcd_connection_recover_channel (self, object_path, channel_props);
    }
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

        va = g_ptr_array_index (channels, i);
        object_path = g_value_get_boxed (va->values);
        channel_props = g_value_get_boxed (va->values + 1);

        if (DEBUGGING)
        {
            GHashTableIter iter;
            gpointer k, v;

            DEBUG ("%s", object_path);

            g_hash_table_iter_init (&iter, channel_props);

            while (g_hash_table_iter_next (&iter, &k, &v))
            {
                gchar *repr = g_strdup_value_contents (v);

                DEBUG("  \"%s\" => %s", (const gchar *) k, repr);
                g_free (repr);
            }
        }

        mcd_connection_found_channel (connection, object_path, channel_props);
    }

    priv->dispatched_initial_channels = TRUE;
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
list_channels_cb (TpConnection *connection,
                  const GPtrArray *structs,
                  const GError *error,
                  gpointer user_data,
                  GObject *weak_object)
{
    McdConnection *self = MCD_CONNECTION (weak_object);
    guint i;

    if (error)
    {
        g_warning ("ListChannels got error: %s", error->message);
        return;
    }

    for (i = 0; i < structs->len; i++)
    {
        GValueArray *va = g_ptr_array_index (structs, i);
        const gchar *object_path;
        GHashTable *channel_props;

        object_path = g_value_get_boxed (va->values + 0);

        DEBUG ("%s (t=%s, ht=%u, h=%u)",
               object_path,
               g_value_get_string (va->values + 1),
               g_value_get_uint (va->values + 2),
               g_value_get_uint (va->values + 3));

        /* this is not the most efficient thing we could possibly do, but
         * we're on a fallback path so it's OK to be a bit slow */
        channel_props = g_hash_table_new (g_str_hash, g_str_equal);
        g_hash_table_insert (channel_props, TP_IFACE_CHANNEL ".ChannelType",
                             va->values + 1);
        g_hash_table_insert (channel_props, TP_IFACE_CHANNEL ".TargetHandleType",
                             va->values + 2);
        g_hash_table_insert (channel_props, TP_IFACE_CHANNEL ".TargetHandle",
                             va->values + 3);
        mcd_connection_found_channel (self, object_path, channel_props);
        g_hash_table_destroy (channel_props);
    }

    self->priv->dispatched_initial_channels = TRUE;
}

static void
mcd_connection_setup_pre_requests (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;

    tp_cli_connection_connect_to_new_channel
        (priv->tp_conn, on_new_channel, priv, NULL,
         (GObject *)connection, NULL);

    tp_cli_connection_call_list_channels (priv->tp_conn, -1,
        list_channels_cb, priv, NULL, (GObject *) connection);
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

    _mcd_dispatcher_add_connection (priv->dispatcher, connection);

    g_signal_emit (connection, signals[READY], 0);
}

void
_mcd_connection_start_dispatching (McdConnection *self)
{
    g_return_if_fail (MCD_IS_CONNECTION (self));
    g_return_if_fail (!self->priv->dispatching_started);

    DEBUG ("%p", self);

    self->priv->dispatching_started = TRUE;

    if (self->priv->has_requests_if)
        mcd_connection_setup_requests (self);
    else
        mcd_connection_setup_pre_requests (self);

    /* and request all channels */
    request_unrequested_channels (self);
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

        g_signal_emit (connection, signals[CONNECTION_STATUS_CHANGED], 0,
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

    g_signal_emit (connection, signals[CONNECTION_STATUS_CHANGED], 0,
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
    g_signal_emit (connection, signals[SELF_PRESENCE_CHANGED], 0,
                   TP_CONNECTION_PRESENCE_TYPE_OFFLINE, "offline", "");
    g_signal_emit (connection, signals[CONNECTION_STATUS_CHANGED], 0,
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
	g_object_unref (priv->tp_conn);
	priv->tp_conn = NULL;
	_mcd_account_tp_connection_changed (priv->account);
    }

    /* the interface proxies obtained from this connection must be deleted, too
     */
    g_free (priv->alias);
    priv->alias = NULL;

    if (priv->recognized_presences)
        g_hash_table_remove_all (priv->recognized_presences);

  priv->dispatching_started = FALSE;
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

    mcd_operation_foreach (MCD_OPERATION (connection),
			   (GFunc) _foreach_channel_remove, connection);

    _mcd_connection_release_tp_connection (connection);
    g_assert (priv->tp_conn == NULL);

    if (priv->account)
    {
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
    {
        mcd_channel_take_error (channel,
                                g_error_new (TP_ERRORS,
                                             TP_ERROR_NOT_IMPLEMENTED,
                                             "No Requests interface"));
        mcd_mission_abort ((McdMission *) channel);
        return TRUE;
    }

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

    _mc_ext_register_dbus_glib_marshallers ();

    tp_connection_init_known_interfaces ();
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

    signals[SELF_PRESENCE_CHANGED] = g_signal_new ("self-presence-changed",
        G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0,
        NULL, NULL, _mcd_marshal_VOID__UINT_STRING_STRING,
        G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);

    signals[SELF_NICKNAME_CHANGED] = g_signal_new ("self-nickname-changed",
        G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0,
        NULL, NULL, g_cclosure_marshal_VOID__STRING,
        G_TYPE_NONE, 1, G_TYPE_STRING);

    signals[CONNECTION_STATUS_CHANGED] = g_signal_new (
        "connection-status-changed", G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0,
        NULL, NULL, _mcd_marshal_VOID__UINT_UINT,
        G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

    signals[READY] = g_signal_new ("ready",
        G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0,
        NULL, NULL, g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);
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
}

/* Public methods */

/* Constant getters. These should probably be removed */

McdAccount *
mcd_connection_get_account (McdConnection * id)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (id);
    return priv->account;
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
        _mcd_channel_close (channel);
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
    const GList *channels, *node;
    McdChannel *channel;

    /* first, see if the channel is in the list of the pending channels */

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

void
mcd_connection_close (McdConnection *connection)
{
    g_return_if_fail (MCD_IS_CONNECTION (connection));

    connection->priv->abort_reason = TP_CONNECTION_STATUS_REASON_REQUESTED;
    _mcd_connection_release_tp_connection (connection);
    mcd_mission_abort (MCD_MISSION (connection));
}

/*
 * _mcd_connection_connect:
 * @connection: the #McdConnection.
 * @params: a #GHashTable of connection parameters
 *
 * Activate @connection.
 */
void
_mcd_connection_connect (McdConnection *connection, GHashTable *params)
{
    McdConnectionPrivate *priv;

    g_return_if_fail (MCD_IS_CONNECTION (connection));
    g_return_if_fail (params != NULL);
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

    if (mcd_account_get_connection_status (priv->account) ==
        TP_CONNECTION_STATUS_DISCONNECTED)
    {
        _mcd_connection_connect_with_params (connection, params);
    }
    else
    {
        DEBUG ("Not connecting because not disconnected (%i)",
               mcd_account_get_connection_status (priv->account));
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

    if (priv->tp_conn != NULL)
    {
        if (G_UNLIKELY (!tp_strdiff (tp_proxy_get_object_path (priv->tp_conn),
                                     obj_path)))
        {
            /* not really meant to happen */
            g_warning ("%s: We already have %s", G_STRFUNC,
                       tp_proxy_get_object_path (priv->tp_conn));
            return;
        }

        DEBUG ("releasing old connection first");
        _mcd_connection_release_tp_connection (connection);
    }

    g_assert (priv->tp_conn == NULL);
    priv->tp_conn = tp_connection_new (priv->dbus_daemon, bus_name,
                                       obj_path, error);
    DEBUG ("new connection is %p", priv->tp_conn);
    if (!priv->tp_conn)
    {
        g_signal_emit (connection, signals[CONNECTION_STATUS_CHANGED], 0,
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

gboolean
_mcd_connection_is_ready (McdConnection *self)
{
    g_return_val_if_fail (MCD_IS_CONNECTION (self), FALSE);

    return (self->priv->tp_conn != NULL) &&
        tp_connection_is_ready (self->priv->tp_conn);
}
