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
 * SECTION:mcd-channel
 * @title: McdChannel
 * @short_description: Channel class representing Telepathy channel class
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-channel.h
 * 
 * FIXME
 */

#include <glib/gi18n.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/dbus.h>

#include "mcd-channel.h"
#include "mcd-enum-types.h"
#include "_gen/gtypes.h"

#define MCD_CHANNEL_PRIV(channel) (MCD_CHANNEL (channel)->priv)
#define INVALID_SELF_HANDLE ((guint) -1)

G_DEFINE_TYPE (McdChannel, mcd_channel, MCD_TYPE_MISSION);

struct _McdChannelPrivate
{
    /* Channel info */
    GQuark type_quark;
    guint handle;
    TpHandleType handle_type;
    gboolean outgoing;
    
    /* Channel created based on the above channel info */
    TpChannel *tp_chan;
    guint has_group_if : 1;

    /* boolean properties */
    guint self_handle_ready : 1;
    guint name_ready : 1;
    guint local_pending_members_ready : 1;

    /* Pending members */
    GArray *pending_local_members;
    gboolean members_accepted;
    gboolean missed;
    guint self_handle;
    
    McdChannelStatus status;
    gchar *channel_name;
    const gchar *initiator_id;
    
    /* Requestor info */
    guint requestor_serial;
    gchar *requestor_client_id;
    
    gboolean is_disposed;
};

typedef struct
{
    guint member;
    guint actor;
} PendingMemberInfo;

typedef struct
{
    gchar *path;

    GHashTable *properties;
    guint64 user_time;
    gchar *preferred_handler;

    gboolean use_existing;
} McdChannelRequestData;

enum _McdChannelSignalType
{
    STATUS_CHANGED,
    MEMBERS_ACCEPTED,
    LAST_SIGNAL
};

enum _McdChannelPropertyType
{
    PROP_CONNECTION=1,
    PROP_TP_CHANNEL,
    PROP_CHANNEL_STATUS,
    PROP_CHANNEL_TYPE,
    PROP_TYPE,
    PROP_CHANNEL_TYPE_QUARK,
    PROP_CHANNEL_HANDLE,
    PROP_HANDLE,
    PROP_CHANNEL_HANDLE_TYPE,
    PROP_HANDLE_TYPE,
    PROP_OUTGOING,
    PROP_REQUESTOR_SERIAL,
    PROP_REQUESTOR_CLIENT_ID,
    PROP_SELF_HANDLE_READY,
    PROP_NAME_READY,
    PROP_INVITER_READY,
};

#define DEPRECATED_PROPERTY_WARNING \
    g_warning ("%s: property %s is deprecated", G_STRFUNC, pspec->name)

#define CD_IMMUTABLE_PROPERTIES "_immprop"
#define CD_ERROR    "_error"
#define CD_REQUEST  "_reqdata"

static guint mcd_channel_signals[LAST_SIGNAL] = { 0 };

#define REQUEST_OBJ_BASE "/com/nokia/MissionControl/requests/r"
static guint last_req_id = 1;


static void _mcd_channel_release_tp_channel (McdChannel *channel,
					     gboolean close_channel);
static void on_proxied_channel_status_changed (McdChannel *source,
                                               McdChannelStatus status,
                                               McdChannel *dest);

static void
channel_request_data_free (McdChannelRequestData *crd)
{
    g_debug ("%s called for %p", G_STRFUNC, crd);
    g_hash_table_unref (crd->properties);
    g_free (crd->preferred_handler);
    g_free (crd->path);
    g_slice_free (McdChannelRequestData, crd);
}

static void
on_members_changed (TpChannel *proxy, const gchar *message,
		    const GArray *added, const GArray *removed,
		    const GArray *l_pending, const GArray *r_pending,
		    guint actor, guint reason, gpointer user_data,
		    GObject *weak_object)
{
    McdChannel *channel = MCD_CHANNEL (weak_object);
    McdChannelPrivate *priv = user_data;
    /* Local pending members? Add to the array and exit. */

    if (l_pending && l_pending->len > 0)
    {
	guint i;
	/* FIXME: Add duplicity check */
	for (i = 0; i < l_pending->len; i++)
	{
	    PendingMemberInfo pmi;

	    pmi.member = g_array_index (l_pending, guint, i);
	    pmi.actor = actor;
	    g_array_append_val (priv->pending_local_members, pmi);
	    g_debug ("Added handle %u to channel pending members", pmi.member);
	}
    }

    /* Added members? If any of them are in the local pending array, we can
     * remove the lock restoration flag */

    if (added && added->len > 0)
    {
	guint i, j;
	g_debug ("%u added members", added->len);
	for (i = 0; i < added->len; i++)
	{
	    guint added_member = g_array_index (added, guint, i);

	    /* N^2 complexity is not good, however with VOIP calls we should
	     * not bump into significant number of members */

	    for (j = 0; j < priv->pending_local_members->len; j++)
	    {
		PendingMemberInfo *pmi;

		pmi = &g_array_index (priv->pending_local_members, PendingMemberInfo, i);
		if (added_member == pmi->member)
		{
		    g_debug
			("Pending local member added -> do not restore lock");
		    g_debug
			("This should appear only when the call was accepted");
		    /* mcd_object_get ()->filters_unlocked_tk_lock = FALSE; */
		    priv->members_accepted = TRUE;
		    g_signal_emit_by_name (channel, "members-accepted");
		    break;
		}
	    }
	}
    }
    /* FIXME: We should also remove members from the local pending
     * array, even if we don't need the info */
    if (removed && removed->len > 0)
    {
	guint i;

	if (actor != priv->self_handle)
	{
	    for (i = 0; i < removed->len; i++)
	    {
		if (actor == g_array_index (removed, guint, i))
		{
		    /* the remote removed itself; if we didn't accept the call,
		     * it's a missed channel */
		    if (!priv->members_accepted) priv->missed = TRUE;
		    break;
		}
	    }
	}
    }
}

static void
group_get_local_pending_members_with_info (TpChannel *proxy,
					   const GPtrArray *l_pending,
					   const GError *error,
					   gpointer user_data,
					   GObject *weak_object)
{
    McdChannelPrivate *priv = user_data;

    priv->local_pending_members_ready = TRUE;
    if (error)
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
	return;
    }

    if (l_pending)
    {
	guint i;
	g_debug ("%u local pending members, adding", l_pending->len);
	/* FIXME: Add duplicity check */
	for (i = 0; i < l_pending->len; i++)
	{
	    PendingMemberInfo pmi;
	    GValueArray *va;

	    va = g_ptr_array_index (l_pending, i);
	    pmi.member = g_value_get_uint (va->values);
	    pmi.actor = g_value_get_uint (va->values + 1);
	    g_array_append_val (priv->pending_local_members, pmi);
	    g_debug ("Added handle %u to channel pending members", pmi.member);
	}
    }
}

/* The callback is called on channel Closed signal */
static void
on_closed (TpChannel *proxy, gpointer user_data, GObject *weak_object)
{
    McdChannel *channel = MCD_CHANNEL (weak_object);

    g_debug ("%s called for %p", G_STRFUNC, channel);
    _mcd_channel_release_tp_channel (channel, FALSE);
    mcd_mission_abort (MCD_MISSION (channel));
    g_debug ("Channel closed");
}

static void
proxy_destroyed (TpProxy *self, guint domain, gint code, gchar *message,
		 gpointer user_data)
{
    McdChannel *channel = user_data;

    g_debug ("Channel proxy destroyed (%s)!", message);
    /*
    McdChannelPrivate *priv = channel->priv;
    g_object_unref (priv->tp_chan);
    priv->tp_chan = NULL;
    */
    mcd_mission_abort (MCD_MISSION (channel));
    g_debug ("Channel closed");
}

static void
group_get_self_handle_cb (TpChannel *proxy, guint self_handle,
			  const GError *error, gpointer user_data,
			  GObject *weak_object)
{
    McdChannel *channel = MCD_CHANNEL (weak_object);
    McdChannelPrivate *priv = user_data;
    if (error)
	g_warning ("get_self_handle failed: %s", error->message);
    else
    {
	priv->self_handle = self_handle;
	g_debug ("channel %p: got self handle %u", channel, self_handle);
    }
    priv->self_handle_ready = TRUE;
    g_object_notify ((GObject *)channel, "self-handle-ready");
}

static inline void
_mcd_channel_setup_group (McdChannel *channel)
{
    McdChannelPrivate *priv = channel->priv;

    tp_cli_channel_interface_group_connect_to_members_changed (priv->tp_chan,
							       on_members_changed,
							       priv, NULL,
							       (GObject *)channel,
							       NULL);
    tp_cli_channel_interface_group_call_get_self_handle (priv->tp_chan, -1,
							 group_get_self_handle_cb,
							 priv, NULL,
							 (GObject *)channel);
    tp_cli_channel_interface_group_call_get_local_pending_members_with_info (priv->tp_chan, -1,
							group_get_local_pending_members_with_info,
									     priv, NULL,
									     (GObject *)channel);
}

static void
inspect_channel_handle_cb (TpConnection *proxy, const gchar **handle_names,
			   const GError *error, gpointer user_data,
			   GObject *weak_object)
{
    McdChannel *channel = MCD_CHANNEL (weak_object);
    McdChannelPrivate *priv = user_data;

    if (error)
	g_warning ("%s: InspectHandles failed: %s", G_STRFUNC, error->message);
    else
	priv->channel_name = g_strdup (handle_names[0]);
    priv->name_ready = TRUE;
    g_object_notify ((GObject *)channel, "name-ready");
}

static void
on_channel_ready (TpChannel *tp_chan, const GError *error, gpointer user_data)
{
    McdChannel *channel, **channel_ptr = user_data;
    McdChannelPrivate *priv;
    TpConnection *tp_conn;
    GArray request_handles;

    channel = *channel_ptr;
    if (channel)
	g_object_remove_weak_pointer ((GObject *)channel,
				      (gpointer)channel_ptr);
    g_slice_free (McdChannel *, channel_ptr);
    if (error)
    {
	g_debug ("%s got error: %s", G_STRFUNC, error->message);
	return;
    }

    if (!channel) return;

    priv = channel->priv;
    g_object_get (priv->tp_chan,
		  "connection", &tp_conn,
		  "handle", &priv->handle,
		  "handle-type", &priv->handle_type,
		  NULL);
    g_debug ("%s: handle %u, type %u", G_STRFUNC,
	     priv->handle_type, priv->handle);
    if (priv->handle_type != 0)
    {
	/* get the name of the channel */
	request_handles.len = 1;
	request_handles.data = (gchar *)&priv->handle;
	tp_cli_connection_call_inspect_handles (tp_conn, -1,
						priv->handle_type,
						&request_handles,
						inspect_channel_handle_cb,
						priv, NULL,
						(GObject *)channel);
    }
    g_object_unref (tp_conn);

    priv->has_group_if = tp_proxy_has_interface_by_id (priv->tp_chan,
						       TP_IFACE_QUARK_CHANNEL_INTERFACE_GROUP);
    if (priv->has_group_if)
	_mcd_channel_setup_group (channel);
}

static void
_mcd_channel_release_tp_channel (McdChannel *channel, gboolean close_channel)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);
    if (priv->tp_chan)
    {
	g_signal_handlers_disconnect_by_func (G_OBJECT (priv->tp_chan),
					      G_CALLBACK (proxy_destroyed),
					      channel);

	if (close_channel && !TP_PROXY (priv->tp_chan)->invalidated &&
            priv->type_quark != TP_IFACE_QUARK_CHANNEL_TYPE_CONTACT_LIST)
	{
	    g_debug ("%s: Requesting telepathy to close the channel", G_STRFUNC);
	    tp_cli_channel_call_close (priv->tp_chan, -1, NULL, NULL, NULL, NULL);
	}
	/* Destroy our proxy */
	g_object_unref (priv->tp_chan);
	
	priv->tp_chan = NULL;
    }
}

static inline void
_mcd_channel_setup (McdChannel *channel, McdChannelPrivate *priv)
{
    McdChannel **channel_ptr;

    channel_ptr = g_slice_alloc (sizeof (McdChannel *));
    *channel_ptr = channel;
    g_object_add_weak_pointer ((GObject *)channel, (gpointer)channel_ptr);
    tp_channel_call_when_ready (priv->tp_chan, on_channel_ready, channel_ptr);

    /* We want to track the channel object closes, because we need to do
     * some cleanups when it's gone */
    tp_cli_channel_connect_to_closed (priv->tp_chan, on_closed,
				      priv, NULL, (GObject *)channel,
				      NULL);
    g_signal_connect (priv->tp_chan, "invalidated",
		      G_CALLBACK (proxy_destroyed), channel);
}

static void
_mcd_channel_set_property (GObject * obj, guint prop_id,
			   const GValue * val, GParamSpec * pspec)
{
    TpChannel *tp_chan;
    McdChannel *channel = MCD_CHANNEL (obj);
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (obj);

    switch (prop_id)
    { 
    case PROP_CHANNEL_STATUS:
        DEPRECATED_PROPERTY_WARNING;
	priv->status = g_value_get_enum (val);
	g_signal_emit_by_name (channel, "status-changed", priv->status);
	break;
    case PROP_CONNECTION:
         break;
    case PROP_TP_CHANNEL:
	tp_chan = g_value_get_object (val);
	if (tp_chan)
	{
            g_return_if_fail (priv->handle >= 0);
 
	    g_object_ref (tp_chan);
	}
	_mcd_channel_release_tp_channel (channel, TRUE);
	priv->tp_chan = tp_chan;
	if (priv->tp_chan)
	    _mcd_channel_setup (channel, priv);
	break;
    case PROP_CHANNEL_TYPE:
        DEPRECATED_PROPERTY_WARNING;
    case PROP_TYPE:
        priv->type_quark = g_quark_from_string (g_value_get_string (val));
	break;
    case PROP_CHANNEL_TYPE_QUARK:
        DEPRECATED_PROPERTY_WARNING;
         break;
    case PROP_CHANNEL_HANDLE:
        DEPRECATED_PROPERTY_WARNING;
    case PROP_HANDLE:
	priv->handle = g_value_get_uint (val);
	break;
    case PROP_CHANNEL_HANDLE_TYPE:
        DEPRECATED_PROPERTY_WARNING;
    case PROP_HANDLE_TYPE:
	priv->handle_type = g_value_get_uint (val);
	break;
    case PROP_OUTGOING:
	priv->outgoing = g_value_get_boolean (val);
	break;
    case PROP_REQUESTOR_SERIAL:
        DEPRECATED_PROPERTY_WARNING;
        priv->requestor_serial = g_value_get_uint (val);
        break;
    case PROP_REQUESTOR_CLIENT_ID:
	g_free (priv->requestor_client_id);
	priv->requestor_client_id = g_value_dup_string (val);
        break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_channel_get_property (GObject * obj, guint prop_id,
			   GValue * val, GParamSpec * pspec)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (obj);

    switch (prop_id)
    {
    case PROP_CONNECTION:
	/* FIXME: This is presumetous and wrong */
	g_value_set_object (val, mcd_mission_get_parent (MCD_MISSION (obj)));
	break;
    case PROP_CHANNEL_STATUS:
        DEPRECATED_PROPERTY_WARNING;
	g_value_set_enum (val, priv->status);
	break;
    case PROP_TP_CHANNEL:
	g_value_set_object (val, priv->tp_chan);
	break;
    case PROP_CHANNEL_TYPE:
        DEPRECATED_PROPERTY_WARNING;
	g_value_set_static_string (val, g_quark_to_string (priv->type_quark));
	break;
    case PROP_CHANNEL_TYPE_QUARK:
        DEPRECATED_PROPERTY_WARNING;
	g_value_set_uint (val, priv->type_quark);
	break;
    case PROP_CHANNEL_HANDLE:
        DEPRECATED_PROPERTY_WARNING;
	g_value_set_uint (val, priv->handle);
	break;
    case PROP_CHANNEL_HANDLE_TYPE:
        DEPRECATED_PROPERTY_WARNING;
	g_value_set_uint (val, priv->handle_type);
	break;
    case PROP_OUTGOING:
	g_value_set_boolean (val, priv->outgoing);
	break;
    case PROP_REQUESTOR_SERIAL:
        DEPRECATED_PROPERTY_WARNING;
	g_value_set_uint (val, priv->requestor_serial);
	break;
    case PROP_REQUESTOR_CLIENT_ID:
	g_value_set_string (val, priv->requestor_client_id);
	break;
    case PROP_SELF_HANDLE_READY:
	g_value_set_boolean (val, priv->self_handle_ready);
	break;
    case PROP_NAME_READY:
	g_value_set_boolean (val, priv->name_ready);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_channel_finalize (GObject * object)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (object);
    
    g_array_free (priv->pending_local_members, TRUE);
    g_free (priv->requestor_client_id);
    g_free (priv->channel_name);
    
    G_OBJECT_CLASS (mcd_channel_parent_class)->finalize (object);
}

static void
_mcd_channel_dispose (GObject * object)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (object);
   
    g_debug ("\n\n%s for %p (is disposed = %d)", G_STRFUNC, object, priv->is_disposed);
    if (priv->is_disposed)
	return;

    priv->is_disposed = TRUE;
    _mcd_channel_release_tp_channel (MCD_CHANNEL (object), TRUE);
    G_OBJECT_CLASS (mcd_channel_parent_class)->dispose (object);
}

static void
mcd_channel_abort (McdMission *mission)
{
    McdChannel *channel = MCD_CHANNEL (mission);
    McdChannelPrivate *priv = channel->priv;

    g_debug ("%s: %p", G_STRFUNC, mission);
    /* If this is still a channel request, signal the failure */
    if (priv->status == MCD_CHANNEL_STATUS_REQUEST ||
        priv->status == MCD_CHANNEL_STATUS_DISPATCHING)
    {
        /* this code-path can only happen if the connection is aborted, as in
         * the other cases we handle the error in McdChannel; for this reason,
         * we use the DISCONNECTED error code */
        GError *error = g_error_new (TP_ERRORS, TP_ERROR_DISCONNECTED,
                                     "Channel aborted");
        _mcd_channel_set_error (channel, error);
    }

    /* chain up with the parent */
    MCD_MISSION_CLASS (mcd_channel_parent_class)->abort (mission);
}

static void
mcd_channel_status_changed (McdChannel *channel, McdChannelStatus status)
{
    channel->priv->status = status;
}

static void
mcd_channel_class_init (McdChannelClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    McdMissionClass *mission_class = MCD_MISSION_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdChannelPrivate));

    object_class->finalize = _mcd_channel_finalize;
    object_class->dispose = _mcd_channel_dispose;
    object_class->set_property = _mcd_channel_set_property;
    object_class->get_property = _mcd_channel_get_property;
    mission_class->abort = mcd_channel_abort;
    klass->status_changed_signal = mcd_channel_status_changed;

    /* signals */
    mcd_channel_signals[STATUS_CHANGED] =
	g_signal_new ("status-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdChannelClass,
				       status_changed_signal),
		      NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE,
		      1, G_TYPE_INT);
    mcd_channel_signals[MEMBERS_ACCEPTED] =
	g_signal_new ("members-accepted", G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdChannelClass,
				       members_accepted_signal),
		      NULL,
		      NULL, g_cclosure_marshal_VOID__VOID,
		      G_TYPE_NONE, 0);

    /* properties */
    g_object_class_install_property
        (object_class, PROP_CONNECTION,
         g_param_spec_object ("connection",
                              "McdConnection",
                              "McdConnection",
                              G_TYPE_OBJECT,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property
        (object_class, PROP_TP_CHANNEL,
         g_param_spec_object ("tp-channel",
                              "Telepathy Channel",
                              "Telepathy Channel",
                              TP_TYPE_CHANNEL,
                              G_PARAM_READWRITE));
    g_object_class_install_property
        (object_class, PROP_CHANNEL_STATUS,
         g_param_spec_enum ("channel-status",
                            "Channel status",
                            "Channel status",
                            MCD_TYPE_CHANNEL_STATUS, MCD_CHANNEL_STATUS_REQUEST,
                            G_PARAM_READWRITE));
    g_object_class_install_property
        (object_class, PROP_CHANNEL_TYPE,
         g_param_spec_string ("channel-type",
                              "Channel type",
                              "Telepathy channel type",
                              NULL,
                              G_PARAM_READWRITE));
    g_object_class_install_property
        (object_class, PROP_TYPE,
         g_param_spec_string ("type", "type", "type",
                              NULL,
                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property
        (object_class, PROP_CHANNEL_TYPE_QUARK,
         g_param_spec_uint ("channel-type-quark",
                            "Telepathy channel type in quark form",
                            "Telepathy channel type in quark form",
                            0, G_MAXINT, 0,
                            G_PARAM_READWRITE));
    g_object_class_install_property
        (object_class, PROP_CHANNEL_HANDLE,
         g_param_spec_uint ("channel-handle",
                            "Telepathy channel handle",
                            "Telepathy channel handle",
                            0, G_MAXINT, 0,
                            G_PARAM_READWRITE));
    g_object_class_install_property
        (object_class, PROP_HANDLE,
         g_param_spec_uint ("handle", "handle", "handle",
                            0, G_MAXUINT, 0,
                            G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property
        (object_class, PROP_CHANNEL_HANDLE_TYPE,
         g_param_spec_uint ("channel-handle-type",
                            "Telepathy channel handle type",
                            "Telepathy channel handle type",
                            0, G_MAXINT, 0,
                            G_PARAM_READWRITE));
    g_object_class_install_property
        (object_class, PROP_HANDLE_TYPE,
         g_param_spec_uint ("handle-type", "handle-type", "handle-type",
                            0, G_MAXUINT, 0,
                            G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property
        (object_class, PROP_OUTGOING,
         g_param_spec_boolean ("outgoing",
                               "Outgoing channel",
                               "True if the channel was requested by us",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property
        (object_class, PROP_REQUESTOR_SERIAL,
         g_param_spec_uint ("requestor-serial",
                            "Requestor serial number",
                            "Requestor serial number",
                            0, G_MAXINT, 0,
                            G_PARAM_READWRITE));
    g_object_class_install_property
        (object_class, PROP_REQUESTOR_CLIENT_ID,
         g_param_spec_string ("requestor-client-id",
                              "Requestor client id",
                              "Requestor client id",
                              NULL,
                              G_PARAM_READWRITE));
    g_object_class_install_property
        (object_class, PROP_SELF_HANDLE_READY,
         g_param_spec_boolean ("self-handle-ready",
                               "Self handle ready",
                               "Self handle ready",
                               FALSE,
                               G_PARAM_READABLE));
    g_object_class_install_property
        (object_class, PROP_NAME_READY,
         g_param_spec_boolean ("name-ready",
                               "Name ready",
                               "Name ready",
                               FALSE,
                               G_PARAM_READABLE));
}

static void
mcd_channel_init (McdChannel * obj)
{
    McdChannelPrivate *priv;
   
    priv = G_TYPE_INSTANCE_GET_PRIVATE (obj, MCD_TYPE_CHANNEL,
					McdChannelPrivate);
    obj->priv = priv;

    priv->self_handle = INVALID_SELF_HANDLE;
    priv->pending_local_members = g_array_new (FALSE, FALSE,
					       sizeof (PendingMemberInfo));
}

McdChannel *
mcd_channel_new (TpChannel * tp_chan,
		 const gchar *type, guint handle,
		 TpHandleType handle_type, gboolean outgoing,
		 guint requestor_serial, const gchar *requestor_client_id)
{
    McdChannel *obj;
    obj = MCD_CHANNEL (g_object_new (MCD_TYPE_CHANNEL,
				     "type", type,
				     "handle", handle,
				     "handle-type", handle_type,
				     "outgoing", outgoing,
				     "requestor-serial", requestor_serial,
				     "requestor-client-id", requestor_client_id,
				     "tp-channel", tp_chan,
				     NULL));
    return obj;
}

/**
 * mcd_channel_new_from_path:
 * @connection: the #TpConnection on which the channel exists.
 * @object_path: the D-Bus object path of an existing channel.
 * @type: the channel type.
 * @handle: the channel handle.
 * @handle_type: the #TpHandleType.
 *
 * Creates a #McdChannel with an associated #TpChannel proxy for the channel
 * located at @object_path.
 *
 * Returns: a new #McdChannel if the #TpChannel was created successfully, %NULL
 * otherwise.
 */
McdChannel *
mcd_channel_new_from_path (TpConnection *connection, const gchar *object_path,
                           const gchar *type, guint handle,
                           TpHandleType handle_type)
{
    McdChannel *channel;
    channel = g_object_new (MCD_TYPE_CHANNEL,
                            "type", type,
                            "handle", handle,
                            "handle-type", handle_type,
                            NULL);
    if (mcd_channel_set_object_path (channel, connection, object_path))
        return channel;
    else
    {
        g_object_unref (channel);
        return NULL;
    }
}

/**
 * mcd_channel_set_object_path:
 * @channel: the #McdChannel.
 * @connection: the #TpConnection on which the channel exists.
 * @object_path: the D-Bus object path of an existing channel.
 *
 * This method makes @channel create a #TpChannel object for @object_path.
 * It must not be called it @channel has already a #TpChannel associated with
 * it.
 *
 * Returns: %TRUE if the #TpChannel has been created, %FALSE otherwise.
 */
gboolean
mcd_channel_set_object_path (McdChannel *channel, TpConnection *connection,
                             const gchar *object_path)
{
    McdChannelPrivate *priv;
    GError *error = NULL;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), FALSE);
    priv = channel->priv;

    g_return_val_if_fail (priv->tp_chan == NULL, FALSE);
    priv->tp_chan = tp_channel_new (connection, object_path,
                                    g_quark_to_string (priv->type_quark),
                                    priv->handle_type,
                                    priv->handle,
                                    &error);
    if (error)
    {
        g_warning ("%s: tp_channel_new returned error: %s",
                   G_STRFUNC, error->message);
        g_error_free (error);
    }

    if (priv->tp_chan)
    {
        _mcd_channel_setup (channel, priv);
        return TRUE;
    }
    else
        return FALSE;
}

void
mcd_channel_set_status (McdChannel *channel, McdChannelStatus status)
{
    g_debug ("%s: %p, %u", G_STRFUNC, channel, status);
    g_return_if_fail(MCD_IS_CHANNEL(channel));

    if (status != channel->priv->status)
    {
        g_object_ref (channel);
        g_signal_emit_by_name (channel, "status-changed", status);
        g_object_unref (channel);
    }
}

McdChannelStatus
mcd_channel_get_status (McdChannel *channel)
{
    return MCD_CHANNEL_PRIV (channel)->status;
}

gboolean
mcd_channel_get_members_accepted (McdChannel *channel)
{
    return MCD_CHANNEL_PRIV (channel)->members_accepted;
}

const gchar *
mcd_channel_get_channel_type (McdChannel *channel)
{
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    return g_quark_to_string (channel->priv->type_quark);
}

GQuark
mcd_channel_get_channel_type_quark (McdChannel *channel)
{
    return MCD_CHANNEL_PRIV (channel)->type_quark;
}

const gchar *
mcd_channel_get_object_path (McdChannel *channel)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);

    return priv->tp_chan ? TP_PROXY (priv->tp_chan)->object_path : NULL;
}

void
mcd_channel_set_handle (McdChannel *channel, guint handle)
{
    g_return_if_fail (MCD_IS_CHANNEL (channel));
    channel->priv->handle = handle;
}

guint
mcd_channel_get_handle (McdChannel *channel)
{
    return MCD_CHANNEL_PRIV (channel)->handle;
}

TpHandleType
mcd_channel_get_handle_type (McdChannel *channel)
{
    return MCD_CHANNEL_PRIV (channel)->handle_type;
}

GPtrArray*
mcd_channel_get_members (McdChannel *channel)
{
    g_warning ("%s called, but shouldn't!", G_STRFUNC);
    return NULL;
}

/**
 * mcd_channel_get_name:
 * @channel: the #McdChannel.
 *
 * Get the Telepathy name of @channel (calls InspectHandles on the channel
 * handle).
 *
 * Returns: a const string holding the channel name.
 */
const gchar *
mcd_channel_get_name (McdChannel *channel)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);

    return priv->channel_name;
}

/**
 * mcd_channel_get_inviter:
 * @channel: the #McdChannel.
 *
 * Get the address of the inviter (i.e. the actor who put us in the pending
 * local members list).
 *
 * Returns: a const string holding the inviter address.
 */
const gchar *
mcd_channel_get_inviter (McdChannel *channel)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);

    return priv->initiator_id;
}

/**
 * mcd_channel_get_self_handle:
 * @channel: the #McdChannel.
 *
 * Gets the self handle (the "self-handle-ready" property tells if this datum
 * is available).
 *
 * Returns: the self handle.
 */
guint
mcd_channel_get_self_handle (McdChannel *channel)
{
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), INVALID_SELF_HANDLE);
    return channel->priv->self_handle;
}

/**
 * mcd_channel_is_missed:
 * @channel: the #McdChannel.
 *
 * Return %TRUE if the remote party removed itself before we could join the
 * channel.
 *
 * Returns: %TRUE if the channel is missed.
 */
gboolean
mcd_channel_is_missed (McdChannel *channel)
{
    return MCD_CHANNEL_PRIV (channel)->missed;
}

/**
 * mcd_channel_leave:
 * @channel: the #McdChannel.
 * @reason: a #TelepathyChannelGroupChangeReason.
 *
 * Leaves @channel with reason @reason.
 *
 * Returns: %TRUE for success, %FALSE otherwise.
 */
gboolean
mcd_channel_leave (McdChannel *channel, const gchar *message,
		   TpChannelGroupChangeReason reason)
{
#if 1
    g_warning ("%s called, but shouldn't!", G_STRFUNC);
    return FALSE;
#else
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);
    DBusGProxy *group;
    GArray members;

    if (!priv->tp_chan) return FALSE;
    group = tp_chan_get_interface (priv->tp_chan,
				   TELEPATHY_CHAN_IFACE_GROUP_QUARK);
    if (!group) return FALSE;
    g_debug ("removing self");
    members.len = 1;
    members.data = (gchar *)&priv->self_handle;
    tp_chan_iface_group_remove_members_with_reason_no_reply (group, &members,
							     message, reason);
    return TRUE;
#endif
}

/*
 * _mcd_channel_set_immutable_properties:
 * @channel: the #McdChannel.
 * @properties: a #GHashTable of immutable properties.
 *
 * Internal function: assign a hash table of properties to @channel.
 */
void
_mcd_channel_set_immutable_properties (McdChannel *channel,
                                       GHashTable *properties)
{
    McdChannelPrivate *priv = channel->priv;
    gboolean present;
    guint handle;

    g_object_set_data_full ((GObject *)channel, CD_IMMUTABLE_PROPERTIES,
                            properties, (GDestroyNotify)g_hash_table_unref);
    /* copy any properties into the channel */
    /* FIXME: this is only a quick fix. This all "immutable properties" thing
     * must be revisited, when a similar TpGlib API exists */
    handle = tp_asv_get_uint32 (properties, TP_IFACE_CHANNEL ".TargetHandle",
                                &present);
    if (present)
        channel->priv->handle = handle;

    priv->initiator_id =
        tp_asv_get_string (properties, TP_IFACE_CHANNEL ".InitiatorID");
}

/*
 * _mcd_channel_get_immutable_properties:
 * @channel: the #McdChannel.
 *
 * Returns: the #GHashTable of the immutable properties.
 */
GHashTable *
_mcd_channel_get_immutable_properties (McdChannel *channel)
{
    return g_object_get_data ((GObject *)channel, CD_IMMUTABLE_PROPERTIES);
}

/*
 * _mcd_channel_details_build_from_list:
 * @channels: a #GList of #McdChannel elements.
 *
 * Returns: a #GPtrArray of Channel_Details, ready to be sent over D-Bus. Free
 * with _mcd_channel_details_free().
 */
GPtrArray *
_mcd_channel_details_build_from_list (GList *channels)
{
    GPtrArray *channel_array;
    GList *list;

    channel_array = g_ptr_array_sized_new (g_list_length (channels));
    for (list = channels; list != NULL; list = list->next)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);
        GHashTable *properties;
        GValue channel_val = { 0, };
        GType type;

        properties = _mcd_channel_get_immutable_properties (channel);

        type = TP_STRUCT_TYPE_CHANNEL_DETAILS;
        g_value_init (&channel_val, type);
        g_value_take_boxed (&channel_val,
                            dbus_g_type_specialized_construct (type));
        dbus_g_type_struct_set (&channel_val,
                                0, mcd_channel_get_object_path (channel),
                                1, properties,
                                G_MAXUINT);

        g_ptr_array_add (channel_array, g_value_get_boxed (&channel_val));
    }

    return channel_array;
}

/*
 * _mcd_channel_details_free:
 * @channels: a #GPtrArray of Channel_Details.
 *
 * Frees the memory used by @channels.
 */
void
_mcd_channel_details_free (GPtrArray *channels)
{
    GValue value = { 0, };

    /* to free the array, put it into a GValue */
    g_value_init (&value, TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST);
    g_value_take_boxed (&value, channels);
    g_value_unset (&value);
}

/*
 * _mcd_channel_get_target_id:
 * @channel: the #McdChannel.
 *
 * Returns: string representing the target contact, or %NULL.
 */
const gchar *
_mcd_channel_get_target_id (McdChannel *channel)
{
    McdChannelRequestData *crd;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    crd = g_object_get_data ((GObject *)channel, CD_REQUEST);
    if (G_UNLIKELY (!crd || !crd->properties)) return NULL;
    return tp_asv_get_string (crd->properties, TP_IFACE_CHANNEL ".TargetID");
}

/*
 * _mcd_channel_set_error:
 * @channel: the #McdChannel.
 * @error: a #GError.
 *
 * Sets @error on channel, and takes ownership of it. As a side effect, if
 * @error is not %NULL this method causes the channel status be set to
 * %MCD_CHANNEL_STATUS_FAILED.
 */
void
_mcd_channel_set_error (McdChannel *channel, GError *error)
{
    g_return_if_fail (MCD_IS_CHANNEL (channel));
    g_object_set_data_full ((GObject *)channel, CD_ERROR,
                            error, (GDestroyNotify)g_error_free);
    if (error)
        mcd_channel_set_status (channel, MCD_CHANNEL_STATUS_FAILED);
}

/*
 * _mcd_channel_get_error:
 * @channel: the #McdChannel.
 *
 * Returns: the #GError, or %NULL if no error is set.
 */
const GError *
_mcd_channel_get_error (McdChannel *channel)
{
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    return g_object_get_data ((GObject *)channel, CD_ERROR);
}

/**
 * mcd_channel_new_request:
 * @properties: a #GHashTable of desired channel properties.
 * @user_time: user action time.
 * @preferred_handler: well-known name of preferred handler.
 *
 * Create a #McdChannel object holding the given properties. The object can
 * then be used to intiate a channel request, by passing it to
 * mcd_connection_request_channel() on a connection in connected state.
 *
 * Returns: a newly created #McdChannel.
 */
McdChannel *
mcd_channel_new_request (GHashTable *properties, guint64 user_time,
                         const gchar *preferred_handler)
{
    McdChannel *channel;
    guint handle;
    TpHandleType handle_type;
    const gchar *channel_type, *target_id;
    McdChannelRequestData *crd;

    channel_type = tp_asv_get_string (properties,
                                      TP_IFACE_CHANNEL ".ChannelType");
    target_id = tp_asv_get_string (properties,
                                   TP_IFACE_CHANNEL ".TargetID");
    handle = tp_asv_get_uint32 (properties,
                                TP_IFACE_CHANNEL ".TargetHandle", NULL);
    handle_type =
        tp_asv_get_uint32 (properties,
                           TP_IFACE_CHANNEL ".TargetHandleType", NULL);

    channel = g_object_new (MCD_TYPE_CHANNEL,
                            "type", channel_type,
                            "handle", handle,
                            "handle-type", handle_type,
                            "outgoing", TRUE,
                            NULL);

    /* TODO: these data could be freed when the channel status becomes
     * MCD_CHANNEL_STATUS_DISPATCHED */
    crd = g_slice_new (McdChannelRequestData);
    crd->path = g_strdup_printf (REQUEST_OBJ_BASE "%u", last_req_id++);
    crd->properties = g_hash_table_ref (properties);
    crd->user_time = user_time;
    crd->preferred_handler = g_strdup (preferred_handler);
    g_object_set_data_full ((GObject *)channel, CD_REQUEST,
                            crd, (GDestroyNotify)channel_request_data_free);

    mcd_channel_set_status (channel, MCD_CHANNEL_STATUS_REQUEST);

    return channel;
}

/*
 * _mcd_channel_get_requested_properties:
 * @channel: the #McdChannel.
 *
 * Returns: #GHashTable of requested properties.
 */
GHashTable *
_mcd_channel_get_requested_properties (McdChannel *channel)
{
    McdChannelRequestData *crd;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    crd = g_object_get_data ((GObject *)channel, CD_REQUEST);
    if (G_UNLIKELY (!crd)) return NULL;
    return crd->properties;
}

/*
 * _mcd_channel_get_request_path:
 * @channel: the #McdChannel.
 *
 * Returns: the object path of the channel request, if the channel is in
 * MCD_CHANNEL_REQUEST status.
 */
const gchar *
_mcd_channel_get_request_path (McdChannel *channel)
{
    McdChannelRequestData *crd;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    crd = g_object_get_data ((GObject *)channel, CD_REQUEST);
    if (G_UNLIKELY (!crd)) return NULL;
    return crd->path;
}

/*
 * _mcd_channel_get_request_user_action_time:
 * @channel: the #McdChannel.
 *
 * Returns: UserActionTime of the channel request, if the channel is in
 * MCD_CHANNEL_REQUEST status.
 */
guint64
_mcd_channel_get_request_user_action_time (McdChannel *channel)
{
    McdChannelRequestData *crd;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), 0);
    crd = g_object_get_data ((GObject *)channel, CD_REQUEST);
    if (G_UNLIKELY (!crd)) return 0;
    return crd->user_time;
}

/*
 * _mcd_channel_get_request_preferred_handler:
 * @channel: the #McdChannel.
 *
 * Returns: the preferred handler specified when requesting the channel, if the
 * channel is in MCD_CHANNEL_REQUEST status.
 */
const gchar *
_mcd_channel_get_request_preferred_handler (McdChannel *channel)
{
    McdChannelRequestData *crd;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    crd = g_object_get_data ((GObject *)channel, CD_REQUEST);
    if (G_UNLIKELY (!crd)) return NULL;
    return crd->preferred_handler;
}

/*
 * _mcd_channel_set_request_use_existing:
 * @channel: the #McdChannel.
 * @use_existing: %TRUE if @channel must be requested via EnsureChannel.
 *
 * Sets the use_existing flag on @channel request.
 */
void
_mcd_channel_set_request_use_existing (McdChannel *channel,
                                       gboolean use_existing)
{
    McdChannelRequestData *crd;

    g_return_if_fail (MCD_IS_CHANNEL (channel));
    crd = g_object_get_data ((GObject *)channel, CD_REQUEST);
    if (G_UNLIKELY (!crd)) return;
    crd->use_existing = use_existing;
}

/*
 * _mcd_channel_get_request_use_existing:
 * @channel: the #McdChannel.
 *
 * Returns: %TRUE if the channel musb be requested via EnsureChannel, %FALSE
 * otherwise.
 */
gboolean
_mcd_channel_get_request_use_existing (McdChannel *channel)
{
    McdChannelRequestData *crd;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), FALSE);
    crd = g_object_get_data ((GObject *)channel, CD_REQUEST);
    if (G_UNLIKELY (!crd)) return FALSE;
    return crd->use_existing;
}

/**
 * mcd_channel_is_requested:
 * @channel: the #McdChannel.
 *
 * Returns: %TRUE if @channel was requested, %FALSE otherwise.
 */
gboolean
mcd_channel_is_requested (McdChannel *channel)
{
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), FALSE);
    return channel->priv->outgoing;
}

/**
 * mcd_channel_get_account:
 * @channel: the #McdChannel.
 *
 * Returns: the #McdAccount on which this channel was created.
 */
McdAccount *
mcd_channel_get_account (McdChannel *channel)
{
    McdMission *connection;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    connection = mcd_mission_get_parent (MCD_MISSION (channel));
    if (G_LIKELY (connection))
        return mcd_connection_get_account (MCD_CONNECTION (connection));
    else
        return NULL;
}

static void
copy_status (McdChannel *source, McdChannel *dest)
{
    McdChannelPrivate *src_priv, *dst_priv;

    src_priv = source->priv;
    dst_priv = dest->priv;
    if (dst_priv->status != src_priv->status)
    {
        g_debug ("%s: source is %d, dest is %d", G_STRFUNC,
                 src_priv->status, dst_priv->status);
        if (src_priv->status == MCD_CHANNEL_STATUS_FAILED)
        {
            const GError *error;

            error = _mcd_channel_get_error (source);
            /* this also takes care of setting the status */
            _mcd_channel_set_error (dest, g_error_copy (error));
        }
        else
            mcd_channel_set_status (dest, src_priv->status);
    }

    if (dst_priv->status == MCD_CHANNEL_STATUS_FAILED ||
        dst_priv->status == MCD_CHANNEL_STATUS_DISPATCHED)
    {
        /* the request is completed, we are not interested in monitor the
         * channel anymore */
        g_signal_handlers_disconnect_by_func
            (source, on_proxied_channel_status_changed, dest);
        mcd_mission_abort (MCD_MISSION (dest));
    }
}

static void
on_proxied_channel_status_changed (McdChannel *source,
                                   McdChannelStatus status,
                                   McdChannel *dest)
{
    copy_status (source, dest);
}

/*
 * _mcd_channel_set_request_proxy:
 * @channel: the requested #McdChannel.
 * @source: the #McdChannel to be proxied.
 *
 * This function turns @channel into a proxy for @source: it listens to
 * "status-changed" signals from @source and replicates them on @channel
 */
void
_mcd_channel_set_request_proxy (McdChannel *channel, McdChannel *source)
{
    g_return_if_fail (MCD_IS_CHANNEL (channel));
    g_return_if_fail (MCD_IS_CHANNEL (source));
    copy_status (source, channel);
    g_signal_connect (source, "status-changed",
                      G_CALLBACK (on_proxied_channel_status_changed), channel);
}

