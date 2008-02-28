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

#include "mcd-channel.h"
#include "mcd-enum-types.h"

#define MCD_CHANNEL_PRIV(channel) (MCD_CHANNEL (channel)->priv)
#define INVALID_SELF_HANDLE ((guint) -1)

G_DEFINE_TYPE (McdChannel, mcd_channel, MCD_TYPE_MISSION);

struct _McdChannelPrivate
{
    /* Channel info */
    gchar *channel_object_path;
    gchar *channel_type;
    GQuark channel_type_quark;
    guint channel_handle;
    TpHandleType channel_handle_type;
    gboolean outgoing;
    
    /* Channel created based on the above channel info */
    TpChannel *tp_chan;
    guint has_group_if : 1;

    /* boolean properties */
    guint self_handle_ready : 1;
    guint name_ready : 1;

    /* Pending members */
    GArray *pending_local_members;
    gboolean members_accepted;
    gboolean missed;
    guint self_handle;
    
    McdChannelStatus status;
    gchar *channel_name;
    
    /* Requestor info */
    guint requestor_serial;
    gchar *requestor_client_id;
    
    gboolean is_disposed;
};

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
    PROP_CHANNEL_OBJECT_PATH,
    PROP_CHANNEL_TYPE,
    PROP_CHANNEL_TYPE_QUARK,
    PROP_CHANNEL_HANDLE,
    PROP_CHANNEL_HANDLE_TYPE,
    PROP_OUTGOING,
    PROP_REQUESTOR_SERIAL,
    PROP_REQUESTOR_CLIENT_ID,
    PROP_SELF_HANDLE_READY,
    PROP_NAME_READY,
};

static guint mcd_channel_signals[LAST_SIGNAL] = { 0 };

static void _mcd_channel_release_tp_channel (McdChannel *channel,
					     gboolean close_channel);

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
	int i;
	/* FIXME: Add duplicity check */
	for (i = 0; i < l_pending->len; i++)
	{
            guint handle;

            handle = g_array_index (l_pending, guint, i);
	    g_array_append_val (priv->pending_local_members, handle);
	    g_debug ("Added handle %u to channel pending members", handle);
	}
    }

    /* Added members? If any of them are in the local pending array, we can
     * remove the lock restoration flag */

    if (added && added->len > 0)
    {
	int i, j;
	g_debug ("%u added members", added->len);
	for (i = 0; i < added->len; i++)
	{
	    guint added_member = g_array_index (added, guint, i);

	    /* N^2 complexity is not good, however with VOIP calls we should
	     * not bump into significant number of members */

	    for (j = 0; j < priv->pending_local_members->len; j++)
	    {
		if (added_member ==
		    g_array_index (priv->pending_local_members, guint, i))
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
	int i;

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
group_get_local_pending_members_cb (TpChannel *proxy, const GArray *l_pending,
				    const GError *error, gpointer user_data,
				    GObject *weak_object)
{
    if (error)
    {
	g_warning ("%s: error: %s", G_STRFUNC, error->message);
	return;
    }

    if (l_pending)
    {
	McdChannelPrivate *priv = user_data;
	int i;
	g_debug ("%u local pending members, adding", l_pending->len);
	/* FIXME: Add duplicity check */
	for (i = 0; i < l_pending->len; i++)
	{
            guint handle;

            handle = g_array_index (l_pending, guint, i);
	    g_array_append_val (priv->pending_local_members, handle);
	    g_debug ("Added handle %u to channel pending members", handle);
	}
    }
}

/* The callback is called on channel Closed signal */
void
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
    McdChannelPrivate *priv = channel->priv;

    g_debug ("Channel proxy destroyed (%s)!", message);
    g_object_unref (priv->tp_chan);
    priv->tp_chan = NULL;
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
    tp_cli_channel_interface_group_call_get_local_pending_members (priv->tp_chan, -1,
								   group_get_local_pending_members_cb,
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
_mcd_channel_ready (McdChannel *channel)
{
    McdChannelPrivate *priv = channel->priv;
    TpConnection *tp_conn;
    GArray request_handles;

    /* get the name of the channel */
    g_object_get (priv->tp_chan, "connection", &tp_conn, NULL);
    request_handles.len = 1;
    request_handles.data = (gchar *)&priv->channel_handle;
    tp_cli_connection_call_inspect_handles (tp_conn, -1,
					    priv->channel_handle_type,
					    &request_handles,
					    inspect_channel_handle_cb,
					    priv, NULL,
					    (GObject *)channel);
    g_object_unref (tp_conn);

    priv->has_group_if = tp_proxy_has_interface_by_id (priv->tp_chan,
						       TP_IFACE_QUARK_CHANNEL_INTERFACE_GROUP);
    if (priv->has_group_if)
	_mcd_channel_setup_group (channel);
}

static void
on_channel_ready (TpChannel *tp_chan, GParamSpec *pspec, McdChannel *channel)
{
    gboolean ready;

    g_object_get (tp_chan, "channel-ready", &ready, NULL);
    if (ready)
	_mcd_channel_ready (channel);
}

static void
close_cb (TpChannel *proxy, const GError *error, gpointer user_data,
	  GObject *weak_object)
{
    if (error)
    {
	g_warning ("%s: Request for channel close failed: %s",
		   G_STRFUNC, error->message);
    }
    g_object_unref (proxy);
}

static void
_mcd_channel_release_tp_channel (McdChannel *channel, gboolean close_channel)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);
    if (priv->tp_chan)
    {
	g_signal_handlers_disconnect_by_func (priv->tp_chan,
					      G_CALLBACK (on_channel_ready),
					      channel);
        
	g_signal_handlers_disconnect_by_func (G_OBJECT (priv->tp_chan),
					      G_CALLBACK (proxy_destroyed),
					      channel);

	if (close_channel && priv->channel_type_quark != TP_IFACE_QUARK_CHANNEL_TYPE_CONTACT_LIST)
	{
	    g_debug ("%s: Requesting telepathy to close the channel", G_STRFUNC);
	    tp_cli_channel_call_close (priv->tp_chan, -1, close_cb, priv, NULL,
				       (GObject *)channel);
	    /* in this case we don't destroy the proxy now; it will be done in
	     * the close_cb */
	}
	else
	    /* Destroy our proxy */
	    g_object_unref (priv->tp_chan);
	
	priv->tp_chan = NULL;
    }
}

static inline void
_mcd_channel_setup (McdChannel *channel, McdChannelPrivate *priv)
{
    gboolean ready;

    /* check if the channel is ready; if not, connect to its "channel-ready"
     * signal */
    g_object_get (priv->tp_chan, "channel-ready", &ready, NULL);
    if (ready)
	_mcd_channel_ready (channel);
    else
	g_signal_connect (priv->tp_chan, "notify::channel-ready",
			  G_CALLBACK (on_channel_ready), channel);

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
	priv->status = g_value_get_enum (val);
	g_signal_emit_by_name (channel, "status-changed", priv->status);
	break;
    case PROP_CONNECTION:
         break;
    case PROP_TP_CHANNEL:
	tp_chan = g_value_get_object (val);
	if (tp_chan)
	{
            g_return_if_fail (priv->channel_object_path != NULL);
            g_return_if_fail (priv->channel_type != NULL);
            g_return_if_fail (priv->channel_handle >= 0);
 
	    g_object_ref (tp_chan);
	}
	_mcd_channel_release_tp_channel (channel, TRUE);
	priv->tp_chan = tp_chan;
	if (priv->tp_chan)
	    _mcd_channel_setup (channel, priv);
	break;
    case PROP_CHANNEL_OBJECT_PATH:
	/* g_return_if_fail (g_value_get_string (val) != NULL); */
	g_free (priv->channel_object_path);
	if (g_value_get_string (val) != NULL)
	    priv->channel_object_path = g_strdup (g_value_get_string (val));
	else
	    priv->channel_object_path = NULL;
	break;
    case PROP_CHANNEL_TYPE:
	/* g_return_if_fail (g_value_get_string (val) != NULL); */
	g_free (priv->channel_type);
	if (g_value_get_string (val) != NULL)
	{
	    priv->channel_type = g_strdup (g_value_get_string (val));
	    priv->channel_type_quark = g_quark_from_string (priv->channel_type);
	}
	else
	{
	    priv->channel_type = NULL;
	    priv->channel_type_quark = 0;
	}
	break;
    case PROP_CHANNEL_TYPE_QUARK:
         break;
    case PROP_CHANNEL_HANDLE:
	priv->channel_handle = g_value_get_uint (val);
	break;
    case PROP_CHANNEL_HANDLE_TYPE:
	priv->channel_handle_type = g_value_get_uint (val);
	break;
    case PROP_OUTGOING:
	priv->outgoing = g_value_get_boolean (val);
	break;
    case PROP_REQUESTOR_SERIAL:
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
	g_value_set_enum (val, priv->status);
	break;
    case PROP_TP_CHANNEL:
	g_value_set_object (val, priv->tp_chan);
	break;
    case PROP_CHANNEL_OBJECT_PATH:
	g_value_set_string (val, priv->channel_object_path);
	break;
    case PROP_CHANNEL_TYPE:
	g_value_set_string (val, priv->channel_type);
	break;
    case PROP_CHANNEL_TYPE_QUARK:
	g_value_set_uint (val, priv->channel_type_quark);
	break;
    case PROP_CHANNEL_HANDLE:
	g_value_set_uint (val, priv->channel_handle);
	break;
    case PROP_CHANNEL_HANDLE_TYPE:
	g_value_set_uint (val, priv->channel_handle_type);
	break;
    case PROP_OUTGOING:
	g_value_set_boolean (val, priv->outgoing);
	break;
    case PROP_REQUESTOR_SERIAL:
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
    
    g_free (priv->channel_object_path);
    g_free (priv->channel_type);
    g_array_free (priv->pending_local_members, TRUE);
    g_free (priv->requestor_client_id);
    g_free (priv->channel_name);
    
    G_OBJECT_CLASS (mcd_channel_parent_class)->finalize (object);
}

static void
_mcd_channel_dispose (GObject * object)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (object);
   
    if (priv->is_disposed)
	return;

    priv->is_disposed = TRUE;
    _mcd_channel_release_tp_channel (MCD_CHANNEL (object), TRUE);
    G_OBJECT_CLASS (mcd_channel_parent_class)->dispose (object);
}

static void
mcd_channel_class_init (McdChannelClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdChannelPrivate));

    object_class->finalize = _mcd_channel_finalize;
    object_class->dispose = _mcd_channel_dispose;
    object_class->set_property = _mcd_channel_set_property;
    object_class->get_property = _mcd_channel_get_property;

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
    g_object_class_install_property (object_class,
				     PROP_CONNECTION,
				     g_param_spec_object ("connection",
							  _ ("McdConnection Object"),
							  _ ("McdConnection Object from which this channel was created"),
							  G_TYPE_OBJECT,
							  G_PARAM_READWRITE | 
							  G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
				     PROP_TP_CHANNEL,
				     g_param_spec_object ("tp-channel",
							  _ ("Telepathy Channel Object"),
							  _ ("Telepathy Channel Object wrapped by it"),
							  TP_TYPE_CHANNEL,
                                                          G_PARAM_READWRITE /* |
                                                          G_PARAM_CONSTRUCT_ONLY */));
    g_object_class_install_property (object_class, PROP_CHANNEL_STATUS,
				     g_param_spec_enum ("channel-status",
							_("Channel status"),
							_("Channel status that indicates the state of channel"),
							MCD_TYPE_CHANNEL_STATUS,
							MCD_CHANNEL_PENDING,
							G_PARAM_READWRITE));
    g_object_class_install_property (object_class, PROP_CHANNEL_OBJECT_PATH,
				     g_param_spec_string ("channel-object-path",
							  _ ("Channel dbus object path"),
							  _ ("DBus Bus name to use by us"),
							  NULL,
							  G_PARAM_READWRITE /*|
							  G_PARAM_CONSTRUCT_ONLY */));
    g_object_class_install_property (object_class, PROP_CHANNEL_TYPE,
				     g_param_spec_string ("channel-type",
							  _ ("Channel type"),
							  _ ("Telepathy channel type"),
							  NULL,
							  G_PARAM_READWRITE /*|
							  G_PARAM_CONSTRUCT_ONLY*/));
    g_object_class_install_property (object_class, PROP_CHANNEL_TYPE_QUARK,
				     g_param_spec_uint ("channel-type-quark",
						       _("Telepathy channel type in quark form"),
						       _("Telepathy channel type in quark form"),
						       0,
						       G_MAXINT,
						       0,
						       G_PARAM_READWRITE /*|
						       G_PARAM_CONSTRUCT_ONLY*/));
    g_object_class_install_property (object_class, PROP_CHANNEL_HANDLE,
				     g_param_spec_uint ("channel-handle",
						       _("Telepathy channel handle"),
						       _("Telepathy channel handle"),
						       0,
						       G_MAXINT,
						       0,
						       G_PARAM_READWRITE /*|
						       G_PARAM_CONSTRUCT_ONLY */));
    g_object_class_install_property (object_class, PROP_CHANNEL_HANDLE_TYPE,
				     g_param_spec_uint ("channel-handle-type",
						       _("Telepathy channel handle type"),
						       _("Telepathy channel handle type"),
						       0,
						       G_MAXINT,
						       0,
						       G_PARAM_READWRITE /* |
						       G_PARAM_CONSTRUCT_ONLY */));
    g_object_class_install_property (object_class, PROP_OUTGOING,
				     g_param_spec_boolean ("outgoing",
						       _("Outgoing channel"),
						       _("True if the channel was requested by us"),
						       FALSE,
						       G_PARAM_READWRITE |
						       G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class, PROP_REQUESTOR_SERIAL,
				     g_param_spec_uint ("requestor-serial",
						       _("Requestor serial number"),
						       _("Requestor serial number"),
						       0,
						       G_MAXINT,
						       0,
						       G_PARAM_READWRITE));
    g_object_class_install_property (object_class, PROP_REQUESTOR_CLIENT_ID,
				     g_param_spec_string ("requestor-client-id",
							  _("Requestor client id"),
							  _("Requestor client id"),
							  NULL, G_PARAM_READWRITE));
    g_object_class_install_property (object_class, PROP_SELF_HANDLE_READY,
				     g_param_spec_boolean ("self-handle-ready",
						       _("Self handle ready"),
						       _("Self handle ready"),
						       FALSE,
						       G_PARAM_READABLE));
    g_object_class_install_property (object_class, PROP_NAME_READY,
				     g_param_spec_boolean ("name-ready",
						       _("Name ready"),
						       _("Name ready"),
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
					       sizeof (guint));
}

McdChannel *
mcd_channel_new (TpChannel * tp_chan, const gchar *channel_object_path,
		 const gchar *channel_type, guint channel_handle,
		 TpHandleType channel_handle_type, gboolean outgoing,
		 guint requestor_serial, const gchar *requestor_client_id)
{
    McdChannel *obj;
    obj = MCD_CHANNEL (g_object_new (MCD_TYPE_CHANNEL,
				     "channel-object-path", channel_object_path,
				     "channel-type", channel_type,
				     "channel-handle", channel_handle,
				     "channel-handle-type", channel_handle_type,
				     "outgoing", outgoing,
				     "requestor-serial", requestor_serial,
				     "requestor-client-id", requestor_client_id,
				     "tp-channel", tp_chan,
				     NULL));
    return obj;
}

void
mcd_channel_set_status (McdChannel *channel, McdChannelStatus status)
{
    g_return_if_fail(MCD_IS_CHANNEL(channel));
    g_object_set (channel, "channel-status", status, NULL);
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
    return MCD_CHANNEL_PRIV (channel)->channel_type;
}

GQuark
mcd_channel_get_channel_type_quark (McdChannel *channel)
{
    return MCD_CHANNEL_PRIV (channel)->channel_type_quark;
}

const gchar *
mcd_channel_get_object_path (McdChannel *channel)
{
    return MCD_CHANNEL_PRIV (channel)->channel_object_path;
}

guint
mcd_channel_get_handle (McdChannel *channel)
{
    return MCD_CHANNEL_PRIV (channel)->channel_handle;
}

TpHandleType
mcd_channel_get_handle_type (McdChannel *channel)
{
    return MCD_CHANNEL_PRIV (channel)->channel_handle_type;
}

#if 0
/* Similar to the another helper, but uses the array version (InspectHandles), free with g_strfreev*/
static gchar **
_contact_handles_to_strings (TpConn * conn, guint handle_type,
			     const GArray * handles)
{
    gchar **contact_addresses = NULL;
    GError *error = NULL;

    tp_conn_inspect_handles (DBUS_G_PROXY (conn), handle_type, handles,
			     &contact_addresses, &error);

    if (error)
    {
	g_warning ("Error %s getting contacts for %u handles",
		   error->message, handles->len);
	g_error_free (error);
    }

    return contact_addresses;
}
#endif

GPtrArray*
mcd_channel_get_members (McdChannel *channel)
{
#if 1
    g_warning ("%s called, but shouldn't!", G_STRFUNC);
    return NULL;
#else
    GObject *connection;
    TpConn *tp_connection;
    GPtrArray *members = NULL; 
    gchar *contact_address;
   
    g_return_val_if_fail(MCD_IS_CHANNEL(channel), NULL);
    g_object_get (G_OBJECT (channel), "connection",
		  &connection, NULL);
    g_object_get (connection, "tp-connection",
		  &tp_connection, NULL);
    
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);
    
    g_assert (priv->tp_chan != NULL);

    g_debug ("Creating members list");
    if (priv->channel_handle_type == TP_CONN_HANDLE_TYPE_CONTACT)
    {
        /* TODO: Now that we have only the multi-handle
         * inspection call, we might be able to unify this
         * and the case below */

        gchar **addresses;
        GArray *handles =
            g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
        g_debug ("Single contact");

        g_array_insert_val (handles, 0,
                priv->channel_handle);

        addresses =
            _contact_handles_to_strings (tp_connection,
                    priv->channel_handle_type,
                    handles);
        g_array_free (handles, TRUE);
        if (!addresses || !addresses[0])
        {
            g_warning ("Unable to get contact address");
        }
        else
        {
            contact_address = g_strdup (addresses[0]);
            g_strfreev (addresses);
            /*Creating the members array and adding contact_address there */
            members = g_ptr_array_new ();
            g_ptr_array_add (members, contact_address);
        }
    }
    
    else			/*Group channel */
    {
        DBusGProxy *group_proxy;
        GArray *contact_handles = NULL;
        GError *error = NULL;

        g_debug ("Multiple contacts");

        /* get a group proxy from the channel */
        group_proxy = tp_chan_get_interface (priv->tp_chan,
                TELEPATHY_CHAN_IFACE_GROUP_QUARK);
	if (!group_proxy)
	    goto err_missing_interface;
        
        tp_chan_iface_group_get_members (DBUS_G_PROXY (group_proxy),
                &contact_handles, &error);
        
        if (error)
        {
            g_warning ("Unable to get group members: %s", error->message);
            g_error_free (error);
        }
        else if (!contact_handles || !contact_handles->len)
        {
            g_warning ("No contact handles");
        }
        else
        {			/*Get the real user names */
            gchar **contact_addresses;
            int i;

            members = g_ptr_array_new ();

            g_debug ("Transforming %i contacts into strings",
                    contact_handles->len);

            contact_addresses =
                _contact_handles_to_strings (tp_connection,
                        TP_CONN_HANDLE_TYPE_CONTACT,
                        contact_handles);

            if (contact_addresses)
            {
                for (i = 0; i < contact_handles->len; i++)
                {
                    g_ptr_array_add (members,
                            g_strdup (contact_addresses[i]));
                }
                g_strfreev (contact_addresses);
            }
            else
            {
                g_warning ("Unable to get contact address(multi)");
            }
        }

        if (contact_handles)
        {
            g_array_free (contact_handles, TRUE);
        }
err_missing_interface:
	;
    }
    
    g_object_unref (connection);
    g_object_unref (tp_connection);
    return members;
#endif
}

#if 0
static inline void
tp_chan_iface_group_remove_members_with_reason_no_reply (DBusGProxy *proxy, const GArray* IN_contacts, const char * IN_message, const guint IN_reason)

{
    dbus_g_proxy_call_no_reply (proxy, "RemoveMembersWithReason", dbus_g_type_get_collection ("GArray", G_TYPE_UINT), IN_contacts, G_TYPE_STRING, IN_message, G_TYPE_UINT, IN_reason, G_TYPE_INVALID);
}
#endif

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

