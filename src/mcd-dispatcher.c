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
#include <errno.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gi18n.h>

#include <dbus/dbus-glib-lowlevel.h>

#include "mcd-signals-marshal.h"
#include "mcd-account-priv.h"
#include "mcd-client-priv.h"
#include "mcd-connection.h"
#include "mcd-connection-priv.h"
#include "mcd-channel.h"
#include "mcd-master.h"
#include "mcd-channel-priv.h"
#include "mcd-dispatcher-context.h"
#include "mcd-dispatcher-priv.h"
#include "mcd-dispatch-operation.h"
#include "mcd-dispatch-operation-priv.h"
#include "mcd-handler-map-priv.h"
#include "mcd-misc.h"

#include <telepathy-glib/defs.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/svc-channel-dispatcher.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>

#include <libmcclient/mc-errors.h>

#include <stdlib.h>
#include <string.h>
#include "sp_timestamp.h"

#define MCD_DISPATCHER_PRIV(dispatcher) (MCD_DISPATCHER (dispatcher)->priv)

static void dispatcher_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (McdDispatcher, mcd_dispatcher, MCD_TYPE_MISSION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_DISPATCHER,
                           dispatcher_iface_init);
    G_IMPLEMENT_INTERFACE (
        TP_TYPE_SVC_CHANNEL_DISPATCHER_INTERFACE_OPERATION_LIST,
        NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
                           tp_dbus_properties_mixin_iface_init))

struct _McdDispatcherContext
{
    gint ref_count;

    guint finished : 1;

    /* If this flag is TRUE, dispatching must be cancelled ASAP */
    guint cancelled : 1;

    /* This is set to TRUE if the incoming channel being dispatched has being
     * requested before the approvers could be run; in that case, the approval
     * phase should be skipped */
    guint skip_approval : 1;

    McdDispatcher *dispatcher;

    GList *channels;
    McdChannel *main_channel;
    McdAccount *account;
    McdDispatchOperation *operation;
    /* bus names (including the common prefix) in preference order */
    GStrv possible_handlers;

    /* This variable is the count of locks that must be removed before handlers
     * can be invoked. Each call to an observer increments this count (and
     * decrements it on return), and for unrequested channels we have an
     * approver lock, too.
     * When the variable gets back to 0, handlers are run. */
    gint client_locks;

    /* Number of approvers that we invoked */
    gint approvers_invoked;

    gchar *protocol;

    /* State-machine internal data fields: */
    GList *chain;

    /* Next function in chain */
    guint next_func_index;
};

typedef struct
{
    McdDispatcher *dispatcher;
    McdChannel *channel;
    gint handler_locks;
    gboolean handled;
} McdChannelRecover;

typedef enum
{
    MCD_CLIENT_APPROVER = 0x1,
    MCD_CLIENT_HANDLER  = 0x2,
    MCD_CLIENT_OBSERVER = 0x4,
    MCD_CLIENT_INTERFACE_REQUESTS  = 0x8,
} McdClientInterface;

typedef struct _McdClient
{
    TpClient *proxy;
    gchar *name;
    McdClientInterface interfaces;
    guint bypass_approver : 1;

    /* If a client was in the ListActivatableNames list, it must not be
     * removed when it disappear from the bus.
     */
    guint activatable : 1;
    guint active : 1;

    /* Channel filters
     * A channel filter is a GHashTable of
     * - key: gchar *property_name
     * - value: GValue of one of the allowed types on the ObserverChannelFilter
     *          spec. The following matching is observed:
     *           * G_TYPE_STRING: 's'
     *           * G_TYPE_BOOLEAN: 'b'
     *           * DBUS_TYPE_G_OBJECT_PATH: 'o'
     *           * G_TYPE_UINT64: 'y' (8b), 'q' (16b), 'u' (32b), 't' (64b)
     *           * G_TYPE_INT64:            'n' (16b), 'i' (32b), 'x' (64b)
     *
     * The list can be NULL if there is no filter, or the filters are not yet
     * retrieven from the D-Bus *ChannelFitler properties. In the last case,
     * the dispatcher just don't dispatch to this client.
     */
    GList *approver_filters;
    GList *handler_filters;
    GList *observer_filters;
} McdClient;

struct _McdDispatcherPrivate
{
    /* Dispatching contexts */
    GList *contexts;

    TpDBusDaemon *dbus_daemon;

    /* Array of channel handler's capabilities, stored as a GPtrArray for
     * performance reasons */
    GPtrArray *channel_handler_caps;

    /* list of McdFilter elements */
    GList *filters;

    /* hash table containing clients
     * char *bus_name -> McdClient */
    GHashTable *clients;

    McdHandlerMap *handler_map;

    McdMaster *master;

    /* We don't want to start dispatching until startup has finished. This
     * is defined as:
     * - activatable clients have been enumerated (ListActivatableNames)
     *   (1 lock)
     * - running clients have been enumerated (ListNames) (1 lock)
     * - each client found that way has been inspected (1 lock per client
     *   for Interfaces, + 1 lock per client per subsequent Get/GetAll call)
     * When nothing more is stopping us from dispatching channels, we start to
     * do so.
     * */
    gsize startup_lock;
    gboolean startup_completed;

    /* connection => itself, borrowed */
    GHashTable *connections;

    /* Initially FALSE, meaning we suppress OperationList.DispatchOperations
     * change notification signals because nobody has retrieved that property
     * yet. Set to TRUE the first time someone reads the DispatchOperations
     * property. */
    gboolean operation_list_active;

    gboolean is_disposed;
    
};

struct cancel_call_data
{
    DBusGProxy *handler_proxy;
    DBusGProxyCall *call;
    McdDispatcher *dispatcher;
};

typedef struct
{
    McdDispatcherContext *context;
    GList *channels;
} McdHandlerCallData;

typedef struct
{
    TpClient *handler;
    gchar *request_path;
} McdRemoveRequestData;

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
    PROP_MCD_MASTER,
    PROP_INTERFACES,
    PROP_DISPATCH_OPERATIONS,
};

enum _McdDispatcherSignalType
{
    CHANNEL_ADDED,
    CHANNEL_REMOVED,
    DISPATCHED,
    DISPATCH_FAILED,
    DISPATCH_COMPLETED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
static GQuark client_ready_quark = 0;

static void mcd_dispatcher_context_unref (McdDispatcherContext * ctx,
                                          const gchar *tag);
static void on_operation_finished (McdDispatchOperation *operation,
                                   McdDispatcherContext *context);


static inline void
mcd_dispatcher_context_ref (McdDispatcherContext *context,
                            const gchar *tag)
{
    g_return_if_fail (context != NULL);
    DEBUG ("%s on %p (ref = %d)", tag, context, context->ref_count);
    context->ref_count++;
}

static void
mcd_handler_call_data_free (McdHandlerCallData *call_data)
{
    DEBUG ("called");
    mcd_dispatcher_context_unref (call_data->context, "CTXREF03");
    g_list_free (call_data->channels);
    g_slice_free (McdHandlerCallData, call_data);
}

/*
 * mcd_dispatcher_context_handler_done:
 * @context: the #McdDispatcherContext.
 * 
 * Called to informs the @context that handling of a channel is completed,
 * either because a channel handler has returned from the HandleChannel(s)
 * call, or because there was an error in calling the handler. 
 * This function checks the status of all the channels in @context, and when
 * there is nothing left to do (either because all channels are dispatched, or
 * because it's impossible to dispatch them) it emits the "dispatch-completed"
 * signal and destroys the @context.
 */
static void
mcd_dispatcher_context_handler_done (McdDispatcherContext *context)
{
    if (context->finished)
    {
        DEBUG ("context %p is already finished", context);
        return;
    }

    context->finished = TRUE;
    g_signal_emit (context->dispatcher,
                   signals[DISPATCH_COMPLETED], 0, context);
}

static void
mcd_client_free (McdClient *client)
{
    if (client->proxy)
    {
        GError error = { TP_DBUS_ERRORS,
            TP_DBUS_ERROR_NAME_OWNER_LOST, "Client disappeared" };

        _mcd_object_ready (client->proxy, client_ready_quark, &error);

        g_object_unref (client->proxy);
    }

    g_free (client->name);

    g_list_foreach (client->approver_filters,
                    (GFunc)g_hash_table_destroy, NULL);
    g_list_free (client->approver_filters);
    client->approver_filters = NULL;

    g_list_foreach (client->handler_filters,
                    (GFunc)g_hash_table_destroy, NULL);
    g_list_free (client->handler_filters);
    client->handler_filters = NULL;

    g_list_foreach (client->observer_filters,
                    (GFunc)g_hash_table_destroy, NULL);
    g_list_free (client->observer_filters);
    client->observer_filters = NULL;

    g_slice_free (McdClient, client);
}

static GList *
chain_add_filter (GList *chain,
		  McdFilterFunc filter,
		  guint priority,
                  gpointer user_data)
{
    GList *elem;
    McdFilter *filter_data;

    filter_data = g_slice_new (McdFilter);
    filter_data->func = filter;
    filter_data->priority = priority;
    filter_data->user_data = user_data;
    for (elem = chain; elem; elem = elem->next)
	if (((McdFilter *)elem->data)->priority >= priority) break;

    return g_list_insert_before (chain, elem, filter_data);
}

/* Returns # of times particular channel type  has been used */
gint
mcd_dispatcher_get_channel_type_usage (McdDispatcher * dispatcher,
				       GQuark chan_type_quark)
{
    const GList *managers, *connections, *channels;
    McdDispatcherPrivate *priv = dispatcher->priv;
    gint usage_counter = 0;

    managers = mcd_operation_get_missions (MCD_OPERATION (priv->master));
    while (managers)
    {
        connections =
            mcd_operation_get_missions (MCD_OPERATION (managers->data));
        while (connections)
        {
            channels =
                mcd_operation_get_missions (MCD_OPERATION (connections->data));
            while (channels)
            {
                McdChannel *channel = MCD_CHANNEL (channels->data);
                McdChannelStatus status;

                status = mcd_channel_get_status (channel);
                if ((status == MCD_CHANNEL_STATUS_DISPATCHING ||
                     status == MCD_CHANNEL_STATUS_HANDLER_INVOKED ||
                     status == MCD_CHANNEL_STATUS_DISPATCHED) &&
                    mcd_channel_get_channel_type_quark (channel) ==
                    chan_type_quark)
                    usage_counter++;
                channels = channels->next;
            }
            connections = connections->next;
        }
	managers = managers->next;
    }

    return usage_counter;
}

static void
on_master_abort (McdMaster *master, McdDispatcherPrivate *priv)
{
    g_object_unref (master);
    priv->master = NULL;
}

/* returns TRUE if the channel matches one property criteria
 */
static gboolean
match_property (GHashTable *channel_properties,
                gchar *property_name,
                GValue *filter_value)
{
    GType filter_type = G_VALUE_TYPE (filter_value);

    g_assert (G_IS_VALUE (filter_value));

    if (filter_type == G_TYPE_STRING)
    {
        const gchar *string;

        string = tp_asv_get_string (channel_properties, property_name);
        if (!string)
            return FALSE;

        return !tp_strdiff (string, g_value_get_string (filter_value));
    }

    if (filter_type == DBUS_TYPE_G_OBJECT_PATH)
    {
        const gchar *path;

        path = tp_asv_get_object_path (channel_properties, property_name);
        if (!path)
            return FALSE;

        return !tp_strdiff (path, g_value_get_boxed (filter_value));
    }

    if (filter_type == G_TYPE_BOOLEAN)
    {
        gboolean valid;
        gboolean b;

        b = tp_asv_get_boolean (channel_properties, property_name, &valid);
        if (!valid)
            return FALSE;

        return !!b == !!g_value_get_boolean (filter_value);
    }

    if (filter_type == G_TYPE_UCHAR || filter_type == G_TYPE_UINT ||
        filter_type == G_TYPE_UINT64)
    {
        gboolean valid;
        guint64 i;

        i = tp_asv_get_uint64 (channel_properties, property_name, &valid);
        if (!valid)
            return FALSE;

        if (filter_type == G_TYPE_UCHAR)
            return i == g_value_get_uchar (filter_value);
        else if (filter_type == G_TYPE_UINT)
            return i == g_value_get_uint (filter_value);
        else
            return i == g_value_get_uint64 (filter_value);
    }

    if (filter_type == G_TYPE_INT || filter_type == G_TYPE_INT64)
    {
        gboolean valid;
        gint64 i;

        i = tp_asv_get_int64 (channel_properties, property_name, &valid);
        if (!valid)
            return FALSE;

        if (filter_type == G_TYPE_INT)
            return i == g_value_get_int (filter_value);
        else
            return i == g_value_get_int64 (filter_value);
    }

    g_warning ("%s: Invalid type: %s",
               G_STRFUNC, g_type_name (filter_type));
    return FALSE;
}

/* return TRUE if the two channel classes are equals
 */
static gboolean
channel_classes_equals (GHashTable *channel_class1, GHashTable *channel_class2)
{
    GHashTableIter iter;
    gchar *property_name;
    GValue *property_value;

    if (g_hash_table_size (channel_class1) !=
        g_hash_table_size (channel_class2))
        return FALSE;

    g_hash_table_iter_init (&iter, channel_class1);
    while (g_hash_table_iter_next (&iter, (gpointer *) &property_name,
                                   (gpointer *) &property_value))
    {
        if (!match_property (channel_class2, property_name, property_value))
            return FALSE;
    }
    return TRUE;
}

/* if the channel matches one of the channel filters, returns a positive
 * number that increases with more specific matches; otherwise, returns 0
 *
 * (implementation detail: the positive number is 1 + the number of keys in the
 * largest filter that matched)
 */
static guint
match_filters (McdChannel *channel, GList *filters)
{
    GHashTable *channel_properties;
    McdChannelStatus status;
    GList *list;
    guint best_quality = 0;

    status = mcd_channel_get_status (channel);
    channel_properties =
        (status == MCD_CHANNEL_STATUS_REQUEST ||
         status == MCD_CHANNEL_STATUS_REQUESTED) ?
        _mcd_channel_get_requested_properties (channel) :
        _mcd_channel_get_immutable_properties (channel);

    for (list = filters; list != NULL; list = list->next)
    {
        GHashTable *filter = list->data;
        GHashTableIter filter_iter;
        gboolean filter_matched = TRUE;
        gchar *property_name;
        GValue *filter_value;
        guint quality;

        /* +1 because the empty hash table matches everything :-) */
        quality = g_hash_table_size (filter) + 1;

        if (quality <= best_quality)
        {
            /* even if this filter matches, there's no way it can be a
             * better-quality match than the best one we saw so far */
            continue;
        }

        g_hash_table_iter_init (&filter_iter, filter);
        while (g_hash_table_iter_next (&filter_iter,
                                       (gpointer *) &property_name,
                                       (gpointer *) &filter_value))
        {
            if (! match_property (channel_properties, property_name,
                                filter_value))
            {
                filter_matched = FALSE;
                break;
            }
        }

        if (filter_matched)
        {
            best_quality = quality;
        }
    }

    return best_quality;
}

static McdClient *
get_default_handler (McdDispatcher *dispatcher, McdChannel *channel)
{
    GHashTableIter iter;
    McdClient *client;

    g_hash_table_iter_init (&iter, dispatcher->priv->clients);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &client))
    {
        if (!client->proxy ||
            !(client->interfaces & MCD_CLIENT_HANDLER))
            continue;

        if (match_filters (channel, client->handler_filters) > 0)
            return client;
    }
    return NULL;
}

static void
mcd_dispatcher_set_channel_handled_by (McdDispatcher *self,
                                       McdChannel *channel,
                                       const gchar *unique_name)
{
    const gchar *path;

    g_assert (unique_name != NULL);

    path = mcd_channel_get_object_path (channel);

    _mcd_channel_set_status (channel, MCD_CHANNEL_STATUS_DISPATCHED);

    _mcd_handler_map_set_channel_handled (self->priv->handler_map,
                                          channel, unique_name);

    g_signal_emit_by_name (self, "dispatched", channel);
}

static void
handle_channels_cb (TpClient *proxy, const GError *error, gpointer user_data,
                    GObject *weak_object)
{
    McdHandlerCallData *call_data = user_data;
    McdDispatcherContext *context = call_data->context;
    GList *list;

    mcd_dispatcher_context_ref (context, "CTXREF02");
    if (error)
    {
        GError *mc_error = NULL;

        g_warning ("%s got error: %s", G_STRFUNC, error->message);

        /* We can't reliably map channel handler error codes to MC error
         * codes. So just using generic error message.
         */
        mc_error = g_error_new (MC_ERROR, MC_CHANNEL_REQUEST_GENERIC_ERROR,
                                "Handle channel failed: %s", error->message);

        for (list = call_data->channels; list != NULL; list = list->next)
        {
            McdChannel *channel = list->data;

            /* if the channel is no longer in the context, don't even try to
             * access it */
            if (!g_list_find (context->channels, channel))
                continue;

            mcd_channel_take_error (channel, g_error_copy (mc_error));
            g_signal_emit_by_name (context->dispatcher, "dispatch-failed",
                                   channel, mc_error);

            /* FIXME: try to dispatch the channels to another handler, instead
             * of just destroying them? */
            _mcd_channel_undispatchable (channel);
        }
        g_error_free (mc_error);
    }
    else
    {
        for (list = call_data->channels; list != NULL; list = list->next)
        {
            McdChannel *channel = list->data;
            const gchar *unique_name;

            /* if the channel is no longer in the context, don't even try to
             * access it */
            if (!g_list_find (context->channels, channel))
                continue;

            unique_name = _mcd_client_proxy_get_unique_name (MCD_CLIENT_PROXY (proxy));

            /* This should always be false in practice - either we already know
             * the handler's unique name (because active handlers' unique names
             * are discovered before their handler filters), or the handler
             * is activatable and was not running, the handler filter came
             * from a .client file, and the bus daemon activated the handler
             * as a side-effect of HandleChannels (in which case
             * NameOwnerChanged should have already been emitted by the time
             * we got a reply to HandleChannels).
             *
             * We recover by whining to stderr and closing the channels, in the
             * interests of at least failing visibly.
             *
             * If dbus-glib exposed more of the details of the D-Bus message
             * passing system, then we could just look at the sender of the
             * reply and bypass this rubbish...
             */
            if (G_UNLIKELY (unique_name == NULL || unique_name[0] == '\0'))
            {
                g_warning ("Client %s returned successfully but doesn't "
                           "exist? dbus-daemon bug suspected",
                           tp_proxy_get_bus_name (proxy));
                g_warning ("Closing channel %s as a result",
                           mcd_channel_get_object_path (channel));
                _mcd_channel_undispatchable (channel);
                continue;
            }

            mcd_dispatcher_set_channel_handled_by (context->dispatcher,
                                                   channel, unique_name);
        }
    }

    mcd_dispatcher_context_handler_done (context);
    mcd_dispatcher_context_unref (context, "CTXREF02");
}

typedef struct
{
    McdClient *client;
    gsize quality;
} PossibleHandler;

static gint
possible_handler_cmp (gconstpointer a_,
                      gconstpointer b_)
{
    const PossibleHandler *a = a_;
    const PossibleHandler *b = b_;

    if (a->client->bypass_approver)
    {
        if (!b->client->bypass_approver)
        {
            /* BypassApproval wins, so a is better than b */
            return 1;
        }
    }
    else if (b->client->bypass_approver)
    {
        /* BypassApproval wins, so b is better than a */
        return -1;
    }

    if (a->quality < b->quality)
    {
        return -1;
    }

    if (b->quality < a->quality)
    {
        return 1;
    }

    return 0;
}

static GStrv
mcd_dispatcher_get_possible_handlers (McdDispatcher *self,
                                      const GList *channels)
{
    GList *handlers = NULL;
    const GList *iter;
    GHashTableIter client_iter;
    gpointer client_p;
    guint n_handlers = 0;
    guint i;
    GStrv ret;

    g_hash_table_iter_init (&client_iter, self->priv->clients);

    while (g_hash_table_iter_next (&client_iter, NULL, &client_p))
    {
        McdClient *client = client_p;
        gsize total_quality = 0;

        if (client->proxy == NULL ||
            !(client->interfaces & MCD_CLIENT_HANDLER))
        {
            /* not a handler at all */
            continue;
        }

        for (iter = channels; iter != NULL; iter = iter->next)
        {
            McdChannel *channel = MCD_CHANNEL (iter->data);
            guint quality;

            quality = match_filters (channel, client->handler_filters);

            if (quality == 0)
            {
                total_quality = 0;
                break;
            }
            else
            {
                total_quality += quality;
            }
        }

        if (total_quality > 0)
        {
            PossibleHandler *ph = g_slice_new0 (PossibleHandler);

            ph->client = client;
            ph->quality = total_quality;

            handlers = g_list_prepend (handlers, ph);
            n_handlers++;
        }
    }

    /* if no handlers can take them all, fail */
    if (handlers == NULL)
    {
        return NULL;
    }

    /* We have at least one handler that can take the whole batch. Sort
     * the possible handlers, most preferred first (i.e. sort by ascending
     * quality then reverse) */
    handlers = g_list_sort (handlers, possible_handler_cmp);
    handlers = g_list_reverse (handlers);

    ret = g_new0 (gchar *, n_handlers + 1);

    for (iter = handlers, i = 0; iter != NULL; iter = iter->next, i++)
    {
        PossibleHandler *ph = iter->data;

        ret[i] = g_strconcat (TP_CLIENT_BUS_NAME_BASE,
                              ph->client->name, NULL);
        g_slice_free (PossibleHandler, ph);
    }

    ret[n_handlers] = NULL;

    g_list_free (handlers);

    return ret;
}

/*
 * mcd_dispatcher_handle_channels:
 * @context: the #McdDispatcherContext
 * @channels: a #GList of borrowed refs to #McdChannel objects, ownership of
 *  which is stolen by this function
 * @handler: the selected handler
 *
 * Invoke the handler for the given channels.
 */
static void
mcd_dispatcher_handle_channels (McdDispatcherContext *context,
                                GList *channels,
                                McdClient *handler)
{
    guint64 user_action_time;
    McdConnection *connection;
    const gchar *account_path, *connection_path;
    GPtrArray *channels_array, *satisfied_requests;
    McdHandlerCallData *handler_data;
    GHashTable *handler_info;
    const GList *cl;

    connection = mcd_dispatcher_context_get_connection (context);
    connection_path = connection ?
        mcd_connection_get_object_path (connection) : NULL;
    if (G_UNLIKELY (!connection_path)) connection_path = "/";

    g_assert (context->account != NULL);
    account_path = mcd_account_get_object_path (context->account);
    if (G_UNLIKELY (!account_path)) account_path = "/";

    channels_array = _mcd_channel_details_build_from_list (channels);

    user_action_time = 0; /* TODO: if we have a CDO, get it from there */
    satisfied_requests = g_ptr_array_new ();
    for (cl = channels; cl != NULL; cl = cl->next)
    {
        McdChannel *channel = MCD_CHANNEL (cl->data);
        const GList *requests;
        guint64 user_time;

        requests = _mcd_channel_get_satisfied_requests (channel);
        while (requests)
        {
            g_ptr_array_add (satisfied_requests, requests->data);
            requests = requests->next;
        }

        /* FIXME: what if we have more than one request? */
        user_time = _mcd_channel_get_request_user_action_time (channel);
        if (user_time)
            user_action_time = user_time;

        _mcd_channel_set_status (channel,
                                 MCD_CHANNEL_STATUS_HANDLER_INVOKED);
    }

    handler_info = g_hash_table_new (g_str_hash, g_str_equal);

    /* The callback needs to get the dispatcher context, and the channels
     * the handler was asked to handle. The context will keep track of how
     * many channels are still to be dispatched,
     * still pending. When all of them return, the dispatching is
     * considered to be completed. */
    handler_data = g_slice_new (McdHandlerCallData);
    handler_data->context = context;
    mcd_dispatcher_context_ref (context, "CTXREF03");
    handler_data->channels = channels;
    DEBUG ("calling HandleChannels on %s for context %p", handler->name,
           context);
    tp_cli_client_handler_call_handle_channels (handler->proxy, -1,
        account_path, connection_path,
        channels_array, satisfied_requests, user_action_time,
        handler_info, handle_channels_cb,
        handler_data, (GDestroyNotify)mcd_handler_call_data_free,
        (GObject *)context->dispatcher);

    g_ptr_array_free (satisfied_requests, TRUE);
    _mcd_channel_details_free (channels_array);
    g_hash_table_unref (handler_info);
}

static void
mcd_dispatcher_run_handlers (McdDispatcherContext *context)
{
    McdDispatcher *self = context->dispatcher;
    GList *channels, *list;
    gchar **iter;

    sp_timestamp ("run handlers");
    mcd_dispatcher_context_ref (context, "CTXREF04");

    /* mcd_dispatcher_handle_channels steals this list */
    channels = g_list_copy (context->channels);

    /* If there is an approved handler chosen by the Approver, it's the only
     * one we'll consider. */
    if (context->operation)
    {
        const gchar *approved_handler = mcd_dispatch_operation_get_handler (
            context->operation);

        if (approved_handler != NULL && approved_handler[0] != '\0')
        {
            gchar *bus_name = g_strconcat (TP_CLIENT_BUS_NAME_BASE,
                                           approved_handler, NULL);
            McdClient *handler = g_hash_table_lookup (self->priv->clients,
                                                      bus_name);

            DEBUG ("Approved handler is %s (still exists: %c)",
                   bus_name, handler != NULL ? 'Y' : 'N');

            g_free (bus_name);

            /* Maybe the handler has exited since we chose it? Otherwise, it's
             * the right choice. */
            if (handler != NULL)
            {
                mcd_dispatcher_handle_channels (context, channels, handler);
                goto finally;
            }

            /* The approver asked for a particular handler, but that handler
             * has vanished. If MC was fully spec-compliant, it wouldn't have
             * replied to the Approver yet, so it could just return an error.
             * However, that particular part of the flying-car future has not
             * yet arrived, so try to recover by dispatching to *something*. */
        }
    }

    g_assert (context->possible_handlers != NULL);

    for (iter = context->possible_handlers; *iter != NULL; iter++)
    {
        McdClient *handler = g_hash_table_lookup (self->priv->clients, *iter);

        DEBUG ("Possible handler: %s (still exists: %c)", *iter,
               handler != NULL ? 'Y' : 'N');

        if (handler != NULL)
        {
            mcd_dispatcher_handle_channels (context, channels, handler);
            goto finally;
        }
    }

    /* All of the usable handlers vanished while we were thinking about it
     * (this can only happen if non-activatable handlers exit after we
     * include them in the list of possible handlers, but before we .
     * We should recover in some better way, perhaps by asking all the
     * approvers again (?), but for now we'll just close all the channels. */

    DEBUG ("No possible handler still exists, giving up");

    for (list = channels; list != NULL; list = list->next)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);
        GError e = { MC_ERROR, MC_CHANNEL_REQUEST_GENERIC_ERROR,
            "Handler no longer available" };

        mcd_channel_take_error (channel, g_error_copy (&e));
        g_signal_emit_by_name (self, "dispatch-failed", channel, &e);
        _mcd_channel_undispatchable (channel);
    }
    g_list_free (channels);

finally:
    mcd_dispatcher_context_unref (context, "CTXREF04");
}

static void
mcd_dispatcher_context_release_client_lock (McdDispatcherContext *context)
{
    g_return_if_fail (context->client_locks > 0);
    DEBUG ("called on %p, locks = %d", context, context->client_locks);
    context->client_locks--;
    if (context->client_locks == 0)
    {
        /* no observers left, let's go on with the dispatching */
        mcd_dispatcher_run_handlers (context);
        mcd_dispatcher_context_unref (context, "CTXREF13");
    }
}

static void
observe_channels_cb (TpClient *proxy, const GError *error,
                     gpointer user_data, GObject *weak_object)
{
    McdDispatcherContext *context = user_data;

    /* we display the error just for debugging, but we don't really care */
    if (error)
        DEBUG ("Observer returned error: %s", error->message);

    if (context->operation)
    {
        _mcd_dispatch_operation_unblock_finished (context->operation);
    }

    mcd_dispatcher_context_release_client_lock (context);
}

/* The returned GPtrArray is allocated, but the contents are borrowed. */
static GPtrArray *
collect_satisfied_requests (GList *channels)
{
    const GList *c, *r;
    GHashTable *set = g_hash_table_new (g_str_hash, g_str_equal);
    GHashTableIter iter;
    gpointer path;
    GPtrArray *ret;

    /* collect object paths into a hash table, to drop duplicates */
    for (c = channels; c != NULL; c = c->next)
    {
        const GList *reqs = _mcd_channel_get_satisfied_requests (c->data);

        for (r = reqs; r != NULL; r = r->next)
        {
            g_hash_table_insert (set, r->data, r->data);
        }
    }

    /* serialize them into a pointer array, which is what dbus-glib wants */
    ret = g_ptr_array_sized_new (g_hash_table_size (set));

    g_hash_table_iter_init (&iter, set);

    while (g_hash_table_iter_next (&iter, &path, NULL))
    {
        g_ptr_array_add (ret, path);
    }

    g_hash_table_destroy (set);

    return ret;
}

static void
mcd_dispatcher_context_unref_5 (gpointer p)
{
    mcd_dispatcher_context_unref (p, "CTXREF05");
}

static void
mcd_dispatcher_run_observers (McdDispatcherContext *context)
{
    McdDispatcherPrivate *priv = context->dispatcher->priv;
    const GList *cl, *channels;
    const gchar *dispatch_operation_path = "/";
    GHashTable *observer_info;
    GHashTableIter iter;
    McdClient *client;

    sp_timestamp ("run observers");
    channels = context->channels;
    observer_info = g_hash_table_new (g_str_hash, g_str_equal);

    g_hash_table_iter_init (&iter, priv->clients);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &client))
    {
        GList *observed = NULL;
        McdConnection *connection;
        McdAccount *account;
        const gchar *account_path, *connection_path;
        GPtrArray *channels_array, *satisfied_requests;

        if (!client->proxy ||
            !(client->interfaces & MCD_CLIENT_OBSERVER))
            continue;

        for (cl = channels; cl != NULL; cl = cl->next)
        {
            McdChannel *channel = MCD_CHANNEL (cl->data);

            if (match_filters (channel, client->observer_filters))
                observed = g_list_prepend (observed, channel);
        }
        if (!observed) continue;

        /* build up the parameters and invoke the observer */
        connection = mcd_dispatcher_context_get_connection (context);
        g_assert (connection != NULL);
        connection_path = mcd_connection_get_object_path (connection);

        account = mcd_connection_get_account (connection);
        g_assert (account != NULL);
        account_path = mcd_account_get_object_path (account);

        /* TODO: there's room for optimization here: reuse the channels_array,
         * if the observed list is the same */
        channels_array = _mcd_channel_details_build_from_list (observed);

        satisfied_requests = collect_satisfied_requests (observed);

        if (context->operation)
        {
            dispatch_operation_path =
                mcd_dispatch_operation_get_path (context->operation);
            _mcd_dispatch_operation_block_finished (context->operation);
        }

        context->client_locks++;
        mcd_dispatcher_context_ref (context, "CTXREF05");
        DEBUG ("calling ObserveChannels on %s for context %p",
               client->name, context);
        tp_cli_client_observer_call_observe_channels (client->proxy, -1,
            account_path, connection_path, channels_array,
            dispatch_operation_path, satisfied_requests, observer_info,
            observe_channels_cb,
            context,
            mcd_dispatcher_context_unref_5,
            (GObject *)context->dispatcher);

        /* don't free the individual object paths, which are borrowed from the
         * McdChannel objects */
        g_ptr_array_free (satisfied_requests, TRUE);

        _mcd_channel_details_free (channels_array);

        g_list_free (observed);
    }

    g_hash_table_destroy (observer_info);
}

/*
 * mcd_dispatcher_context_approver_not_invoked:
 * @context: the #McdDispatcherContext.
 *
 * This function is called when an approver returned error on
 * AddDispatchOperation(), and is used to keep track of how many approvers we
 * have contacted. If all of them fail, then we continue the dispatching.
 */
static void
mcd_dispatcher_context_approver_not_invoked (McdDispatcherContext *context)
{
    g_return_if_fail (context->approvers_invoked > 0);
    context->approvers_invoked--;

    if (context->approvers_invoked == 0)
        mcd_dispatcher_context_release_client_lock (context);
}

static void
add_dispatch_operation_cb (TpClient *proxy, const GError *error,
                           gpointer user_data, GObject *weak_object)
{
    McdDispatcherContext *context = user_data;

    if (error)
    {
        DEBUG ("AddDispatchOperation %s (context %p) on approver %s failed: "
               "%s",
               mcd_dispatch_operation_get_path (context->operation),
               context, tp_proxy_get_object_path (proxy), error->message);

        /* if all approvers fail to add the DO, then we behave as if no
         * approver was registered: i.e., we continue dispatching */
        context->approvers_invoked--;
        if (context->approvers_invoked == 0)
            mcd_dispatcher_context_release_client_lock (context);
    }
    else
    {
        DEBUG ("Approver %s accepted AddDispatchOperation %s (context %p)",
               tp_proxy_get_object_path (proxy),
               mcd_dispatch_operation_get_path (context->operation),
               context);
    }

    if (context->operation)
    {
        _mcd_dispatch_operation_unblock_finished (context->operation);
    }
}

static void
mcd_dispatcher_context_unref_6 (gpointer p)
{
    mcd_dispatcher_context_unref (p, "CTXREF06");
}

static void
mcd_dispatcher_run_approvers (McdDispatcherContext *context)
{
    McdDispatcherPrivate *priv = context->dispatcher->priv;
    const GList *cl, *channels;
    GHashTableIter iter;
    McdClient *client;

    g_return_if_fail (context->operation != NULL);
    sp_timestamp ("run approvers");

    /* we temporarily increment this count and decrement it at the end of the
     * function, to make sure it won't become 0 while we are still invoking
     * approvers */
    context->approvers_invoked = 1;

    context->client_locks++;
    channels = context->channels;
    g_hash_table_iter_init (&iter, priv->clients);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &client))
    {
        GPtrArray *channel_details;
        const gchar *dispatch_operation;
        GHashTable *properties;
        gboolean matched = FALSE;

        if (!client->proxy ||
            !(client->interfaces & MCD_CLIENT_APPROVER))
            continue;

        for (cl = channels; cl != NULL; cl = cl->next)
        {
            McdChannel *channel = MCD_CHANNEL (cl->data);

            if (match_filters (channel, client->approver_filters))
            {
                matched = TRUE;
                break;
            }
        }
        if (!matched) continue;

        dispatch_operation =
            mcd_dispatch_operation_get_path (context->operation);
        properties =
            mcd_dispatch_operation_get_properties (context->operation);
        channel_details =
            _mcd_dispatch_operation_dup_channel_details (context->operation);

        DEBUG ("Calling AddDispatchOperation on approver %s for CDO %s @ %p "
               "of context %p", client->name, dispatch_operation,
               context->operation, context);

        context->approvers_invoked++;
        _mcd_dispatch_operation_block_finished (context->operation);

        mcd_dispatcher_context_ref (context, "CTXREF06");
        tp_cli_client_approver_call_add_dispatch_operation (client->proxy, -1,
            channel_details, dispatch_operation, properties,
            add_dispatch_operation_cb,
            context,
            mcd_dispatcher_context_unref_6,
            (GObject *)context->dispatcher);

        g_boxed_free (TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST, channel_details);
    }

    /* This matches the approvers count set to 1 at the beginning of the
     * function */
    mcd_dispatcher_context_approver_not_invoked (context);
}

static gboolean
handlers_can_bypass_approval (McdDispatcherContext *context)
{
    McdDispatcher *self = context->dispatcher;
    gchar **iter;

    g_assert (context->possible_handlers != NULL);

    for (iter = context->possible_handlers; *iter != NULL; iter++)
    {
        McdClient *handler = g_hash_table_lookup (self->priv->clients,
                                                  *iter);

        /* If the best handler that still exists bypasses approval, then
         * we're going to bypass approval.
         *
         * Also, because handlers are sorted with the best ones first, and
         * handlers with BypassApproval are "better", we can be sure that if
         * we've found a handler that still exists and does not bypass
         * approval, no handler bypasses approval. */
        if (handler != NULL)
        {
            DEBUG ("%s has BypassApproval=%c", *iter,
                   handler->bypass_approver ? 'T' : 'F');
            return handler->bypass_approver;
        }
    }

    /* If no handler still exists, we don't bypass approval, although if that
     * happens we're basically doomed anyway. */
    return FALSE;
}

/* Happens at the end of successful filter chain execution (empty chain
 * is always successful)
 */
static void
mcd_dispatcher_run_clients (McdDispatcherContext *context)
{
    mcd_dispatcher_context_ref (context, "CTXREF07");
    context->client_locks = 1; /* we release this lock at the end of the
                                    function */

    /* CTXREF13 is released after all client locks are released */
    mcd_dispatcher_context_ref (context, "CTXREF13");

    mcd_dispatcher_run_observers (context);

    if (context->operation)
    {
        /* if we have a dispatch operation, it means that the channels were not
         * requested: start the Approvers */

        /* but if the handlers have the BypassApproval flag set, then don't */
        if (!context->skip_approval &&
            !handlers_can_bypass_approval (context))
            mcd_dispatcher_run_approvers (context);
    }

    mcd_dispatcher_context_release_client_lock (context);
    mcd_dispatcher_context_unref (context, "CTXREF07");
}

/*
 * _mcd_dispatcher_context_abort:
 *
 * Abort processing of all the channels in the @context, as if they could not
 * be dispatched.
 *
 * This should only be invoked because filter plugins want to terminate a
 * channel.
 */
static void
_mcd_dispatcher_context_abort (McdDispatcherContext *context,
                               const GError *error)
{
    GList *list;

    g_return_if_fail (context);

    /* make a temporary copy, which is destroyed during the loop - otherwise
     * we'll be trying to iterate over context->channels at the same time
     * that mcd_mission_abort results in modifying it, which would be bad */
    list = g_list_copy (context->channels);
    g_list_foreach (list, (GFunc) g_object_ref, NULL);

    while (list != NULL)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);

        if (mcd_channel_get_error (channel) == NULL)
            mcd_channel_take_error (channel, g_error_copy (error));

        _mcd_channel_undispatchable (channel);

        g_object_unref (channel);
        list = g_list_delete_link (list, list);
    }
}

static void
on_channel_abort_context (McdChannel *channel, McdDispatcherContext *context)
{
    const GError *error;
    GList *li = g_list_find (context->channels, channel);

    DEBUG ("Channel %p aborted while in a dispatcher context", channel);

    /* if it was a channel request, and it was cancelled, then the whole
     * context should be aborted */
    error = mcd_channel_get_error (channel);
    if (error && error->code == TP_ERROR_CANCELLED)
        context->cancelled = TRUE;

    /* Losing the channel might mean we get freed, which would make some of
     * the operations below very unhappy */
    mcd_dispatcher_context_ref (context, "CTXREF08");

    if (context->operation)
    {
        /* the CDO owns the linked list and we just borrow it; in case it's
         * the head of the list that we're deleting, we need to ask the CDO
         * to update our idea of what the list is before emitting any signals.
         *
         * FIXME: this is alarmingly fragile */
        _mcd_dispatch_operation_lose_channel (context->operation, channel,
                                              &(context->channels));
    }
    else
    {
        /* we own the linked list */
        context->channels = g_list_delete_link (context->channels, li);
    }

    if (li != NULL)
    {
        /* we used to have a ref to it, until it was removed from the linked
         * list, either by us or by the CDO. (Do not dereference li at this
         * point - it has been freed!) */
        g_object_unref (channel);
    }

    if (context->channels == NULL)
    {
        DEBUG ("Nothing left in this context");
    }

    mcd_dispatcher_context_unref (context, "CTXREF08");
}

static void
on_operation_finished (McdDispatchOperation *operation,
                       McdDispatcherContext *context)
{
    /* This is emitted when the HandleWith() or Claimed() are invoked on the
     * CDO: according to which of these have happened, we run the choosen
     * handler or we don't. */

    if (context->dispatcher->priv->operation_list_active)
    {
        tp_svc_channel_dispatcher_interface_operation_list_emit_dispatch_operation_finished (
            context->dispatcher, mcd_dispatch_operation_get_path (operation));
    }

    if (context->channels == NULL)
    {
        DEBUG ("Nothing left to dispatch");

        if (context->client_locks > 0)
        {
            /* this would have been released when all the locks were released,
             * but now we're never going to do that */
            mcd_dispatcher_context_unref (context, "CTXREF13");
        }
    }
    else if (mcd_dispatch_operation_is_claimed (operation))
    {
        GList *list;

        /* we don't release the client lock, in order to not run the handlers.
         * But we have to mark all channels as dispatched, and free the
         * @context */
        for (list = context->channels; list != NULL; list = list->next)
        {
            McdChannel *channel = MCD_CHANNEL (list->data);

            mcd_dispatcher_set_channel_handled_by (context->dispatcher,
                channel, _mcd_dispatch_operation_get_claimer (operation));
        }

        mcd_dispatcher_context_handler_done (context);

        /* this would have been released when all the locks were released, but
         * we're never going to do that */
        g_assert (context->client_locks > 0);
        mcd_dispatcher_context_unref (context, "CTXREF13");
    }
    else
    {
        /* this is the lock set in mcd_dispatcher_run_approvers(): releasing
         * this will make the handlers run */
        mcd_dispatcher_context_release_client_lock (context);
    }
}

/* ownership of channels, possible_handlers is stolen */
static void
_mcd_dispatcher_enter_state_machine (McdDispatcher *dispatcher,
                                     GList *channels,
                                     GStrv possible_handlers,
                                     gboolean requested)
{
    McdDispatcherContext *context;
    McdDispatcherPrivate *priv;
    GList *list;
    McdChannel *channel;
    McdAccount *account;

    g_return_if_fail (MCD_IS_DISPATCHER (dispatcher));
    g_return_if_fail (channels != NULL);
    g_return_if_fail (MCD_IS_CHANNEL (channels->data));

    account = mcd_channel_get_account (channels->data);
    if (G_UNLIKELY (!account))
    {
        g_warning ("%s called with no account", G_STRFUNC);
        return;
    }

    priv = dispatcher->priv;

    /* Preparing and filling the context */
    context = g_new0 (McdDispatcherContext, 1);
    DEBUG ("CTXREF11 on %p", context);
    context->ref_count = 1;
    context->dispatcher = dispatcher;
    context->account = account;
    context->channels = channels;
    context->chain = priv->filters;
    context->possible_handlers = possible_handlers;

    DEBUG ("new dispatcher context %p for %s channel %p (%s): %s",
           context, requested ? "requested" : "unrequested",
           context->channels->data,
           context->channels->next == NULL ? "only" : "and more",
           mcd_channel_get_object_path (context->channels->data));

    priv->contexts = g_list_prepend (priv->contexts, context);
    if (!requested)
    {
        context->operation =
            _mcd_dispatch_operation_new (priv->dbus_daemon, channels,
                                         possible_handlers);

        if (priv->operation_list_active)
        {
            tp_svc_channel_dispatcher_interface_operation_list_emit_new_dispatch_operation (
                dispatcher,
                mcd_dispatch_operation_get_path (context->operation),
                mcd_dispatch_operation_get_properties (context->operation));
        }

        g_signal_connect (context->operation, "finished",
                          G_CALLBACK (on_operation_finished), context);
    }

    for (list = channels; list != NULL; list = list->next)
    {
        channel = MCD_CHANNEL (list->data);

        g_object_ref (channel); /* We hold separate refs for state machine */
        g_signal_connect_after (channel, "abort",
                                G_CALLBACK (on_channel_abort_context),
                                context);
    }

    if (priv->filters != NULL)
    {
        DEBUG ("entering state machine for context %p", context);

        sp_timestamp ("invoke internal filters");

        mcd_dispatcher_context_ref (context, "CTXREF01");
	mcd_dispatcher_context_process (context, TRUE);
    }
    else
    {
        DEBUG ("No filters found for context %p, "
               "starting the channel handler", context);

	mcd_dispatcher_run_clients (context);
    }

    mcd_dispatcher_context_unref (context, "CTXREF11");
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

static const char * const interfaces[] = {
    TP_IFACE_CHANNEL_DISPATCHER_INTERFACE_OPERATION_LIST,
    NULL
};

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

    case PROP_INTERFACES:
        g_value_set_static_boxed (val, interfaces);
        break;

    case PROP_DISPATCH_OPERATIONS:
        {
            GList *iter;
            GPtrArray *operations = g_ptr_array_new ();

            /* Side-effect: from now on, emit change notification signals for
             * this property */
            priv->operation_list_active = TRUE;

            for (iter = priv->contexts; iter != NULL; iter = iter->next)
            {
                McdDispatcherContext *context = iter->data;

                if (context->operation != NULL &&
                    !_mcd_dispatch_operation_is_finished (context->operation))
                {
                    GValueArray *va = g_value_array_new (2);

                    g_value_array_append (va, NULL);
                    g_value_array_append (va, NULL);

                    g_value_init (va->values + 0, DBUS_TYPE_G_OBJECT_PATH);
                    g_value_init (va->values + 1,
                                  TP_HASH_TYPE_STRING_VARIANT_MAP);

                    g_value_set_boxed (va->values + 0,
                        mcd_dispatch_operation_get_path (context->operation));
                    g_value_set_boxed (va->values + 1,
                        mcd_dispatch_operation_get_properties (
                            context->operation));

                    g_ptr_array_add (operations, va);
                }
            }

            g_value_take_boxed (val, operations);
        }
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

    if (priv->filters)
    {
        GList *list;
        for (list = priv->filters; list != NULL; list = list->next)
            g_slice_free (McdFilter, list->data);
        g_list_free (priv->filters);
    }

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

    if (priv->handler_map)
    {
        g_object_unref (priv->handler_map);
        priv->handler_map = NULL;
    }

    g_hash_table_destroy (priv->clients);
    g_hash_table_destroy (priv->connections);

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

    G_OBJECT_CLASS (mcd_dispatcher_parent_class)->dispose (object);
}

static GHashTable *
parse_client_filter (GKeyFile *file, const gchar *group)
{
    GHashTable *filter;
    gchar **keys;
    gsize len = 0;
    guint i;

    filter = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                    (GDestroyNotify) tp_g_value_slice_free);

    keys = g_key_file_get_keys (file, group, &len, NULL);
    for (i = 0; i < len; i++)
    {
        const gchar *key;
        const gchar *space;
        gchar *file_property;
        gchar file_property_type;

        key = keys[i];
        space = g_strrstr (key, " ");

        if (space == NULL || space[1] == '\0' || space[2] != '\0')
        {
            g_warning ("Invalid key %s in client file", key);
            continue;
        }
        file_property_type = space[1];
        file_property = g_strndup (key, space - key);

        switch (file_property_type)
        {
        case 'q':
        case 'u':
        case 't': /* unsigned integer */
            {
                /* g_key_file_get_integer cannot be used because we need
                 * to support 64 bits */
                guint x;
                GValue *value = tp_g_value_slice_new (G_TYPE_UINT64);
                gchar *str = g_key_file_get_string (file, group, key,
                                                    NULL);
                errno = 0;
                x = g_ascii_strtoull (str, NULL, 0);
                if (errno != 0)
                {
                    g_warning ("Invalid unsigned integer '%s' in client"
                               " file", str);
                }
                else
                {
                    g_value_set_uint64 (value, x);
                    g_hash_table_insert (filter, file_property, value);
                }
                g_free (str);
                break;
            }

        case 'y':
        case 'n':
        case 'i':
        case 'x': /* signed integer */
            {
                gint x;
                GValue *value = tp_g_value_slice_new (G_TYPE_INT64);
                gchar *str = g_key_file_get_string (file, group, key, NULL);
                errno = 0;
                x = g_ascii_strtoll (str, NULL, 0);
                if (errno != 0)
                {
                    g_warning ("Invalid signed integer '%s' in client"
                               " file", str);
                }
                else
                {
                    g_value_set_uint64 (value, x);
                    g_hash_table_insert (filter, file_property, value);
                }
                g_free (str);
                break;
            }

        case 'b':
            {
                GValue *value = tp_g_value_slice_new (G_TYPE_BOOLEAN);
                gboolean b = g_key_file_get_boolean (file, group, key, NULL);
                g_value_set_boolean (value, b);
                g_hash_table_insert (filter, file_property, value);
                break;
            }

        case 's':
            {
                GValue *value = tp_g_value_slice_new (G_TYPE_STRING);
                gchar *str = g_key_file_get_string (file, group, key, NULL);

                g_value_take_string (value, str);
                g_hash_table_insert (filter, file_property, value);
                break;
            }

        case 'o':
            {
                GValue *value = tp_g_value_slice_new
                    (DBUS_TYPE_G_OBJECT_PATH);
                gchar *str = g_key_file_get_string (file, group, key, NULL);

                g_value_take_boxed (value, str);
                g_hash_table_insert (filter, file_property, value);
                break;
            }

        default:
            g_warning ("Invalid key %s in client file", key);
            continue;
        }
    }
    g_strfreev (keys);

    return filter;
}

static void
mcd_client_set_filters (McdClient *client,
                        McdClientInterface interface,
                        GPtrArray *filters)
{
    GList **client_filters;
    guint i;

    switch (interface)
    {
        case MCD_CLIENT_OBSERVER:
            client_filters = &client->observer_filters;
            break;

        case MCD_CLIENT_APPROVER:
            client_filters = &client->approver_filters;
            break;

        case MCD_CLIENT_HANDLER:
            client_filters = &client->handler_filters;
            break;

        default:
            g_assert_not_reached ();
    }

    if (*client_filters != NULL)
    {
        g_list_foreach (*client_filters, (GFunc) g_hash_table_destroy, NULL);
        g_list_free (*client_filters);
        *client_filters = NULL;
    }

    for (i = 0 ; i < filters->len ; i++)
    {
        GHashTable *channel_class = g_ptr_array_index (filters, i);
        GHashTable *new_channel_class;
        GHashTableIter iter;
        gchar *property_name;
        GValue *property_value;
        gboolean valid_filter = TRUE;

        new_channel_class = g_hash_table_new_full
            (g_str_hash, g_str_equal, g_free,
             (GDestroyNotify) tp_g_value_slice_free);

        g_hash_table_iter_init (&iter, channel_class);
        while (g_hash_table_iter_next (&iter, (gpointer *) &property_name,
                                       (gpointer *) &property_value)) 
        {
            GValue *filter_value;
            GType property_type = G_VALUE_TYPE (property_value);

            if (property_type == G_TYPE_BOOLEAN ||
                property_type == G_TYPE_STRING ||
                property_type == DBUS_TYPE_G_OBJECT_PATH)
            {
                filter_value = tp_g_value_slice_new
                    (G_VALUE_TYPE (property_value));
                g_value_copy (property_value, filter_value);
            }
            else if (property_type == G_TYPE_UCHAR ||
                     property_type == G_TYPE_UINT ||
                     property_type == G_TYPE_UINT64)
            {
                filter_value = tp_g_value_slice_new (G_TYPE_UINT64);
                g_value_transform (property_value, filter_value);
            }
            else if (property_type == G_TYPE_INT ||
                     property_type == G_TYPE_INT64)
            {
                filter_value = tp_g_value_slice_new (G_TYPE_INT64);
                g_value_transform (property_value, filter_value);
            }
            else
            {
                /* invalid type, do not add this filter */
                g_warning ("%s: Property %s has an invalid type (%s)",
                           G_STRFUNC, property_name,
                           g_type_name (G_VALUE_TYPE (property_value)));
                valid_filter = FALSE;
                break;
            }

            g_hash_table_insert (new_channel_class, g_strdup (property_name),
                                 filter_value);
        }

        if (valid_filter)
            *client_filters = g_list_prepend
                (*client_filters, new_channel_class);
        else
            g_hash_table_destroy (new_channel_class);
    }
}

static void
mcd_dispatcher_release_startup_lock (McdDispatcher *self)
{
    if (self->priv->startup_completed)
        return;

    DEBUG ("%p (decrementing from %" G_GSIZE_FORMAT ")",
           self, self->priv->startup_lock);

    g_assert (self->priv->startup_lock >= 1);

    self->priv->startup_lock--;

    if (self->priv->startup_lock == 0)
    {
        GHashTableIter iter;
        gpointer k;

        DEBUG ("All initial clients have been inspected");
        self->priv->startup_completed = TRUE;

        g_hash_table_iter_init (&iter, self->priv->connections);

        while (g_hash_table_iter_next (&iter, &k, NULL))
        {
            _mcd_connection_start_dispatching (k);
        }
    }
}

static void
get_channel_filter_cb (TpProxy *proxy,
                       const GValue *value,
                       const GError *error,
                       gpointer user_data,
                       GObject *weak_object)
{
    McdDispatcher *self = MCD_DISPATCHER (weak_object);
    McdClient *client;
    const gchar *bus_name = tp_proxy_get_bus_name (proxy);

    client = g_hash_table_lookup (self->priv->clients, bus_name);

    if (G_UNLIKELY (client == NULL))
    {
        DEBUG ("Client %s vanished while we were getting its Client filters",
               bus_name);
        goto finally;
    }

    if (error != NULL)
    {
        DEBUG ("error getting a filter list for client %s: %s #%d: %s",
               tp_proxy_get_object_path (proxy),
               g_quark_to_string (error->domain), error->code, error->message);
        goto finally;
    }

    if (!G_VALUE_HOLDS (value, TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST))
    {
        DEBUG ("wrong type for filter property on client %s: %s",
               tp_proxy_get_object_path (proxy), G_VALUE_TYPE_NAME (value));
        goto finally;
    }

    mcd_client_set_filters (client, GPOINTER_TO_UINT (user_data),
                            g_value_get_boxed (value));
finally:
    mcd_dispatcher_release_startup_lock (self);
}

static void
handler_get_all_cb (TpProxy *proxy,
                    GHashTable *properties,
                    const GError *error,
                    gpointer unused G_GNUC_UNUSED,
                    GObject *weak_object)
{
    McdClientProxy *client_proxy = MCD_CLIENT_PROXY (proxy);
    McdDispatcher *self = MCD_DISPATCHER (weak_object);
    McdClient *client;
    const gchar *bus_name = tp_proxy_get_bus_name (proxy);
    GPtrArray *filters, *channels;
    const gchar *unique_name;

    if (error != NULL)
    {
        DEBUG ("GetAll(Handler) for client %s failed: %s #%d: %s",
               bus_name, g_quark_to_string (error->domain), error->code,
               error->message);
        goto finally;
    }

    client = g_hash_table_lookup (self->priv->clients, bus_name);

    if (G_UNLIKELY (client == NULL))
    {
        DEBUG ("Client %s vanished while getting its Handler properties",
               bus_name);
        goto finally;
    }

    filters = tp_asv_get_boxed (properties, "HandlerChannelFilter",
                                TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST);

    if (filters != NULL)
    {
        DEBUG ("%s has %u HandlerChannelFilter entries", client->name,
               filters->len);
        mcd_client_set_filters (client, MCD_CLIENT_HANDLER, filters);
    }
    else
    {
        DEBUG ("%s HandlerChannelFilter absent or wrong type, assuming "
               "no channels can match", client->name);
    }

    /* if wrong type or absent, assuming False is reasonable */
    client->bypass_approver = tp_asv_get_boolean (properties, "BypassApproval",
                                                  NULL);
    DEBUG ("%s has BypassApproval=%c", client->name,
           client->bypass_approver ? 'T' : 'F');

    channels = tp_asv_get_boxed (properties, "HandledChannels",
                                 MC_ARRAY_TYPE_OBJECT);

    unique_name = _mcd_client_proxy_get_unique_name (client_proxy);

    /* This function is only called in response to the McdClientProxy
     * signalling ready, which means it knows whether it has a unique name
     * (is running) or not */
    g_assert (unique_name != NULL);

    if (unique_name[0] == '\0')
    {
        /* if it said it was handling channels but it doesn't seem to exist,
         * then we don't believe it */
        DEBUG ("%s doesn't seem to exist, assuming no channels handled",
               client->name);
    }
    else if (channels != NULL)
    {
        guint i;

        for (i = 0; i < channels->len; i++)
        {
            const gchar *path = g_ptr_array_index (channels, i);

            DEBUG ("%s (%s) is handling %s", client->name, unique_name,
                   path);

            _mcd_handler_map_set_path_handled (self->priv->handler_map,
                                               path, unique_name);
        }
    }

finally:
    mcd_dispatcher_release_startup_lock (self);
}

static void
client_add_interface_by_id (McdClient *client)
{
    TpProxy *proxy = (TpProxy *) client->proxy;

    tp_proxy_add_interface_by_id (proxy, TP_IFACE_QUARK_CLIENT);
    if (client->interfaces & MCD_CLIENT_APPROVER)
        tp_proxy_add_interface_by_id (proxy,
                                      TP_IFACE_QUARK_CLIENT_APPROVER);
    if (client->interfaces & MCD_CLIENT_HANDLER)
        tp_proxy_add_interface_by_id (proxy,
                                      TP_IFACE_QUARK_CLIENT_HANDLER);
    if (client->interfaces & MCD_CLIENT_INTERFACE_REQUESTS)
        tp_proxy_add_interface_by_id (proxy, TP_IFACE_QUARK_CLIENT_INTERFACE_REQUESTS);
    if (client->interfaces & MCD_CLIENT_OBSERVER)
        tp_proxy_add_interface_by_id (proxy,
                                      TP_IFACE_QUARK_CLIENT_OBSERVER);
}

static void
get_interfaces_cb (TpProxy *proxy,
                   const GValue *out_Value,
                   const GError *error,
                   gpointer user_data,
                   GObject *weak_object)
{
    McdDispatcher *self = MCD_DISPATCHER (weak_object);
    /* McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (self); */
    McdClient *client;
    gchar **arr;
    const gchar *bus_name = tp_proxy_get_bus_name (proxy);

    if (error != NULL)
    {
        DEBUG ("Error getting Interfaces for Client %s, assuming none: "
               "%s %d %s", bus_name,
               g_quark_to_string (error->domain), error->code, error->message);
        goto finally;
    }

    if (!G_VALUE_HOLDS (out_Value, G_TYPE_STRV))
    {
        DEBUG ("Wrong type getting Interfaces for Client %s, assuming none: "
               "%s", bus_name, G_VALUE_TYPE_NAME (out_Value));
        goto finally;
    }

    client = g_hash_table_lookup (self->priv->clients, bus_name);

    if (G_UNLIKELY (client == NULL))
    {
        DEBUG ("Client %s vanished while we were getting its interfaces",
               bus_name);
        goto finally;
    }

    arr = g_value_get_boxed (out_Value);

    while (arr != NULL && *arr != NULL)
    {
        if (strcmp (*arr, TP_IFACE_CLIENT_APPROVER) == 0)
            client->interfaces |= MCD_CLIENT_APPROVER;
        if (strcmp (*arr, TP_IFACE_CLIENT_HANDLER) == 0)
            client->interfaces |= MCD_CLIENT_HANDLER;
        if (strcmp (*arr, TP_IFACE_CLIENT_INTERFACE_REQUESTS) == 0)
            client->interfaces |= MCD_CLIENT_INTERFACE_REQUESTS;
        if (strcmp (*arr, TP_IFACE_CLIENT_OBSERVER) == 0)
            client->interfaces |= MCD_CLIENT_OBSERVER;
        arr++;
    }

    DEBUG ("Client %s", client->name);

    client_add_interface_by_id (client);
    if (client->interfaces & MCD_CLIENT_APPROVER)
    {
        if (!self->priv->startup_completed)
            self->priv->startup_lock++;

        DEBUG ("%s is an Approver", client->name);

        tp_cli_dbus_properties_call_get
            (client->proxy, -1, TP_IFACE_CLIENT_APPROVER,
             "ApproverChannelFilter", get_channel_filter_cb,
             GUINT_TO_POINTER (MCD_CLIENT_APPROVER), NULL, G_OBJECT (self));
    }
    if (client->interfaces & MCD_CLIENT_HANDLER)
    {
        if (!self->priv->startup_completed)
            self->priv->startup_lock++;

        DEBUG ("%s is a Handler", client->name);

        tp_cli_dbus_properties_call_get_all
            (client->proxy, -1, TP_IFACE_CLIENT_HANDLER,
             handler_get_all_cb, NULL, NULL, G_OBJECT (self));
    }
    if (client->interfaces & MCD_CLIENT_OBSERVER)
    {
        if (!self->priv->startup_completed)
            self->priv->startup_lock++;

        DEBUG ("%s is an Observer", client->name);

        tp_cli_dbus_properties_call_get
            (client->proxy, -1, TP_IFACE_CLIENT_OBSERVER,
             "ObserverChannelFilter", get_channel_filter_cb,
             GUINT_TO_POINTER (MCD_CLIENT_OBSERVER), NULL, G_OBJECT (self));
    }

finally:
    mcd_dispatcher_release_startup_lock (self);
}

static void
parse_client_file (McdClient *client, GKeyFile *file)
{
    gchar **iface_names, **groups;
    guint i;
    gsize len = 0;

    iface_names = g_key_file_get_string_list (file, TP_IFACE_CLIENT,
                                              "Interfaces", 0, NULL);
    if (!iface_names)
        return;

    for (i = 0; iface_names[i] != NULL; i++)
    {
        if (strcmp (iface_names[i], TP_IFACE_CLIENT_APPROVER) == 0)
            client->interfaces |= MCD_CLIENT_APPROVER;
        else if (strcmp (iface_names[i], TP_IFACE_CLIENT_HANDLER) == 0)
            client->interfaces |= MCD_CLIENT_HANDLER;
        else if (strcmp (iface_names[i], TP_IFACE_CLIENT_INTERFACE_REQUESTS) == 0)
            client->interfaces |= MCD_CLIENT_INTERFACE_REQUESTS;
        else if (strcmp (iface_names[i], TP_IFACE_CLIENT_OBSERVER) == 0)
            client->interfaces |= MCD_CLIENT_OBSERVER;
    }
    g_strfreev (iface_names);

    /* parse filtering rules */
    groups = g_key_file_get_groups (file, &len);
    for (i = 0; i < len; i++)
    {
        if (client->interfaces & MCD_CLIENT_APPROVER &&
            g_str_has_prefix (groups[i], TP_IFACE_CLIENT_APPROVER
                              ".ApproverChannelFilter "))
        {
            client->approver_filters =
                g_list_prepend (client->approver_filters,
                                parse_client_filter (file, groups[i]));
        }
        else if (client->interfaces & MCD_CLIENT_HANDLER &&
            g_str_has_prefix (groups[i], TP_IFACE_CLIENT_HANDLER
                              ".HandlerChannelFilter "))
        {
            client->handler_filters =
                g_list_prepend (client->handler_filters,
                                parse_client_filter (file, groups[i]));
        }
        else if (client->interfaces & MCD_CLIENT_OBSERVER &&
            g_str_has_prefix (groups[i], TP_IFACE_CLIENT_OBSERVER
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
        g_key_file_get_boolean (file, TP_IFACE_CLIENT_HANDLER,
                                "BypassApproval", NULL);
}

static gchar *
find_client_file (const gchar *client_name)
{
    const gchar * const *dirs;
    const gchar *dirname;
    const gchar *env_dirname;
    gchar *filename, *absolute_filepath;

    /* 
     * The full path is $XDG_DATA_DIRS/telepathy/clients/clientname.client
     * or $XDG_DATA_HOME/telepathy/clients/clientname.client
     * For testing purposes, we also look for $MC_CLIENTS_DIR/clientname.client
     * if $MC_CLIENTS_DIR is set.
     */
    filename = g_strdup_printf ("%s.client", client_name);
    env_dirname = g_getenv ("MC_CLIENTS_DIR");
    if (env_dirname)
    {
        absolute_filepath = g_build_filename (env_dirname, filename, NULL);
        if (g_file_test (absolute_filepath, G_FILE_TEST_IS_REGULAR))
            goto finish;
        g_free (absolute_filepath);
    }

    dirname = g_get_user_data_dir ();
    if (G_LIKELY (dirname))
    {
        absolute_filepath = g_build_filename (dirname, "telepathy/clients",
                                              filename, NULL);
        if (g_file_test (absolute_filepath, G_FILE_TEST_IS_REGULAR))
            goto finish;
        g_free (absolute_filepath);
    }

    dirs = g_get_system_data_dirs ();
    for (dirname = *dirs; dirname != NULL; dirs++, dirname = *dirs)
    {
        absolute_filepath = g_build_filename (dirname, "telepathy/clients",
                                              filename, NULL);
        if (g_file_test (absolute_filepath, G_FILE_TEST_IS_REGULAR))
            goto finish;
        g_free (absolute_filepath);
    }

    absolute_filepath = NULL;
finish:
    g_free (filename);
    return absolute_filepath;
}

static McdClient *
create_mcd_client (McdDispatcher *self,
                   const gchar *name,
                   gboolean activatable,
                   const gchar *owner)
{
    /* McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (self); */
    McdClient *client;

    g_assert (g_str_has_prefix (name, TP_CLIENT_BUS_NAME_BASE));

    client = g_slice_new0 (McdClient);
    client->name = g_strdup (name + MC_CLIENT_BUS_NAME_BASE_LEN);
    client->activatable = activatable;
    if (!activatable)
        client->active = TRUE;

    client->proxy = (TpClient *) _mcd_client_proxy_new (
        self->priv->dbus_daemon, client->name, owner);

    DEBUG ("McdClient created for %s", name);

    return client;
}

/* FIXME: eventually this whole chain should move into McdClientProxy */
static void
mcd_client_start_introspection (McdClientProxy *proxy,
                                McdDispatcher *dispatcher)
{
    gchar *filename;
    gboolean file_found = FALSE;
    McdClient *client;
    const gchar *bus_name = tp_proxy_get_bus_name (proxy);

    client = g_hash_table_lookup (dispatcher->priv->clients, bus_name);

    if (client == NULL)
    {
        DEBUG ("Client %s vanished before it became ready", bus_name);
        goto finally;
    }

    /* The .client file is not mandatory as per the spec. However if it
     * exists, it is better to read it than activating the service to read the
     * D-Bus properties.
     */
    filename = find_client_file (client->name);
    if (filename)
    {
        GKeyFile *file;
        GError *error = NULL;

        file = g_key_file_new ();
        g_key_file_load_from_file (file, filename, 0, &error);
        if (G_LIKELY (!error))
        {
            DEBUG ("File found for %s: %s", client->name, filename);
            parse_client_file (client, file);
            file_found = TRUE;
        }
        else
        {
            g_warning ("Loading file %s failed: %s", filename, error->message);
            g_error_free (error);
        }
        g_key_file_free (file);
        g_free (filename);
    }

    if (!file_found)
    {
        DEBUG ("No .client file for %s. Ask on D-Bus.", client->name);

        if (!dispatcher->priv->startup_completed)
            dispatcher->priv->startup_lock++;

        tp_cli_dbus_properties_call_get (client->proxy, -1,
            TP_IFACE_CLIENT, "Interfaces", get_interfaces_cb, NULL,
            NULL, G_OBJECT (dispatcher));
    }
    else
    {
        client_add_interface_by_id (client);

        if ((client->interfaces & MCD_CLIENT_HANDLER) != 0 &&
            _mcd_client_proxy_is_active (proxy))
        {
            DEBUG ("%s is an active, activatable Handler", client->name);

            /* We need to investigate whether it is handling any channels */

            if (!dispatcher->priv->startup_completed)
                dispatcher->priv->startup_lock++;

            tp_cli_dbus_properties_call_get_all (client->proxy, -1,
                                                 TP_IFACE_CLIENT_HANDLER,
                                                 handler_get_all_cb,
                                                 NULL, NULL,
                                                 G_OBJECT (dispatcher));
        }
    }

finally:
    /* paired with the lock taken when we made the McdClient */
    mcd_dispatcher_release_startup_lock (dispatcher);
}

/* Check the list of strings whether they are valid well-known names of
 * Telepathy clients and create McdClient objects for each of them.
 */
static void
mcd_dispatcher_add_client (McdDispatcher *self,
                           const gchar *name,
                           gboolean activatable,
                           const gchar *owner)
{
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (self);
    McdClient *client;

    if (!g_str_has_prefix (name, TP_CLIENT_BUS_NAME_BASE))
    {
        /* This is not a Telepathy Client */
        return;
    }

    if (!_mcd_client_check_valid_name (name + MC_CLIENT_BUS_NAME_BASE_LEN,
                                       NULL))
    {
        /* This is probably meant to be a Telepathy Client, but it's not */
        DEBUG ("Ignoring invalid Client name: %s",
               name + MC_CLIENT_BUS_NAME_BASE_LEN);

        return;
    }

    client = g_hash_table_lookup (priv->clients, name);

    if (client)
    {
        /* This Telepathy Client is already known so don't create it
         * again. However, set the activatable bit now.
         */
        if (activatable)
        {
            client->activatable = TRUE;
        }
        else
        {
            _mcd_client_proxy_set_active ((McdClientProxy *) client->proxy,
                                          owner);
            client->active = TRUE;
        }

        return;
    }

    DEBUG ("Register client %s", name);

    /* paired with one in mcd_client_start_introspection */
    if (!self->priv->startup_completed)
        self->priv->startup_lock++;

    client = create_mcd_client (self, name, activatable, owner);

    g_hash_table_insert (priv->clients, g_strdup (name), client);

    g_signal_connect (client->proxy, "ready",
                      G_CALLBACK (mcd_client_start_introspection),
                      self);
}

static void
list_activatable_names_cb (TpDBusDaemon *proxy,
    const gchar **names,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
    McdDispatcher *self = MCD_DISPATCHER (weak_object);

    if (error != NULL)
    {
        DEBUG ("ListActivatableNames returned error, assuming none: %s %d: %s",
               g_quark_to_string (error->domain), error->code, error->message);
    }
    else if (names != NULL)
    {
        const gchar **iter = names;

        DEBUG ("ListActivatableNames returned");

        while (*iter != NULL)
        {
            mcd_dispatcher_add_client (self, *iter, TRUE, NULL);
            iter++;
        }
    }

    /* paired with the lock taken in _constructed */
    mcd_dispatcher_release_startup_lock (self);
}

static void
list_names_cb (TpDBusDaemon *proxy,
    const gchar **names,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
    McdDispatcher *self = MCD_DISPATCHER (weak_object);

    if (error != NULL)
    {
        DEBUG ("ListNames returned error, assuming none: %s %d: %s",
               g_quark_to_string (error->domain), error->code, error->message);
    }
    else if (names != NULL)
    {
        const gchar **iter = names;

        DEBUG ("ListNames returned");

        while (*iter != NULL)
        {
            mcd_dispatcher_add_client (self, *iter, FALSE, NULL);
            iter++;
        }
    }

    tp_cli_dbus_daemon_call_list_activatable_names (self->priv->dbus_daemon,
        -1, list_activatable_names_cb, NULL, NULL, weak_object);
    /* deliberately not calling mcd_dispatcher_release_startup_lock here -
     * this function is "lock-neutral" (we would take a lock for
     * ListActivatableNames then release the one used for ListNames),
     * so simplify by doing nothing */
}

static void
name_owner_changed_cb (TpDBusDaemon *proxy,
    const gchar *name,
    const gchar *old_owner,
    const gchar *new_owner,
    gpointer user_data,
    GObject *weak_object)
{
    McdDispatcher *self = MCD_DISPATCHER (weak_object);
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (self);

    /* dbus-glib guarantees this */
    g_assert (name != NULL);
    g_assert (old_owner != NULL);
    g_assert (new_owner != NULL);

    if (old_owner[0] == '\0' && new_owner[0] != '\0')
    {
        mcd_dispatcher_add_client (self, name, FALSE, new_owner);
    }
    else if (old_owner[0] != '\0' && new_owner[0] == '\0')
    {
        /* The name disappeared from the bus. It might be either well-known
         * or unique */
        McdClient *client;

        client = g_hash_table_lookup (priv->clients, name);

        if (client)
        {
            client->active = FALSE;
            _mcd_client_proxy_set_inactive ((McdClientProxy *) client->proxy);

            if (!client->activatable)
            {
                g_hash_table_remove (priv->clients, name);
            }
        }

        if (name[0] == ':')
        {
            /* it's a unique name - maybe it was handling some channels? */
            _mcd_handler_map_set_handler_crashed (priv->handler_map, name);
        }
    }
    else if (old_owner[0] != '\0' && new_owner[0] != '\0')
    {
        /* Atomic ownership handover - handle this like an exit + startup */
        name_owner_changed_cb (proxy, name, old_owner, "", user_data,
                               weak_object);
        name_owner_changed_cb (proxy, name, "", new_owner, user_data,
                               weak_object);
    }
    else
    {
        /* dbus-daemon is sick */
        DEBUG ("Malformed message from the D-Bus daemon about '%s' "
               "('%s' -> '%s')", name, old_owner, new_owner);
    }
}

static void
mcd_dispatcher_constructed (GObject *object)
{
    DBusGConnection *dgc;
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (object);
    GError *error = NULL;

    DEBUG ("Starting to look for clients");
    priv->startup_completed = FALSE;
    priv->startup_lock = 1;   /* the ListNames call we're about to make */

    tp_cli_dbus_daemon_connect_to_name_owner_changed (priv->dbus_daemon,
        name_owner_changed_cb, NULL, NULL, object, NULL);

    tp_cli_dbus_daemon_call_list_names (priv->dbus_daemon,
        -1, list_names_cb, NULL, NULL, object);

    dgc = TP_PROXY (priv->dbus_daemon)->dbus_connection;

    if (!tp_dbus_daemon_request_name (priv->dbus_daemon,
                                      MCD_CHANNEL_DISPATCHER_BUS_NAME,
                                      TRUE /* idempotent */, &error))
    {
        /* FIXME: put in proper error handling when MC gains the ability to
         * be the AM or the CD but not both */
        g_error ("Failed registering '%s' service: %s",
                 MCD_CHANNEL_DISPATCHER_BUS_NAME, error->message);
        g_error_free (error);
        exit (1);
    }

    dbus_g_connection_register_g_object (dgc,
                                         MCD_CHANNEL_DISPATCHER_OBJECT_PATH,
                                         object);
}

static void
mcd_dispatcher_class_init (McdDispatcherClass * klass)
{
    static TpDBusPropertiesMixinPropImpl cd_props[] = {
        { "Interfaces", "interfaces", NULL },
        { NULL }
    };
    static TpDBusPropertiesMixinPropImpl op_list_props[] = {
        { "DispatchOperations", "dispatch-operations", NULL },
        { NULL }
    };
    static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
        { TP_IFACE_CHANNEL_DISPATCHER,
          tp_dbus_properties_mixin_getter_gobject_properties,
          NULL,
          cd_props,
        },
        { TP_IFACE_CHANNEL_DISPATCHER_INTERFACE_OPERATION_LIST,
          tp_dbus_properties_mixin_getter_gobject_properties,
          NULL,
          op_list_props,
        },
        { NULL }
    };
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (McdDispatcherPrivate));

    object_class->constructed = mcd_dispatcher_constructed;
    object_class->set_property = _mcd_dispatcher_set_property;
    object_class->get_property = _mcd_dispatcher_get_property;
    object_class->finalize = _mcd_dispatcher_finalize;
    object_class->dispose = _mcd_dispatcher_dispose;

    signals[CHANNEL_ADDED] =
	g_signal_new ("channel_added",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      G_STRUCT_OFFSET (McdDispatcherClass,
				       channel_added_signal),
		      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE, 1, MCD_TYPE_CHANNEL);
    
    signals[CHANNEL_REMOVED] =
	g_signal_new ("channel_removed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      G_STRUCT_OFFSET (McdDispatcherClass,
				       channel_removed_signal),
		      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE, 1, MCD_TYPE_CHANNEL);
    
    signals[DISPATCHED] =
	g_signal_new ("dispatched",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      G_STRUCT_OFFSET (McdDispatcherClass,
				       dispatched_signal),
		      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE, 1, MCD_TYPE_CHANNEL);
    
    signals[DISPATCH_FAILED] =
	g_signal_new ("dispatch-failed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      G_STRUCT_OFFSET (McdDispatcherClass,
				       dispatch_failed_signal),
		      NULL, NULL, _mcd_marshal_VOID__OBJECT_POINTER,
		      G_TYPE_NONE, 2, MCD_TYPE_CHANNEL, G_TYPE_POINTER);

    /**
     * McdDispatcher::dispatch-completed:
     * @dispatcher: the #McdDispatcher.
     * @context: a #McdDispatcherContext.
     *
     * This signal is emitted when a dispatch operation has terminated. One can
     * inspect @context to get the status of the channels.
     * After this signal returns, @context is no longer valid.
     */
    signals[DISPATCH_COMPLETED] =
        g_signal_new ("dispatch-completed",
                      G_OBJECT_CLASS_TYPE (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);

    /* Properties */
    g_object_class_install_property
        (object_class, PROP_DBUS_DAEMON,
         g_param_spec_object ("dbus-daemon", "DBus daemon", "DBus daemon",
                              TP_TYPE_DBUS_DAEMON,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property
        (object_class, PROP_MCD_MASTER,
         g_param_spec_object ("mcd-master", "McdMaster", "McdMaster",
                              MCD_TYPE_MASTER,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

    g_object_class_install_property
        (object_class, PROP_INTERFACES,
         g_param_spec_boxed ("interfaces", "Interfaces", "Interfaces",
                             G_TYPE_STRV,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (object_class, PROP_DISPATCH_OPERATIONS,
         g_param_spec_boxed ("dispatch-operations",
                             "ChannelDispatchOperation details",
                             "A dbus-glib a(oa{sv})",
                             TP_ARRAY_TYPE_DISPATCH_OPERATION_DETAILS_LIST,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    client_ready_quark = g_quark_from_static_string ("mcd_client_ready");

    klass->dbus_properties_class.interfaces = prop_interfaces,
    tp_dbus_properties_mixin_class_init (object_class,
        G_STRUCT_OFFSET (McdDispatcherClass, dbus_properties_class));
}

static void
_build_channel_capabilities (const gchar *channel_type, guint type_flags,
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
			    1, type_flags,
			    G_MAXUINT);

    g_ptr_array_add (capabilities, g_value_get_boxed (&cap));
}

static void
mcd_dispatcher_init (McdDispatcher * dispatcher)
{
    McdDispatcherPrivate *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE (dispatcher, MCD_TYPE_DISPATCHER,
                                        McdDispatcherPrivate);
    dispatcher->priv = priv;

    priv->operation_list_active = FALSE;

    priv->clients = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
        (GDestroyNotify) mcd_client_free);

    priv->handler_map = _mcd_handler_map_new ();

    priv->connections = g_hash_table_new (NULL, NULL);
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

/**
 * mcd_dispatcher_context_proceed:
 * @context: a #McdDispatcherContext
 *
 * Must be called by plugin filters exactly once per invocation of the filter
 * function, to proceed with processing of the @context. This does nothing
 * if @context has already finished.
 */
void
mcd_dispatcher_context_proceed (McdDispatcherContext *context)
{
    GError error = { TP_ERRORS, 0, NULL };
    McdFilter *filter;

    if (context->cancelled)
    {
        error.code = TP_ERROR_CANCELLED;
        error.message = "Channel request cancelled";
        _mcd_dispatcher_context_abort (context, &error);
        goto no_more;
    }

    if (context->channels == NULL)
    {
        DEBUG ("No channels left");
        goto no_more;
    }

    filter = g_list_nth_data (context->chain, context->next_func_index);

    if (filter != NULL)
    {
        context->next_func_index++;
        DEBUG ("Next filter");
        mcd_dispatcher_context_ref (context, "CTXREF10");
        filter->func (context, filter->user_data);
        mcd_dispatcher_context_unref (context, "CTXREF10");
        /* The state machine goes on... this function will be invoked again
         * (perhaps recursively, or perhaps later) by filter->func. */
        return;
    }

    mcd_dispatcher_run_clients (context);

no_more:
    mcd_dispatcher_context_unref (context, "CTXREF01");
}

/**
 * mcd_dispatcher_context_forget_all:
 * @context: a #McdDispatcherContext
 *
 * Stop processing channels in @context, but do not close them. They will
 * no longer be dispatched, and the ChannelDispatchOperation (if any)
 * will emit ChannelLost.
 */
void
mcd_dispatcher_context_forget_all (McdDispatcherContext *context)
{
    GList *list;

    g_return_if_fail (context);

    /* make a temporary copy, which is destroyed during the loop - otherwise
     * we'll be trying to iterate over context->channels at the same time
     * that mcd_mission_abort results in modifying it, which would be bad */
    list = g_list_copy (context->channels);
    g_list_foreach (list, (GFunc) g_object_ref, NULL);

    while (list != NULL)
    {
        mcd_mission_abort (list->data);
        g_object_unref (list->data);
        list = g_list_delete_link (list, list);
    }

    g_return_if_fail (context->channels == NULL);
}

/**
 * mcd_dispatcher_context_destroy_all:
 * @context: a #McdDispatcherContext
 *
 * Consider all channels in the #McdDispatcherContext to be undispatchable,
 * and close them destructively. Information loss might result.
 *
 * Plugins must still call mcd_dispatcher_context_proceed() afterwards,
 * to release their reference to the dispatcher context.
 */
void
mcd_dispatcher_context_destroy_all (McdDispatcherContext *context)
{
    GList *list;

    g_return_if_fail (context);

    list = g_list_copy (context->channels);
    g_list_foreach (list, (GFunc) g_object_ref, NULL);

    while (list != NULL)
    {
        _mcd_channel_undispatchable (list->data);
        g_object_unref (list->data);
        list = g_list_delete_link (list, list);
    }

    mcd_dispatcher_context_forget_all (context);
}

/**
 * mcd_dispatcher_context_close_all:
 * @context: a #McdDispatcherContext
 * @reason: a reason code
 * @message: a message to be used if applicable, which should be "" if
 *  no message is appropriate
 *
 * Close all channels in the #McdDispatcherContext. If @reason is not
 * %TP_CHANNEL_GROUP_CHANGE_REASON_NONE and/or @message is non-empty,
 * attempt to use the RemoveMembersWithReason D-Bus method to specify
 * a message and reason, falling back to the Close method if that doesn't
 * work.
 *
 * Plugins must still call mcd_dispatcher_context_proceed() afterwards,
 * to release their reference to the dispatcher context.
 */
void
mcd_dispatcher_context_close_all (McdDispatcherContext *context,
                                  TpChannelGroupChangeReason reason,
                                  const gchar *message)
{
    GList *list;

    g_return_if_fail (context);

    if (message == NULL)
    {
        message = "";
    }

    list = g_list_copy (context->channels);
    g_list_foreach (list, (GFunc) g_object_ref, NULL);

    while (list != NULL)
    {
        _mcd_channel_depart (list->data, reason, message);
        g_object_unref (list->data);
        list = g_list_delete_link (list, list);
    }

    mcd_dispatcher_context_forget_all (context);
}

/**
 * mcd_dispatcher_context_process:
 * @context: a #McdDispatcherContext
 * @result: %FALSE if the channels are to be destroyed
 *
 * Continue to process the @context.
 *
 * mcd_dispatcher_context_process (c, TRUE) is exactly equivalent to
 * mcd_dispatcher_context_proceed (c), which should be used instead in new
 * code.
 *
 * mcd_dispatcher_context_process (c, TRUE) is exactly equivalent to
 * mcd_dispatcher_context_destroy_all (c) followed by
 * mcd_dispatcher_context_proceed (c), which should be used instead in new
 * code.
 */
void
mcd_dispatcher_context_process (McdDispatcherContext * context, gboolean result)
{
    if (!result)
    {
        mcd_dispatcher_context_destroy_all (context);
    }

    mcd_dispatcher_context_proceed (context);
}

static void
mcd_dispatcher_context_unref (McdDispatcherContext * context,
                              const gchar *tag)
{
    McdDispatcherPrivate *priv;
    GList *list;

    /* FIXME: check for leaks */
    g_return_if_fail (context);
    g_return_if_fail (context->ref_count > 0);

    DEBUG ("%s on %p (ref = %d)", tag, context, context->ref_count);
    context->ref_count--;
    if (context->ref_count == 0)
    {
        DEBUG ("freeing the context %p", context);
        for (list = context->channels; list != NULL; list = list->next)
        {
            McdChannel *channel = MCD_CHANNEL (list->data);
            g_signal_handlers_disconnect_by_func (channel,
                G_CALLBACK (on_channel_abort_context), context);
            g_object_unref (channel);
        }
        /* disposing the dispatch operation also frees the channels list */
        if (context->operation)
        {
            g_signal_handlers_disconnect_by_func (context->operation,
                                                  on_operation_finished,
                                                  context);

            if (_mcd_dispatch_operation_finish (context->operation) &&
                context->dispatcher->priv->operation_list_active)
            {
                tp_svc_channel_dispatcher_interface_operation_list_emit_dispatch_operation_finished (
                    context->dispatcher,
                    mcd_dispatch_operation_get_path (context->operation));
            }

            g_object_unref (context->operation);
        }
        else
            g_list_free (context->channels);

        /* remove the context from the list of active contexts */
        priv = MCD_DISPATCHER_PRIV (context->dispatcher);
        priv->contexts = g_list_remove (priv->contexts, context);

        g_strfreev (context->possible_handlers);
        g_free (context->protocol);
        g_free (context);
    }
}

/* CONTEXT API */

/* Context getters */
TpChannel *
mcd_dispatcher_context_get_channel_object (McdDispatcherContext * ctx)
{
    TpChannel *tp_chan;
    g_return_val_if_fail (ctx, 0);
    g_object_get (G_OBJECT (mcd_dispatcher_context_get_channel (ctx)),
                  "tp-channel", &tp_chan, NULL);
    g_object_unref (G_OBJECT (tp_chan));
    return tp_chan;
}

McdDispatcher*
mcd_dispatcher_context_get_dispatcher (McdDispatcherContext * ctx)
{
    return ctx->dispatcher;
}

/**
 * mcd_dispatcher_context_get_connection:
 * @context: the #McdDispatcherContext.
 *
 * Returns: the #McdConnection.
 */
McdConnection *
mcd_dispatcher_context_get_connection (McdDispatcherContext *context)
{
    g_return_val_if_fail (context, NULL);
    g_return_val_if_fail (context->channels != NULL, NULL);
    return MCD_CONNECTION (mcd_mission_get_parent
                           (MCD_MISSION (context->channels->data)));
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
    if (ctx->main_channel)
        return ctx->main_channel;
    else
        return ctx->channels ? MCD_CHANNEL (ctx->channels->data) : NULL;
}

/**
 * mcd_dispatcher_context_get_channels:
 * @context: the #McdDispatcherContext.
 *
 * Returns: a #GList of #McdChannel elements.
 */
const GList *
mcd_dispatcher_context_get_channels (McdDispatcherContext *context)
{
    g_return_val_if_fail (context != NULL, NULL);
    return context->channels;
}

/**
 * mcd_dispatcher_context_get_channel_by_type:
 * @context: the #McdDispatcherContext.
 * @type: the #GQuark representing the channel type.
 *
 * Returns: the first #McdChannel of the requested type, or %NULL.
 */
McdChannel *
mcd_dispatcher_context_get_channel_by_type (McdDispatcherContext *context,
                                            GQuark type)
{
    GList *list;

    g_return_val_if_fail (context != NULL, NULL);
    for (list = context->channels; list != NULL; list = list->next)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);

        if (mcd_channel_get_channel_type_quark (channel) == type)
            return channel;
    }
    return NULL;
}

GPtrArray *
_mcd_dispatcher_get_channel_capabilities (McdDispatcher *dispatcher,
                                          const gchar *protocol)
{
    McdDispatcherPrivate *priv = dispatcher->priv;
    GPtrArray *channel_handler_caps;
    GHashTableIter iter;
    gpointer key, value;

    channel_handler_caps = g_ptr_array_new ();

    /* Add the capabilities from the new-style clients */
    g_hash_table_iter_init (&iter, priv->clients);
    while (g_hash_table_iter_next (&iter, &key, &value)) 
    {
        McdClient *client = value;
        GList *list;

        for (list = client->handler_filters; list != NULL; list = list->next)
        {
            GHashTable *channel_class = list->data;
            const gchar *channel_type;
            guint type_flags;

            channel_type = tp_asv_get_string (channel_class,
                                              TP_IFACE_CHANNEL ".ChannelType");
            if (!channel_type) continue;

            /* There is currently no way to map the HandlerChannelFilter client
             * property into type-specific capabilities. Let's pretend we
             * support everything. */
            type_flags = 0xffffffff;

            _build_channel_capabilities (channel_type, type_flags,
                                         channel_handler_caps);
        }
    }
    return channel_handler_caps;
}

GPtrArray *
_mcd_dispatcher_get_channel_enhanced_capabilities (McdDispatcher *dispatcher)
{
    McdDispatcherPrivate *priv = dispatcher->priv;
    GHashTableIter iter;
    gpointer key, value;
    GPtrArray *caps = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, priv->clients);
    while (g_hash_table_iter_next (&iter, &key, &value)) 
    {
        McdClient *client = value;
        GList *list;

        for (list = client->handler_filters; list != NULL; list = list->next)
        {
            GHashTable *channel_class = list->data;
            guint i;
            gboolean already_in_caps = FALSE;

            /* Check if the filter is already in the caps variable */
            for (i = 0 ; i < caps->len ; i++)
            {
                GHashTable *channel_class2 = g_ptr_array_index (caps, i);
                if (channel_classes_equals (channel_class, channel_class2))
                {
                    already_in_caps = TRUE;
                    break;
                }
            }

            if (! already_in_caps)
                g_ptr_array_add (caps, channel_class);
        }
    }

    return caps;
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

static void
remove_request_data_free (McdRemoveRequestData *rrd)
{
    g_object_unref (rrd->handler);
    g_free (rrd->request_path);
    g_slice_free (McdRemoveRequestData, rrd);
}

static void
on_request_status_changed (McdChannel *channel, McdChannelStatus status,
                           McdRemoveRequestData *rrd)
{
    if (status != MCD_CHANNEL_STATUS_FAILED &&
        status != MCD_CHANNEL_STATUS_DISPATCHED)
        return;

    DEBUG ("called, %u", status);
    if (status == MCD_CHANNEL_STATUS_FAILED)
    {
        const GError *error;
        gchar *err_string;
        error = mcd_channel_get_error (channel);
        err_string = _mcd_build_error_string (error);
        /* no callback, as we don't really care */
        DEBUG ("calling RemoveRequest on %s for %s",
               tp_proxy_get_object_path (rrd->handler), rrd->request_path);
        tp_cli_client_interface_requests_call_remove_request
            (rrd->handler, -1, rrd->request_path, err_string, error->message,
             NULL, NULL, NULL, NULL);
        g_free (err_string);
    }

    /* we don't need the McdRemoveRequestData anymore */
    remove_request_data_free (rrd);
    g_signal_handlers_disconnect_by_func (channel, on_request_status_changed,
                                          rrd);
}

/*
 * _mcd_dispatcher_add_request:
 * @context: the #McdDispatcherContext.
 * @account: the #McdAccount.
 * @channels: a #McdChannel in MCD_CHANNEL_REQUEST state.
 *
 * Add a request; this basically means invoking AddRequest (and maybe
 * RemoveRequest) on the channel handler.
 */
void
_mcd_dispatcher_add_request (McdDispatcher *dispatcher, McdAccount *account,
                             McdChannel *channel)
{
    McdDispatcherPrivate *priv;
    McdClient *handler = NULL;
    GHashTable *properties;
    GValue v_user_time = { 0, };
    GValue v_requests = { 0, };
    GValue v_account = { 0, };
    GValue v_preferred_handler = { 0, };
    GValue v_interfaces = { 0, };
    GPtrArray *requests;
    McdRemoveRequestData *rrd;

    g_return_if_fail (MCD_IS_DISPATCHER (dispatcher));
    g_return_if_fail (MCD_IS_CHANNEL (channel));

    priv = dispatcher->priv;

    handler = get_default_handler (dispatcher, channel);
    if (!handler)
    {
        /* No handler found. But it's possible that by the time that the
         * channel will be created some handler will have popped up, so we
         * must not destroy it. */
        DEBUG ("No handler for request %s",
               _mcd_channel_get_request_path (channel));
        return;
    }

    if (!(handler->interfaces & MCD_CLIENT_INTERFACE_REQUESTS))
    {
        DEBUG ("Default handler %s for request %s doesn't want AddRequest",
               handler->name, _mcd_channel_get_request_path (channel));
        return;
    }

    DEBUG ("Calling AddRequest on default handler %s for request %s",
           handler->name, _mcd_channel_get_request_path (channel));

    properties = g_hash_table_new (g_str_hash, g_str_equal);

    g_value_init (&v_user_time, G_TYPE_UINT64);
    g_value_set_uint64 (&v_user_time,
                        _mcd_channel_get_request_user_action_time (channel));
    g_hash_table_insert (properties, "org.freedesktop.Telepathy.ChannelRequest"
                         ".UserActionTime", &v_user_time);

    requests = g_ptr_array_sized_new (1);
    g_ptr_array_add (requests,
                     _mcd_channel_get_requested_properties (channel));
    g_value_init (&v_requests, dbus_g_type_get_collection ("GPtrArray",
         TP_HASH_TYPE_QUALIFIED_PROPERTY_VALUE_MAP));
    g_value_set_static_boxed (&v_requests, requests);
    g_hash_table_insert (properties, "org.freedesktop.Telepathy.ChannelRequest"
                         ".Requests", &v_requests);

    g_value_init (&v_account, DBUS_TYPE_G_OBJECT_PATH);
    g_value_set_static_boxed (&v_account,
                              mcd_account_get_object_path (account));
    g_hash_table_insert (properties, "org.freedesktop.Telepathy.ChannelRequest"
                         ".Account", &v_account);

    g_value_init (&v_interfaces, G_TYPE_STRV);
    g_value_set_static_boxed (&v_interfaces, NULL);
    g_hash_table_insert (properties, "org.freedesktop.Telepathy.ChannelRequest"
                         ".Interfaces", &v_interfaces);

    g_value_init (&v_preferred_handler, G_TYPE_STRING);
    g_value_set_static_string (&v_preferred_handler,
        _mcd_channel_get_request_preferred_handler (channel));
    g_hash_table_insert (properties, "org.freedesktop.Telepathy.ChannelRequest"
                         ".PreferredHandler", &v_preferred_handler);

    tp_cli_client_interface_requests_call_add_request (
        handler->proxy, -1,
        _mcd_channel_get_request_path (channel), properties,
        NULL, NULL, NULL, NULL);

    g_hash_table_unref (properties);
    g_ptr_array_free (requests, TRUE);

    /* Prepare for a RemoveRequest */
    rrd = g_slice_new (McdRemoveRequestData);
    /* store the request path, because it might not be available when the
     * channel status changes */
    rrd->request_path = g_strdup (_mcd_channel_get_request_path (channel));
    rrd->handler = handler->proxy;
    g_object_ref (handler->proxy);
    /* we must watch whether the request fails and in that case call
     * RemoveRequest */
    g_signal_connect (channel, "status-changed",
                      G_CALLBACK (on_request_status_changed), rrd);
}

/*
 * _mcd_dispatcher_take_channels:
 * @dispatcher: the #McdDispatcher.
 * @channels: a #GList of #McdChannel elements.
 * @requested: whether the channels were requested by MC.
 *
 * Dispatch @channels. The #GList @channels will be no longer valid after this
 * function has been called.
 */
void
_mcd_dispatcher_take_channels (McdDispatcher *dispatcher, GList *channels,
                               gboolean requested)
{
    GList *list;
    GStrv possible_handlers;

    if (channels == NULL)
    {
        /* trivial case */
        return;
    }

    /* See if there are any handlers that can take all these channels */
    possible_handlers = mcd_dispatcher_get_possible_handlers (dispatcher,
                                                              channels);

    if (possible_handlers == NULL)
    {
        if (channels->next == NULL)
        {
            /* There's exactly one channel and we can't handle it. It must
             * die. */
            _mcd_channel_undispatchable (channels->data);
            g_list_free (channels);
        }
        else
        {
            /* there are >= 2 channels - split the batch up and try again */
            while (channels != NULL)
            {
                list = channels;
                channels = g_list_remove_link (channels, list);
                _mcd_dispatcher_take_channels (dispatcher, list, requested);
            }
        }
    }
    else
    {
        for (list = channels; list != NULL; list = list->next)
            _mcd_channel_set_status (MCD_CHANNEL (list->data),
                                     MCD_CHANNEL_STATUS_DISPATCHING);

        _mcd_dispatcher_enter_state_machine (dispatcher, channels,
                                             possible_handlers, requested);
    }
}

/**
 * mcd_dispatcher_add_filter:
 * @dispatcher: The #McdDispatcher.
 * @filter: the filter function to be registered.
 * @priority: The priority of the filter.
 * @user_data: user data to be passed to @filter on invocation.
 *
 * Register a filter into the dispatcher chain: @filter will be invoked
 * whenever channels need to be dispatched.
 */
void
mcd_dispatcher_add_filter (McdDispatcher *dispatcher,
                           McdFilterFunc filter,
                           guint priority,
                           gpointer user_data)
{
    McdDispatcherPrivate *priv;

    g_return_if_fail (MCD_IS_DISPATCHER (dispatcher));
    priv = dispatcher->priv;
    priv->filters =
        chain_add_filter (priv->filters, filter, priority, user_data);
}

/**
 * mcd_dispatcher_add_filters:
 * @dispatcher: The #McdDispatcher.
 * @filters: a zero-terminated array of #McdFilter elements.
 *
 * Convenience function to add a batch of filters at once.
 */
void
mcd_dispatcher_add_filters (McdDispatcher *dispatcher,
                            const McdFilter *filters)
{
    const McdFilter *filter;

    g_return_if_fail (filters != NULL);

    for (filter = filters; filter->func != NULL; filter++)
        mcd_dispatcher_add_filter (dispatcher, filter->func,
                                   filter->priority,
                                   filter->user_data);
}

static void
on_redispatched_channel_status_changed (McdChannel *channel,
                                        McdChannelStatus status)
{
    if (status == MCD_CHANNEL_STATUS_DISPATCHED)
    {
        mcd_mission_abort (MCD_MISSION (channel));
    }
}

/*
 * _mcd_dispatcher_reinvoke_handler:
 * @dispatcher: The #McdDispatcher.
 * @channel: a #McdChannel.
 *
 * Re-invoke the channel handler for @channel.
 */
static void
_mcd_dispatcher_reinvoke_handler (McdDispatcher *dispatcher,
                                  McdChannel *channel)
{
    McdDispatcherContext *context;
    GList *list;

    /* Preparing and filling the context */
    context = g_new0 (McdDispatcherContext, 1);
    DEBUG ("CTXREF12 on %p", context);
    context->ref_count = 1;
    context->dispatcher = dispatcher;
    context->channels = g_list_prepend (NULL, channel);
    context->account = mcd_channel_get_account (channel);

    list = g_list_append (NULL, channel);
    context->possible_handlers = mcd_dispatcher_get_possible_handlers (
        dispatcher, list);
    g_list_free (list);

    /* We must ref() the channel, because
     * mcd_dispatcher_context_unref() will unref() it */
    g_object_ref (channel);
    mcd_dispatcher_run_handlers (context);
    /* the context will be unreferenced once it leaves the state machine */

    mcd_dispatcher_context_unref (context, "CTXREF12");
}

static McdDispatcherContext *
find_context_from_channel (McdDispatcher *dispatcher, McdChannel *channel)
{
    GList *list;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    for (list = dispatcher->priv->contexts; list != NULL; list = list->next)
    {
        McdDispatcherContext *context = list->data;

        if (g_list_find (context->channels, channel))
            return context;
    }
    return NULL;
}

void
_mcd_dispatcher_add_channel_request (McdDispatcher *dispatcher,
                                     McdChannel *channel, McdChannel *request)
{
    McdChannelStatus status;

    status = mcd_channel_get_status (channel);

    /* if the channel is already dispatched, just reinvoke the handler; if it
     * is not, @request must mirror the status of @channel */
    if (status == MCD_CHANNEL_STATUS_DISPATCHED)
    {
        DEBUG ("reinvoking handler on channel %p", channel);

        /* copy the object path and the immutable properties from the
         * existing channel */
        _mcd_channel_copy_details (request, channel);

        /* destroy the McdChannel object after it is dispatched */
        g_signal_connect_after
            (request, "status-changed",
             G_CALLBACK (on_redispatched_channel_status_changed), NULL);

        _mcd_dispatcher_reinvoke_handler (dispatcher, request);
    }
    else
    {
        _mcd_channel_set_request_proxy (request, channel);
        if (status == MCD_CHANNEL_STATUS_DISPATCHING)
        {
            McdDispatcherContext *context;

            context = find_context_from_channel (dispatcher, channel);
            DEBUG ("channel %p is in context %p", channel, context);
            if (context->approvers_invoked > 0)
            {
                /* the existing channel is waiting for approval; but since the
                 * same channel has been requested, the approval operation must
                 * terminate */
                if (G_LIKELY (context->operation))
                    mcd_dispatch_operation_handle_with (context->operation,
                                                        NULL, NULL);
            }
            else
                context->skip_approval = TRUE;
        }
        DEBUG ("channel %p is proxying %p", request, channel);
    }
}

void
_mcd_dispatcher_recover_channel (McdDispatcher *dispatcher,
                                 McdChannel *channel)
{
    McdDispatcherPrivate *priv;
    const gchar *path;
    const gchar *unique_name;
    gboolean requested;

    /* we must check if the channel is already being handled by some client; to
     * do this, we can examine the active handlers' "HandledChannel" property.
     * By now, we should already have done this, because startup has completed.
     */
    g_return_if_fail (MCD_IS_DISPATCHER (dispatcher));
    priv = dispatcher->priv;
    g_return_if_fail (priv->startup_completed);

    path = mcd_channel_get_object_path (channel);
    unique_name = _mcd_handler_map_get_handler (priv->handler_map, path);

    if (unique_name != NULL)
    {
        DEBUG ("Channel %s is already handled by process %s",
               path, unique_name);
        _mcd_channel_set_status (channel,
                                 MCD_CHANNEL_STATUS_DISPATCHED);
        _mcd_handler_map_set_channel_handled (priv->handler_map, channel,
                                              unique_name);
    }
    else
    {
        DEBUG ("%s is unhandled, redispatching", path);

        requested = mcd_channel_is_requested (channel);
        _mcd_dispatcher_take_channels (dispatcher,
                                       g_list_prepend (NULL, channel),
                                       requested);
    }
}

static void
dispatcher_request_channel (McdDispatcher *self,
                            const gchar *account_path,
                            GHashTable *requested_properties,
                            gint64 user_action_time,
                            const gchar *preferred_handler,
                            DBusGMethodInvocation *context,
                            gboolean ensure)
{
    McdAccountManager *am;
    McdAccount *account;
    McdChannel *channel;
    GError *error = NULL;
    const gchar *path;

    g_return_if_fail (account_path != NULL);
    g_return_if_fail (requested_properties != NULL);
    g_return_if_fail (preferred_handler != NULL);

    g_object_get (self->priv->master,
                  "account-manager", &am,
                  NULL);

    g_assert (am != NULL);

    account = mcd_account_manager_lookup_account_by_path (am,
                                                          account_path);

    if (account == NULL)
    {
        g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "No such account: %s", account_path);
        goto despair;
    }

    if (preferred_handler[0] != '\0')
    {
        if (!tp_dbus_check_valid_bus_name (preferred_handler,
                                           TP_DBUS_NAME_TYPE_WELL_KNOWN,
                                           &error))
        {
            /* The error is TP_DBUS_ERROR_INVALID_BUS_NAME, which has no D-Bus
             * representation; re-map to InvalidArgument. */
            error->domain = TP_ERRORS;
            error->code = TP_ERROR_INVALID_ARGUMENT;
            goto despair;
        }

        if (!g_str_has_prefix (preferred_handler, TP_CLIENT_BUS_NAME_BASE))
        {
            g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                         "Not a Telepathy Client: %s", preferred_handler);
            goto despair;
        }
    }

    channel = _mcd_account_create_request (account, requested_properties,
                                           user_action_time, preferred_handler,
                                           ensure, FALSE, &error);

    if (channel == NULL)
    {
        /* FIXME: ideally this would be emitted as a Failed signal after
         * Proceed is called, but for the particular failure case here (low
         * memory) perhaps we don't want to */
        goto despair;
    }

    path = _mcd_channel_get_request_path (channel);

    g_assert (path != NULL);

    /* This is OK because the signatures of CreateChannel and EnsureChannel
     * are the same */
    tp_svc_channel_dispatcher_return_from_create_channel (context, path);

    _mcd_dispatcher_add_request (self, account, channel);

    /* We've done all we need to with this channel: the ChannelRequests code
     * keeps it alive as long as is necessary */
    g_object_unref (channel);
    goto finally;

despair:
    dbus_g_method_return_error (context, error);
    g_error_free (error);

finally:
    g_object_unref (am);
}

static void
dispatcher_create_channel (TpSvcChannelDispatcher *iface,
                           const gchar *account_path,
                           GHashTable *requested_properties,
                           gint64 user_action_time,
                           const gchar *preferred_handler,
                           DBusGMethodInvocation *context)
{
    dispatcher_request_channel (MCD_DISPATCHER (iface),
                                account_path,
                                requested_properties,
                                user_action_time,
                                preferred_handler,
                                context,
                                FALSE);
}

static void
dispatcher_ensure_channel (TpSvcChannelDispatcher *iface,
                           const gchar *account_path,
                           GHashTable *requested_properties,
                           gint64 user_action_time,
                           const gchar *preferred_handler,
                           DBusGMethodInvocation *context)
{
    dispatcher_request_channel (MCD_DISPATCHER (iface),
                                account_path,
                                requested_properties,
                                user_action_time,
                                preferred_handler,
                                context,
                                TRUE);
}

static void
dispatcher_iface_init (gpointer g_iface,
                       gpointer iface_data G_GNUC_UNUSED)
{
#define IMPLEMENT(x) tp_svc_channel_dispatcher_implement_##x (\
    g_iface, dispatcher_##x)
    IMPLEMENT (create_channel);
    IMPLEMENT (ensure_channel);
#undef IMPLEMENT
}

static void
mcd_dispatcher_lost_connection (gpointer data,
                                GObject *corpse)
{
    McdDispatcher *self = MCD_DISPATCHER (data);

    /* not safe to dereference corpse any more, so just print its address -
     * that's enough to pair up with add_connection calls */
    DEBUG ("%p: %p", self, corpse);

    g_hash_table_remove (self->priv->connections, corpse);
    g_object_unref (self);
}

void
_mcd_dispatcher_add_connection (McdDispatcher *self,
                                McdConnection *connection)
{
    g_return_if_fail (MCD_IS_DISPATCHER (self));

    DEBUG ("%p: %p (%s)", self, connection,
           mcd_connection_get_object_path (connection));

    g_hash_table_insert (self->priv->connections, connection, connection);
    g_object_weak_ref ((GObject *) connection, mcd_dispatcher_lost_connection,
                       g_object_ref (self));

    if (self->priv->startup_completed)
    {
        _mcd_connection_start_dispatching (connection);
    }
    /* else _mcd_connection_start_dispatching will be called when we're ready
     * for it */
}
