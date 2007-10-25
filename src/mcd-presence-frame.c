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
 * SECTION:mcd-presence-frame
 * @title: McdPresenceFrame
 * @short_description: Presence maintenance framework
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-presence-frame.h
 * 
 * FIXME
 */

#include "mcd-signals-marshal.h"
#include "mcd-presence-frame.h"

#define MCD_PRESENCE_FRAME_PRIV(pframe) (G_TYPE_INSTANCE_GET_PRIVATE ((pframe), \
				  MCD_TYPE_PRESENCE_FRAME, \
				  McdPresenceFramePrivate))

G_DEFINE_TYPE (McdPresenceFrame, mcd_presence_frame, MCD_TYPE_MISSION);

typedef struct _McdPresence
{
    McPresence presence;
    gchar *message;
    TelepathyConnectionStatus connection_status;
    TelepathyConnectionStatusReason connection_reason;
} McdPresence;

typedef struct _McdPresenceFramePrivate
{
    McdPresence *requested_presence;
    McdPresence *actual_presence;
    McdPresence *last_presence;
    GHashTable *account_presence;

    gboolean is_stable;
} McdPresenceFramePrivate;

typedef struct _McdActualPresenceInfo {
    McPresence presence;
    McPresence requested_presence;
    gboolean found;
} McdActualPresenceInfo;

enum _McdPresenceFrameSignalType
{
    /* Request */
    PRESENCE_REQUESTED,
    
    /* Account specific changes */
    PRESENCE_CHANGED,
    STATUS_CHANGED,
    
    /* Accumulated changes */
    PRESENCE_ACTUAL,
    STATUS_ACTUAL,
    PRESENCE_STABLE,
    
    LAST_SIGNAL
};

static guint mcd_presence_frame_signals[LAST_SIGNAL] = { 0 };

static McdPresence *
mcd_presence_new (McPresence tp_presence,
		  const gchar * presence_message,
		  TelepathyConnectionStatus connection_status,
		  TelepathyConnectionStatusReason connection_reason)
{
    McdPresence *presence = g_new0 (McdPresence, 1);
    presence->presence = tp_presence;
    if (presence_message)
    {
	presence->message = g_strdup (presence_message);
    }

    else
    {
	presence->message = NULL;
    }

    presence->connection_status = connection_status;
    presence->connection_reason = connection_reason;
    return presence;
}

static void
mcd_presence_free (McdPresence * presence)
{
    g_free (presence->message);
    g_free (presence);
}

static McdPresence *
mcd_presence_copy (McdPresence * presence)
{
    return mcd_presence_new (presence->presence,
			     presence->message,
			     presence->connection_status,
			     presence->connection_reason);
}

static void
_mcd_presence_frame_dispose (GObject * object)
{
    McdPresenceFramePrivate *priv;

    priv = MCD_PRESENCE_FRAME_PRIV (object);

    if (priv->account_presence)
    {
	g_hash_table_destroy (priv->account_presence);
	priv->account_presence = NULL;
    }

    G_OBJECT_CLASS (mcd_presence_frame_parent_class)->dispose (object);
}

static void
_mcd_presence_frame_finalize (GObject * object)
{
    McdPresenceFrame *cobj;
    McdPresenceFramePrivate *priv;

    cobj = MCD_PRESENCE_FRAME (object);
    priv = MCD_PRESENCE_FRAME_PRIV (object);

    mcd_presence_free (priv->actual_presence);
    if (priv->requested_presence)
	mcd_presence_free (priv->requested_presence);
    if (priv->last_presence)
	mcd_presence_free (priv->last_presence);

    G_OBJECT_CLASS (mcd_presence_frame_parent_class)->finalize (object);
}

static void
mcd_presence_frame_disconnect (McdMission *mission)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (mission);

    /* If connectivity goes away MC will abort processing a presence request;
     * so we must clear the requested-presence for consistency, or McdMaster
     * will think we are still trying to go online. */
    if (priv->requested_presence)
    {
	mcd_presence_free (priv->requested_presence);
	priv->requested_presence = NULL;
    }
}

static void
mcd_presence_frame_class_init (McdPresenceFrameClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    McdMissionClass *mission_class = MCD_MISSION_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (McdPresenceFramePrivate));

    object_class->dispose = _mcd_presence_frame_dispose;
    object_class->finalize = _mcd_presence_frame_finalize;
    mission_class->disconnect = mcd_presence_frame_disconnect;

    /* FIXME: Telepathy doesn't currently registers it's enums to glib so we are compelled to register the 
     * signal handler's arguments as INTs below */
    /* signals */
    mcd_presence_frame_signals[PRESENCE_REQUESTED] =
	g_signal_new ("presence-requested",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdPresenceFrameClass,
				       presence_requested_signal),
		      NULL, NULL,
		      mcd_marshal_VOID__INT_STRING,
		      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
    mcd_presence_frame_signals[PRESENCE_CHANGED] =
	g_signal_new ("presence-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdPresenceFrameClass,
				       presence_set_signal),
		      NULL, NULL,
		      mcd_marshal_VOID__OBJECT_INT_STRING,
		      G_TYPE_NONE, 3, G_TYPE_OBJECT, G_TYPE_INT, G_TYPE_STRING);
    mcd_presence_frame_signals[STATUS_CHANGED] =
	g_signal_new ("status-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdPresenceFrameClass,
				       status_changed_signal),
		      NULL, NULL,
		      mcd_marshal_VOID__OBJECT_INT_INT,
		      G_TYPE_NONE, 3, G_TYPE_OBJECT, G_TYPE_INT, G_TYPE_INT);
    mcd_presence_frame_signals[PRESENCE_ACTUAL] =
	g_signal_new ("presence-actual",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdPresenceFrameClass,
				       presence_actual_signal),
		      NULL, NULL,
		      mcd_marshal_VOID__INT_STRING,
		      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
    mcd_presence_frame_signals[STATUS_ACTUAL] =
	g_signal_new ("status-actual",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdPresenceFrameClass,
				       status_actual_signal),
		      NULL, NULL,
		      mcd_marshal_VOID__INT,
		      G_TYPE_NONE, 1, G_TYPE_INT);
    mcd_presence_frame_signals[PRESENCE_STABLE] =
	g_signal_new ("presence-stable",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      0,
		      NULL, NULL,
		      mcd_marshal_VOID__BOOLEAN,
		      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
mcd_presence_frame_init (McdPresenceFrame * obj)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (obj);

    priv->account_presence =
	g_hash_table_new_full (g_direct_hash, g_direct_equal,
			       (GDestroyNotify) g_object_unref,
			       (GDestroyNotify) mcd_presence_free);

    priv->actual_presence = mcd_presence_new (MC_PRESENCE_UNSET,
					      NULL,
					      TP_CONN_STATUS_DISCONNECTED,
					      TP_CONN_STATUS_REASON_NONE_SPECIFIED);
    priv->requested_presence = NULL;
    priv->last_presence = NULL;

    priv->is_stable = TRUE;
}

/* Public */

McdPresenceFrame *
mcd_presence_frame_new (void)
{
    McdPresenceFrame *obj;
    obj = MCD_PRESENCE_FRAME (g_object_new (MCD_TYPE_PRESENCE_FRAME, NULL));
    return obj;
}

static void
_mcd_presence_frame_print_account (McAccount *account, McdPresence *presence,
				   McdPresenceFrame *presence_frame)
{
    g_debug ("    Account: %s", mc_account_get_unique_name (account));
    if (presence->message)
    {
	g_debug ("        Presence = %d, Status = %d, Message = %s",
		 presence->presence, presence->connection_status,
		 presence->message);
    }
    else
    {
	g_debug ("        Presence = %d, Status = %d",
		 presence->presence, presence->connection_status);
    }
}

static void
_mcd_presence_frame_print (McdPresenceFrame *presence_frame)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    
    g_debug ("PresenceFrame state:");
    g_debug ("[");
    g_hash_table_foreach (priv->account_presence,
			  (GHFunc)_mcd_presence_frame_print_account,
			  presence_frame);
    g_debug ("]");
}

static void
_mcd_presence_frame_request_presence (McdPresenceFrame * presence_frame,
				      McPresence presence,
				      const gchar * presence_message)
{
    McdPresenceFramePrivate *priv;
    TelepathyConnectionStatus status;

    g_return_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame));
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    if (priv->requested_presence)
    {
	mcd_presence_free (priv->requested_presence);
    }

    if (presence == MC_PRESENCE_OFFLINE)
    {
	status = TP_CONN_STATUS_DISCONNECTED;
    }

    else
    {
	status = TP_CONN_STATUS_CONNECTED;
    }

    priv->requested_presence = mcd_presence_new (presence, presence_message,
						 status,
						 TP_CONN_STATUS_REASON_REQUESTED);
    g_debug ("%s: Presence %d is being requested", G_STRFUNC, presence);

    g_signal_emit_by_name (presence_frame, "presence-requested",
			   presence, presence_message);
}

void
mcd_presence_frame_request_presence (McdPresenceFrame * presence_frame,
				     McPresence presence,
				     const gchar * presence_message)
{
    McdPresenceFramePrivate *priv;

    g_return_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame));
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    if (priv->last_presence)
    {
	mcd_presence_free (priv->last_presence);
    }
    priv->last_presence = mcd_presence_copy (priv->actual_presence);
    
    g_debug ("%s: updated last_presence = %d, msg = %s", G_STRFUNC,
	     priv->last_presence->presence,
	     priv->last_presence->message);

    if (priv->last_presence->presence == MC_PRESENCE_UNSET)
    {
	priv->last_presence->presence = MC_PRESENCE_OFFLINE;
    }

    if (mcd_debug_get_level() > 0)
    {
	g_debug ("Presence requested: %d", presence);
	_mcd_presence_frame_print (presence_frame);
    }
    
    _mcd_presence_frame_request_presence (presence_frame, presence,
					  presence_message);
}

gboolean
mcd_presence_frame_cancel_last_request (McdPresenceFrame * presence_frame)
{
    McdPresenceFramePrivate *priv;
    McPresence presence;
    gchar *message;

    g_return_val_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame), FALSE);
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    if (priv->last_presence != NULL)
    {
	presence = priv->last_presence->presence;
	if (priv->last_presence->message)
	{
	    message = g_strdup (priv->last_presence->message);
	}

	else
	{
	    message = NULL;
	}

	mcd_presence_free (priv->last_presence);
	priv->last_presence = NULL;

	_mcd_presence_frame_request_presence (presence_frame, presence,
					      message);
	return TRUE;
    }

    else
    {
	return FALSE;
    }
}

McPresence
mcd_presence_frame_get_requested_presence (McdPresenceFrame * presence_frame)
{
    McdPresenceFramePrivate *priv;
    g_return_val_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame),
			  MC_PRESENCE_UNSET);
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    if (priv->requested_presence)
	return priv->requested_presence->presence;
    else
	return MC_PRESENCE_UNSET;
}

const gchar *
mcd_presence_frame_get_requested_presence_message (McdPresenceFrame *
						   presence_frame)
{
    McdPresenceFramePrivate *priv;
    g_return_val_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame), NULL);
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    if (priv->requested_presence)
	return priv->requested_presence->message;
    else
	return NULL;
}

McPresence
mcd_presence_frame_get_actual_presence (McdPresenceFrame * presence_frame)
{
    McdPresenceFramePrivate *priv;
    g_return_val_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame),
			  MC_PRESENCE_UNSET);
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    return priv->actual_presence->presence;
}

const gchar *
mcd_presence_frame_get_actual_presence_message (McdPresenceFrame *
						presence_frame)
{
    McdPresenceFramePrivate *priv;
    g_return_val_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame), NULL);
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    return priv->actual_presence->message;
}

static void
_mcd_presence_frame_update_actual_presences (gpointer key,
					     gpointer val,
					     McdActualPresenceInfo *info)
{
    McdPresence *account_presence = (McdPresence *) val;

    if (info->found) return;
    if (account_presence->presence == info->requested_presence)
    {
	info->presence = account_presence->presence;
	info->found = TRUE;
    }
    else if (info->presence < account_presence->presence)
	info->presence = account_presence->presence;
}

static void
_mcd_presence_frame_update_actual_presence (McdPresenceFrame * presence_frame,
					    const gchar * presence_message)
{
    McdPresenceFramePrivate *priv;
    McdActualPresenceInfo pi;
    McPresence old_presence;
    TelepathyConnectionStatus connection_status;
    TelepathyConnectionStatusReason connection_reason;
    
    g_debug ("%s called", G_STRFUNC);

    pi.presence = MC_PRESENCE_UNSET;
    pi.requested_presence = mcd_presence_frame_get_requested_presence (presence_frame);
    pi.found = FALSE;
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    g_hash_table_foreach (priv->account_presence,
			  (GHFunc)_mcd_presence_frame_update_actual_presences,
			  &pi);

    connection_status = priv->actual_presence->connection_status;
    connection_reason = priv->actual_presence->connection_reason;

    old_presence = priv->actual_presence->presence;
    mcd_presence_free (priv->actual_presence);
    priv->actual_presence = mcd_presence_new (pi.presence,
					      presence_message,
					      connection_status,
					      connection_reason);

    g_debug ("%s: presence actual: %d", G_STRFUNC, pi.presence);
    if (old_presence != pi.presence)
    {
	g_signal_emit_by_name (G_OBJECT (presence_frame),
			       "presence-actual", pi.presence, presence_message);
    }
}

void
mcd_presence_frame_set_account_presence (McdPresenceFrame * presence_frame,
					 McAccount * account,
					 McPresence
					 presence,
					 const gchar * presence_message)
{
    McdPresenceFramePrivate *priv;
    McdPresence *account_presence;

    g_return_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame));

    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    account_presence = g_hash_table_lookup (priv->account_presence, account);

    g_return_if_fail (account_presence != NULL);
    if (account_presence->presence == presence)
    {
        g_debug ("%s: presence already set, not setting", G_STRFUNC);
        return;
    }

    g_debug ("%s: changing presence of account %s from %d to %d",
             G_STRFUNC, mc_account_get_unique_name (account),
             account_presence->presence,
             presence);
    account_presence->presence = presence;
    g_free (account_presence->message);
    account_presence->message = NULL;
    if (presence_message)
	account_presence->message = g_strdup (presence_message);

    g_signal_emit_by_name (presence_frame, "presence-changed", account,
			   presence, presence_message);

    _mcd_presence_frame_update_actual_presence (presence_frame,
						presence_message);
    
    if (mcd_debug_get_level() > 0)
    {
	g_debug ("Presence Set for account: %s: %d",
		 mc_account_get_unique_name (account),
		 presence);
	_mcd_presence_frame_print (presence_frame);
    }
    
}

McPresence
mcd_presence_frame_get_account_presence (McdPresenceFrame * presence_frame,
					 McAccount * account)
{
    McdPresenceFramePrivate *priv;
    McPresence ret_presence;

    g_return_val_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame),
			  MC_PRESENCE_UNSET);
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    ret_presence = MC_PRESENCE_UNSET;
    if (priv->account_presence)
    {
	McdPresence *presence;
	presence = g_hash_table_lookup (priv->account_presence, account);
	if (presence)
	    ret_presence = presence->presence;
    }
    return ret_presence;
}

const gchar *
mcd_presence_frame_get_account_presence_message (McdPresenceFrame *
						 presence_frame,
						 McAccount * account)
{
    McdPresenceFramePrivate *priv;

    g_return_val_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame), NULL);
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    if (priv->account_presence)
    {
	McdPresence *presence;
	presence = g_hash_table_lookup (priv->account_presence, account);
	if (presence)
	    return presence->message;
    }
    return NULL;
}

static void
_mcd_presence_frame_update_actual_statuses (gpointer key,
					    gpointer val, gpointer user_data)
{
    McdPresence *account_presence = (McdPresence *) val;
    TelepathyConnectionStatus *connection_status =
	(TelepathyConnectionStatus *) user_data;

    if (*connection_status > account_presence->connection_status)
	*connection_status = account_presence->connection_status;
}

static void
_mcd_presence_frame_update_actual_status (McdPresenceFrame * presence_frame)
{
    McdPresenceFramePrivate *priv;
    TelepathyConnectionStatus connection_status = TP_CONN_STATUS_DISCONNECTED;
    TelepathyConnectionStatus old_connection_status;
    TelepathyConnectionStatusReason connection_reason;
    McPresence presence;
    gchar *presence_message = NULL;
    
    g_debug ("%s called", G_STRFUNC);

    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    g_hash_table_foreach (priv->account_presence,
			  _mcd_presence_frame_update_actual_statuses,
			  &connection_status);

    connection_reason = priv->actual_presence->connection_reason;
    presence = priv->actual_presence->presence;
    if (priv->actual_presence->message)
	presence_message = g_strdup (priv->actual_presence->message);
    
    old_connection_status = priv->actual_presence->connection_status;
    mcd_presence_free (priv->actual_presence);
    priv->actual_presence = mcd_presence_new (presence,
					      presence_message,
					      connection_status,
					      connection_reason);

    g_debug ("%s: status actual: %d", G_STRFUNC, connection_status);
    if (old_connection_status != connection_status)
	g_signal_emit_by_name (G_OBJECT (presence_frame), "status-actual",
			       connection_status);
}

static void
_mcd_presence_frame_check_stable (McAccount *account, McdPresence *presence, gboolean *stable)
{
    g_debug ("%s: status = %d", G_STRFUNC, presence->connection_status);
    if (presence->connection_status == TP_CONN_STATUS_CONNECTING)
       	*stable = FALSE;
}

static void
_mcd_presence_frame_update_stable (McdPresenceFrame *presence_frame)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    g_debug ("%s: account_presence = %p", G_STRFUNC, priv->account_presence);
    priv->is_stable = TRUE;
    if (priv->account_presence)
	g_hash_table_foreach (priv->account_presence, 
			      (GHFunc)_mcd_presence_frame_check_stable,
			      &priv->is_stable);
}

void
mcd_presence_frame_set_account_status (McdPresenceFrame * presence_frame,
				       McAccount * account,
				       TelepathyConnectionStatus
				       connection_status,
				       TelepathyConnectionStatusReason
				       connection_reason)
{
    McdPresenceFramePrivate *priv;
    McdPresence *account_presence;
    TelepathyConnectionStatus previous_status;
    gboolean was_stable;

    g_return_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame));

    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    account_presence = g_hash_table_lookup (priv->account_presence, account);

    if (account_presence == NULL)
    {
	g_warning ("%s: Can not find account presence for account %p (%s)",
		   G_STRFUNC, account,
		   mc_account_get_unique_name (account));
        return;
    }
    
    previous_status = account_presence->connection_status;
    account_presence->connection_status = connection_status;
    account_presence->connection_reason = connection_reason;

    /* If no changed, then return */
    if (previous_status == connection_status)
	return;

    if (connection_status == TP_CONN_STATUS_DISCONNECTED)
    {
	/* We first set UNSET presence */
	mcd_presence_frame_set_account_presence (presence_frame, account,
						 MC_PRESENCE_UNSET,
						 NULL);
	/* and then set DISCONNECTED */
	g_signal_emit_by_name (presence_frame, "status-changed", account,
			       connection_status, connection_reason);
	_mcd_presence_frame_update_actual_status (presence_frame);
    }
    else if (connection_status == TP_CONN_STATUS_CONNECTING)
    {
	g_signal_emit_by_name (presence_frame, "status-changed", account,
			       connection_status, connection_reason);
	_mcd_presence_frame_update_actual_status (presence_frame);
    }
    else if (connection_status == TP_CONN_STATUS_CONNECTED)
    {
	/* We first set CONNECTED */
	g_signal_emit_by_name (presence_frame, "status-changed", account,
			       connection_status, connection_reason);
	_mcd_presence_frame_update_actual_status (presence_frame);
	
	/* and then set AVAILABLE presence */
	mcd_presence_frame_set_account_presence (presence_frame, account,
						 MC_PRESENCE_AVAILABLE,
						 NULL);
    }

    was_stable = priv->is_stable;
    _mcd_presence_frame_update_stable (presence_frame);
    if (was_stable != priv->is_stable || priv->is_stable)
	g_signal_emit (presence_frame,
		       mcd_presence_frame_signals[PRESENCE_STABLE],
		       0, priv->is_stable);
    
    if (mcd_debug_get_level() > 0)
    {
	g_debug ("%s: was stable = %d, is_stable = %d", G_STRFUNC, was_stable, priv->is_stable);
	g_debug ("Account Status Set for account: %s: %d", mc_account_get_unique_name (account),
		 connection_status);
	_mcd_presence_frame_print (presence_frame);
    }
}

TelepathyConnectionStatus
mcd_presence_frame_get_account_status (McdPresenceFrame * presence_frame,
				       McAccount * account)
{
    McdPresenceFramePrivate *priv;
    TelepathyConnectionStatus conn_status;

    g_return_val_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame),
			  TP_CONN_STATUS_DISCONNECTED);
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    conn_status = TP_CONN_STATUS_DISCONNECTED;
    if (priv->account_presence)
    {
	McdPresence *presence;
	presence = g_hash_table_lookup (priv->account_presence, account);
	if (presence)
	    conn_status = presence->connection_status;
    }
    return conn_status;
}

TelepathyConnectionStatusReason
mcd_presence_frame_get_account_status_reason (McdPresenceFrame *
					      presence_frame,
					      McAccount * account)
{
    McdPresenceFramePrivate *priv;
    TelepathyConnectionStatusReason conn_reason;

    g_return_val_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame),
			  TP_CONN_STATUS_REASON_NONE_SPECIFIED);
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    conn_reason = TP_CONN_STATUS_REASON_NONE_SPECIFIED;
    if (priv->account_presence)
    {
	McdPresence *presence;
	presence = g_hash_table_lookup (priv->account_presence, account);
	if (presence)
	    conn_reason = presence->connection_reason;
    }
    return conn_reason;
}

void
mcd_presence_frame_set_accounts (McdPresenceFrame * presence_frame,
				 const GList * accounts)
{
    const GList *node;
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    if (priv->account_presence)
    {
	g_hash_table_destroy (priv->account_presence);
	priv->account_presence =
	    g_hash_table_new_full (g_direct_hash, g_direct_equal,
				   (GDestroyNotify) g_object_unref,
				   (GDestroyNotify) mcd_presence_free);
    }
    for (node = accounts; node; node = node->next)
    {
	g_object_ref (node->data);
	g_hash_table_insert (priv->account_presence, node->data,
			     mcd_presence_new (MC_PRESENCE_UNSET,
					       NULL,
					       TP_CONN_STATUS_DISCONNECTED,
					       TP_CONN_STATUS_REASON_NONE_SPECIFIED));
    }
}

gboolean
mcd_presence_frame_add_account (McdPresenceFrame * presence_frame,
                                McAccount * account)
{
    McdPresence *presence;
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    /* Let's see if we already have the account */
    presence = g_hash_table_lookup (priv->account_presence, account);

    if (presence != NULL)
    {
        return FALSE;
    }
    
    presence = mcd_presence_new (MC_PRESENCE_UNSET,
            NULL,
            TP_CONN_STATUS_DISCONNECTED,
            TP_CONN_STATUS_REASON_NONE_SPECIFIED);
    g_object_ref (account);
    g_hash_table_insert (priv->account_presence,
            account,
            presence);
    
    /*mcd_presence_frame_set_account_presence (presence_frame, account,
					     priv->actual_presence->presence,
					     NULL);*/
    return TRUE;
}

gboolean
mcd_presence_frame_remove_account (McdPresenceFrame * presence_frame,
                                   McAccount * account)
{
    McdPresence *presence;
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    /* Let's see if we have the account */
    presence = g_hash_table_lookup (priv->account_presence, account);

    if (presence == NULL)
    {
        return FALSE;
    }
    
    g_debug ("%s: removing account %s", G_STRFUNC, mc_account_get_unique_name (account));
    /*mcd_presence_frame_set_account_presence (presence_frame, account,
					     MC_PRESENCE_UNSET,
					     NULL);*/
    /*_mcd_presence_frame_update_actual_presence (presence_frame, NULL);*/
    g_hash_table_remove (priv->account_presence, account);
    if (g_hash_table_size (priv->account_presence) == 0)
    {
	if (priv->requested_presence)
	{
	    mcd_presence_free (priv->requested_presence);
	    priv->requested_presence = NULL;
	}
    }
    
    return TRUE;
}

/**
 * mcd_presence_frame_is_stable:
 * @presence_frame: The #McdPresenceFrame.
 *
 * Returns #TRUE if there isn't any account currently trying to connect.
 */
gboolean
mcd_presence_frame_is_stable (McdPresenceFrame *presence_frame)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    return priv->is_stable;
}

