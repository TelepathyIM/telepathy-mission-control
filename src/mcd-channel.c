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
#include <telepathy-glib/util.h>

#include "mcd-channel.h"
#include "mcd-enum-types.h"
#include "_gen/gtypes.h"

#define MCD_CHANNEL_PRIV(channel) (MCD_CHANNEL (channel)->priv)

G_DEFINE_TYPE (McdChannel, mcd_channel, MCD_TYPE_MISSION);

typedef struct _McdChannelRequestData McdChannelRequestData;

struct _McdChannelPrivate
{
    gboolean outgoing;

    TpChannel *tp_chan;

    /* boolean properties */
    guint has_group_if : 1;
    guint close_on_dispose : 1;
    guint members_accepted : 1;
    guint missed : 1;
    guint is_disposed : 1;

    McdChannelStatus status;

    McdChannelRequestData *request_data;
};

struct _McdChannelRequestData
{
    GList *paths;

    GHashTable *properties;
    guint target_handle; /* used only if the Requests interface is absent */
    guint64 user_time;
    gchar *preferred_handler;

    gboolean use_existing;
};

enum _McdChannelSignalType
{
    STATUS_CHANGED,
    MEMBERS_ACCEPTED,
    LAST_SIGNAL
};

enum _McdChannelPropertyType
{
    PROP_TP_CHANNEL = 1,
    PROP_OUTGOING,
    PROP_SELF_HANDLE_READY,
    PROP_NAME_READY,
    PROP_INVITER_READY,
};

#define DEPRECATED_PROPERTY_WARNING \
    g_warning ("%s: property %s is deprecated", G_STRFUNC, pspec->name)

#define CD_ERROR    "_error"

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
    GList *list;

    g_debug ("%s called for %p", G_STRFUNC, crd);
    g_hash_table_unref (crd->properties);
    g_free (crd->preferred_handler);
    list = crd->paths;
    while (list)
    {
        g_free (list->data);
        list = g_list_delete_link (list, list);
    }
    g_slice_free (McdChannelRequestData, crd);
}

static void
on_members_changed (TpChannel *proxy, const gchar *message,
		    const GArray *added, const GArray *removed,
		    const GArray *l_pending, const GArray *r_pending,
                    guint actor, guint reason, McdChannel *channel)
{
    McdChannelPrivate *priv = channel->priv;
    TpHandle self_handle;
    guint i;

    self_handle = tp_channel_group_get_self_handle (proxy);

    if (added && added->len > 0)
    {
	g_debug ("%u added members", added->len);
	for (i = 0; i < added->len; i++)
	{
	    guint added_member = g_array_index (added, guint, i);

            /* see whether we are the added member */
            if (added_member == self_handle)
            {
                g_debug ("This should appear only when the call was accepted");
                priv->members_accepted = TRUE;
                g_signal_emit_by_name (channel, "members-accepted");
                break;
            }
	}
    }

    if (removed && removed->len > 0 && actor != self_handle)
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

static inline void
_mcd_channel_setup_group (McdChannel *channel)
{
    McdChannelPrivate *priv = channel->priv;

    g_signal_connect (priv->tp_chan, "group-members-changed",
                      G_CALLBACK (on_members_changed), channel);
    g_object_notify ((GObject *)channel, "self-handle-ready");
}

static void
on_channel_ready (TpChannel *tp_chan, const GError *error, gpointer user_data)
{
    McdChannel *channel, **channel_ptr = user_data;
    McdChannelPrivate *priv;

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
    g_object_notify ((GObject *)channel, "name-ready");

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
                                              G_CALLBACK (on_members_changed),
                                              channel);
	g_signal_handlers_disconnect_by_func (G_OBJECT (priv->tp_chan),
					      G_CALLBACK (proxy_destroyed),
					      channel);

	if (close_channel && !TP_PROXY (priv->tp_chan)->invalidated &&
            tp_channel_get_channel_type_id (priv->tp_chan) !=
            TP_IFACE_QUARK_CHANNEL_TYPE_CONTACT_LIST)
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
    case PROP_TP_CHANNEL:
	tp_chan = g_value_get_object (val);
	if (tp_chan)
	    g_object_ref (tp_chan);
	_mcd_channel_release_tp_channel (channel, TRUE);
	priv->tp_chan = tp_chan;
	if (priv->tp_chan)
	    _mcd_channel_setup (channel, priv);
	break;
    case PROP_OUTGOING:
	priv->outgoing = g_value_get_boolean (val);
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
    case PROP_TP_CHANNEL:
	g_value_set_object (val, priv->tp_chan);
	break;
    case PROP_OUTGOING:
	g_value_set_boolean (val, priv->outgoing);
	break;
    case PROP_SELF_HANDLE_READY:
    case PROP_NAME_READY:
        g_value_set_boolean (val, priv->tp_chan &&
                             tp_channel_is_ready (priv->tp_chan));
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_channel_dispose (GObject * object)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (object);
   
    g_debug ("\n\n%s for %p (is disposed = %d)", G_STRFUNC, object, priv->is_disposed);
    if (priv->is_disposed)
	return;

    priv->is_disposed = TRUE;

    if (priv->request_data)
    {
        channel_request_data_free (priv->request_data);
        priv->request_data = NULL;
    }

    _mcd_channel_release_tp_channel (MCD_CHANNEL (object),
                                     priv->close_on_dispose);
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
        priv->status == MCD_CHANNEL_STATUS_REQUESTED ||
        priv->status == MCD_CHANNEL_STATUS_DISPATCHING ||
        priv->status == MCD_CHANNEL_STATUS_HANDLER_INVOKED)
    {
        /* this code-path can only happen if the connection is aborted, as in
         * the other cases we handle the error in McdChannel; for this reason,
         * we use the DISCONNECTED error code */
        GError *error = g_error_new (TP_ERRORS, TP_ERROR_DISCONNECTED,
                                     "Channel aborted");
        _mcd_channel_set_error (channel, error);
    }
    _mcd_channel_release_tp_channel (channel, TRUE);

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
        (object_class, PROP_TP_CHANNEL,
         g_param_spec_object ("tp-channel",
                              "Telepathy Channel",
                              "Telepathy Channel",
                              TP_TYPE_CHANNEL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property
        (object_class, PROP_OUTGOING,
         g_param_spec_boolean ("outgoing",
                               "Outgoing channel",
                               "True if the channel was requested by us",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
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

    priv->close_on_dispose = TRUE;
}

McdChannel *
mcd_channel_new (TpChannel * tp_chan,
		 const gchar *type, guint handle,
		 TpHandleType handle_type, gboolean outgoing,
		 guint requestor_serial, const gchar *requestor_client_id)
{
    g_warning ("%s is deprecated", G_STRFUNC);
    return NULL;
}

/**
 * mcd_channel_new_from_properties:
 * @connection: the #TpConnection on which the channel exists.
 * @object_path: the D-Bus object path of an existing channel.
 * @properties: #GHashTable of immutable channel properties.
 *
 * Creates a #McdChannel with an associated #TpChannel proxy for the channel
 * located at @object_path.
 *
 * Returns: a new #McdChannel if the #TpChannel was created successfully, %NULL
 * otherwise.
 */
McdChannel *
mcd_channel_new_from_properties (TpConnection *connection,
                                 const gchar *object_path,
                                 const GHashTable *properties)
{
    McdChannel *channel;
    TpChannel *tp_chan;
    GError *error = NULL;


    tp_chan = tp_channel_new_from_properties (connection, object_path,
                                              properties, &error);
    if (G_UNLIKELY (error))
    {
        g_warning ("%s: got error: %s", G_STRFUNC, error->message);
        g_error_free (error);
        return NULL;
    }

    channel = g_object_new (MCD_TYPE_CHANNEL,
                            "tp-channel", tp_chan,
                            NULL);
    g_object_unref (tp_chan);
    return channel;
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
    GHashTable *props;
    GValue v_type = { 0 };
    GValue v_handle = { 0 };
    GValue v_handle_type = { 0 };
    McdChannel *channel;

    props = g_hash_table_new (g_str_hash, g_str_equal);

    g_value_init (&v_type, G_TYPE_STRING);
    g_value_set_static_string (&v_type, type);
    g_hash_table_insert (props, TP_IFACE_CHANNEL ".ChannelType", &v_type);

    g_value_init (&v_handle, G_TYPE_UINT);
    g_value_set_uint (&v_handle, handle);
    g_hash_table_insert (props, TP_IFACE_CHANNEL ".TargetHandle", &v_handle);

    g_value_init (&v_handle_type, G_TYPE_UINT);
    g_value_set_uint (&v_handle_type, handle_type);
    g_hash_table_insert (props, TP_IFACE_CHANNEL ".TargetHandleType",
                         &v_handle_type);

    channel = mcd_channel_new_from_properties (connection, object_path, props);

    g_hash_table_unref (props);
    return channel;
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
                                    mcd_channel_get_channel_type (channel),
                                    mcd_channel_get_handle_type (channel),
                                    mcd_channel_get_handle (channel),
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

/**
 * mcd_channel_create_proxy:
 * @channel: the #McdChannel.
 * @connection: the #TpConnection on which the channel exists.
 * @object_path: the D-Bus object path of an existing channel.
 * @properties: #GHashTable of immutable channel properties, or %NULL.
 *
 * This method makes @channel create a #TpChannel object for @object_path.
 * It must not be called if @channel has already a #TpChannel associated with
 * it.
 *
 * Returns: %TRUE if the #TpChannel has been created, %FALSE otherwise.
 */
gboolean
_mcd_channel_create_proxy (McdChannel *channel, TpConnection *connection,
                           const gchar *object_path,
                           const GHashTable *properties)
{
    TpChannel *tp_chan;
    GError *error = NULL;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), FALSE);
    tp_chan = tp_channel_new_from_properties (connection, object_path,
                                              properties, &error);
    if (G_UNLIKELY (error))
    {
        g_warning ("%s: got error: %s", G_STRFUNC, error->message);
        g_error_free (error);
        return FALSE;
    }

    g_object_set (channel,
                  "tp-channel", tp_chan,
                  NULL);
    g_object_unref (tp_chan);
    return TRUE;
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
    McdChannelPrivate *priv;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    priv = channel->priv;
    if (priv->tp_chan)
        return tp_channel_get_channel_type (priv->tp_chan);

    if (G_LIKELY (priv->request_data && priv->request_data->properties))
        return tp_asv_get_string (priv->request_data->properties,
                                  TP_IFACE_CHANNEL ".ChannelType");

    return NULL;
}

GQuark
mcd_channel_get_channel_type_quark (McdChannel *channel)
{
    McdChannelPrivate *priv;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), 0);
    priv = channel->priv;
    if (priv->tp_chan)
        return tp_channel_get_channel_type_id (priv->tp_chan);

    if (G_LIKELY (priv->request_data && priv->request_data->properties))
    {
        const gchar *type;
        type = tp_asv_get_string (priv->request_data->properties,
                                  TP_IFACE_CHANNEL ".ChannelType");
        return g_quark_from_string (type);
    }

    return 0;
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
    McdChannelPrivate *priv;

    g_return_if_fail (MCD_IS_CHANNEL (channel));
    priv = channel->priv;
    g_return_if_fail (priv->status != MCD_CHANNEL_STATUS_REQUEST);

    /* we cannot add the handle to the request_data->properties hash table,
     * because we don't know how the table elements should be allocated. So,
     * use a separate field in the request_data structure.
     * In any case, this function is called only if the new Requests interface
     * is not available on the connection, meaning that we won't directly use
     * the hash table anyway.
     */
    priv->request_data->target_handle = handle;
}

guint
mcd_channel_get_handle (McdChannel *channel)
{
    McdChannelPrivate *priv;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), 0);
    priv = channel->priv;
    if (priv->tp_chan)
        return tp_channel_get_handle (priv->tp_chan, NULL);

    if (G_LIKELY (priv->request_data))
    {
        if (priv->request_data->properties)
        {
            gboolean valid;
            guint handle;

            handle = tp_asv_get_uint32 (priv->request_data->properties,
                                        TP_IFACE_CHANNEL ".TargetHandle",
                                        &valid);
            if (valid)
                return handle;
        }
        return priv->request_data->target_handle;
    }
    else
        return 0;
}

TpHandleType
mcd_channel_get_handle_type (McdChannel *channel)
{
    McdChannelPrivate *priv;
    guint handle_type = TP_HANDLE_TYPE_NONE;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), 0);
    priv = channel->priv;
    if (priv->tp_chan)
        tp_channel_get_handle (priv->tp_chan, &handle_type);
    else if (G_LIKELY (priv->request_data && priv->request_data->properties))
        handle_type = tp_asv_get_uint32
            (priv->request_data->properties,
             TP_IFACE_CHANNEL ".TargetHandleType", NULL);

    return handle_type;
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
    McdChannelPrivate *priv;
    GHashTable *properties = NULL;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    priv = channel->priv;
    if (priv->tp_chan)
        properties = tp_channel_borrow_immutable_properties (priv->tp_chan);
    else if (G_LIKELY (priv->request_data))
        properties = priv->request_data->properties;

    if (!properties) return NULL;
    return tp_asv_get_string (properties, TP_IFACE_CHANNEL ".TargetID");
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
    McdChannelPrivate *priv;
    GHashTable *properties = NULL;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    priv = channel->priv;
    if (priv->tp_chan)
    {
        properties = tp_channel_borrow_immutable_properties (priv->tp_chan);
        if (properties)
            return tp_asv_get_string (properties, TP_IFACE_CHANNEL ".TargetID");
    }
    return NULL;
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
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), 0);
    if (channel->priv->tp_chan)
        return tp_channel_group_get_self_handle (channel->priv->tp_chan);
    return 0;
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
    g_return_if_fail (MCD_IS_CHANNEL (channel));

    if (G_LIKELY (channel->priv->tp_chan))
        g_object_set (channel->priv->tp_chan,
                      "channel-properties", properties,
                      NULL);
    else
        g_warning ("%s: no TpChannel yet!", G_STRFUNC);
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
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);

    return channel->priv->tp_chan ?
        tp_channel_borrow_immutable_properties (channel->priv->tp_chan) :
            NULL;
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
    crd = channel->priv->request_data;
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
                            "outgoing", TRUE,
                            NULL);

    /* TODO: these data could be freed when the channel status becomes
     * MCD_CHANNEL_STATUS_DISPATCHED */
    crd = g_slice_new (McdChannelRequestData);
    crd->paths = g_list_prepend (NULL, g_strdup_printf (REQUEST_OBJ_BASE "%u",
                                                        last_req_id++));
    crd->properties = g_hash_table_ref (properties);
    crd->user_time = user_time;
    crd->preferred_handler = g_strdup (preferred_handler);
    channel->priv->request_data = crd;

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
    crd = channel->priv->request_data;
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
    const GList *satisfied_requests;

    satisfied_requests = _mcd_channel_get_satisfied_requests (channel);
    return satisfied_requests ? satisfied_requests->data : NULL;
}

/*
 * _mcd_channel_get_satisfied_requests:
 * @channel: the #McdChannel.
 *
 * Returns: a list of the object paths of the channel requests satisfied by
 * this channel, if the channel status is not yet MCD_CHANNEL_STATUS_DISPATCHED
 * or MCD_CHANNEL_STATUS_FAILED.
 */
const GList *
_mcd_channel_get_satisfied_requests (McdChannel *channel)
{
    McdChannelRequestData *crd;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    crd = channel->priv->request_data;
    if (G_UNLIKELY (!crd)) return NULL;
    return crd->paths;
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
    crd = channel->priv->request_data;
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
    crd = channel->priv->request_data;
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
    crd = channel->priv->request_data;
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
    crd = channel->priv->request_data;
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
    const gchar *request_path;

    g_return_if_fail (MCD_IS_CHANNEL (channel));
    g_return_if_fail (MCD_IS_CHANNEL (source));

    /* Now @source is also satisfying the request of @channel */
    request_path = _mcd_channel_get_request_path (channel);
    if (G_LIKELY (request_path))
    {
        McdChannelRequestData *crd;
        crd = source->priv->request_data;
        if (G_LIKELY (crd))
            crd->paths = g_list_prepend (crd->paths, g_strdup (request_path));
    }

    copy_status (source, channel);
    g_signal_connect (source, "status-changed",
                      G_CALLBACK (on_proxied_channel_status_changed), channel);
}

/*
 * _mcd_channel_copy_details:
 * @channel: the #McdChannel.
 * @source: the #McdChannel from which details are to be copied.
 *
 * Copy the #TpProxy and all associated properties from channel @source into
 * @channel.
 */
void
_mcd_channel_copy_details (McdChannel *channel, McdChannel *source)
{
    g_return_if_fail (MCD_IS_CHANNEL (channel));
    g_return_if_fail (MCD_IS_CHANNEL (source));

    channel->priv->tp_chan = g_object_ref (source->priv->tp_chan);
    channel->priv->close_on_dispose = FALSE;
}

