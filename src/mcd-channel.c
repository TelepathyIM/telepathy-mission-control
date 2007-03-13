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

#include <glib/gi18n.h>
#include <libtelepathy/tp-chan-iface-group-gen.h>
#include <libtelepathy/tp-constants.h>
#include <libtelepathy/tp-conn.h>

#include "mcd-channel.h"
#include "mcd-enum-types.h"

#define MCD_CHANNEL_PRIV(channel) (G_TYPE_INSTANCE_GET_PRIVATE ((channel), \
				   MCD_TYPE_CHANNEL, \
				   McdChannelPrivate))

G_DEFINE_TYPE (McdChannel, mcd_channel, MCD_TYPE_MISSION);

typedef struct _McdChannelPrivate
{
    /* Channel info */
    gchar *channel_object_path;
    gchar *channel_type;
    GQuark channel_type_quark;
    guint channel_handle;
    TelepathyHandleType channel_handle_type;
    gboolean outgoing;
    
    /* Channel created based on the above channel info */
    TpChan *tp_chan;
    
    /* Pending members */
    GArray *pending_local_members;
    gboolean members_accepted;
    
    McdChannelStatus status;
    gchar *channel_name;
    
    /* Requestor info */
    guint requestor_serial;
    gchar *requestor_client_id;
    
    gboolean is_disposed;
    
} McdChannelPrivate;

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
    PROP_REQUESTOR_CLIENT_ID
};

static guint mcd_channel_signals[LAST_SIGNAL] = { 0 };

static void _mcd_channel_release_tp_channel (McdChannel *channel,
					     gboolean close_channel);

static void
on_channel_members_changed (DBusGProxy * group_proxy,
			    const gchar * message, GArray * added,
			    GArray * removed, GArray * l_pending,
			    GArray * r_pending, guint actor,
			    guint reason, gpointer userdata)
{
    McdChannel *channel = MCD_CHANNEL (userdata);
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (userdata);
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
}

static void
get_local_pending_cb (DBusGProxy * group_proxy,
		      GArray * l_pending, GError * error, gpointer userdata)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (userdata);
    if (l_pending)
    {
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
	g_array_free (l_pending, TRUE);
    }
}

/* The callback is called on channel Closed signal */
void
on_tp_channel_closed (DBusGProxy * tp_chan, gpointer userdata)
{
    McdChannel *channel = MCD_CHANNEL (userdata);
    
    _mcd_channel_release_tp_channel (channel, FALSE);
    mcd_mission_abort (MCD_MISSION (channel));
    g_debug ("Channel closed");
}

static void proxy_destroyed (DBusGProxy *tp_chan, McdChannel *channel)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);

    g_debug ("Channel proxy destroyed!");
    g_object_unref (tp_chan);
    priv->tp_chan = NULL;
    mcd_mission_abort (MCD_MISSION (channel));
    g_debug ("Channel closed");
}

static void
_mcd_channel_release_tp_channel (McdChannel *channel, gboolean close_channel)
{
    DBusGProxy *group_iface;
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (channel);
    if (priv->tp_chan)
    {
	GError *error = NULL;
	
        g_debug ("%s: getting group_iface", G_STRFUNC);
	group_iface = tp_chan_get_interface (priv->tp_chan,
					     TELEPATHY_CHAN_IFACE_GROUP_QUARK);
	if (group_iface)
	{
	    dbus_g_proxy_disconnect_signal (group_iface,"MembersChanged",
					    G_CALLBACK (on_channel_members_changed),
					    channel);
	}
        
	dbus_g_proxy_disconnect_signal (DBUS_G_PROXY (priv->tp_chan),
					"Closed",
					G_CALLBACK (on_tp_channel_closed),
					channel);
	g_signal_handlers_disconnect_by_func (G_OBJECT (priv->tp_chan),
					      G_CALLBACK (proxy_destroyed),
					      channel);

	if (close_channel && priv->channel_type_quark != TELEPATHY_CHAN_IFACE_CONTACTLIST_QUARK)
	{
	    g_debug ("%s: Requesting telepathy to close the channel", G_STRFUNC);
	    tp_chan_close (DBUS_G_PROXY (priv->tp_chan), &error);
	    if (error)
	    {
		g_warning ("%s: Request for channel close failed: %s",
			   G_STRFUNC, error->message);
		g_error_free (error);
	    }
	}
	/* Destroy our proxy */
	g_object_unref (priv->tp_chan);
	
	priv->tp_chan = NULL;
    }
}

static void
_mcd_channel_set_property (GObject * obj, guint prop_id,
			   const GValue * val, GParamSpec * pspec)
{
    DBusGProxy *group_iface;
    TpChan *tp_chan;
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
 
	    /* FIXME: BUG in libtelepathy */
	    /* g_return_if_fail (TELEPATHY_IS_CHAN (tp_chan)); */
	    g_object_ref (tp_chan);
	}
	_mcd_channel_release_tp_channel (channel, TRUE);
	priv->tp_chan = tp_chan;
	if (priv->tp_chan)
	{
	    group_iface = tp_chan_get_interface (priv->tp_chan,
						 TELEPATHY_CHAN_IFACE_GROUP_QUARK);
	    if (group_iface)
	    {
		/* Setup channel watches */
		dbus_g_proxy_connect_signal (group_iface, "MembersChanged",
					     G_CALLBACK (on_channel_members_changed),
					     channel, NULL);
		tp_chan_iface_group_get_local_pending_members_async (group_iface,
								     get_local_pending_cb,
								     channel);
	    }
	    /* We want to track the channel object closes, because we need to do
	     * some cleanups when it's gone */
	    dbus_g_proxy_connect_signal (DBUS_G_PROXY (priv->tp_chan), "Closed",
					 G_CALLBACK (on_tp_channel_closed),
					 channel, NULL);
	    g_signal_connect (priv->tp_chan, "destroy",
			      G_CALLBACK (proxy_destroyed), channel);

	}
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
							  TELEPATHY_CHAN_TYPE,
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
}

static void
mcd_channel_init (McdChannel * obj)
{
    McdChannelPrivate *priv = MCD_CHANNEL_PRIV (obj);
    priv->pending_local_members = g_array_new (FALSE, FALSE,
					       sizeof (guint));
}

McdChannel *
mcd_channel_new (TpChan * tp_chan, const gchar *channel_object_path,
		 const gchar *channel_type, guint channel_handle,
		 TelepathyHandleType channel_handle_type, gboolean outgoing,
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

TelepathyHandleType
mcd_channel_get_handle_type (McdChannel *channel)
{
    return MCD_CHANNEL_PRIV (channel)->channel_handle_type;
}

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

GPtrArray*
mcd_channel_get_members (McdChannel *channel)
{
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
    }
    
    g_object_unref (connection);
    g_object_unref (tp_connection);
    return members;
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

    if (!priv->channel_name)
    {
	GObject *connection = NULL;
	TpConn *tp_conn = NULL;
	GArray *request_handles;
	gchar **handle_names = NULL;
	GError *error = NULL;

	request_handles = g_array_new (FALSE, FALSE, sizeof (guint));
	g_array_append_val (request_handles, priv->channel_handle);

	g_object_get (channel, "connection", &connection, NULL);
	g_object_get (connection, "tp-connection", &tp_conn, NULL);
	if (!tp_conn_inspect_handles (DBUS_G_PROXY (tp_conn),
				      priv->channel_handle_type,
				      request_handles,
				      &handle_names, &error))
	{
	    g_warning ("%s: InspectHandles failed: %s",
		       G_STRFUNC, error->message);
	    g_error_free (error);
	}
	else
	    priv->channel_name = handle_names[0];

	g_object_unref (connection);
	g_object_unref (tp_conn);
	g_array_free (request_handles, TRUE);
	/* free only the pointer array, not the string itself */
	g_free (handle_names);
    }

    return priv->channel_name;
}

