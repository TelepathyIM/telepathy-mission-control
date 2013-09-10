/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright © 2007-2009 Nokia Corporation.
 * Copyright © 2009-2010 Collabora Ltd.
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

#include "config.h"

#include "mcd-channel.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "channel-utils.h"
#include "mcd-account-priv.h"
#include "mcd-channel-priv.h"
#include "mcd-enum-types.h"
#include "request.h"

#include "_gen/interfaces.h"

#define MCD_CHANNEL_PRIV(channel) (MCD_CHANNEL (channel)->priv)

G_DEFINE_TYPE (McdChannel, mcd_channel, MCD_TYPE_MISSION)

typedef struct _McdChannelRequestData McdChannelRequestData;

struct _McdChannelPrivate
{
    TpChannel *tp_chan;
    GError *error;

    /* boolean properties */
    guint outgoing : 1;
    guint is_disposed : 1;
    guint is_aborted : 1;
    guint constructing : 1;
    guint is_proxy : 1;

    McdChannelStatus status;

    McdRequest *request;

    /* List of reffed McdRequest */
    GList *satisfied_requests;
    gint64 latest_request_time;
};

enum _McdChannelSignalType
{
    STATUS_CHANGED,
    LAST_SIGNAL
};

enum _McdChannelPropertyType
{
    PROP_TP_CHANNEL = 1,
    PROP_OUTGOING,
    PROP_ACCOUNT_PATH,
    PROP_REQUESTS,
    PROP_USER_ACTION_TIME,
    PROP_PREFERRED_HANDLER,
    PROP_INTERFACES,
    PROP_HINTS,
};

static guint mcd_channel_signals[LAST_SIGNAL] = { 0 };


static void _mcd_channel_release_tp_channel (McdChannel *channel);
static void on_proxied_channel_status_changed (McdChannel *source,
                                               McdChannelStatus status,
                                               McdChannel *dest);

static void
proxy_destroyed (TpProxy *self, guint domain, gint code, gchar *message,
		 gpointer user_data)
{
    McdChannel *channel = user_data;

    DEBUG ("Channel proxy invalidated: %s %d: %s",
           g_quark_to_string (domain), code, message);
    mcd_mission_abort (MCD_MISSION (channel));
}

static void
on_channel_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    TpChannel *tp_chan = TP_CHANNEL (source_object);
    McdChannel *channel, **channel_ptr = user_data;
    GError *error = NULL;

    if (!tp_proxy_prepare_finish (tp_chan, result, &error))
    {
        DEBUG ("failed to prepare channel: %s", error->message);
        g_clear_error (&error);
        return;
    }

    channel = *channel_ptr;
    if (channel)
	g_object_remove_weak_pointer ((GObject *)channel,
				      (gpointer)channel_ptr);
    g_slice_free (McdChannel *, channel_ptr);
    if (error)
    {
        DEBUG ("got error: %s", error->message);
	return;
    }

    if (!channel) return;

    DEBUG ("channel %p is ready", channel);
    channel->priv->outgoing = tp_channel_get_requested (tp_chan);
}

void
_mcd_channel_close (McdChannel *channel)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);

    if (!_mcd_tp_channel_should_close (priv->tp_chan, "closing"))
    {
        return;
    }

    DEBUG ("%p: calling Close() on %s", channel,
           mcd_channel_get_object_path (channel));
    tp_cli_channel_call_close (priv->tp_chan, -1, NULL, NULL, NULL, NULL);
}

void
_mcd_channel_undispatchable (McdChannel *channel)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);

    if (!_mcd_tp_channel_should_close (priv->tp_chan, "destroying"))
    {
        return;
    }

    DEBUG ("%p: %s", channel, mcd_channel_get_object_path (channel));

    /* Call Destroy() if possible, or Close() */
    if (tp_proxy_has_interface_by_id (priv->tp_chan,
        TP_IFACE_QUARK_CHANNEL_INTERFACE_DESTROYABLE))
    {
        DEBUG ("calling Destroy()");
        tp_cli_channel_interface_destroyable_call_destroy (priv->tp_chan,
                                                           -1, NULL,
                                                           NULL, NULL,
                                                           NULL);
    }
    else
    {
        DEBUG ("calling Close()");
        tp_cli_channel_call_close (priv->tp_chan, -1, NULL, NULL, NULL,
                                   NULL);
    }
}

static void
_mcd_channel_release_tp_channel (McdChannel *channel)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);
    if (priv->tp_chan)
    {
	g_signal_handlers_disconnect_by_func (G_OBJECT (priv->tp_chan),
					      G_CALLBACK (proxy_destroyed),
					      channel);

        /* Destroy our proxy */
        tp_clear_object (&priv->tp_chan);
    }
}

static void
_mcd_channel_setup (McdChannel *channel, McdChannelPrivate *priv)
{
    McdChannel **channel_ptr;

    channel_ptr = g_slice_alloc (sizeof (McdChannel *));
    *channel_ptr = channel;
    g_object_add_weak_pointer ((GObject *)channel, (gpointer)channel_ptr);
    tp_proxy_prepare_async (priv->tp_chan, NULL, on_channel_ready, channel_ptr);

    g_signal_connect (priv->tp_chan, "invalidated",
		      G_CALLBACK (proxy_destroyed), channel);

    priv->outgoing = tp_channel_get_requested (priv->tp_chan);
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
	_mcd_channel_release_tp_channel (channel);
	priv->tp_chan = tp_chan;
        if (priv->tp_chan && !priv->constructing)
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

    case PROP_ACCOUNT_PATH:
        if (priv->request != NULL)
        {
            g_object_get_property ((GObject *) priv->request,
                                   "account-path", val);
            break;
        }
        g_value_set_static_boxed (val, "/");
        break;

    case PROP_USER_ACTION_TIME:
        if (priv->request != NULL)
        {
            g_object_get_property ((GObject *) priv->request,
                                   "user-action-time", val);
            break;
        }
        g_value_set_int64 (val, TP_USER_ACTION_TIME_NOT_USER_ACTION);
        break;

    case PROP_PREFERRED_HANDLER:
        if (priv->request != NULL)
        {
            g_object_get_property ((GObject *) priv->request,
                                   "preferred-handler", val);
            break;
        }
        g_value_set_static_string (val, "");
        break;

    case PROP_REQUESTS:
        if (priv->request != NULL)
        {
            g_object_get_property ((GObject *) priv->request,
                                   "requests", val);
            break;
        }
        g_value_take_boxed (val, g_ptr_array_sized_new (0));
        break;

    case PROP_INTERFACES:
        if (priv->request != NULL)
        {
            g_object_get_property ((GObject *) priv->request,
                                   "interfaces", val);
            break;
        }
        g_value_take_boxed (val, NULL);
        break;

    case PROP_HINTS:
        if (priv->request != NULL)
        {
            g_object_get_property ((GObject *) priv->request,
                                   "hints", val);
            break;
        }
        g_value_take_boxed (val, g_hash_table_new (NULL, NULL));
        break;

    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_channel_constructed (GObject * object)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (object);

    if (G_OBJECT_CLASS (mcd_channel_parent_class)->constructed)
        G_OBJECT_CLASS (mcd_channel_parent_class)->constructed (object);

    priv->constructing = FALSE;

    if (priv->tp_chan)
        _mcd_channel_setup (MCD_CHANNEL (object), priv);
}

static void
_mcd_channel_dispose (GObject * object)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (object);
   
    DEBUG ("%p (is disposed = %d)", object, priv->is_disposed);
    if (priv->is_disposed)
	return;

    priv->is_disposed = TRUE;

    tp_clear_object (&priv->request);

    _mcd_channel_release_tp_channel (MCD_CHANNEL (object));
    G_OBJECT_CLASS (mcd_channel_parent_class)->dispose (object);
}

static void
_mcd_channel_finalize (GObject * object)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (object);
    GList *list;

    list = priv->satisfied_requests;

    while (list)
    {
        g_object_unref (list->data);
        list = g_list_delete_link (list, list);
    }

    if (priv->error != NULL)
    {
        g_error_free (priv->error);
        priv->error = NULL;
    }

    G_OBJECT_CLASS (mcd_channel_parent_class)->finalize (object);
}

static void
mcd_channel_abort (McdMission *mission)
{
    McdChannel *channel = MCD_CHANNEL (mission);
    McdChannelPrivate *priv = channel->priv;

    DEBUG ("%p", mission);
    if (priv->is_aborted)
    {
        DEBUG ("Already aborted");
        return;
    }
    priv->is_aborted = TRUE;
    /* If this is still a channel request, signal the failure */
    if (priv->status == MCD_CHANNEL_STATUS_REQUEST ||
        priv->status == MCD_CHANNEL_STATUS_REQUESTED ||
        priv->status == MCD_CHANNEL_STATUS_DISPATCHING ||
        priv->status == MCD_CHANNEL_STATUS_HANDLER_INVOKED)
    {
        /* this code-path can only happen if the connection is aborted, as in
         * the other cases we handle the error in McdChannel; for this reason,
         * we use the DISCONNECTED error code */
        GError *error = g_error_new (TP_ERROR, TP_ERROR_DISCONNECTED,
                                     "Channel aborted");
        mcd_channel_take_error (channel, error);
    }

    _mcd_channel_set_status (channel, MCD_CHANNEL_STATUS_ABORTED);

    /* chain up with the parent */
    MCD_MISSION_CLASS (mcd_channel_parent_class)->abort (mission);
}

static void
mcd_channel_status_changed (McdChannel *channel, McdChannelStatus status)
{
    channel->priv->status = status;

    switch (status)
    {
        case MCD_CHANNEL_STATUS_UNDISPATCHED:
        case MCD_CHANNEL_STATUS_DISPATCHING:
        case MCD_CHANNEL_STATUS_HANDLER_INVOKED:
        case MCD_CHANNEL_STATUS_DISPATCHED:
            g_assert (channel->priv->tp_chan != NULL);
            break;

        case MCD_CHANNEL_STATUS_REQUEST:
        case MCD_CHANNEL_STATUS_REQUESTED:
            g_assert (channel->priv->tp_chan == NULL);
            break;

        case MCD_CHANNEL_STATUS_FAILED:
        case MCD_CHANNEL_STATUS_ABORTED:
            { /* no particular assertion */ }

        /* no default case, so the compiler will warn on unhandled states */
    }

    if (channel->priv->request != NULL &&
        !_mcd_request_is_complete (channel->priv->request))
    {
        if (status == MCD_CHANNEL_STATUS_FAILED)
        {
            const GError *error = mcd_channel_get_error (channel);

            if (G_LIKELY (error != NULL))
            {
                _mcd_request_set_failure (channel->priv->request,
                                          error->domain, error->code,
                                          error->message);
            }
            else
            {
                g_critical ("Requested channel's status changed to FAILED "
                            "without a proper error");
                _mcd_request_set_failure (channel->priv->request,
                                          TP_ERROR, TP_ERROR_NOT_AVAILABLE,
                                          "MC bug! FAILED but no error");
            }
        }
        else if (status == MCD_CHANNEL_STATUS_DISPATCHED)
        {
            _mcd_request_set_success (channel->priv->request,
                                      channel->priv->tp_chan);
        }
        else if (status == MCD_CHANNEL_STATUS_HANDLER_INVOKED)
        {
            _mcd_request_set_uncancellable (channel->priv->request);
        }
    }
}

static void
mcd_channel_class_init (McdChannelClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    McdMissionClass *mission_class = MCD_MISSION_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdChannelPrivate));

    object_class->constructed = _mcd_channel_constructed;
    object_class->dispose = _mcd_channel_dispose;
    object_class->finalize = _mcd_channel_finalize;
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
        (object_class, PROP_ACCOUNT_PATH,
         g_param_spec_boxed ("account-path",
                             "Account",
                             "Object path of the Account",
                             DBUS_TYPE_G_OBJECT_PATH,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (object_class, PROP_PREFERRED_HANDLER,
         g_param_spec_string ("preferred-handler",
                             "PreferredHandler",
                             "Well-known bus name of the preferred Handler",
                             NULL,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (object_class, PROP_USER_ACTION_TIME,
         g_param_spec_int64 ("user-action-time",
                             "UserActionTime",
                             "Time of user action",
                             G_MININT64, G_MAXINT64,
                             TP_USER_ACTION_TIME_NOT_USER_ACTION,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (object_class, PROP_REQUESTS,
         g_param_spec_boxed ("requests",
                             "Requests",
                             "A dbus-glib aa{sv}",
                             TP_ARRAY_TYPE_QUALIFIED_PROPERTY_VALUE_MAP_LIST,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (object_class, PROP_INTERFACES,
         g_param_spec_boxed ("interfaces",
                             "Interfaces",
                             "A dbus-glib 'as'",
                             G_TYPE_STRV,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (object_class, PROP_HINTS,
         g_param_spec_boxed ("hints",
                             "Hints",
                             "GHashTable",
                             TP_HASH_TYPE_STRING_VARIANT_MAP,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
mcd_channel_init (McdChannel * obj)
{
    McdChannelPrivate *priv;
   
    priv = G_TYPE_INSTANCE_GET_PRIVATE (obj, MCD_TYPE_CHANNEL,
					McdChannelPrivate);
    obj->priv = priv;

    priv->status = MCD_CHANNEL_STATUS_UNDISPATCHED;
    priv->constructing = TRUE;
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

    tp_chan = tp_simple_client_factory_ensure_channel (
        tp_proxy_get_factory (connection), connection, object_path,
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
    GValue v_type = G_VALUE_INIT;
    GValue v_handle = G_VALUE_INIT;
    GValue v_handle_type = G_VALUE_INIT;
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

    tp_chan = tp_simple_client_factory_ensure_channel (
        tp_proxy_get_factory (connection), connection, object_path,
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
_mcd_channel_set_status (McdChannel *channel, McdChannelStatus status)
{
    DEBUG ("%p, %u", channel, status);
    g_return_if_fail(MCD_IS_CHANNEL(channel));

    if (status != channel->priv->status)
    {
        if (status != MCD_CHANNEL_STATUS_ABORTED)
            g_return_if_fail (channel->priv->status !=
                              MCD_CHANNEL_STATUS_FAILED);
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

const gchar *
mcd_channel_get_object_path (McdChannel *channel)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);

    return priv->tp_chan ? tp_proxy_get_object_path (priv->tp_chan) : NULL;
}

/*
 * mcd_channel_dup_immutable_properties:
 * @channel: the #McdChannel.
 *
 * Returns: the %G_VARIANT_TYPE_VARDICT of the immutable properties.
 */
GVariant *
mcd_channel_dup_immutable_properties (McdChannel *channel)
{
    GVariant *ret;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);

    if (channel->priv->tp_chan == NULL)
    {
        DEBUG ("Channel %p has no associated TpChannel", channel);
        return NULL;
    }

    ret = tp_channel_dup_immutable_properties (channel->priv->tp_chan);

    if (ret == NULL)
    {
        DEBUG ("Channel %p TpChannel %s (%p) has no immutable properties yet",
               channel, tp_proxy_get_object_path (channel->priv->tp_chan),
               channel->priv->tp_chan);
        return NULL;
    }

    return ret;
}

/**
 * mcd_channel_take_error:
 * @channel: the #McdChannel.
 * @error: a #GError.
 *
 * Sets @error on channel, and takes ownership of it (the error will eventually
 * be freed with g_error_free()). As a side effect, if @error is not %NULL this
 * method causes the channel status be set to %MCD_CHANNEL_STATUS_FAILED.
 */
void
mcd_channel_take_error (McdChannel *channel, GError *error)
{
    g_return_if_fail (MCD_IS_CHANNEL (channel));

    if (channel->priv->error != NULL)
        g_error_free (channel->priv->error);

    channel->priv->error = error;

    if (error)
        _mcd_channel_set_status (channel, MCD_CHANNEL_STATUS_FAILED);
}

/**
 * mcd_channel_get_error:
 * @channel: the #McdChannel.
 *
 * Returns: the #GError, or %NULL if no error is set.
 */
const GError *
mcd_channel_get_error (McdChannel *channel)
{
    McdChannelPrivate *priv;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);

    priv = channel->priv;
    if (priv->error)
        return priv->error;

    if (priv->tp_chan)
        return tp_proxy_get_invalidated (priv->tp_chan);

    return NULL;
}

static void
_mcd_channel_request_cancelling_cb (McdRequest *request,
                                    McdChannel *self)
{
    McdChannelStatus status = mcd_channel_get_status (self);

    g_object_ref (self);
    DEBUG ("%p in status %u", self, status);

    mcd_channel_take_error (self, g_error_new (TP_ERROR,
                                               TP_ERROR_CANCELLED,
                                               "Cancelled"));

    /* If we're coming from state REQUEST, the call to the CM hasn't
     * happened yet; now that we're in state FAILED, it never will,
     * because mcd_connection_request_channel() now checks that.
     *
     * If we're coming from state REQUESTED, we need to close the channel
     * when the CM tells us where it is, so we can't now.
     *
     * If we're coming from state DISPATCHING, we need to shoot it down
     * now.
     *
     * Anything else is too late.
     */
    if (status == MCD_CHANNEL_STATUS_DISPATCHING)
    {
        _mcd_channel_close (self);
        mcd_mission_abort ((McdMission *) self);
    }

    g_object_unref (self);
}

/*
 * _mcd_channel_new_request:
 * @clients: the client registry
 * @account: an account.
 * @properties: a #GHashTable of desired channel properties.
 * @user_time: user action time.
 * @preferred_handler: well-known name of preferred handler.
 * @hints: client hints from the request, or %NULL
 * @use_existing: use EnsureChannel if %TRUE or CreateChannel if %FALSE
 *
 * Create a #McdChannel object holding the given properties. The object can
 * then be used to intiate a channel request, by passing it to
 * mcd_connection_request_channel() on a connection in connected state.
 *
 * Returns: a newly created #McdChannel.
 */
McdChannel *
_mcd_channel_new_request (McdRequest *request)
{
    McdChannel *channel;

    channel = g_object_new (MCD_TYPE_CHANNEL,
                            "outgoing", TRUE,
                            NULL);

    /* TODO: this could be freed when the channel status becomes
     * MCD_CHANNEL_STATUS_DISPATCHED or MCD_CHANNEL_STATUS_FAILED? */
    channel->priv->request = request;

    channel->priv->satisfied_requests = g_list_prepend (NULL,
        g_object_ref (channel->priv->request));
    channel->priv->latest_request_time =
        _mcd_request_get_user_action_time (request);

    _mcd_channel_set_status (channel, MCD_CHANNEL_STATUS_REQUEST);

    /* for the moment McdChannel implements the later stages of cancelling */
    tp_g_signal_connect_object (request, "cancelling",
        G_CALLBACK (_mcd_channel_request_cancelling_cb), channel, 0);

    return channel;
}

McdRequest *
_mcd_channel_get_request (McdChannel *self)
{
    g_return_val_if_fail (MCD_IS_CHANNEL (self), NULL);
    return self->priv->request;   /* may be NULL */
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
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);

    if (channel->priv->request == NULL)
        return NULL;

    return _mcd_request_get_properties (channel->priv->request);
}

/*
 * _mcd_channel_get_satisfied_requests:
 * @channel: the #McdChannel.
 * @get_latest_time: if not %NULL, the most recent request time will be copied
 *  through this pointer
 *
 * Returns: a newly allocated hash table mapping channel object paths
 * to McdRequest objects satisfied by this channel
 */
GHashTable *
_mcd_channel_get_satisfied_requests (McdChannel *channel,
                                     gint64 *get_latest_time)
{
    GList *l;
    GHashTable *result;
    const gchar *path;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);

    if (get_latest_time != NULL)
        *get_latest_time = channel->priv->latest_request_time;

    result = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, g_object_unref);

    for (l = channel->priv->satisfied_requests; l != NULL; l = g_list_next (l))
    {
        path = _mcd_request_get_object_path (l->data);
        g_assert (path != NULL);
        g_hash_table_insert (result, g_strdup (path), g_object_ref (l->data));
    }

    return result;
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
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);

    if (G_UNLIKELY (channel->priv->request == NULL))
        return NULL;

    return _mcd_request_get_preferred_handler (channel->priv->request);
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
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), FALSE);

    if (G_UNLIKELY (channel->priv->request == NULL))
        return FALSE;

    return _mcd_request_get_use_existing (channel->priv->request);
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
        DEBUG ("source is %d, dest is %d", src_priv->status, dst_priv->status);
        if (src_priv->status == MCD_CHANNEL_STATUS_FAILED)
        {
            const GError *error;

            error = mcd_channel_get_error (source);
            /* this also takes care of setting the status */
            mcd_channel_take_error (dest, g_error_copy (error));
        }
        else
            _mcd_channel_set_status (dest, src_priv->status);
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
 * @channel: a #McdChannel representing a channel request
 * @source: the primary #McdChannel that wraps a remote Channel, which
 *  may or may not also be a channel request
 *
 * This function turns @channel into a proxy for @source: it listens to
 * "status-changed" signals from @source and replicates them on @channel.
 * See _mcd_channel_is_primary_for_path for terminology and rationale.
 */
void
_mcd_channel_set_request_proxy (McdChannel *channel, McdChannel *source)
{
    g_return_if_fail (MCD_IS_CHANNEL (channel));
    g_return_if_fail (MCD_IS_CHANNEL (source));
    g_return_if_fail (MCD_IS_REQUEST (channel->priv->request));

    g_return_if_fail (!source->priv->is_proxy);
    g_return_if_fail (source->priv->tp_chan != NULL);

    _mcd_channel_copy_details (channel, source);

    /* Now @source is also satisfying the request of @channel */
    source->priv->latest_request_time = MAX (source->priv->latest_request_time,
        channel->priv->latest_request_time);

    source->priv->satisfied_requests = g_list_prepend (
        source->priv->satisfied_requests,
        g_object_ref (channel->priv->request));

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

    channel->priv->is_proxy = TRUE;
    channel->priv->tp_chan = g_object_ref (source->priv->tp_chan);
}

TpChannel *
mcd_channel_get_tp_channel (McdChannel *channel)
{
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);

    return channel->priv->tp_chan;
}

static void
mcd_channel_depart_cb (GObject *source_object,
                       GAsyncResult *result,
                       gpointer data G_GNUC_UNUSED)
{
    GError *error = NULL;

    /* By this point, TpChannel has already called Close() for us;
     * we only get an error if that failed. If Close() fails, there's
     * not a whole lot we can do about it. */

    if (!tp_channel_leave_finish (TP_CHANNEL (source_object), result, &error))
    {
        WARNING ("failed to depart, even via Close(): %s %d: %s",
               g_quark_to_string (error->domain), error->code, error->message);
        g_error_free (error);
    }
}

typedef struct {
    TpChannelGroupChangeReason reason;
    gchar *message;
} DepartData;

static void
mcd_channel_ready_to_depart_cb (GObject *source_object,
                                GAsyncResult *result,
                                gpointer data)
{
    TpChannel *channel = TP_CHANNEL (source_object);
    DepartData *d = data;
    GError *error = NULL;

    if (!tp_proxy_prepare_finish (channel, result, &error))
    {
        DEBUG ("%s %d: %s", g_quark_to_string (error->domain), error->code,
               error->message);
        g_free (d->message);
        g_slice_free (DepartData, d);
        g_clear_error (&error);
        return;
    }

    /* If it's a Group, this will leave gracefully.
     * If not, it will just close it. Either's good. */
    tp_channel_leave_async (channel, d->reason, d->message,
                            mcd_channel_depart_cb, NULL);
}

void
_mcd_channel_depart (McdChannel *channel,
                     TpChannelGroupChangeReason reason,
                     const gchar *message)
{
    DepartData *d;
    const GError *invalidated;
    GQuark just_group_feature[] = { TP_CHANNEL_FEATURE_GROUP, 0 };

    g_return_if_fail (MCD_IS_CHANNEL (channel));

    g_return_if_fail (channel->priv->tp_chan != NULL);
    g_return_if_fail (message != NULL);

    invalidated = tp_proxy_get_invalidated (channel->priv->tp_chan);

    if (invalidated != NULL)
    {
        DEBUG ("%s %d: %s", g_quark_to_string (invalidated->domain),
               invalidated->code, invalidated->message);
        return;
    }

    if (message[0] == '\0' && reason == TP_CHANNEL_GROUP_CHANGE_REASON_NONE)
    {
        /* exactly equivalent to Close(), so skip the Group interface */
        tp_channel_close_async (channel->priv->tp_chan, NULL, NULL);
        return;
    }

    d = g_slice_new (DepartData);
    d->reason = reason;
    d->message = g_strdup (message);

    /* tp_channel_leave_async documents calling it without first preparing
     * GROUP as deprecated. */
    tp_proxy_prepare_async (channel->priv->tp_chan, just_group_feature,
                            mcd_channel_ready_to_depart_cb, d);
}

/*
 * _mcd_channel_is_primary_for_path:
 * @self: an McdChannel
 * @channel_path: the object path of a TpChannel
 *
 * McdChannel leads a double life. A McdChannel can either be created to
 * represent a channel request (implementing ChannelRequest), or to wrap a
 * TpChannel (a remote Channel).
 *
 * When a new TpChannel satisfies a single channel request, the McdChannel
 * representing that channel request becomes the object that wraps that
 * TpChannel.
 *
 * When the same TpChannel satisfies more than one channel request, one of
 * the McdChannel instances representing the requests is chosen to wrap
 * that TpChannel in the same way. Let that instance be the "primary"
 * McdChannel. The remaining McdChannel instances become "proxies" for that
 * primary McdChannel.
 *
 * Only the primary McdChannel is suitable for passing to the McdDispatcher,
 * since it is the only one that tracks the complete list of requests being
 * satisfied by the TpChannel.
 *
 * Returns: %TRUE if @self is the primary McdChannel for @channel_path
 */
gboolean
_mcd_channel_is_primary_for_path (McdChannel *self,
                                  const gchar *channel_path)
{
    if (self->priv->tp_chan == NULL)
        return FALSE;

    if (self->priv->is_proxy)
        return FALSE;

    if (tp_strdiff (tp_proxy_get_object_path (self->priv->tp_chan),
                    channel_path))
        return FALSE;

    return TRUE;
}
