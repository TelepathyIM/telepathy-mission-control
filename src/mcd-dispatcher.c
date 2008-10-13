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
 * SECTION:mcd-dispatcher
 * @title: McdDispatcher
 * @short_description: Dispatcher class to dispatch channels to handlers
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-dispatcher.h
 * 
 * FIXME
 */

#include <dlfcn.h>
#include <glib/gi18n.h>

#include "mcd-signals-marshal.h"
#include "mcd-connection.h"
#include "mcd-channel.h"
#include "mcd-master.h"
#include "mcd-chan-handler.h"
#include "mcd-dispatcher-context.h"
#include "mcd-misc.h"
#include "_gen/interfaces.h"
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/proxy-subclass.h>

#include <libmcclient/mc-errors.h>

#include <string.h>

#define MCD_DISPATCHER_PRIV(dispatcher) (G_TYPE_INSTANCE_GET_PRIVATE ((dispatcher), \
				  MCD_TYPE_DISPATCHER, \
				  McdDispatcherPrivate))

G_DEFINE_TYPE (McdDispatcher, mcd_dispatcher, MCD_TYPE_MISSION);

struct _McdDispatcherContext
{
    McdDispatcher *dispatcher;
    
    /*The actual channel */
    McdChannel *channel;

    gchar *protocol;

    /* State-machine internal data fields: */
    GList *chain;

    /* Next function in chain */
    guint next_func_index;
};

typedef struct _McdDispatcherArgs
{
    McdDispatcher *dispatcher;
    const gchar *protocol;
    GPtrArray *channel_handler_caps;
} McdDispatcherArgs;

#define MCD_FILTER_CHANNEL_TYPE 0x1
#define MCD_FILTER_HANDLE_TYPE  0x2
#define MCD_FILTER_REQUESTED    0x4
typedef struct _McdClientFilter
{
    guint field_mask;
    GQuark channel_type;
    TpHandleType handle_type;
    guint requested : 1;
} McdClientFilter;

typedef enum
{
    MCD_CLIENT_APPROVER = 0x1,
    MCD_CLIENT_HANDLER  = 0x2,
    MCD_CLIENT_OBSERVER = 0x4,
} McdClientInterface;

typedef struct _McdClient
{
    TpProxy *proxy;
    gchar *name;
    McdClientInterface interfaces;
    guint bypass_approver : 1;
    /* each element is a McdClientFilter */
    GList *approver_filters;
    GList *handler_filters;
    GList *observer_filters;
} McdClient;

#define MCD_IFACE_CLIENT "org.freedesktop.Telepathy.Client"
#define MCD_IFACE_CLIENT_APPROVER "org.freedesktop.Telepathy.Client.Approver"
#define MCD_IFACE_CLIENT_HANDLER "org.freedesktop.Telepathy.Client.Handler"
#define MCD_IFACE_CLIENT_OBSERVER "org.freedesktop.Telepathy.Client.Observer"

typedef struct _McdDispatcherPrivate
{
    /* Pending state machine contexts */
    GSList *state_machine_list;
    
    /* All active channels */
    GList *channels;
    
    GData *interface_filters;
    TpDBusDaemon *dbus_daemon;

    /* Channel handlers */
    GHashTable *channel_handler_hash;
    /* Array of channel handler's capabilities, stored as a GPtrArray for
     * performance reasons */
    GPtrArray *channel_handler_caps;

    /* each element is a McdClient struct */
    GList *clients;

    McdMaster *master;
 
    gboolean is_disposed;
    
} McdDispatcherPrivate;

struct iface_chains_t
{
    GList *chain_in;
    GList *chain_out;
};

struct cancel_call_data
{
    DBusGProxy *handler_proxy;
    DBusGProxyCall *call;
    McdDispatcher *dispatcher;
};

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
    PROP_MCD_MASTER,
};

enum _McdDispatcherSignalType
{
    CHANNEL_ADDED,
    CHANNEL_REMOVED,
    DISPATCHED,
    DISPATCH_FAILED,
    LAST_SIGNAL
};

static guint mcd_dispatcher_signals[LAST_SIGNAL] = { 0 };

static void mcd_dispatcher_context_free (McdDispatcherContext * ctx);
typedef void (*tp_ch_handle_channel_reply) (DBusGProxy *proxy, GError *error, gpointer userdata);

static void
mcd_client_filter_free (McdClientFilter *filter)
{
    g_slice_free (McdClientFilter, filter);
}

static void
mcd_client_free (McdClient *client)
{
    if (client->proxy)
        g_object_unref (client->proxy);

    g_free (client->name);
    g_list_foreach (client->approver_filters,
                    (GFunc)mcd_client_filter_free, NULL);
    g_list_foreach (client->handler_filters,
                    (GFunc)mcd_client_filter_free, NULL);
    g_list_foreach (client->observer_filters,
                    (GFunc)mcd_client_filter_free, NULL);
}

static void
tp_ch_handle_channel_async_callback (DBusGProxy *proxy, DBusGProxyCall *call, void *user_data)
{
  DBusGAsyncData *data = (DBusGAsyncData*) user_data;
  GError *error = NULL;
  dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID);
  (*(tp_ch_handle_channel_reply)data->cb) (proxy, error, data->userdata);
  return;
}

static inline DBusGProxyCall*
tp_ch_handle_channel_async (DBusGProxy *proxy, const char * IN_Bus_Name, const char* IN_Connection, const char * IN_Channel_Type, const char* IN_Channel, const guint IN_Handle_Type, const guint IN_Handle, tp_ch_handle_channel_reply callback, gpointer userdata)

{
  DBusGAsyncData *stuff;
  stuff = g_new (DBusGAsyncData, 1);
  stuff->cb = G_CALLBACK (callback);
  stuff->userdata = userdata;
  return dbus_g_proxy_begin_call (proxy, "HandleChannel", tp_ch_handle_channel_async_callback, stuff, g_free, G_TYPE_STRING, IN_Bus_Name, DBUS_TYPE_G_OBJECT_PATH, IN_Connection, G_TYPE_STRING, IN_Channel_Type, DBUS_TYPE_G_OBJECT_PATH, IN_Channel, G_TYPE_UINT, IN_Handle_Type, G_TYPE_UINT, IN_Handle, G_TYPE_INVALID);
}

static inline DBusGProxyCall *
tp_ch_handle_channel_2_async (DBusGProxy *proxy,
			      const char * IN_Bus_Name,
			      const char* IN_Connection,
			      const char *IN_Channel_Type,
			      const char* IN_Channel,
			      const guint IN_Handle_Type, const guint IN_Handle,
			      gboolean incoming, guint request_id,
			      const GHashTable *options,
			      tp_ch_handle_channel_reply callback,
			      gpointer userdata)

{
    DBusGAsyncData *stuff;
    stuff = g_new (DBusGAsyncData, 1);
    stuff->cb = G_CALLBACK (callback);
    stuff->userdata = userdata;
    return dbus_g_proxy_begin_call (proxy, "HandleChannel2",
				    tp_ch_handle_channel_async_callback,
				    stuff, g_free,
				    G_TYPE_STRING, IN_Bus_Name,
				    DBUS_TYPE_G_OBJECT_PATH, IN_Connection,
				    G_TYPE_STRING, IN_Channel_Type,
				    DBUS_TYPE_G_OBJECT_PATH, IN_Channel,
				    G_TYPE_UINT, IN_Handle_Type,
				    G_TYPE_UINT, IN_Handle,
				    /* New params for version 2: */
				    G_TYPE_BOOLEAN, incoming,
				    G_TYPE_UINT, request_id,
				    dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), options,
				    G_TYPE_INVALID);
}

/* REGISTRATION/DEREGISTRATION of filters*/

/* A convenience function for acquiring the chain for particular channel
type and filter flag combination. */

static GList *
_mcd_dispatcher_get_filter_chain (McdDispatcher * dispatcher,
				  GQuark channel_type_quark,
				  guint filter_flags)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (dispatcher);
    struct iface_chains_t *iface_chains;
    GList *filter_chain = NULL;

    iface_chains =
	(struct iface_chains_t *)
	g_datalist_id_get_data (&(priv->interface_filters), channel_type_quark);

    if (iface_chains == NULL)
    {
	g_debug ("%s: No chains for interface %s", G_STRFUNC,
		 g_quark_to_string (channel_type_quark));
    }
    else
	switch (filter_flags)
	{
	case MCD_FILTER_IN:
	    filter_chain = iface_chains->chain_in;
	    break;
	case MCD_FILTER_OUT:
	    filter_chain = iface_chains->chain_out;
	    break;

	default:
	    g_warning ("Unsupported filter flag value");
	    break;
	}

    return filter_chain;
}

static GList *
chain_add_filter (GList *chain,
		  McdFilterFunc filter,
		  guint priority,
                  gpointer user_data)
{
    GList *elem;
    McdFilter *filter_data;

    filter_data = g_malloc (sizeof (McdFilter));
    filter_data->func = filter;
    filter_data->priority = priority;
    filter_data->user_data = user_data;
    for (elem = chain; elem; elem = elem->next)
	if (((McdFilter *)elem->data)->priority >= priority) break;

    return g_list_insert_before (chain, elem, filter_data);
}

static GList *
chain_remove_filter (GList *chain, McdFilterFunc func)
{
    GList *elem, *new_chain = NULL;

    /* since in-place modification of a list is error prone (especially if the
     * same filter has been registered in the same chain with different
     * priorities), we build a new list with the remaining elements */
    for (elem = chain; elem; elem = elem->next)
    {
	if (((McdFilter *)elem->data)->func == func)
	    g_free (elem->data);
	else
	    new_chain = g_list_append (new_chain, elem->data);
    }
    g_list_free (chain);

    return new_chain;
}

static void
free_filter_chains (struct iface_chains_t *chains)
{
    if (chains->chain_in)
    {
	g_list_foreach (chains->chain_in, (GFunc)g_free, NULL);
	g_list_free (chains->chain_in);
    }
    if (chains->chain_out)
    {
	g_list_foreach (chains->chain_out, (GFunc)g_free, NULL);
	g_list_free (chains->chain_out);
    }
    g_free (chains);
}

/**
 * mcd_dispatcher_register_filter:
 * @dispatcher: The #McdDispatcher.
 * @filter: the filter function to be registered.
 * @channel_type_quark: Quark indicating the channel type.
 * @filter_flags: The flags for the filter, such as incoming/outgoing.
 * @priority: The priority of the filter.
 *
 * Indicates to Mission Control that we want to register a filter for a unique
 * combination of channel type/filter flags.
 */
void
mcd_dispatcher_register_filter (McdDispatcher *dispatcher,
			       	McdFilterFunc filter,
				GQuark channel_type_quark,
				guint filter_flags, guint priority,
				gpointer user_data)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (dispatcher);
    struct iface_chains_t *iface_chains = NULL;

    /* Check if the interface already has stored data, otherwise create it */

    if (!(iface_chains = g_datalist_id_get_data (&(priv->interface_filters),
						 channel_type_quark)))
    {
	iface_chains = g_new0 (struct iface_chains_t, 1);
	g_datalist_id_set_data_full (&(priv->interface_filters),
				     channel_type_quark, iface_chains,
				     (GDestroyNotify)free_filter_chains);
    }

    switch (filter_flags)
    {
    case MCD_FILTER_IN:
	iface_chains->chain_in = chain_add_filter (iface_chains->chain_in,
						   filter, priority, user_data);
	break;
    case MCD_FILTER_OUT:
	iface_chains->chain_out = chain_add_filter (iface_chains->chain_out,
						    filter, priority, user_data);
	break;
    default:
	g_warning ("Unknown filter flag value!");
    }
}

/**
 * mcd_dispatcher_unregister_filter:
 * @dispatcher: The #McdDispatcher.
 * @filter: the filter function to be registered.
 * @channel_type_quark: Quark indicating the channel type.
 * @filter_flags: The flags for the filter, such as incoming/outgoing.
 *
 * Indicates to Mission Control that we will not want to have a filter
 * for particular unique channel type/filter flags combination anymore.
 */
void
mcd_dispatcher_unregister_filter (McdDispatcher * dispatcher,
				  McdFilterFunc filter,
				  GQuark channel_type_quark,
				  guint filter_flags)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (dispatcher);

    /* First, do we have anything registered for that channel type? */
    struct iface_chains_t *chains =
	(struct iface_chains_t *)
	g_datalist_id_get_data (&(priv->interface_filters),
				channel_type_quark);
    if (chains == NULL)
    {
	g_warning ("Attempting to unregister from an empty filter chain");
	return;
    }

    switch (filter_flags)
    {
    case MCD_FILTER_IN:
	/* No worries about memory leaks, as these are function pointers */
	chains->chain_in = chain_remove_filter(chains->chain_in, filter);
	break;
    case MCD_FILTER_OUT:
	chains->chain_out = chain_remove_filter(chains->chain_out, filter);
	break;
    default:
	g_warning ("Unknown filter flag value!");
    }

    /* Both chains are empty? We may as well free the struct then */

    if (chains->chain_in == NULL && chains->chain_out == NULL)
    {
	/* ? Should we dlclose the plugin as well..? */
	g_datalist_id_remove_data (&(priv->interface_filters),
				   channel_type_quark);
    }

    return;
}

/**
 * mcd_dispatcher_register_filters:
 * @dispatcher: The #McdDispatcher.
 * @filters: a zero-terminated array of #McdFilter elements.
 * @channel_type_quark: Quark indicating the channel type.
 * @filter_flags: The flags for the filter, such as incoming/outgoing.
 *
 * Convenience function to register a batch of filters at once.
 */
void
mcd_dispatcher_register_filters (McdDispatcher *dispatcher,
				 McdFilter *filters,
				 GQuark channel_type_quark,
				 guint filter_flags)
{
    McdFilter *filter;

    g_return_if_fail (filters != NULL);

    for (filter = filters; filter->func != NULL; filter++)
	mcd_dispatcher_register_filter (dispatcher, filter->func,
				       	channel_type_quark,
					filter_flags, filter->priority,
					filter->user_data);
}

/* Returns # of times particular channel type  has been used */
gint
mcd_dispatcher_get_channel_type_usage (McdDispatcher * dispatcher,
				       GQuark chan_type_quark)
{
    GList *node;
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (dispatcher);
    gint usage_counter = 0;
    
    node = priv->channels;
    while (node)
    {
	McdChannel *chan = (McdChannel*) node->data;
	if (chan && chan_type_quark == mcd_channel_get_channel_type_quark (chan))
	    usage_counter++;
	node = node->next;
    }

    return usage_counter;
}

/* The callback is called on channel Closed signal */
static void
on_channel_abort_list (McdChannel *channel, McdDispatcher *dispatcher)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (dispatcher);
    
    g_debug ("Abort Channel; Removing channel from list");
    priv->channels = g_list_remove (priv->channels, channel);
    g_signal_emit_by_name (dispatcher, "channel-removed", channel);
    g_object_unref (channel);
}

static void
on_master_abort (McdMaster *master, McdDispatcherPrivate *priv)
{
    g_object_unref (master);
    priv->master = NULL;
}

/* CHANNEL HANDLING */

/* Ensure that when the channelhandler dies, the channels do not be
 * left around (e.g. when VOIP UI dies, the call used to hang
 * around)
 */
static void
_mcd_dispatcher_channel_handler_destroy_cb (DBusGProxy * channelhandler,
					   gpointer userdata)
{
    McdChannel *channel;

    /* If the channel has already been destroyed, do not bother doing
     * anything. */
    if (!userdata || !(G_IS_OBJECT (userdata)) || !(MCD_IS_CHANNEL (userdata)))
    {
	g_debug ("Channel has already been closed. No need to clean up.");
	return;
    }

    channel = MCD_CHANNEL (userdata);

    g_debug ("Channelhandler object been destroyed, chan still valid.");
    mcd_mission_abort (MCD_MISSION (channel));
}

static void
disconnect_proxy_destry_cb (McdChannel *channel, DBusGProxy *channelhandler)
{
    g_signal_handlers_disconnect_by_func (channelhandler,
				    _mcd_dispatcher_channel_handler_destroy_cb,
				    channel);
    g_object_unref (channelhandler);
}

static void
cancel_proxy_call (McdChannel *channel, struct cancel_call_data *call_data)
{
    GError *mc_error = NULL;

    dbus_g_proxy_cancel_call (call_data->handler_proxy, call_data->call);
    
    g_debug ("%s: signalling Handle channel failed", G_STRFUNC);
    
    /* We can't reliably map channel handler error codes to MC error
     * codes. So just using generic error message.
     */
    mc_error = g_error_new (MC_ERROR, MC_CHANNEL_REQUEST_GENERIC_ERROR,
			    "Channel aborted");
    
    g_signal_emit (call_data->dispatcher,
		   mcd_dispatcher_signals[DISPATCH_FAILED], 0,
		   channel, mc_error);
    g_error_free (mc_error);
}

static void
_mcd_dispatcher_handle_channel_async_cb (DBusGProxy * proxy, GError * error,
					 gpointer userdata)
{
    McdDispatcherContext *context = userdata;
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (context->dispatcher);
    McdChannel *channel;
    const gchar *protocol = NULL;
    GHashTable *channel_handler;
    McdChannelHandler *chandler;

    channel = mcd_dispatcher_context_get_channel (context);
    protocol = mcd_dispatcher_context_get_protocol_name (context);

    channel_handler = g_hash_table_lookup (priv->channel_handler_hash,
					   mcd_channel_get_channel_type (channel));

    chandler = g_hash_table_lookup (channel_handler, protocol);
    if (!chandler)
	chandler = g_hash_table_lookup (channel_handler, "default");

    g_signal_handlers_disconnect_matched (channel, G_SIGNAL_MATCH_FUNC,	0, 0,
					  NULL, cancel_proxy_call, NULL);

    /* We'll no longer need this proxy instance. */
    if (proxy && DBUS_IS_G_PROXY (proxy))
    {
	g_object_unref (proxy);
    }

    if (error != NULL)
    {
	GError *mc_error = NULL;
	
	g_warning ("Handle channel failed: %s", error->message);
	
	/* We can't reliably map channel handler error codes to MC error
	 * codes. So just using generic error message.
	 */
	mc_error = g_error_new (MC_ERROR, MC_CHANNEL_REQUEST_GENERIC_ERROR,
				"Handle channel failed: %s", error->message);
	
	g_signal_emit_by_name (context->dispatcher, "dispatch-failed",
			       context->channel, mc_error);
	
	g_error_free (mc_error);
	g_error_free (error);
	if (context->channel)
	    mcd_mission_abort (MCD_MISSION (context->channel));
	return;
    }

    /* In case the channel handler dies unexpectedly, we
     * may end up in very confused state if we do
     * nothing. Thus, we'll try to handle the death */
    
    {
	DBusGConnection *dbus_connection;
	GError *unique_proxy_error = NULL;
        DBusGProxy *unique_name_proxy;
	
	dbus_connection = TP_PROXY (priv->dbus_daemon)->dbus_connection;
	
	unique_name_proxy =
	    dbus_g_proxy_new_for_name_owner (dbus_connection,
					     chandler->bus_name,
					     chandler->obj_path,
					     "org.freedesktop.Telepathy.ChannelHandler",
					     &unique_proxy_error);
	if (unique_proxy_error == NULL)
	{
	    g_debug ("Adding the destroy handler support.");
	    g_signal_connect (unique_name_proxy,
			      "destroy",
			      G_CALLBACK (_mcd_dispatcher_channel_handler_destroy_cb),
			      context->channel);
	    g_signal_connect (context->channel, "abort",
			      G_CALLBACK(disconnect_proxy_destry_cb),
			      unique_name_proxy);
	}
    }

    g_signal_emit_by_name (context->dispatcher, "dispatched", channel);
    mcd_dispatcher_context_free (context);
}

/* Happens at the end of successful filter chain execution (empty chain
 * is always successful)
 */
static void
_mcd_dispatcher_start_channel_handler (McdDispatcherContext * context)
{
    McdChannelHandler *chandler;
    McdDispatcherPrivate *priv;
    McdChannel *channel;
    const gchar *protocol;
    GHashTable *channel_handler;
    

    g_return_if_fail (context);

    priv = MCD_DISPATCHER_PRIV (context->dispatcher);
    channel = mcd_dispatcher_context_get_channel (context); 
    protocol = mcd_dispatcher_context_get_protocol_name (context);

    channel_handler =
	g_hash_table_lookup (priv->channel_handler_hash,
			     mcd_channel_get_channel_type (channel));

    chandler = g_hash_table_lookup (channel_handler, protocol);
    if (chandler == NULL)
	chandler = g_hash_table_lookup (channel_handler, "default");
    
    if (chandler == NULL)
    {
	GError *mc_error;
	g_debug ("No handler for channel type %s",
		 mcd_channel_get_channel_type (channel));
	
	mc_error = g_error_new (MC_ERROR, MC_CHANNEL_REQUEST_GENERIC_ERROR,
				"No handler for channel type %s",
				mcd_channel_get_channel_type (channel));
	g_signal_emit_by_name (context->dispatcher, "dispatch-failed", channel,
			       mc_error);
	g_error_free (mc_error);
    }
    else
    {
	struct cancel_call_data *call_data;
	DBusGProxyCall *call;
	TpConnection *tp_conn;
	DBusGProxy *handler_proxy;
	const McdConnection *connection = mcd_dispatcher_context_get_connection (context);
	DBusGConnection *dbus_connection;

	dbus_connection = TP_PROXY (priv->dbus_daemon)->dbus_connection;
        g_object_get (G_OBJECT (connection),
                      "tp-connection", &tp_conn, NULL);

	handler_proxy = dbus_g_proxy_new_for_name (dbus_connection,
							       chandler->bus_name,
							       chandler->obj_path,
				"org.freedesktop.Telepathy.ChannelHandler");
	
	g_debug ("Starting chan handler (bus = %s, obj = '%s'): conn = %s, chan_type = %s,"
		 " obj_path = %s, handle_type = %d, handle = %d",
		 chandler->bus_name,
		 chandler->obj_path,
		 TP_PROXY (tp_conn)->object_path,
		 mcd_channel_get_channel_type (channel),
		 mcd_channel_get_object_path (channel),
		 mcd_channel_get_handle_type (channel),
		 mcd_channel_get_handle (channel));
 
	if (chandler->version >= 2)
	{
	    gboolean outgoing;
	    guint request_id;
	    GHashTable *options;

	    g_debug ("new chandler");
	    g_object_get (channel,
			  "outgoing", &outgoing,
			  "requestor-serial", &request_id,
			  NULL);
	    options = g_hash_table_new (g_str_hash, g_str_equal);
	    call = tp_ch_handle_channel_2_async (handler_proxy,
					/*Connection bus */
					TP_PROXY (tp_conn)->bus_name,
					/*Connection path */
					TP_PROXY (tp_conn)->object_path,
					/*Channel type */
					mcd_channel_get_channel_type (channel),
					/*Object path */
					mcd_channel_get_object_path (channel),
					mcd_channel_get_handle_type (channel),
					mcd_channel_get_handle (channel),
					!outgoing,
					request_id,
					options,
					_mcd_dispatcher_handle_channel_async_cb,
					context);
	    g_hash_table_destroy (options);
	}
	else
	    call = tp_ch_handle_channel_async (handler_proxy,
					/*Connection bus */
					TP_PROXY (tp_conn)->bus_name,
					/*Connection path */
					TP_PROXY (tp_conn)->object_path,
					/*Channel type */
					mcd_channel_get_channel_type (channel),
					/*Object path */
					mcd_channel_get_object_path (channel),
					mcd_channel_get_handle_type (channel),
					mcd_channel_get_handle (channel),
					_mcd_dispatcher_handle_channel_async_cb,
					context);
	call_data = g_malloc (sizeof (struct cancel_call_data));
	call_data->call = call;
	call_data->handler_proxy = handler_proxy;
	call_data->dispatcher = context->dispatcher;
	g_signal_connect_data (channel, "abort", G_CALLBACK(cancel_proxy_call),
			  call_data, (GClosureNotify)g_free, 0);
        g_object_unref (tp_conn);
    }
}

static void
_mcd_dispatcher_drop_channel_handler (McdDispatcherContext * context)
{
    g_return_if_fail(context);
    
    /* drop from the queue and close channel */
    
    /* FIXME: The queue functionality is still missing. Add support for
    it, once it's available. */
    
    if (context->channel != NULL)
    {
	/* Context will be destroyed on this emission, so be careful
	 * not to access it after this.
	 */
	mcd_mission_abort (MCD_MISSION (context->channel));
    }
}

/* STATE MACHINE */

static void
_mcd_dispatcher_leave_state_machine (McdDispatcherContext * context)
{
    McdDispatcherPrivate *priv =
	MCD_DISPATCHER_PRIV (context->dispatcher);

    /* _mcd_dispatcher_drop_channel_handler (context); */

    priv->state_machine_list =
	g_slist_remove (priv->state_machine_list, context);

    mcd_dispatcher_context_free (context);
}

static void
on_channel_abort_context (McdChannel *channel, McdDispatcherContext *context)
{
    g_debug ("Abort Channel; Destroying state machine context.");
    _mcd_dispatcher_leave_state_machine (context);
}

/* Entering the state machine */
static void
_mcd_dispatcher_enter_state_machine (McdDispatcher *dispatcher,
				     McdChannel *channel)
{
    McdDispatcherContext *context;
    GList *chain;
    GQuark chan_type_quark;
    gboolean outgoing;
    gint filter_flags;
    
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (dispatcher);

    chan_type_quark = mcd_channel_get_channel_type_quark (channel);
    g_object_get (G_OBJECT (channel),
		  "outgoing", &outgoing,
		  NULL);

    filter_flags = outgoing ? MCD_FILTER_OUT: MCD_FILTER_IN;
    chain = _mcd_dispatcher_get_filter_chain (dispatcher,
					      chan_type_quark,
					      filter_flags);
    
    /* Preparing and filling the context */
    context = g_new0 (McdDispatcherContext, 1);
    context->dispatcher = dispatcher;
    context->channel = channel;
    context->chain = chain;

    /* Context must be destroyed when the channel is destroyed */
    g_object_ref (channel); /* We hold separate refs for state machine */
    g_signal_connect_after (channel, "abort", G_CALLBACK (on_channel_abort_context),
		      context);
    
    if (chain)
    {
        g_debug ("entering state machine for channel of type: %s",
             g_quark_to_string (chan_type_quark));

	priv->state_machine_list =
	    g_slist_prepend (priv->state_machine_list, context);
	mcd_dispatcher_context_process (context, TRUE);
    }
    else
    {
	g_debug ("No filters found for type %s, starting the channel handler", g_quark_to_string (chan_type_quark));
	_mcd_dispatcher_start_channel_handler (context);
    }
}

static gint
channel_on_state_machine (McdDispatcherContext *context, McdChannel *channel)
{
    return (context->channel == channel) ? 0 : 1;
}

static void
_mcd_dispatcher_send (McdDispatcher * dispatcher, McdChannel * channel)
{
    McdDispatcherPrivate *priv;
    g_return_if_fail (MCD_IS_DISPATCHER (dispatcher));
    g_return_if_fail (MCD_IS_CHANNEL (channel));

    mcd_channel_set_status (channel, MCD_CHANNEL_DISPATCHING);
    priv = MCD_DISPATCHER_PRIV (dispatcher);

    /* it can happen that this function gets called when the same channel has
     * already entered the state machine or even when it has already been
     * dispatched; so, check if this channel is already known to the
     * dispatcher: */
    if (g_list_find (priv->channels, channel))
    {
	McdDispatcherContext *context = NULL;
	GSList *list;
	g_debug ("%s: channel is already in dispatcher", G_STRFUNC);

	/* check if channel has already been dispatched (if it's still in the
	 * state machine list, this means that it hasn't) */
	list = g_slist_find_custom (priv->state_machine_list, channel,
				    (GCompareFunc)channel_on_state_machine);
	if (list) context = list->data;
	if (context)
	{
	    gboolean outgoing;
	    g_debug ("%s: channel found in the state machine (%p)", G_STRFUNC, context);
	    g_object_get (G_OBJECT (channel),
			  "outgoing", &outgoing,
			  NULL);

	    g_debug ("channel is %s", outgoing ? "outgoing" : "incoming");
	    /* this channel has not been dispatched; we can get to this point if:
	     * 1) the channel is incoming (i.e. the contacts plugin icon is
	     *    blinking) but the user didn't realize that and instead
	     *    requested the same channel again;
	     * 2) theif channel is outgoing, and it was requested again before
	     *    it could be created; I'm not sure this can really happen,
	     *    though. In this case we don't have to do anything, just ignore
	     *    this second request */
	    if (!outgoing)
	    {
		/* incoming channel: the state machine is probably stucked
		 * waiting for the user to acknowledge the channel. We bypass
		 * all that and instead launch the channel handler; this will
		 * free the context, but we still have to remove it from the
		 * machine state list ourselves.
		 * The filters should connect to the "dispatched" signal to
		 * catch this particular situation and clean-up gracefully. */
		_mcd_dispatcher_start_channel_handler (context);
		priv->state_machine_list =
		    g_slist_remove(priv->state_machine_list, context);

	    }
	}
	else
	{
	    /* The channel was not found in the state machine, hence it must
	     * have been already dispatched.
	     * We could get to this point if the UI crashed while this channel
	     * was open, and now the user is requesting it again. just go straight
	     * and start the channel handler. */
	    g_debug ("%s: channel is already dispatched, starting handler", G_STRFUNC);
	    /* Preparing and filling the context */
	    context = g_new0 (McdDispatcherContext, 1);
	    context->dispatcher = dispatcher;
	    context->channel = channel;

	    /* We must ref() the channel, because mcd_dispatcher_context_free()
	     * will unref() it */
	    g_object_ref (channel);
	    _mcd_dispatcher_start_channel_handler (context);
	}
	return;
    }
    
    /* Get hold of it in our all channels list */
    g_object_ref (channel); /* We hold separate refs for channels list */
    g_signal_connect (channel, "abort", G_CALLBACK (on_channel_abort_list),
		      dispatcher);
    priv->channels = g_list_prepend (priv->channels, channel);
    
    g_signal_emit_by_name (dispatcher, "channel-added", channel);
    _mcd_dispatcher_enter_state_machine (dispatcher, channel);
}

static void
_mcd_dispatcher_set_property (GObject * obj, guint prop_id,
			      const GValue * val, GParamSpec * pspec)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (obj);
    McdMaster *master;

    switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
	if (priv->dbus_daemon)
	    g_object_unref (priv->dbus_daemon);
	priv->dbus_daemon = TP_DBUS_DAEMON (g_value_dup_object (val));
	break;
    case PROP_MCD_MASTER:
	master = g_value_get_object (val);
	g_object_ref (G_OBJECT (master));
	if (priv->master)
        {
            g_signal_handlers_disconnect_by_func (G_OBJECT (master), G_CALLBACK (on_master_abort), NULL);
	    g_object_unref (priv->master);
        }
	priv->master = master;
        g_signal_connect (G_OBJECT (master), "abort", G_CALLBACK (on_master_abort), priv);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_dispatcher_get_property (GObject * obj, guint prop_id,
			      GValue * val, GParamSpec * pspec)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (obj);

    switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
	g_value_set_object (val, priv->dbus_daemon);
	break;
    case PROP_MCD_MASTER:
	g_value_set_object (val, priv->master);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_dispatcher_finalize (GObject * object)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (object);

    g_hash_table_destroy (priv->channel_handler_hash);

    G_OBJECT_CLASS (mcd_dispatcher_parent_class)->finalize (object);
}

static void
_mcd_dispatcher_dispose (GObject * object)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (object);
    
    if (priv->is_disposed)
    {
	return;
    }
    priv->is_disposed = TRUE;

    g_list_foreach (priv->clients, (GFunc)mcd_client_free, NULL);

    if (priv->master)
    {
	g_object_unref (priv->master);
	priv->master = NULL;
    }

    if (priv->dbus_daemon)
    {
	g_object_unref (priv->dbus_daemon);
	priv->dbus_daemon = NULL;
    }

    if (priv->channels)
    {
	g_list_free (priv->channels);
	priv->channels = NULL;
    }

    if (priv->interface_filters)
    {
	g_datalist_clear (&priv->interface_filters);
	priv->interface_filters = NULL;
    }
    G_OBJECT_CLASS (mcd_dispatcher_parent_class)->dispose (object);
}

gboolean
mcd_dispatcher_send (McdDispatcher * dispatcher, McdChannel *channel)
{
    g_return_val_if_fail (MCD_IS_DISPATCHER (dispatcher), FALSE);
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), FALSE);
    MCD_DISPATCHER_GET_CLASS (dispatcher)->send (dispatcher, channel);
    return TRUE;
}

static McdClientFilter *
parse_client_filter (GKeyFile *file, const gchar *group)
{
    McdClientFilter *filter;
    gchar **keys;
    gsize len = 0;
    guint i;

    filter = g_slice_new0 (McdClientFilter);

    keys = g_key_file_get_keys (file, group, &len, NULL);
    for (i = 0; i < len; i++)
    {
        const gchar *key;

        key = keys[i];
        if (strcmp (key, TP_IFACE_CHANNEL ".Type s") == 0)
        {
            gchar *string;

            string = g_key_file_get_string (file, group, key, NULL);
            filter->channel_type = g_quark_from_string (string);
            g_free (string);

            filter->field_mask |= MCD_FILTER_CHANNEL_TYPE;
        }
        else if (strcmp (key, TP_IFACE_CHANNEL ".TargetHandleType u") == 0)
        {
            filter->handle_type =
                (guint) g_key_file_get_integer (file, group, key, NULL);
            filter->field_mask |= MCD_FILTER_HANDLE_TYPE;
        }
        else if (strcmp (key, TP_IFACE_CHANNEL ".Requested b") == 0)
        {
            filter->requested =
                g_key_file_get_boolean (file, group, key, NULL);
            filter->field_mask |= MCD_FILTER_REQUESTED;
        }
        else
        {
            g_warning ("Invalid key %s in client file", key);
            continue;
        }
    }
    g_strfreev (keys);

    return filter;
}

static void
create_client_proxy (McdDispatcherPrivate *priv, McdClient *client)
{
    gchar *bus_name, *object_path;

    bus_name = g_strconcat ("org.freedesktop.Telepathy.Client.",
                            client->name, NULL);
    object_path = g_strconcat ("/org/freedesktop/Telepathy/Client/",
                               client->name, NULL);
    client->proxy = g_object_new (TP_TYPE_PROXY,
                                  "dbus-daemon", priv->dbus_daemon,
                                  "object-path", object_path,
                                  "bus-name", bus_name,
                                  NULL);
    g_free (object_path);
    g_free (bus_name);

    tp_proxy_add_interface_by_id (client->proxy, MC_IFACE_QUARK_CLIENT);
    if (client->interfaces & MCD_CLIENT_APPROVER)
        tp_proxy_add_interface_by_id (client->proxy,
                                      MC_IFACE_QUARK_CLIENT_APPROVER);
    if (client->interfaces & MCD_CLIENT_HANDLER)
        tp_proxy_add_interface_by_id (client->proxy,
                                      MC_IFACE_QUARK_CLIENT_HANDLER);
    if (client->interfaces & MCD_CLIENT_OBSERVER)
        tp_proxy_add_interface_by_id (client->proxy,
                                      MC_IFACE_QUARK_CLIENT_OBSERVER);

    priv->clients = g_list_prepend (priv->clients, client);
}

static gboolean
parse_client_file (const gchar *path, const gchar *filename,
                   gpointer user_data)
{
    McdDispatcherPrivate *priv = user_data;
    GKeyFile *file;
    const gchar *extension;
    gchar **iface_names, **groups;
    McdClient *client;
    guint i;
    gsize len = 0;
    GError *error = NULL;

    extension = g_strrstr (filename, ".client");
    if (!extension || extension[7] != 0) return TRUE;

    file = g_key_file_new ();
    g_key_file_load_from_file (file, path, 0, &error);
    if (error)
    {
        g_warning ("Error loading %s: %s", path, error->message);
        goto finish;
    }

    iface_names = g_key_file_get_string_list (file, MCD_IFACE_CLIENT,
                                              "Interfaces", 0, NULL);
    if (!iface_names)
        goto finish;

    client = g_slice_new0 (McdClient);
    for (i = 0; iface_names[i] != NULL; i++)
    {
        if (strcmp (iface_names[i], MCD_IFACE_CLIENT_APPROVER) == 0)
            client->interfaces |= MCD_CLIENT_APPROVER;
        else if (strcmp (iface_names[i], MCD_IFACE_CLIENT_HANDLER) == 0)
            client->interfaces |= MCD_CLIENT_HANDLER;
        else if (strcmp (iface_names[i], MCD_IFACE_CLIENT_OBSERVER) == 0)
            client->interfaces |= MCD_CLIENT_OBSERVER;
    }
    g_strfreev (iface_names);

    /* parse filtering rules */
    groups = g_key_file_get_groups (file, &len);
    for (i = 0; i < len; i++)
    {
        if (client->interfaces & MCD_CLIENT_APPROVER &&
            g_str_has_prefix (groups[i], MCD_IFACE_CLIENT_APPROVER
                              ".ApproverChannelFilter "))
        {
            client->approver_filters =
                g_list_prepend (client->approver_filters,
                                parse_client_filter (file, groups[i]));
        }
        else if (client->interfaces & MCD_CLIENT_HANDLER &&
            g_str_has_prefix (groups[i], MCD_IFACE_CLIENT_HANDLER
                              ".HandlerChannelFilter "))
        {
            client->handler_filters =
                g_list_prepend (client->handler_filters,
                                parse_client_filter (file, groups[i]));
        }
        else if (client->interfaces & MCD_CLIENT_OBSERVER &&
            g_str_has_prefix (groups[i], MCD_IFACE_CLIENT_OBSERVER
                              ".ObserverChannelFilter "))
        {
            client->observer_filters =
                g_list_prepend (client->observer_filters,
                                parse_client_filter (file, groups[i]));
        }
    }
    g_strfreev (groups);

    /* Other client options */
    client->bypass_approver =
        g_key_file_get_boolean (file, MCD_IFACE_CLIENT_HANDLER,
                                "BypassApprover", NULL);

    client->name = g_strndup (filename, extension - filename);
    g_debug ("%s: adding client %s from .client file",
             G_STRFUNC, client->name);

    create_client_proxy (priv, client);

finish:
    g_key_file_free (file);
    return TRUE;
}

static void
mcd_dispatcher_constructed (GObject *object)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (object);

    _mcd_xdg_data_subdir_foreach ("telepathy/clients",
                                  parse_client_file, priv);
}

static void
mcd_dispatcher_class_init (McdDispatcherClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (McdDispatcherPrivate));

    object_class->constructed = mcd_dispatcher_constructed;
    object_class->set_property = _mcd_dispatcher_set_property;
    object_class->get_property = _mcd_dispatcher_get_property;
    object_class->finalize = _mcd_dispatcher_finalize;
    object_class->dispose = _mcd_dispatcher_dispose;
    klass->send = _mcd_dispatcher_send;

    mcd_dispatcher_signals[CHANNEL_ADDED] =
	g_signal_new ("channel_added",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      G_STRUCT_OFFSET (McdDispatcherClass,
				       channel_added_signal),
		      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE, 1, MCD_TYPE_CHANNEL);
    
    mcd_dispatcher_signals[CHANNEL_REMOVED] =
	g_signal_new ("channel_removed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      G_STRUCT_OFFSET (McdDispatcherClass,
				       channel_removed_signal),
		      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE, 1, MCD_TYPE_CHANNEL);
    
    mcd_dispatcher_signals[DISPATCHED] =
	g_signal_new ("dispatched",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      G_STRUCT_OFFSET (McdDispatcherClass,
				       dispatched_signal),
		      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE, 1, MCD_TYPE_CHANNEL);
    
    mcd_dispatcher_signals[DISPATCH_FAILED] =
	g_signal_new ("dispatch-failed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      G_STRUCT_OFFSET (McdDispatcherClass,
				       dispatch_failed_signal),
		      NULL, NULL, _mcd_marshal_VOID__OBJECT_POINTER,
		      G_TYPE_NONE, 2, MCD_TYPE_CHANNEL, G_TYPE_POINTER);
    
    /* Properties */
    g_object_class_install_property (object_class,
				     PROP_DBUS_DAEMON,
				     g_param_spec_object ("dbus-daemon",
							  _("DBus daemon"),
							  _("DBus daemon"),
							  TP_TYPE_DBUS_DAEMON,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT));
    g_object_class_install_property (object_class,
				     PROP_MCD_MASTER,
				     g_param_spec_object ("mcd-master",
							   _("McdMaster"),
							   _("McdMaster"),
							   MCD_TYPE_MASTER,
							   G_PARAM_READWRITE |
							   G_PARAM_CONSTRUCT));
}

static void
_build_channel_capabilities (gchar *channel_type, McdChannelHandler *handler,
			     GPtrArray *capabilities)
{
    GValue cap = {0,};
    GType cap_type;

    cap_type = dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING,
				       G_TYPE_UINT, G_TYPE_INVALID);
    g_value_init (&cap, cap_type);
    g_value_take_boxed (&cap, dbus_g_type_specialized_construct (cap_type));

    dbus_g_type_struct_set (&cap,
			    0, channel_type,
			    1, handler->capabilities,
			    G_MAXUINT);

    g_ptr_array_add (capabilities, g_value_get_boxed (&cap));
}


static void
_channel_capabilities (gchar *ctype, GHashTable *channel_handler,
		       McdDispatcherArgs *args)
{
    McdChannelHandler *handler;

    handler = g_hash_table_lookup (channel_handler, args->protocol);

    if (!handler)
	handler = g_hash_table_lookup (channel_handler, "default");

    _build_channel_capabilities (ctype, handler, args->channel_handler_caps);
}

static void
mcd_dispatcher_init (McdDispatcher * dispatcher)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (dispatcher);

    g_datalist_init (&(priv->interface_filters));
    
    priv->channel_handler_hash = mcd_get_channel_handlers ();
}

McdDispatcher *
mcd_dispatcher_new (TpDBusDaemon *dbus_daemon, McdMaster *master)
{
    McdDispatcher *obj;
    obj = MCD_DISPATCHER (g_object_new (MCD_TYPE_DISPATCHER,
					"dbus-daemon", dbus_daemon,
					"mcd-master", master, 
					NULL));
    return obj;
}

/* The new state machine walker function for pluginized filters*/

void
mcd_dispatcher_context_process (McdDispatcherContext * context, gboolean result)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (context->dispatcher);
    
    if (result)
    {
	McdFilter *filter;

	filter = g_list_nth_data (context->chain, context->next_func_index);
	/* Do we still have functions to go through? */
	if (filter)
	{
	    context->next_func_index++;
	    
	    g_debug ("Next filter");
	    filter->func (context, filter->user_data);
	    return; /*State machine goes on...*/
	}
	else
	{
	    /* Context would be destroyed somewhere in this call */
	    _mcd_dispatcher_start_channel_handler (context);
	}
    }
    else
    {
	g_debug ("Filters failed, disposing request");
	
	/* Some filter failed. The request shall not be handled. */
	/* Context would be destroyed somewhere in this call */
	_mcd_dispatcher_drop_channel_handler (context);
    }
    
    /* FIXME: Should we remove the request in other cases? */
    priv->state_machine_list =
	g_slist_remove(priv->state_machine_list, context);
}

static void
mcd_dispatcher_context_free (McdDispatcherContext * context)
{
    /* FIXME: check for leaks */
    g_return_if_fail (context);

    /* Freeing context data */
    if (context->channel)
    {
	g_signal_handlers_disconnect_by_func (context->channel,
					      G_CALLBACK (on_channel_abort_context),
					      context);
	g_object_unref (context->channel);
    }
    g_free (context->protocol);
    g_free (context);
}

/* CONTEXT API */

/* Context getters */
TpChannel *
mcd_dispatcher_context_get_channel_object (McdDispatcherContext * ctx)
{
    TpChannel *tp_chan;
    g_return_val_if_fail (ctx, 0);
    g_object_get (G_OBJECT (ctx->channel), "tp-channel", &tp_chan, NULL);
    g_object_unref (G_OBJECT (tp_chan));
    return tp_chan;
}

McdDispatcher*
mcd_dispatcher_context_get_dispatcher (McdDispatcherContext * ctx)
{
    return ctx->dispatcher;
}

McdConnection *
mcd_dispatcher_context_get_connection (McdDispatcherContext * context)
{
    McdConnection *connection;

    g_return_val_if_fail (context, NULL);

    g_object_get (G_OBJECT (context->channel),
		  "connection", &connection,
		  NULL);
    g_object_unref (G_OBJECT (connection));

    return connection;
}

TpConnection *
mcd_dispatcher_context_get_connection_object (McdDispatcherContext * ctx)
{
    const McdConnection *connection;
    TpConnection *tp_conn;
   
    connection = mcd_dispatcher_context_get_connection (ctx); 
    g_object_get (G_OBJECT (connection), "tp-connection",
		  &tp_conn, NULL);
   
    g_object_unref (tp_conn); 
    return tp_conn;
}

McdChannel *
mcd_dispatcher_context_get_channel (McdDispatcherContext * ctx)
{
    g_return_val_if_fail (ctx, 0);
    return ctx->channel;
}

McdChannelHandler *
mcd_dispatcher_context_get_chan_handler (McdDispatcherContext * ctx)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (ctx->dispatcher);
    McdChannel *channel;
    const gchar *protocol;
    McdChannelHandler *chandler;
    GHashTable *channel_handler;
    
    channel = mcd_dispatcher_context_get_channel (ctx);
    protocol = mcd_dispatcher_context_get_protocol_name (ctx);

    channel_handler =
	g_hash_table_lookup (priv->channel_handler_hash,
			     mcd_channel_get_channel_type (channel));

    chandler =  g_hash_table_lookup (channel_handler, protocol);
    if (!chandler)
        chandler =  g_hash_table_lookup (channel_handler, "default");

    return chandler;
     
}

/*Returns an array of the participants in the channel*/
GPtrArray *
mcd_dispatcher_context_get_members (McdDispatcherContext * ctx)
{
    return mcd_channel_get_members (ctx->channel);
}

GPtrArray *mcd_dispatcher_get_channel_capabilities (McdDispatcher * dispatcher,
						    const gchar *protocol)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (dispatcher);
    McdDispatcherArgs args;

    args.dispatcher = dispatcher;
    args.protocol = protocol;
    args.channel_handler_caps = g_ptr_array_new ();

    g_hash_table_foreach (priv->channel_handler_hash,
			  (GHFunc)_channel_capabilities,
			  &args);

    return args.channel_handler_caps;
}

const gchar *
mcd_dispatcher_context_get_protocol_name (McdDispatcherContext *context)
{
    McdConnection *conn;
    McdAccount *account;

    if (!context->protocol)
    {
	conn = mcd_dispatcher_context_get_connection (context);
	account = mcd_connection_get_account (conn);
	context->protocol = g_strdup (mcd_account_get_protocol_name (account));
    }
    
    return context->protocol;
}

