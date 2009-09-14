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
 * SECTION:mcd-channel
 * @title: McdChannel
 * @short_description: Channel class representing Telepathy channel class
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-channel.h
 * 
 * FIXME
 */

#include "mcd-channel.h"

#include <glib/gi18n.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel-request.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>

#include "mcd-account-priv.h"
#include "mcd-channel-priv.h"
#include "mcd-enum-types.h"

#define MCD_CHANNEL_PRIV(channel) (MCD_CHANNEL (channel)->priv)

static void request_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (McdChannel, mcd_channel, MCD_TYPE_MISSION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_REQUEST, request_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
                           tp_dbus_properties_mixin_iface_init))

typedef struct _McdChannelRequestData McdChannelRequestData;

struct _McdChannelPrivate
{
    TpChannel *tp_chan;
    GError *error;

    /* boolean properties */
    guint outgoing : 1;
    guint has_group_if : 1;
    guint members_accepted : 1;
    guint missed : 1;
    guint is_disposed : 1;
    guint is_aborted : 1;
    guint constructing : 1;

    McdChannelStatus status;

    McdChannelRequestData *request_data;
    GList *satisfied_requests;
};

struct _McdChannelRequestData
{
    gchar *path;

    GHashTable *properties;
    gint64 user_time;
    gchar *preferred_handler;
    McdAccount *account;  /* weak ref */

    gboolean proceeding;
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
    PROP_ACCOUNT_PATH,
    PROP_REQUESTS,
    PROP_USER_ACTION_TIME,
    PROP_PREFERRED_HANDLER,
    PROP_INTERFACES,
};

#define DEPRECATED_PROPERTY_WARNING \
    g_warning ("%s: property %s is deprecated", G_STRFUNC, pspec->name)

static guint mcd_channel_signals[LAST_SIGNAL] = { 0 };

#define REQUEST_OBJ_BASE "/com/nokia/MissionControl/requests/r"
static guint last_req_id = 1;


static void _mcd_channel_release_tp_channel (McdChannel *channel);
static void on_proxied_channel_status_changed (McdChannel *source,
                                               McdChannelStatus status,
                                               McdChannel *dest);

static void
channel_request_data_free (McdChannelRequestData *crd)
{
    DEBUG ("called for %p", crd);
    g_hash_table_unref (crd->properties);
    g_free (crd->preferred_handler);
    g_free (crd->path);
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
    TpHandle conn_self_handle = 0;
    TpHandle removed_handle = 0;
    guint i;

    self_handle = tp_channel_group_get_self_handle (proxy);
    conn_self_handle =
      tp_connection_get_self_handle (tp_channel_borrow_connection (proxy));

    DEBUG ("called (actor %u, reason %u, self_handle %u, conn_self_handle %u)",
           actor, reason, tp_channel_group_get_self_handle (proxy),
           conn_self_handle);

    if (added && added->len > 0)
    {
        DEBUG ("%u added members", added->len);
	for (i = 0; i < added->len; i++)
	{
	    guint added_member = g_array_index (added, guint, i);
            DEBUG ("added member %u", added_member);

            /* see whether we are the added member */
            if (added_member == self_handle)
            {
                DEBUG ("This should appear only when the call was accepted");
                priv->members_accepted = TRUE;
                g_signal_emit_by_name (channel, "members-accepted");
                break;
            }
	}
    }

    if (removed && removed->len > 0 &&
        (actor == 0 || (actor != self_handle && actor != conn_self_handle)))
    {
        for (i = 0; i < removed->len; i++)
        {
            removed_handle = g_array_index (removed, guint, i);
            DEBUG ("removed member %u", removed_handle);
            if (removed_handle == self_handle ||
                removed_handle == conn_self_handle)
            {
                /* We are removed (end of call), marking as missed, if not
                 * already accespted the call */
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

    DEBUG ("Channel proxy invalidated: %s %d: %s",
           g_quark_to_string (domain), code, message);
    mcd_mission_abort (MCD_MISSION (channel));
}

static inline void
_mcd_channel_setup_group (McdChannel *channel)
{
    McdChannelPrivate *priv = channel->priv;

    g_signal_connect (priv->tp_chan, "group-members-changed",
                      G_CALLBACK (on_members_changed), channel);
}

static void
on_channel_ready (TpChannel *tp_chan, const GError *error, gpointer user_data)
{
    McdChannel *channel, **channel_ptr = user_data;
    McdChannelPrivate *priv;
    gboolean requested, valid;

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
    priv = channel->priv;
    requested = tp_asv_get_boolean
        (tp_channel_borrow_immutable_properties (tp_chan),
         TP_IFACE_CHANNEL ".Requested", &valid);
    if (valid)
        priv->outgoing = requested;

    priv->has_group_if = tp_proxy_has_interface_by_id (priv->tp_chan,
						       TP_IFACE_QUARK_CHANNEL_INTERFACE_GROUP);
    if (priv->has_group_if)
	_mcd_channel_setup_group (channel);
}

static gboolean
mcd_channel_should_close (McdChannel *channel,
                          const gchar *verb)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);
    const GError *invalidated;
    GQuark channel_type;

    if (priv->tp_chan == NULL)
    {
        DEBUG ("Not %s %p: no TpChannel", verb, channel);
        return FALSE;
    }

    invalidated = TP_PROXY (priv->tp_chan)->invalidated;

    if (invalidated != NULL)
    {
        DEBUG ("Not %s %p, already invalidated: %s %d: %s",
               verb, channel, g_quark_to_string (invalidated->domain),
               invalidated->code, invalidated->message);
        return FALSE;
    }

    channel_type = tp_channel_get_channel_type_id (priv->tp_chan);

    if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_CONTACT_LIST)
    {
        DEBUG ("Not %s %p, it's a ContactList", verb, channel);
        return FALSE;
    }

    if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_TUBES)
    {
        DEBUG ("Not %s %p, it's an old Tubes channel", verb, channel);
        return FALSE;
    }

    return TRUE;
}

void
_mcd_channel_close (McdChannel *channel)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);

    if (!mcd_channel_should_close (channel, "closing"))
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

    if (!mcd_channel_should_close (channel, "destroying"))
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
                                              G_CALLBACK (on_members_changed),
                                              channel);
	g_signal_handlers_disconnect_by_func (G_OBJECT (priv->tp_chan),
					      G_CALLBACK (proxy_destroyed),
					      channel);

	/* Destroy our proxy */
	g_object_unref (priv->tp_chan);
	
	priv->tp_chan = NULL;
    }
}

static void
_mcd_channel_setup (McdChannel *channel, McdChannelPrivate *priv)
{
    McdChannel **channel_ptr;
    GHashTable *properties;

    channel_ptr = g_slice_alloc (sizeof (McdChannel *));
    *channel_ptr = channel;
    g_object_add_weak_pointer ((GObject *)channel, (gpointer)channel_ptr);
    tp_channel_call_when_ready (priv->tp_chan, on_channel_ready, channel_ptr);

    g_signal_connect (priv->tp_chan, "invalidated",
		      G_CALLBACK (proxy_destroyed), channel);

    properties = tp_channel_borrow_immutable_properties (priv->tp_chan);
    if (properties)
    {
        gboolean requested, valid = FALSE;
        requested = tp_asv_get_boolean
            (properties, TP_IFACE_CHANNEL ".Requested", &valid);
        if (valid)
            priv->outgoing = requested;
    }
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
        if (priv->request_data != NULL &&
            priv->request_data->account != NULL)
        {
            g_value_set_boxed (val,
                mcd_account_get_object_path (priv->request_data->account));
            break;
        }
        g_value_set_static_boxed (val, "/");
        break;

    case PROP_USER_ACTION_TIME:
        if (priv->request_data != NULL)
        {
            g_value_set_int64 (val, priv->request_data->user_time);
            break;
        }
        g_value_set_int64 (val, 0);
        break;

    case PROP_PREFERRED_HANDLER:
        if (priv->request_data != NULL)
        {
            g_value_set_string (val, priv->request_data->preferred_handler);
            break;
        }
        g_value_set_static_string (val, "");
        break;

    case PROP_REQUESTS:
        if (priv->request_data != NULL &&
            priv->request_data->properties != NULL)
        {
            GPtrArray *arr = g_ptr_array_sized_new (1);

            g_ptr_array_add (arr,
                             g_hash_table_ref (priv->request_data->properties));

            g_value_take_boxed (val, arr);
            break;
        }
        g_value_take_boxed (val, g_ptr_array_sized_new (0));
        break;

    case PROP_INTERFACES:
        /* we have no interfaces */
        g_value_set_static_boxed (val, NULL);
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
mcd_channel_lost_account (gpointer data,
                          GObject *ex_account)
{
    McdChannel *self = MCD_CHANNEL (data);

    DEBUG ("%p: %p", self, ex_account);

    g_assert (self->priv->request_data != NULL);
    g_assert ((gpointer) self->priv->request_data->account ==
              (gpointer) ex_account);
    g_assert (self->priv->status == MCD_CHANNEL_STATUS_FAILED);

    self->priv->request_data->account = NULL;
}

static void
_mcd_channel_dispose (GObject * object)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (object);
   
    DEBUG ("%p (is disposed = %d)", object, priv->is_disposed);
    if (priv->is_disposed)
	return;

    priv->is_disposed = TRUE;

    if (priv->request_data)
    {
        if (priv->request_data->account != NULL)
        {
            g_object_weak_unref ((GObject *) priv->request_data->account,
                                 mcd_channel_lost_account, object);
        }

        channel_request_data_free (priv->request_data);
        priv->request_data = NULL;
    }

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
        g_free (list->data);
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
        GError *error = g_error_new (TP_ERRORS, TP_ERROR_DISCONNECTED,
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
}

static void
mcd_channel_class_init (McdChannelClass * klass)
{
    static TpDBusPropertiesMixinPropImpl request_props[] = {
        { "Account", "account-path", NULL },
        { "UserActionTime", "user-action-time", NULL },
        { "PreferredHandler", "preferred-handler", NULL },
        { "Interfaces", "interfaces", NULL },
        { "Requests", "requests", NULL },
        { NULL }
    };
    static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
        { TP_IFACE_CHANNEL_REQUEST,
          tp_dbus_properties_mixin_getter_gobject_properties,
          NULL,
          request_props,
        },
        { NULL }
    };
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
                             "Time of user action in seconds since 1970",
                             G_MININT64, G_MAXINT64, 0,
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

    klass->dbus_properties_class.interfaces = prop_interfaces,
    tp_dbus_properties_mixin_class_init (object_class,
        G_STRUCT_OFFSET (McdChannelClass, dbus_properties_class));
}

static void
mcd_channel_init (McdChannel * obj)
{
    McdChannelPrivate *priv;
   
    priv = G_TYPE_INSTANCE_GET_PRIVATE (obj, MCD_TYPE_CHANNEL,
					McdChannelPrivate);
    obj->priv = priv;

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
    }

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
            return tp_asv_get_string (properties,
                                      TP_IFACE_CHANNEL ".InitiatorID");
    }
    return NULL;
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

/*
 * _mcd_channel_get_immutable_properties:
 * @channel: the #McdChannel.
 *
 * Returns: the #GHashTable of the immutable properties.
 */
GHashTable *
_mcd_channel_get_immutable_properties (McdChannel *channel)
{
    GHashTable *ret;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);

    if (channel->priv->tp_chan == NULL)
    {
        DEBUG ("Channel %p has no associated TpChannel", channel);
        return NULL;
    }

    ret = tp_channel_borrow_immutable_properties (channel->priv->tp_chan);

    if (ret == NULL)
    {
        DEBUG ("Channel %p TpChannel %s (%p) has no immutable properties yet",
               channel, tp_proxy_get_object_path (channel->priv->tp_chan),
               channel->priv->tp_chan);
        return NULL;
    }

    return ret;
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
        return TP_PROXY (priv->tp_chan)->invalidated;

    return NULL;
}

/**
 * mcd_channel_new_request:
 * @account: an account.
 * @dgc: a #DBusGConnection on which to export the ChannelRequest object.
 * @properties: a #GHashTable of desired channel properties.
 * @user_time: user action time.
 * @preferred_handler: well-known name of preferred handler.
 * @use_existing: use EnsureChannel if %TRUE or CreateChannel if %FALSE
 * @proceeding: behave as though Proceed has already been called
 *
 * Create a #McdChannel object holding the given properties. The object can
 * then be used to intiate a channel request, by passing it to
 * mcd_connection_request_channel() on a connection in connected state.
 *
 * Returns: a newly created #McdChannel.
 */
McdChannel *
mcd_channel_new_request (McdAccount *account,
                         DBusGConnection *dgc,
                         GHashTable *properties,
                         gint64 user_time,
                         const gchar *preferred_handler,
                         gboolean use_existing,
                         gboolean proceeding)
{
    McdChannel *channel;
    McdChannelRequestData *crd;

    channel = g_object_new (MCD_TYPE_CHANNEL,
                            "outgoing", TRUE,
                            NULL);

    /* TODO: these data could be freed when the channel status becomes
     * MCD_CHANNEL_STATUS_DISPATCHED or MCD_CHANNEL_STATUS_FAILED */
    crd = g_slice_new (McdChannelRequestData);
    crd->path = g_strdup_printf (REQUEST_OBJ_BASE "%u", last_req_id++);
    crd->properties = g_hash_table_ref (properties);
    crd->user_time = user_time;
    crd->preferred_handler = g_strdup (preferred_handler);
    crd->use_existing = use_existing;
    crd->proceeding = proceeding;

    /* the McdAccount almost certainly lives longer than we do, but in case it
     * doesn't, use a weak ref here */
    g_object_weak_ref ((GObject *) account, mcd_channel_lost_account,
                       channel);
    crd->account = account;

    channel->priv->request_data = crd;
    channel->priv->satisfied_requests = g_list_prepend (NULL,
                                                        g_strdup (crd->path));

    _mcd_channel_set_status (channel, MCD_CHANNEL_STATUS_REQUEST);

    /* This could do with refactoring so that requests are a separate object
     * that dies at the appropriate time, but for now the path of least
     * resistance is to have the McdChannel be a ChannelRequest throughout
     * its lifetime */
    dbus_g_connection_register_g_object (dgc, crd->path, (GObject *) channel);

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
    McdChannelRequestData *crd;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    crd = channel->priv->request_data;
    return crd ? crd->path : NULL;
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
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    return channel->priv->satisfied_requests;
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
        source->priv->satisfied_requests =
            g_list_prepend (source->priv->satisfied_requests,
                            g_strdup (request_path));
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
}

TpChannel *
mcd_channel_get_tp_channel (McdChannel *channel)
{
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);

    return channel->priv->tp_chan;
}

static void
channel_request_proceed (TpSvcChannelRequest *iface,
                         DBusGMethodInvocation *context)
{
    McdChannel *self = MCD_CHANNEL (iface);

    if (G_UNLIKELY (self->priv->request_data == NULL))
    {
        GError na = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "McdChannel is on D-Bus but is not actually a request" };

        /* shouldn't be possible, but this code is quite tangled */
        g_warning ("%s: channel %p is on D-Bus but not actually a request",
                   G_STRFUNC, self);
        dbus_g_method_return_error (context, &na);
        return;
    }

    if (G_UNLIKELY (self->priv->request_data->account == NULL))
    {
        GError na = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "McdChannel has no Account, cannot proceed" };

        /* likewise, shouldn't be possible but this code is quite tangled */
        g_warning ("%s: channel %p has no Account, so cannot proceed",
                   G_STRFUNC, self);
        dbus_g_method_return_error (context, &na);
        return;
    }

    if (self->priv->request_data->proceeding)
    {
        GError na = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "Proceed has already been called; stop calling it" };

        dbus_g_method_return_error (context, &na);
    }

    self->priv->request_data->proceeding = TRUE;
    _mcd_account_proceed_with_request (self->priv->request_data->account,
                                       self);
    tp_svc_channel_request_return_from_proceed (context);
}

gboolean
_mcd_channel_request_cancel (McdChannel *self,
                             GError **error)
{
    McdChannelStatus status = mcd_channel_get_status (self);

    DEBUG ("%p in status %u", self, status);

    if (status == MCD_CHANNEL_STATUS_REQUEST ||
        status == MCD_CHANNEL_STATUS_REQUESTED ||
        status == MCD_CHANNEL_STATUS_DISPATCHING)
    {
        g_object_ref (self);
        mcd_channel_take_error (self, g_error_new (TP_ERRORS,
                                                   TP_ERROR_CANCELLED,
                                                   "Cancelled"));

        /* REQUESTED is a special case: the channel must not be aborted now,
         * because we need to explicitly close the channel object when it will
         * be created by the CM. In that case, mcd_mission_abort() will be
         * called once the Create/EnsureChannel method returns, if the channel
         * is ours */
        if (status != MCD_CHANNEL_STATUS_REQUESTED)
            mcd_mission_abort (MCD_MISSION (self));

        g_object_unref (self);
        return TRUE;
    }
    else
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                     "ChannelRequest is not cancellable (status=%u)",
                     status);
        return FALSE;
    }
}

static void
channel_request_cancel (TpSvcChannelRequest *iface,
                        DBusGMethodInvocation *context)
{
    McdChannel *self = MCD_CHANNEL (iface);
    GError *error = NULL;

    if (_mcd_channel_request_cancel (self, &error))
    {
        tp_svc_channel_request_return_from_cancel (context);
    }
    else
    {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

static void
request_iface_init (gpointer g_iface,
                    gpointer iface_data G_GNUC_UNUSED)
{
#define IMPLEMENT(x) tp_svc_channel_request_implement_##x (\
    g_iface, channel_request_##x)
    IMPLEMENT (proceed);
    IMPLEMENT (cancel);
#undef IMPLEMENT
}

static void
mcd_channel_depart_cb (TpChannel *channel,
                       const GError *error,
                       gpointer data G_GNUC_UNUSED,
                       GObject *weak_object G_GNUC_UNUSED)
{
    if (error == NULL)
    {
        DEBUG ("successful");
        return;
    }

    DEBUG ("failed to depart, calling Close instead: %s %d: %s",
           g_quark_to_string (error->domain), error->code, error->message);
    tp_cli_channel_call_close (channel, -1, NULL, NULL, NULL, NULL);
}

typedef struct {
    TpChannelGroupChangeReason reason;
    gchar *message;
} DepartData;

static void
mcd_channel_ready_to_depart_cb (TpChannel *channel,
                                const GError *error,
                                gpointer data)
{
    DepartData *d = data;

    if (error != NULL)
    {
        DEBUG ("%s %d: %s", g_quark_to_string (error->domain), error->code,
               error->message);
        g_free (d->message);
        g_slice_free (DepartData, d);
        return;
    }

    if (tp_proxy_has_interface_by_id (channel,
                                      TP_IFACE_QUARK_CHANNEL_INTERFACE_GROUP))
    {
        GArray *a = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
        guint self_handle = tp_channel_group_get_self_handle (channel);

        g_array_append_val (a, self_handle);

        tp_cli_channel_interface_group_call_remove_members_with_reason (
            channel, -1, a, d->message, d->reason,
            mcd_channel_depart_cb, NULL, NULL, NULL);

        g_array_free (a, TRUE);
        g_free (d->message);
        g_slice_free (DepartData, d);
    }
}

void
_mcd_channel_depart (McdChannel *channel,
                     TpChannelGroupChangeReason reason,
                     const gchar *message)
{
    DepartData *d;
    const GError *invalidated;

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
        tp_cli_channel_call_close (channel->priv->tp_chan, -1,
                                   NULL, NULL, NULL, NULL);
        return;
    }

    d = g_slice_new (DepartData);
    d->reason = reason;
    d->message = g_strdup (message == NULL ? "" : message);

    tp_channel_call_when_ready (channel->priv->tp_chan,
                                mcd_channel_ready_to_depart_cb, d);
}
