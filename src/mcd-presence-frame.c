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
#include "mcd-account.h"
#include <telepathy-glib/util.h>

#define MCD_PRESENCE_FRAME_PRIV(pframe) (G_TYPE_INSTANCE_GET_PRIVATE ((pframe), \
				  MCD_TYPE_PRESENCE_FRAME, \
				  McdPresenceFramePrivate))

G_DEFINE_TYPE (McdPresenceFrame, mcd_presence_frame, MCD_TYPE_MISSION);

typedef struct _McdPresence
{
    McPresence presence;
    gchar *message;
    TpConnectionStatus connection_status;
    TpConnectionStatusReason connection_reason;
} McdPresence;

typedef struct _McdPresenceFramePrivate
{
    McdAccountManager *account_manager;

    McdPresence *requested_presence;
    McdPresence *actual_presence;
    McdPresence *last_presence;
    GList *accounts;

    TpConnectionStatus actual_status;
} McdPresenceFramePrivate;

typedef struct _McdActualPresenceInfo {
    TpConnectionPresenceType presence;
    TpConnectionPresenceType requested_presence;
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
    
    LAST_SIGNAL
};

static const gchar *presence_statuses[] = {
    NULL,
    "offline",
    "available",
    "away",
    "xa",
    "hidden",
    "dnd",
    NULL
};

static guint mcd_presence_frame_signals[LAST_SIGNAL] = { 0 };

static McdPresence *
mcd_presence_new (McPresence tp_presence,
		  const gchar * presence_message,
		  TpConnectionStatus connection_status,
		  TpConnectionStatusReason connection_reason)
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

    g_list_foreach (priv->accounts, (GFunc)g_object_unref, NULL);
    g_list_free (priv->accounts);
    priv->accounts = NULL;

    if (priv->account_manager)
    {
	g_object_unref (priv->account_manager);
	priv->account_manager = NULL;
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
request_presence (gpointer key, gpointer value, gpointer userdata)
{
    McdAccount *account = value;
    McdPresence *p = userdata;

    mcd_account_request_presence (account,
				  (TpConnectionPresenceType)p->presence,
				  presence_statuses[p->presence],
				  p->message);
}

static void
presence_requested_signal (McdPresenceFrame *presence_frame,
			   McPresence presence, const gchar *presence_message)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    GHashTable *accounts;
    McdPresence p;

    if (!priv->account_manager) return;

    accounts = mcd_account_manager_get_valid_accounts (priv->account_manager);
    p.presence = presence;
    p.message = (gchar *)presence_message;
    g_hash_table_foreach (accounts, request_presence, &p);
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

    klass->presence_requested_signal = presence_requested_signal;

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
		      G_TYPE_NONE, 3, MCD_TYPE_ACCOUNT, G_TYPE_INT, G_TYPE_STRING);
    mcd_presence_frame_signals[STATUS_CHANGED] =
	g_signal_new ("status-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdPresenceFrameClass,
				       status_changed_signal),
		      NULL, NULL,
		      mcd_marshal_VOID__OBJECT_INT_INT,
		      G_TYPE_NONE, 3, MCD_TYPE_ACCOUNT, G_TYPE_INT, G_TYPE_INT);
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
		      0,
		      NULL, NULL,
		      mcd_marshal_VOID__INT,
		      G_TYPE_NONE, 1, G_TYPE_INT);
}

static void
mcd_presence_frame_init (McdPresenceFrame * obj)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (obj);

    priv->actual_presence = mcd_presence_new (MC_PRESENCE_UNSET,
					      NULL,
					      TP_CONNECTION_STATUS_DISCONNECTED,
					      TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
    priv->requested_presence = NULL;
    priv->last_presence = NULL;

    priv->actual_status = TP_CONNECTION_STATUS_DISCONNECTED;
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
_mcd_presence_frame_request_presence (McdPresenceFrame * presence_frame,
				      McPresence presence,
				      const gchar * presence_message)
{
    McdPresenceFramePrivate *priv;
    TpConnectionStatus status;

    g_return_if_fail (MCD_IS_PRESENCE_FRAME (presence_frame));
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);

    if (priv->requested_presence)
    {
	mcd_presence_free (priv->requested_presence);
    }

    if (presence == MC_PRESENCE_OFFLINE)
    {
	status = TP_CONNECTION_STATUS_DISCONNECTED;
    }

    else
    {
	status = TP_CONNECTION_STATUS_CONNECTED;
    }

    priv->requested_presence = mcd_presence_new (presence, presence_message,
						 status,
						 TP_CONNECTION_STATUS_REASON_REQUESTED);
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

    g_debug ("Presence requested: %d", presence);
    
    _mcd_presence_frame_request_presence (presence_frame, presence,
					  presence_message);
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
_mcd_presence_frame_update_actual_presences (gpointer val, gpointer userdata)
{
    McdAccount *account = val;
    McdActualPresenceInfo *info = userdata;
    TpConnectionPresenceType presence;
    const gchar *status, *message;

    if (info->found) return;
    mcd_account_get_requested_presence (account, &presence,
				       	&status, &message);
    if (presence == info->requested_presence)
    {
	info->presence = presence;
	info->found = TRUE;
    }
    else if (info->presence < presence)
	info->presence = presence;
}

static void
_mcd_presence_frame_update_actual_presence (McdPresenceFrame * presence_frame,
					    const gchar * presence_message)
{
    McdPresenceFramePrivate *priv;
    McdActualPresenceInfo pi;
    TpConnectionStatus connection_status;
    TpConnectionStatusReason connection_reason;
    gboolean changed;
    
    g_debug ("%s called", G_STRFUNC);

    pi.presence = MC_PRESENCE_UNSET;
    pi.requested_presence = mcd_presence_frame_get_requested_presence (presence_frame);
    pi.found = FALSE;
    priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    g_list_foreach (priv->accounts,
		    _mcd_presence_frame_update_actual_presences, &pi);

    connection_status = priv->actual_presence->connection_status;
    connection_reason = priv->actual_presence->connection_reason;

    changed = (priv->actual_presence->presence != pi.presence) ||
              (tp_strdiff (priv->actual_presence->message, presence_message));

    mcd_presence_free (priv->actual_presence);
    priv->actual_presence = mcd_presence_new (pi.presence,
					      presence_message,
					      connection_status,
					      connection_reason);

    g_debug ("%s: presence actual: %d", G_STRFUNC, pi.presence);
    if (changed)
    {    
	g_signal_emit_by_name (G_OBJECT (presence_frame),
			       "presence-actual", pi.presence, presence_message);
    }
}

/* TODO: remove this useless func */
TpConnectionStatus
mcd_presence_frame_get_account_status (McdPresenceFrame * presence_frame,
				       McdAccount *account)
{
    return mcd_account_get_connection_status (account);
}

/* TODO: remove this useless func */
TpConnectionStatusReason
mcd_presence_frame_get_account_status_reason (McdPresenceFrame *
					      presence_frame,
					      McdAccount * account)
{
    return mcd_account_get_connection_status_reason (account);
}

static void
on_account_current_presence_changed (McdAccount *account,
				     TpConnectionPresenceType presence,
				     const gchar *status, const gchar *message,
				     McdPresenceFrame *presence_frame)
{
    g_signal_emit_by_name (presence_frame, "presence-changed", account,
			   presence, message);

    _mcd_presence_frame_update_actual_presence (presence_frame,
						message);
}

static void
_mcd_presence_frame_update_actual_status (McdPresenceFrame *presence_frame)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    GList *list;

    priv->actual_status = TP_CONNECTION_STATUS_DISCONNECTED;
    for (list = priv->accounts; list; list = list->next)
    {
	McdAccount *account = list->data;
	TpConnectionStatus status;

	status = mcd_account_get_connection_status (account);
	g_debug ("Account %s is %d", mcd_account_get_unique_name (account),
		 status);
	if (status == TP_CONNECTION_STATUS_CONNECTING)
	{
	    priv->actual_status = status;
	    break;
	}
	else if (status == TP_CONNECTION_STATUS_CONNECTED)
	    priv->actual_status = status;
    }
}

static void
on_account_connection_status_changed (McdAccount *account,
				      TpConnectionStatus status,
				      TpConnectionStatusReason reason,
				      McdPresenceFrame *presence_frame)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    TpConnectionStatus conn_status;

    g_signal_emit (presence_frame, mcd_presence_frame_signals[STATUS_CHANGED],
		   0, account, status, reason);

    conn_status = priv->actual_status;
    _mcd_presence_frame_update_actual_status (presence_frame);
    if (conn_status != priv->actual_status ||
       	priv->actual_status != TP_CONNECTION_STATUS_CONNECTING)
	g_signal_emit (presence_frame,
		       mcd_presence_frame_signals[STATUS_ACTUAL],
		       0, priv->actual_status);
}

static gboolean
mcd_presence_frame_add_account (McdPresenceFrame * presence_frame,
                                McdAccount *account)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    GList *pos;

    /* Let's see if we already have the account */
    pos = g_list_find (priv->accounts, account);
    if (pos) return FALSE;

    g_object_ref (account);
    priv->accounts = g_list_prepend (priv->accounts,
				     account);
    g_signal_connect (account, "current-presence-changed",
		      G_CALLBACK (on_account_current_presence_changed),
		      presence_frame);
    g_signal_connect (account, "connection-status-changed",
		      G_CALLBACK (on_account_connection_status_changed),
		      presence_frame);
    
    return TRUE;
}

static gboolean
mcd_presence_frame_remove_account (McdPresenceFrame * presence_frame,
                                   McdAccount *account)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    GList *pos;

    pos = g_list_find (priv->accounts, account);
    if (!pos) return FALSE;
    
    g_debug ("%s: removing account %s", G_STRFUNC, mcd_account_get_unique_name (account));
    /*_mcd_presence_frame_update_actual_presence (presence_frame, NULL);*/
    g_signal_handlers_disconnect_by_func (account, 
					  on_account_current_presence_changed,
					  presence_frame);
    g_signal_handlers_disconnect_by_func (account, 
					  on_account_connection_status_changed,
					  presence_frame);
    g_object_unref (account);
    priv->accounts = g_list_delete_link (priv->accounts, pos);

    if (g_list_length (priv->accounts) == 0)
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
    return priv->actual_status != TP_CONNECTION_STATUS_CONNECTING;
}

static void
add_account (gpointer key, gpointer value, gpointer userdata)
{
    McdPresenceFrame *presence_frame = userdata;
    McdAccount *account = value;

    mcd_presence_frame_add_account (presence_frame, account);
}

static void
on_account_validity_changed (McdAccountManager *account_manager,
			     const gchar *object_path, gboolean valid,
			     McdPresenceFrame *presence_frame)
{
    McdAccount *account;

    account = mcd_account_manager_lookup_account_by_path (account_manager,
							  object_path);
    if (valid)
	mcd_presence_frame_add_account (presence_frame, account);
    else
	mcd_presence_frame_remove_account (presence_frame, account);
}

void
mcd_presence_frame_set_account_manager (McdPresenceFrame *presence_frame,
					McdAccountManager *account_manager)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    GHashTable *accounts;

    g_object_ref (account_manager);
    priv->account_manager = account_manager;
    accounts = mcd_account_manager_get_valid_accounts (priv->account_manager);

    g_hash_table_foreach (accounts, add_account, presence_frame);

    g_signal_connect (account_manager, "account-validity-changed",
		      G_CALLBACK (on_account_validity_changed),
		      presence_frame);
}

GList *
mcd_presence_frame_get_accounts (McdPresenceFrame *presence_frame)
{
    McdPresenceFramePrivate *priv = MCD_PRESENCE_FRAME_PRIV (presence_frame);
    return priv->accounts;
}

