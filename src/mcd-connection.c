/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright © 2007-2011 Nokia Corporation.
 * Copyright © 2009-2011 Collabora Ltd.
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

#include "config.h"

#include "mcd-connection.h"
#include "mcd-connection-service-points.h"

#include <string.h>
#include <sys/types.h>
#include <sched.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <telepathy-glib/proxy-subclass.h>

#include "mcd-account-priv.h"
#include "mcd-channel-priv.h"
#include "mcd-connection-priv.h"
#include "mcd-dispatcher-priv.h"
#include "mcd-channel.h"
#include "mcd-misc.h"
#include "mcd-slacker.h"
#include "sp_timestamp.h"

#define INITIAL_RECONNECTION_TIME   3 /* seconds */
#define RECONNECTION_MULTIPLIER     3
#define MAXIMUM_RECONNECTION_TIME   30 * 60 /* half an hour */

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

    /* Telepathy connection manager */
    TpConnectionManager *tp_conn_mgr;

    /* Telepathy connection */
    TpConnection *tp_conn;

    /* Things to do before calling Connect */
    guint tasks_before_connect;

    guint reconnect_timer; 	/* timer for reconnection */
    guint reconnect_interval;
    guint probation_timer;      /* for mcd_connection_probation_ended_cb */
    guint probation_drop_count;

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
    guint has_power_saving_if : 1;

    /* FALSE until the dispatcher has said it's ready for us */
    guint dispatching_started : 1;
    /* FALSE until channels announced by NewChannel/NewChannels need to be
     * dispatched */
    guint dispatched_initial_channels : 1;

    /* FALSE until we got the first PresencesChanged for the self handle */
    guint got_presences_changed : 1;

    /* TRUE if the last status change was to CONNECTED */
    guint connected : 1;

    /* FALSE until mcd_connection_close() is called */
    guint closed : 1;

    /* FALSE until connected and the supported presence statuses retrieved */
    guint presence_info_ready : 1;

    gchar *alias;

    gboolean is_disposed;
    gboolean service_points_watched;

    McdSlacker *slacker;

    struct
    {
        TpIntset *handles;
        GSList *numbers;
    } emergency;
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
    PROP_SLACKER,
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
        _mcd_account_set_changing_presence (priv->account, FALSE);

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
        /* not user-initiated */
        _mcd_account_connection_begin (connection->priv->account, FALSE);
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
        TpConnectionPresenceType curr_presence;
        const gchar *curr_status;
        const gchar *curr_message;

        DEBUG ("Setting status '%s' of type %u ('%s' was requested)",
               adj_status, presence, status);

        mcd_account_get_current_presence (priv->account, &curr_presence,
                                          &curr_status, &curr_message);
        if (curr_presence == presence &&
            tp_strdiff (curr_status, adj_status) == 0 &&
            tp_strdiff (curr_message, message) == 0)
        {
            // PresencesChanged won't be emitted and Account.ChangingPresence
            // will never go to FALSE, so forcibly set it to FALSE
            _mcd_account_set_changing_presence (priv->account, FALSE);
        }

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

    g_return_if_fail (statuses != NULL);

    /* This function can be called both before and after Connect() - before
     * CONNECTED the Connection tells us the presences it believes it will
     * probably support, and after CONNECTED it tells us the presences it
     * *actually* supports (which might be less numerous). */
    g_hash_table_remove_all (priv->recognized_presences);

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
    if (priv->connected)
    {
        priv->presence_info_ready = TRUE;
    }

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
    g_array_unref (self_handle_array);

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

    if (priv->dispatched_initial_channels)
    {
        channel = mcd_channel_new_from_path (proxy,
                                             chan_obj_path,
                                             chan_type, handle, handle_type);
        if (G_UNLIKELY (!channel)) return;
        mcd_operation_take_mission (MCD_OPERATION (connection),
                                    MCD_MISSION (channel));

        /* MC no longer calls RequestChannel. As a result, if suppress_handler
         * is TRUE, we know that this channel was requested "behind our back",
         * therefore we should call ObserveChannels, but refrain from calling
         * AddDispatchOperation or HandleChannels.
         *
         * We assume that channels without suppress_handler are incoming. */
        _mcd_dispatcher_add_channel (priv->dispatcher, channel,
                                     suppress_handler, suppress_handler);
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

/* this signal handler only deals with our own avatar updates */
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
avatars_known_token_cb (TpConnection *proxy, GHashTable *tokens,
                        const GError *error, gpointer user_data,
                        GObject *weak_object)
{
    McdConnection *connection = MCD_CONNECTION (weak_object);
    McdConnectionPrivate *priv = connection->priv;
    const gchar *token;
    TpHandle self_handle = tp_connection_get_self_handle (proxy);
    gpointer handle = user_data;

    if (error)
    {
        g_warning ("%s: error: %s", G_STRFUNC, error->message);
        return;
    }

    if (handle != GUINT_TO_POINTER (self_handle))
        return;

    token = g_hash_table_lookup (tokens, handle);

    /* we have a token and it is not the empty string (ie no avatar) */
    if (token != NULL && *token != '\0')
    {
        GArray handles = { (gchar *) &handle, 1 };

        /* if we have an avatar, set off the request to fetch it: this will *
           get picked up as we connect to the retrieved signal elsewhere    */
        tp_cli_connection_interface_avatars_call_request_avatars (priv->tp_conn,
                                                                  -1,
                                                                  &handles,
                                                                  avatars_request_avatars_cb,
                                                                  NULL,
                                                                  NULL,
                                                                  weak_object);
    }
    else
    {   /* no avatar => will never get a retrieved signal - remove manually */
        GError *avatar_error = NULL;
        McdAccount *account = mcd_connection_get_account (connection);

        if (!_mcd_account_set_avatar (account, NULL, "", "", &avatar_error))
            DEBUG ("Attempt to clear avatar failed: %s", avatar_error->message);
    }
}

/* this signal handler only deals with our own avatar updates */
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
        GArray handles = { (gchar *) &contact_id, 1 };

        DEBUG ("avatar has changed or been erased");
        tp_cli_connection_interface_avatars_call_get_known_avatar_tokens (priv->tp_conn,
                                                                          -1,
                                                                          &handles,
                                                                          avatars_known_token_cb,
                                                                          GUINT_TO_POINTER (contact_id),
                                                                          NULL,
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
        g_array_unref (avatar);
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
	g_array_unref (avatar);
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
    g_hash_table_unref (aliases);
}

static void
_mcd_connection_get_aliases_cb (TpConnection *proxy,
                                GHashTable *aliases,
                                const GError *error,
                                gpointer user_data,
                                GObject *weak_object)
{
    McdConnectionPrivate *priv = user_data;
    guint self_handle;
    const gchar *alias;

    DEBUG ("called");

    if (error != NULL)
    {
        DEBUG ("GetAliases([SelfHandle]) failed: %s", error->message);
        return;
    }

    self_handle = tp_connection_get_self_handle (proxy);

    alias = g_hash_table_lookup (aliases,GUINT_TO_POINTER (self_handle));

    if (alias != NULL &&
        (priv->alias == NULL || tp_strdiff (priv->alias, alias)))
    {
            g_free (priv->alias);
            priv->alias = g_strdup (alias);
            g_signal_emit (weak_object, signals[SELF_NICKNAME_CHANGED], 0,
                           alias);
    }
}

static void
_mcd_connection_setup_alias (McdConnection *connection)
{
    McdConnectionPrivate *priv = connection->priv;
    GArray *self_handle_array;
    guint self_handle;

    self_handle_array = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
    self_handle = tp_connection_get_self_handle (priv->tp_conn);
    g_array_append_val (self_handle_array, self_handle);

    tp_cli_connection_interface_aliasing_connect_to_aliases_changed (priv->tp_conn,
								     on_aliases_changed,
								     priv, NULL,
								     (GObject *)connection,
								     NULL);

    tp_cli_connection_interface_aliasing_call_get_aliases
        (priv->tp_conn, -1, self_handle_array, _mcd_connection_get_aliases_cb,
         priv, NULL, (GObject *) connection);
    g_array_unref (self_handle_array);
}

static void
_mcd_connection_setup_power_saving (McdConnection *connection)
{
  McdConnectionPrivate *priv = connection->priv;

  if (priv->slacker == NULL)
    return;

  DEBUG ("is %sactive", mcd_slacker_is_inactive (priv->slacker) ? "in" : "");

  if (mcd_slacker_is_inactive (priv->slacker))
    tp_cli_connection_interface_power_saving_call_set_power_saving (priv->tp_conn, -1,
        TRUE, NULL, NULL, NULL, NULL);
}

static gboolean
mcd_connection_reconnect (McdConnection *connection)
{
    DEBUG ("%p", connection);
    _mcd_connection_attempt (connection);
    return FALSE;
}

/* Number of seconds after which to assume the connection is basically stable.
 * If we have too many disconnections within this time, assume something
 * serious is wrong, and stop reconnecting. */
#define PROBATION_SEC 120
/* Maximum number of dropped connections within PROBATION_SEC. Connections
 * that never reached CONNECTED state don't count towards this limit, so we'll
 * keep retrying indefinitely for those (with exponential back-off). */
#define PROBATION_MAX_DROPPED 3

static gboolean
mcd_connection_probation_ended_cb (gpointer user_data)
{
    McdConnection *self = MCD_CONNECTION (user_data);
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (self);

    /* We've been connected for PROBATION_SEC seconds. We can probably now
     * assume that the connection is stable */
    if (priv->tp_conn != NULL)
    {
        DEBUG ("probation finished, assuming connection is stable: %s",
               tp_proxy_get_object_path (self->priv->tp_conn));
        self->priv->probation_drop_count = 0;
        self->priv->reconnect_interval = INITIAL_RECONNECTION_TIME;
    }
    else /* probation timer survived beyond its useful life */
    {
        g_warning ("probation error: timer should have been removed when the "
                   "TpConnection was released");
    }

    self->priv->probation_timer = 0;

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
                       conn_status, conn_reason, tp_conn);
        priv->abort_reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;
        priv->connected = FALSE;
        break;

    case TP_CONNECTION_STATUS_CONNECTED:
        {
            g_signal_emit (connection, signals[CONNECTION_STATUS_CHANGED], 0,
                           conn_status, conn_reason, tp_conn);

            if (priv->probation_timer == 0)
            {
                DEBUG ("setting probation timer (%d) seconds, for %s",
                       PROBATION_SEC, tp_proxy_get_object_path (tp_conn));
                priv->probation_timer = g_timeout_add_seconds (PROBATION_SEC,
                    mcd_connection_probation_ended_cb, connection);
                priv->probation_drop_count = 0;
            }

            mcd_connection_service_point_setup (connection,
                                                !priv->service_points_watched);
            priv->service_points_watched = TRUE;

            priv->connected = TRUE;
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
        /* priv->connected will be reset to FALSE in the invalidated
         * callback */
	break;

    default:
	g_warning ("Unknown telepathy connection status");
    }
}

static void
mcd_connection_invalidated_cb (TpConnection *tp_conn,
                               guint domain,
                               gint code,
                               gchar *message,
                               McdConnection *connection)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    DEBUG ("Proxy destroyed (%s)!", message);

    _mcd_connection_release_tp_connection (connection);

    if (priv->connected &&
        priv->abort_reason != TP_CONNECTION_STATUS_REASON_REQUESTED &&
        priv->probation_timer != 0)
    {
        DEBUG ("connection dropped while on probation: %s",
               tp_proxy_get_object_path (tp_conn));

        if (++priv->probation_drop_count > PROBATION_MAX_DROPPED)
        {
            DEBUG ("connection dropped too many times, will stop "
                   "reconnecting");
        }
    }

    priv->connected = FALSE;

    if ((priv->abort_reason == TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED ||
         priv->abort_reason == TP_CONNECTION_STATUS_REASON_NETWORK_ERROR) &&
        priv->probation_drop_count <= PROBATION_MAX_DROPPED)
    {
        /* we were disconnected by a network error or by a connection manager
         * crash (in the latter case, we get NoneSpecified as a reason): don't
         * abort the connection but try to reconnect later */
        if (priv->reconnect_timer == 0)
        {
            DEBUG ("Preparing for reconnection in %u seconds",
                priv->reconnect_interval);
            priv->reconnect_timer = g_timeout_add_seconds
                (priv->reconnect_interval,
                 (GSourceFunc)mcd_connection_reconnect, connection);
            priv->reconnect_interval *= RECONNECTION_MULTIPLIER;

            if (priv->reconnect_interval >= MAXIMUM_RECONNECTION_TIME)
                priv->reconnect_interval = MAXIMUM_RECONNECTION_TIME;
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

static gboolean
_mcd_connection_request_channel (McdConnection *connection,
                                 McdChannel *channel);

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

McdChannel *
mcd_connection_find_channel_by_path (McdConnection *connection,
                      const gchar *object_path)
{
    const GList *list = NULL;

    list = mcd_operation_get_missions (MCD_OPERATION (connection));

    while (list)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);

        if (_mcd_channel_is_primary_for_path (channel, object_path))
        {
            return channel;
        }

        list = list->next;
    }
    return NULL;
}

static gboolean mcd_connection_need_dispatch (McdConnection *connection,
                                              const gchar *object_path,
                                              GHashTable *props);

static void
on_new_channels (TpConnection *proxy, const GPtrArray *channels,
                 gpointer user_data, GObject *weak_object)
{
    McdConnection *connection = MCD_CONNECTION (weak_object);
    McdConnectionPrivate *priv = user_data;
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

    sp_timestamp ("NewChannels received");
    for (i = 0; i < channels->len; i++)
    {
        GValueArray *va;
        const gchar *object_path;
        GHashTable *props;
        GValue *value;
        gboolean requested = FALSE;
        gboolean only_observe = FALSE;
        McdChannel *channel;

        va = g_ptr_array_index (channels, i);
        object_path = g_value_get_boxed (va->values);
        props = g_value_get_boxed (va->values + 1);

        only_observe = !mcd_connection_need_dispatch (connection, object_path,
                                                      props);

        /* Don't do anything for requested channels */
        value = g_hash_table_lookup (props, TP_IFACE_CHANNEL ".Requested");
        if (value && g_value_get_boolean (value))
            requested = TRUE;

        /* if the channel was a request, we already have an object for it;
         * otherwise, create a new one */
        channel = mcd_connection_find_channel_by_path (connection, object_path);
        if (!channel)
        {
            channel = mcd_channel_new_from_properties (proxy, object_path,
                                                       props);
            if (G_UNLIKELY (!channel)) continue;

            mcd_operation_take_mission (MCD_OPERATION (connection),
                                        MCD_MISSION (channel));
        }

        if (!requested)
        {
            /* we always dispatch unrequested (incoming) channels */
            only_observe = FALSE;
        }

        _mcd_dispatcher_add_channel (priv->dispatcher, channel, requested,
                                     only_observe);
    }
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

    _mcd_dispatcher_recover_channel (priv->dispatcher, channel,
      mcd_account_get_object_path (priv->account));
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
        g_hash_table_unref (channel_props);
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
on_connection_ready (GObject *source_object, GAsyncResult *result,
                     gpointer user_data)
{
    TpConnection *tp_conn = TP_CONNECTION (source_object);
    McdConnection *connection, **connection_ptr = user_data;
    McdConnectionPrivate *priv;
    GError *error = NULL;

    connection = *connection_ptr;
    if (connection)
	g_object_remove_weak_pointer ((GObject *)connection,
				      (gpointer)connection_ptr);
    g_slice_free (McdConnection *, connection_ptr);

    if (!tp_proxy_prepare_finish (tp_conn, result, &error))
    {
        DEBUG ("got error: %s", error->message);
        g_clear_error (&error);
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
        TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_CAPABILITIES);
    priv->has_power_saving_if = tp_proxy_has_interface_by_id (tp_conn,
        TP_IFACE_QUARK_CONNECTION_INTERFACE_POWER_SAVING);

    if (priv->has_presence_if)
	_mcd_connection_setup_presence (connection);

    if (priv->has_avatars_if)
	_mcd_connection_setup_avatar (connection);

    if (priv->has_alias_if)
	_mcd_connection_setup_alias (connection);

    if (priv->has_power_saving_if)
      _mcd_connection_setup_power_saving (connection);

    if (!priv->dispatching_started)
        _mcd_dispatcher_add_connection (priv->dispatcher, connection);

    request_unrequested_channels (connection);

    g_signal_emit (connection, signals[READY], 0);
}

void
_mcd_connection_start_dispatching (McdConnection *self,
                                   GPtrArray *client_caps)
{
    g_return_if_fail (MCD_IS_CONNECTION (self));
    g_return_if_fail (!self->priv->dispatching_started);

    DEBUG ("%p", self);

    self->priv->dispatching_started = TRUE;

    if (tp_proxy_has_interface_by_id (self->priv->tp_conn,
            TP_IFACE_QUARK_CONNECTION_INTERFACE_REQUESTS))
        mcd_connection_setup_requests (self);
    else
        mcd_connection_setup_pre_requests (self);

    /* FIXME: why is this here? if we need to update caps before and after   *
     * connected, it should be in the call_when_ready callback.              */
    _mcd_connection_update_client_caps (self, client_caps);
}

void
_mcd_connection_update_client_caps (McdConnection *self,
                                    GPtrArray *client_caps)
{
    g_return_if_fail (MCD_IS_CONNECTION (self));

    if (!self->priv->has_contact_capabilities_if)
    {
        DEBUG ("ContactCapabilities unsupported");
        return;
    }

    DEBUG ("Sending client caps to connection");
    tp_cli_connection_interface_contact_capabilities_call_update_capabilities
      (self->priv->tp_conn, -1, client_caps, NULL, NULL, NULL, NULL);
}

static void
mcd_connection_done_task_before_connect (McdConnection *self)
{
    if (--self->priv->tasks_before_connect == 0)
    {
        if (self->priv->tp_conn == NULL)
        {
            DEBUG ("TpConnection went away, not doing anything");
        }

        if (tp_proxy_has_interface_by_id (self->priv->tp_conn,
                TP_IFACE_QUARK_CONNECTION_INTERFACE_REQUESTS))
        {
            _mcd_dispatcher_add_connection (self->priv->dispatcher, self);
        }

        DEBUG ("%s: Calling Connect()",
               tp_proxy_get_object_path (self->priv->tp_conn));
        tp_cli_connection_call_connect (self->priv->tp_conn, -1, connect_cb,
                                        self->priv, NULL, (GObject *) self);
    }
}

static void
mcd_connection_early_get_statuses_cb (TpProxy *proxy,
                                      const GValue *v_statuses,
                                      const GError *error,
                                      gpointer user_data,
                                      GObject *weak_object)
{
    McdConnection *self = MCD_CONNECTION (weak_object);

    if (self->priv->tp_conn != (TpConnection *) proxy)
    {
        DEBUG ("Connection %p has been replaced with %p, stopping",
               proxy, self->priv->tp_conn);
        return;
    }

    /* This is before we called Connect(), and may or may not be before
     * connection-ready has been signalled. */

    if (error == NULL)
    {
        DEBUG ("%s: Early Get(Statuses) succeeded",
               tp_proxy_get_object_path (proxy));

        /* This will trigger a call to SetPresence, but don't wait for that to
         * finish before calling Connect (there's no need to). */
        presence_get_statuses_cb (proxy, v_statuses, error, self->priv,
                                  weak_object);
    }
    else
    {
        DEBUG ("%s: Early Get(Statuses) failed (not a problem, will try "
               "again later): %s #%d: %s",
               tp_proxy_get_object_path (proxy),
               g_quark_to_string (error->domain), error->code, error->message);
    }

    mcd_connection_done_task_before_connect (self);
}

static void
mcd_connection_early_get_interfaces_cb (TpConnection *tp_conn,
                                        const gchar **interfaces,
                                        const GError *error,
                                        gpointer user_data,
                                        GObject *weak_object)
{
    McdConnection *self = MCD_CONNECTION (weak_object);
    const gchar **iter;

    if (self->priv->tp_conn != tp_conn)
    {
        DEBUG ("Connection %p has been replaced with %p, stopping",
               tp_conn, self->priv->tp_conn);
        return;
    }

    if (error != NULL)
    {
        DEBUG ("%s: Early GetInterfaces failed (not a problem, will try "
               "again later): %s #%d: %s",
               tp_proxy_get_object_path (tp_conn),
               g_quark_to_string (error->domain), error->code, error->message);
    }
    else
    {
        for (iter = interfaces; *iter != NULL; iter++)
        {
            GQuark q = g_quark_try_string (*iter);

            /* if the interface is not recognised, q will just be 0 */

            if (q == TP_IFACE_QUARK_CONNECTION_INTERFACE_SIMPLE_PRESENCE)
            {
                /* nail on the interface (TpConnection will eventually know
                 * how to do this for itself) */
                tp_proxy_add_interface_by_id ((TpProxy *) tp_conn, q);
                self->priv->has_presence_if = TRUE;

                self->priv->tasks_before_connect++;

                tp_cli_dbus_properties_call_get (tp_conn, -1,
                    TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE, "Statuses",
                    mcd_connection_early_get_statuses_cb, NULL, NULL,
                    (GObject *) self);
            }
            else if (q == TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_CAPABILITIES)
            {
                GPtrArray *client_caps;

                /* nail on the interface (TpConnection will eventually know
                 * how to do this for itself) */
                tp_proxy_add_interface_by_id ((TpProxy *) tp_conn, q);
                self->priv->has_contact_capabilities_if = TRUE;

                /* we don't need to delay Connect for this, it can be
                 * fire-and-forget */

                client_caps = _mcd_dispatcher_dup_client_caps (
                    self->priv->dispatcher);

                if (client_caps != NULL)
                {
                    _mcd_connection_update_client_caps (self, client_caps);
                    g_ptr_array_foreach (client_caps,
                                         (GFunc) g_value_array_free, NULL);
                    g_ptr_array_unref (client_caps);
                }
                /* else the McdDispatcher hasn't sorted itself out yet, so
                 * we can't usefully pre-load capabilities - we'll be told
                 * the real capabilities as soon as it has worked them out */
            }
            else if (q == TP_IFACE_QUARK_CONNECTION_INTERFACE_REQUESTS)
            {
              /* If we have the Requests iface, we could start dispatching
               * before the connection is in CONNECTED state */
              tp_proxy_add_interface_by_id ((TpProxy *) tp_conn, q);
            }
        }
    }

    mcd_connection_done_task_before_connect (self);
}

typedef struct {
    /* really a McdConnection *, but g_object_add_weak_pointer wants a
     * gpointer */
    gpointer mcd_connection;
} RequestConnectionData;

static RequestConnectionData *
request_connection_data_new (McdConnection *connection)
{
    RequestConnectionData *rcd = g_slice_new (RequestConnectionData);

    rcd->mcd_connection = connection;
    g_object_add_weak_pointer (rcd->mcd_connection, &rcd->mcd_connection);
    return rcd;
}

static void
request_connection_data_free (gpointer p)
{
    RequestConnectionData *rcd = p;

    if (rcd->mcd_connection != NULL)
    {
        g_object_remove_weak_pointer (rcd->mcd_connection,
                                      &rcd->mcd_connection);
        rcd->mcd_connection = NULL;
    }

    g_slice_free (RequestConnectionData, rcd);
}

static void
request_connection_cb (TpConnectionManager *proxy, const gchar *bus_name,
                       const gchar *obj_path, const GError *tperror,
                       gpointer user_data, GObject *weak_object)
{
    RequestConnectionData *rcd = user_data;
    McdConnection *connection = rcd->mcd_connection;
    McdConnectionPrivate *priv;
    GError *error = NULL;

    if (connection == NULL || connection->priv->closed)
    {
        DEBUG ("RequestConnection returned after we'd decided not to use this "
               "connection");

        /* We no longer want this connection, in fact */
        if (tperror != NULL)
        {
            DEBUG ("It failed anyway: %s", tperror->message);
        }
        else
        {
            /* no point in making a TpConnection for something we're just
             * going to throw away */
            DBusGProxy *tmp_proxy = dbus_g_proxy_new_for_name
                (tp_proxy_get_dbus_connection (proxy),
                 bus_name, obj_path, TP_IFACE_CONNECTION);

            DEBUG ("Disconnecting it: %s", obj_path);
            dbus_g_proxy_call_no_reply (tmp_proxy, "Disconnect",
                                        G_TYPE_INVALID);
            g_object_unref (tmp_proxy);
        }

        if (connection != NULL)
        {
            g_signal_emit (connection, signals[CONNECTION_STATUS_CHANGED], 0,
                           TP_CONNECTION_STATUS_DISCONNECTED,
                           TP_CONNECTION_STATUS_REASON_REQUESTED, NULL);
        }

        return;
    }

    priv = connection->priv;

    if (tperror)
    {
        g_warning ("%s: RequestConnection failed: %s",
                   G_STRFUNC, tperror->message);

        g_signal_emit (connection, signals[CONNECTION_STATUS_CHANGED], 0,
            TP_CONNECTION_STATUS_DISCONNECTED,
            TP_CONNECTION_STATUS_REASON_NETWORK_ERROR, NULL);
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

    priv->tasks_before_connect = 1;

    /* TpConnection doesn't yet know how to get this information before
     * the Connection goes to CONNECTED, so we'll have to do it ourselves */
    tp_cli_connection_call_get_interfaces (priv->tp_conn, -1,
        mcd_connection_early_get_interfaces_cb, NULL, NULL,
        (GObject *) connection);
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
                   TP_CONNECTION_STATUS_REASON_REQUESTED, NULL);

    /* If the McdConnection gets aborted (which results in it being freed!),
     * we need to kill off the Connection. So, we can't use connection as the
     * weak_object.
     *
     * A better design in MC 5.3.x would be for the McdConnection to have
     * a ref held for the duration of this call, not be freed, and signal
     * that it is no longer useful in some way other than getting aborted. */
    tp_cli_connection_manager_call_request_connection (priv->tp_conn_mgr, -1,
        protocol_name, params, request_connection_cb,
        request_connection_data_new (connection), request_connection_data_free,
        NULL);
}

static void
_mcd_connection_finalize (GObject * object)
{
    McdConnection *connection = MCD_CONNECTION (object);
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);

    g_free (priv->alias);
    if (priv->recognized_presences)
        g_hash_table_unref (priv->recognized_presences);

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
                   priv->abort_reason, priv->tp_conn);

    if (priv->tp_conn)
    {
	/* Disconnect signals */
	g_signal_handlers_disconnect_by_func (priv->tp_conn,
					      G_CALLBACK (on_connection_status_changed),
					      connection);
        g_signal_handlers_disconnect_by_func (G_OBJECT (priv->tp_conn),
            G_CALLBACK (mcd_connection_invalidated_cb), connection);

	_mcd_connection_call_disconnect (connection);

        /* the tp_connection has gone away, so we no longer need (or want) *
           the probation timer to go off: there's nothing for it to check  */
        if (priv->probation_timer > 0)
        {
            g_source_remove (priv->probation_timer);
            priv->probation_timer = 0;
        }

        tp_clear_object (&priv->tp_conn);
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
on_inactivity_changed (McdSlacker *slacker,
    gboolean inactive,
    McdConnection *self)
{
  McdConnectionPrivate *priv = self->priv;
  DEBUG ("%sactive, %s have power saving iface.", inactive ? "in" : "",
      priv->has_power_saving_if ? "has" : "doesn't");

  if (priv->has_power_saving_if)
    tp_cli_connection_interface_power_saving_call_set_power_saving (priv->tp_conn, -1,
        inactive, NULL, NULL, NULL, NULL);
}

static void
_mcd_connection_constructed (GObject * object)
{
    McdConnection *self = MCD_CONNECTION (object);
    McdConnectionPrivate *priv = self->priv;

    if (priv->slacker != NULL)
      g_signal_connect (priv->slacker, "inactivity-changed",
          G_CALLBACK (on_inactivity_changed), self);
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

    if (priv->probation_timer)
    {
        g_source_remove (priv->probation_timer);
        priv->probation_timer = 0;
    }

    if (priv->reconnect_timer)
    {
        g_source_remove (priv->reconnect_timer);
        priv->reconnect_timer = 0;
    }

    mcd_operation_foreach (MCD_OPERATION (connection),
			   (GFunc) _foreach_channel_remove, connection);

    _mcd_connection_release_tp_connection (connection);
    g_assert (priv->tp_conn == NULL);

    if (priv->account)
    {
        g_signal_handlers_disconnect_by_func (priv->account,
                                              G_CALLBACK (on_account_removed),
                                              object);
        tp_clear_object (&priv->account);
    }

    if (priv->slacker != NULL)
      {
        g_signal_handlers_disconnect_by_func (priv->slacker,
                                              G_CALLBACK (on_inactivity_changed),
                                              connection);

        tp_clear_object (&priv->slacker);
      }

    tp_clear_object (&priv->tp_conn_mgr);
    tp_clear_object (&priv->dispatcher);
    tp_clear_object (&priv->dbus_daemon);

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
	tp_clear_object (&priv->dispatcher);
	priv->dispatcher = dispatcher;
	break;
    case PROP_DBUS_DAEMON:
	tp_clear_object (&priv->dbus_daemon);
	priv->dbus_daemon = TP_DBUS_DAEMON (g_value_dup_object (val));
	break;
    case PROP_TP_MANAGER:
	tp_conn_mgr = g_value_get_object (val);
	g_object_ref (tp_conn_mgr);
	tp_clear_object (&priv->tp_conn_mgr);
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
    case PROP_SLACKER:
      g_assert (priv->slacker == NULL);
      priv->slacker = g_value_dup_object (val);
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
    case PROP_SLACKER:
      g_value_set_object (val, priv->slacker);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

/*
 * mcd_connection_need_dispatch:
 * @connection: the #McdConnection.
 * @object_path: the object path of the new channel (only for debugging)
 * @props: the properties of the new channel
 *
 * This functions must be called in response to a NewChannels signals, and is
 * responsible for deciding whether MC must handle the channels or not.
 */
static gboolean
mcd_connection_need_dispatch (McdConnection *connection,
                              const gchar *object_path,
                              GHashTable *props)
{
    McdAccount *account = mcd_connection_get_account (connection);
    gboolean requested = FALSE, requested_by_us = FALSE;

    if (_mcd_account_needs_dispatch (account))
    {
        DEBUG ("Account %s must always be dispatched, bypassing checks",
               mcd_account_get_object_path (account));
        return TRUE;
    }

    /* We must _not_ handle channels that have the Requested flag set but that
     * have no McdChannel object associated: these are the channels directly
     * requested to the CM by some other application, and we must ignore them
     */

    requested = tp_asv_get_boolean (props, TP_IFACE_CHANNEL ".Requested",
                                    NULL);
    if (requested)
    {
        if (mcd_connection_find_channel_by_path (connection, object_path))
            requested_by_us = TRUE;
    }

    /* handle only bundles which were not requested or that were requested
     * through MC */
    return !requested || requested_by_us;
}

gboolean
_mcd_connection_target_id_is_urgent (McdConnection *self, const gchar *name)
{
  GSList *list = self->priv->emergency.numbers;

  for (; list != NULL; list = g_slist_next (list))
    {
      const gchar **number = list->data;

      for (; number != NULL && *number != NULL; number++)
        {
          if (!tp_strdiff (*number, name))
            return TRUE;
        }
    }

  return FALSE;
}

gboolean
_mcd_connection_target_handle_is_urgent (McdConnection *self, guint handle)
{
  if (handle != 0 && self->priv->emergency.handles != NULL)
    return tp_intset_is_member (self->priv->emergency.handles, handle);

  return FALSE;
}

static gboolean
_mcd_connection_request_channel (McdConnection *connection,
                                 McdChannel *channel)
{
    McdConnectionPrivate *priv = MCD_CONNECTION_PRIV (connection);
    gboolean ret;

    g_return_val_if_fail (priv->tp_conn != NULL, FALSE);
    g_return_val_if_fail (TP_IS_CONNECTION (priv->tp_conn), FALSE);

    if (!tp_proxy_is_prepared (priv->tp_conn, TP_CONNECTION_FEATURE_CONNECTED))
    {
        /* don't request any channel until the connection is ready (because we
         * don't know if the CM implements the Requests interface). The channel
         * will be processed once the connection is ready */
        return TRUE;
    }

    if (tp_proxy_has_interface_by_id (priv->tp_conn,
            TP_IFACE_QUARK_CONNECTION_INTERFACE_REQUESTS))
    {
        ret = request_channel_new_iface (connection, channel);
    }
    else
    {
        mcd_channel_take_error (channel,
                                g_error_new (TP_ERROR,
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
    object_class->constructed = _mcd_connection_constructed;
    object_class->set_property = _mcd_connection_set_property;
    object_class->get_property = _mcd_connection_get_property;

    _mcd_ext_register_dbus_glib_marshallers ();

    tp_connection_init_known_interfaces ();

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
    g_object_class_install_property
        (object_class, PROP_SLACKER,
         g_param_spec_object ("slacker",
                              "MCE slacker",
                              "Slacker object notifies us of user inactivity",
                              MCD_TYPE_SLACKER,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    signals[SELF_PRESENCE_CHANGED] = g_signal_new ("self-presence-changed",
        G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0,
        NULL, NULL, NULL,
        G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);

    signals[SELF_NICKNAME_CHANGED] = g_signal_new ("self-nickname-changed",
        G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0,
        NULL, NULL, g_cclosure_marshal_VOID__STRING,
        G_TYPE_NONE, 1, G_TYPE_STRING);

    signals[CONNECTION_STATUS_CHANGED] = g_signal_new (
        "connection-status-changed", G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0,
        NULL, NULL, NULL,
        G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, TP_TYPE_CONNECTION);

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
        mc_error = g_error_copy (error);
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
        existing = mcd_connection_find_channel_by_path (connection,
            channel_path);
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
        /* Timeout of 5 hours: 5 * 3600 * 1000 */
        tp_cli_connection_interface_requests_call_ensure_channel
            (priv->tp_conn, 18000000, properties, ensure_channel_cb,
             connection, NULL, (GObject *)channel);
    }
    else
    {
        /* Timeout of 5 hours: 5 * 3600 * 1000 */
        tp_cli_connection_interface_requests_call_create_channel
            (priv->tp_conn, 18000000, properties, create_channel_cb,
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

    if (mcd_channel_get_status (channel) == MCD_CHANNEL_STATUS_FAILED)
    {
        DEBUG ("Channel %p failed already, never mind", channel);
        _mcd_channel_close (channel);
        mcd_mission_abort (MCD_MISSION (channel));
        /* FIXME: the boolean return is a decoy - everyone returns TRUE and
         * every caller ignores it - so it's not clear what FALSE would
         * mean. */
        return TRUE;
    }

    if (!mcd_mission_get_parent ((McdMission *)channel))
        mcd_operation_take_mission (MCD_OPERATION (connection),
                                    MCD_MISSION (channel));

    return _mcd_connection_request_channel (connection, channel);
}

void
mcd_connection_close (McdConnection *connection)
{
    g_return_if_fail (MCD_IS_CONNECTION (connection));

    connection->priv->closed = TRUE;
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
    TpConnectionStatus status = TP_CONNECTION_STATUS_DISCONNECTED;

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

    /* the account's status can be CONNECTING _before_ we get here, because
     * for the account that includes things like trying to bring up an IAP
     * via an McdTransport plugin. So we have to use the actual status of
     * the connection (or DISCONNECTED if we havan't got one yet) */
    if (priv->tp_conn != NULL)
        status = tp_connection_get_status (priv->tp_conn, NULL);

    if (status == TP_CONNECTION_STATUS_DISCONNECTED ||
        status == TP_UNKNOWN_CONNECTION_STATUS)
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
    GQuark features[] = {
      TP_CONNECTION_FEATURE_CONNECTED,
      0
    };

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
            TP_CONNECTION_STATUS_REASON_NETWORK_ERROR,
            NULL);
        return;
    }
    /* FIXME: need some way to feed the status into the Account, but we don't
     * actually know it yet */
    _mcd_account_tp_connection_changed (priv->account, priv->tp_conn);

    /* Setup signals */
    g_signal_connect (priv->tp_conn, "invalidated",
                      G_CALLBACK (mcd_connection_invalidated_cb), connection);
    g_signal_connect (priv->tp_conn, "notify::status",
                      G_CALLBACK (on_connection_status_changed),
                      connection);
    /* HACK for cancelling the _call_when_ready() callback when our object gets
     * destroyed */
    connection_ptr = g_slice_alloc (sizeof (McdConnection *));
    *connection_ptr = connection;
    g_object_add_weak_pointer ((GObject *)connection,
                               (gpointer)connection_ptr);
    tp_proxy_prepare_async (priv->tp_conn, features,
                            on_connection_ready, connection_ptr);
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
        tp_proxy_is_prepared (self->priv->tp_conn, TP_CONNECTION_FEATURE_CONNECTED);
}

gboolean
_mcd_connection_presence_info_is_ready (McdConnection *self)
{
    g_return_val_if_fail (MCD_IS_CONNECTION (self), FALSE);

    return self->priv->presence_info_ready;
}

static void clear_emergency_handles (McdConnectionPrivate *priv)
{
  tp_clear_pointer (&priv->emergency.handles, tp_intset_destroy);
}

static void clear_emergency_numbers (McdConnectionPrivate *priv)
{
  g_slist_foreach (priv->emergency.numbers, (GFunc) g_strfreev, NULL);
  tp_clear_pointer (&priv->emergency.numbers, g_slist_free);
}

void _mcd_connection_clear_emergency_data (McdConnection *self)
{
    clear_emergency_handles (self->priv);
    clear_emergency_numbers (self->priv);
}

void _mcd_connection_take_emergency_numbers (McdConnection *self, GSList *numbers)
{
    McdConnectionPrivate *priv = self->priv;

    if (priv->emergency.numbers != NULL)
      {
        clear_emergency_numbers (priv);
        g_critical ("Overwriting old emergency numbers");
      }

    priv->emergency.numbers = numbers;
}

void
_mcd_connection_take_emergency_handles (McdConnection *self, TpIntset *handles)
{
    McdConnectionPrivate *priv = self->priv;

    if (priv->emergency.handles != NULL)
      {
        clear_emergency_handles (priv);
        g_critical ("Overwriting old emergency handles");
      }

    priv->emergency.handles = handles;
}
