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
#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gi18n.h>

#include <dbus/dbus-glib-lowlevel.h>

#include "client-registry.h"
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
#include "mcd-dispatch-operation-priv.h"
#include "mcd-handler-map-priv.h"
#include "mcd-misc.h"

#include <telepathy-glib/defs.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/handle-repo-dynamic.h>
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

    McdDispatcher *dispatcher;

    McdDispatchOperation *operation;

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

struct _McdDispatcherPrivate
{
    /* Dispatching contexts */
    GList *contexts;

    /* Channel dispatch operations */
    GList *operations;

    TpDBusDaemon *dbus_daemon;

    /* Array of channel handler's capabilities, stored as a GPtrArray for
     * performance reasons */
    GPtrArray *channel_handler_caps;

    /* list of McdFilter elements */
    GList *filters;

    /* hash table containing clients
     * char *bus_name -> McdClientProxy */
    McdClientRegistry *clients;

    McdHandlerMap *handler_map;

    McdMaster *master;

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

static void mcd_dispatcher_context_unref (McdDispatcherContext * ctx,
                                          const gchar *tag);
static void on_operation_finished (McdDispatchOperation *operation,
                                   McdDispatcher *self);


static inline void
mcd_dispatcher_context_ref (McdDispatcherContext *context,
                            const gchar *tag)
{
    g_return_if_fail (context != NULL);
    DEBUG ("%s on %p (ref = %d)", tag, context, context->ref_count);
    context->ref_count++;
}

static void
mcd_dispatcher_context_unref_3 (gpointer p)
{
    mcd_dispatcher_context_unref (p, "CTXREF03");
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
                {
                    DEBUG ("Channel %p is active", channel);
                    usage_counter++;
                }
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
match_filters (GHashTable *channel_properties,
               const GList *filters,
               gboolean assume_requested)
{
    const GList *list;
    guint best_quality = 0;

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
            if (assume_requested &&
                ! tp_strdiff (property_name, TP_IFACE_CHANNEL ".Requested"))
            {
                if (! G_VALUE_HOLDS_BOOLEAN (filter_value) ||
                    ! g_value_get_boolean (filter_value))
                {
                    filter_matched = FALSE;
                    break;
                }
            }
            else if (! match_property (channel_properties, property_name,
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

static McdClientProxy *
mcd_dispatcher_guess_request_handler (McdDispatcher *dispatcher,
                                      McdChannel *channel)
{
    GHashTable *channel_properties;
    GHashTableIter iter;
    gpointer client;

    /* FIXME: return the "most preferred" handler, not just any handler that
     * can take it */

    channel_properties = _mcd_channel_get_requested_properties (channel);

    _mcd_client_registry_init_hash_iter (dispatcher->priv->clients, &iter);
    while (g_hash_table_iter_next (&iter, NULL, &client))
    {
        if (!tp_proxy_has_interface_by_id (client,
                                           TP_IFACE_QUARK_CLIENT_HANDLER))
            continue;

        if (match_filters (channel_properties,
            _mcd_client_proxy_get_handler_filters (client),
            TRUE) > 0)
            return client;
    }
    return NULL;
}

static void mcd_dispatcher_run_handlers (McdDispatchOperation *op,
                                         McdDispatcherContext *context);

static void
handle_channels_cb (TpClient *proxy, const GError *error, gpointer user_data,
                    GObject *weak_object)
{
    McdDispatcherContext *context = user_data;

    mcd_dispatcher_context_ref (context, "CTXREF02");
    _mcd_dispatch_operation_handle_channels_cb (context->operation,
                                                proxy, error);
    mcd_dispatcher_context_unref (context, "CTXREF02");
}

typedef struct
{
    McdClientProxy *client;
    gboolean bypass;
    gsize quality;
} PossibleHandler;

static gint
possible_handler_cmp (gconstpointer a_,
                      gconstpointer b_)
{
    const PossibleHandler *a = a_;
    const PossibleHandler *b = b_;

    if (a->bypass)
    {
        if (!b->bypass)
        {
            /* BypassApproval wins, so a is better than b */
            return 1;
        }
    }
    else if (b->bypass)
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
mcd_dispatcher_dup_possible_handlers (McdDispatcher *self,
                                      const GList *channels,
                                      const gchar *must_have_unique_name)
{
    GList *handlers = NULL;
    const GList *iter;
    GHashTableIter client_iter;
    gpointer client_p;
    guint n_handlers = 0;
    guint i;
    GStrv ret;

    _mcd_client_registry_init_hash_iter (self->priv->clients, &client_iter);

    while (g_hash_table_iter_next (&client_iter, NULL, &client_p))
    {
        McdClientProxy *client = MCD_CLIENT_PROXY (client_p);
        gsize total_quality = 0;

        if (must_have_unique_name != NULL &&
            tp_strdiff (must_have_unique_name,
                        _mcd_client_proxy_get_unique_name (client)))
        {
            /* we're trying to redispatch to an existing handler, and this is
             * not it */
            continue;
        }

        if (!tp_proxy_has_interface_by_id (client,
                                           TP_IFACE_QUARK_CLIENT_HANDLER))
        {
            /* not a handler at all */
            continue;
        }

        for (iter = channels; iter != NULL; iter = iter->next)
        {
            McdChannel *channel = MCD_CHANNEL (iter->data);
            GHashTable *properties;
            guint quality;

            properties = _mcd_channel_get_immutable_properties (channel);

            if (properties == NULL)
            {
                properties = _mcd_channel_get_requested_properties (channel);
            }

            quality = match_filters (properties,
                _mcd_client_proxy_get_handler_filters (client),
                FALSE);

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
            ph->bypass = _mcd_client_proxy_get_bypass_approval (client);
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

        ret[i] = g_strdup (tp_proxy_get_bus_name (ph->client));
        g_slice_free (PossibleHandler, ph);
    }

    ret[n_handlers] = NULL;

    g_list_free (handlers);

    return ret;
}

/*
 * mcd_dispatcher_handle_channels:
 * @context: the #McdDispatcherContext
 * @handler: the selected handler
 *
 * Invoke the handler for the given channels.
 */
static void
mcd_dispatcher_handle_channels (McdDispatcherContext *context,
                                McdClientProxy *handler)
{
    guint64 user_action_time;
    McdConnection *connection;
    const gchar *account_path, *connection_path;
    GPtrArray *channels_array, *satisfied_requests;
    GHashTable *handler_info;
    const GList *cl;

    connection = mcd_dispatcher_context_get_connection (context);
    connection_path = connection ?
        mcd_connection_get_object_path (connection) : NULL;
    if (G_UNLIKELY (!connection_path)) connection_path = "/";

    account_path = _mcd_dispatch_operation_get_account_path
        (context->operation);

    channels_array = _mcd_dispatch_operation_dup_channel_details
        (context->operation);

    user_action_time = 0; /* TODO: if we have a CDO, get it from there */
    satisfied_requests = g_ptr_array_new ();

    for (cl = _mcd_dispatch_operation_peek_channels (context->operation);
         cl != NULL;
         cl = cl->next)
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
    mcd_dispatcher_context_ref (context, "CTXREF03");
    DEBUG ("calling HandleChannels on %s for context %p",
           tp_proxy_get_bus_name (handler), context);
    tp_cli_client_handler_call_handle_channels ((TpClient *) handler,
        -1, account_path, connection_path,
        channels_array, satisfied_requests, user_action_time,
        handler_info, handle_channels_cb,
        context, mcd_dispatcher_context_unref_3,
        (GObject *)context->dispatcher);

    g_ptr_array_free (satisfied_requests, TRUE);
    _mcd_channel_details_free (channels_array);
    g_hash_table_unref (handler_info);
}

static void
mcd_dispatcher_run_handlers (McdDispatchOperation *op,
                             McdDispatcherContext *context)
{
    McdDispatcher *self = context->dispatcher;
    GList *channels, *list;
    const gchar * const *possible_handlers;
    const gchar * const *iter;
    const gchar *approved_handler = _mcd_dispatch_operation_get_handler (
        context->operation);

    sp_timestamp ("run handlers");
    mcd_dispatcher_context_ref (context, "CTXREF04");

    /* If there is an approved handler chosen by the Approver, it's the only
     * one we'll consider. */

    if (approved_handler != NULL && approved_handler[0] != '\0')
    {
        gchar *bus_name = g_strconcat (TP_CLIENT_BUS_NAME_BASE,
                                       approved_handler, NULL);
        McdClientProxy *handler = _mcd_client_registry_lookup (
            self->priv->clients, bus_name);
        gboolean failed = _mcd_dispatch_operation_get_handler_failed
            (context->operation, bus_name);

        DEBUG ("Approved handler is %s (still exists: %c, "
               "already failed: %c)", bus_name,
               handler != NULL ? 'Y' : 'N',
               failed ? 'Y' : 'N');

        g_free (bus_name);

        /* Maybe the handler has exited since we chose it, or maybe we
         * already tried it? Otherwise, it's the right choice. */
        if (handler != NULL && !failed)
        {
            mcd_dispatcher_handle_channels (context, handler);
            goto finally;
        }

        /* The approver asked for a particular handler, but that handler
         * has vanished. If MC was fully spec-compliant, it wouldn't have
         * replied to the Approver yet, so it could just return an error.
         * However, that particular part of the flying-car future has not
         * yet arrived, so try to recover by dispatching to *something*. */
    }

    possible_handlers = _mcd_dispatch_operation_get_possible_handlers (
        context->operation);

    for (iter = possible_handlers; iter != NULL && *iter != NULL; iter++)
    {
        McdClientProxy *handler = _mcd_client_registry_lookup (
            self->priv->clients, *iter);
        gboolean failed = _mcd_dispatch_operation_get_handler_failed
            (context->operation, *iter);

        DEBUG ("Possible handler: %s (still exists: %c, already failed: %c)",
               *iter, handler != NULL ? 'Y' : 'N', failed ? 'Y' : 'N');

        if (handler != NULL && !failed)
        {
            mcd_dispatcher_handle_channels (context, handler);
            goto finally;
        }
    }

    /* All of the usable handlers vanished while we were thinking about it
     * (this can only happen if non-activatable handlers exit after we
     * include them in the list of possible handlers, but before we .
     * We should recover in some better way, perhaps by asking all the
     * approvers again (?), but for now we'll just close all the channels. */

    DEBUG ("No possible handler still exists, giving up");

    channels = _mcd_dispatch_operation_dup_channels (context->operation);

    for (list = channels; list != NULL; list = list->next)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);
        GError e = { MC_ERROR, MC_CHANNEL_REQUEST_GENERIC_ERROR,
            "Handler no longer available" };

        mcd_channel_take_error (channel, g_error_copy (&e));
        _mcd_channel_undispatchable (channel);
        g_object_unref (channel);
    }

    g_list_free (channels);

finally:
    mcd_dispatcher_context_unref (context, "CTXREF04");
}

static void
observe_channels_cb (TpClient *proxy, const GError *error,
                     gpointer user_data, GObject *weak_object)
{
    McdDispatcherContext *context = user_data;

    /* we display the error just for debugging, but we don't really care */
    if (error)
        DEBUG ("Observer %s returned error: %s",
               tp_proxy_get_object_path (proxy), error->message);
    else
        DEBUG ("success from %s", tp_proxy_get_object_path (proxy));

    _mcd_dispatch_operation_dec_observers_pending (context->operation);
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
    gpointer client_p;

    sp_timestamp ("run observers");
    channels = _mcd_dispatch_operation_peek_channels (context->operation);
    observer_info = g_hash_table_new (g_str_hash, g_str_equal);

    _mcd_client_registry_init_hash_iter (priv->clients, &iter);
    while (g_hash_table_iter_next (&iter, NULL, &client_p))
    {
        McdClientProxy *client = MCD_CLIENT_PROXY (client_p);
        GList *observed = NULL;
        McdConnection *connection;
        const gchar *account_path, *connection_path;
        GPtrArray *channels_array, *satisfied_requests;

        if (!tp_proxy_has_interface_by_id (client,
                                           TP_IFACE_QUARK_CLIENT_OBSERVER))
            continue;

        for (cl = channels; cl != NULL; cl = cl->next)
        {
            McdChannel *channel = MCD_CHANNEL (cl->data);
            GHashTable *properties;

            properties = _mcd_channel_get_immutable_properties (channel);
            g_assert (properties != NULL);

            if (match_filters (properties,
                _mcd_client_proxy_get_observer_filters (client),
                FALSE))
                observed = g_list_prepend (observed, channel);
        }
        if (!observed) continue;

        /* build up the parameters and invoke the observer */
        connection = mcd_dispatcher_context_get_connection (context);
        g_assert (connection != NULL);
        connection_path = mcd_connection_get_object_path (connection);

        account_path = _mcd_dispatch_operation_get_account_path
            (context->operation);

        /* TODO: there's room for optimization here: reuse the channels_array,
         * if the observed list is the same */
        channels_array = _mcd_channel_details_build_from_list (observed);

        satisfied_requests = collect_satisfied_requests (observed);

        if (_mcd_dispatch_operation_needs_approval (context->operation))
        {
            dispatch_operation_path =
                _mcd_dispatch_operation_get_path (context->operation);
        }

        _mcd_dispatch_operation_inc_observers_pending (context->operation);
        mcd_dispatcher_context_ref (context, "CTXREF05");

        DEBUG ("calling ObserveChannels on %s for context %p",
               tp_proxy_get_bus_name (client), context);
        tp_cli_client_observer_call_observe_channels (
            (TpClient *) client, -1,
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

static void
add_dispatch_operation_cb (TpClient *proxy, const GError *error,
                           gpointer user_data, GObject *weak_object)
{
    McdDispatcherContext *context = user_data;

    if (error)
    {
        DEBUG ("AddDispatchOperation %s (context %p) on approver %s failed: "
               "%s",
               _mcd_dispatch_operation_get_path (context->operation),
               context, tp_proxy_get_object_path (proxy), error->message);
    }
    else
    {
        DEBUG ("Approver %s accepted AddDispatchOperation %s (context %p)",
               tp_proxy_get_object_path (proxy),
               _mcd_dispatch_operation_get_path (context->operation),
               context);

        if (!_mcd_dispatch_operation_is_awaiting_approval (context->operation))
        {
            _mcd_dispatch_operation_set_awaiting_approval (context->operation,
                                                           TRUE);
            mcd_dispatcher_context_ref (context, "CTXREF14");
        }
    }

    /* If all approvers fail to add the DO, then we behave as if no
     * approver was registered: i.e., we continue dispatching. If at least
     * one approver accepted it, then we can still continue dispatching,
     * since it will be stalled until awaiting_approval becomes FALSE. */
    _mcd_dispatch_operation_dec_ado_pending (context->operation);
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
    gpointer client_p;

    g_return_if_fail (_mcd_dispatch_operation_needs_approval (
        context->operation));
    sp_timestamp ("run approvers");

    /* we temporarily increment this count and decrement it at the end of the
     * function, to make sure it won't become 0 while we are still invoking
     * approvers */
    _mcd_dispatch_operation_inc_ado_pending (context->operation);

    channels = _mcd_dispatch_operation_peek_channels (context->operation);
    _mcd_client_registry_init_hash_iter (priv->clients, &iter);
    while (g_hash_table_iter_next (&iter, NULL, &client_p))
    {
        McdClientProxy *client = MCD_CLIENT_PROXY (client_p);
        GPtrArray *channel_details;
        const gchar *dispatch_operation;
        GHashTable *properties;
        gboolean matched = FALSE;

        if (!tp_proxy_has_interface_by_id (client,
                                           TP_IFACE_QUARK_CLIENT_APPROVER))
            continue;

        for (cl = channels; cl != NULL; cl = cl->next)
        {
            McdChannel *channel = MCD_CHANNEL (cl->data);
            GHashTable *channel_properties;

            channel_properties = _mcd_channel_get_immutable_properties (channel);
            g_assert (channel_properties != NULL);

            if (match_filters (channel_properties,
                _mcd_client_proxy_get_approver_filters (client),
                FALSE))
            {
                matched = TRUE;
                break;
            }
        }
        if (!matched) continue;

        dispatch_operation =
            _mcd_dispatch_operation_get_path (context->operation);
        properties =
            _mcd_dispatch_operation_get_properties (context->operation);
        channel_details =
            _mcd_dispatch_operation_dup_channel_details (context->operation);

        DEBUG ("Calling AddDispatchOperation on approver %s for CDO %s @ %p "
               "of context %p", tp_proxy_get_bus_name (client),
               dispatch_operation, context->operation, context);

        _mcd_dispatch_operation_inc_ado_pending (context->operation);

        mcd_dispatcher_context_ref (context, "CTXREF06");
        tp_cli_client_approver_call_add_dispatch_operation (
            (TpClient *) client, -1,
            channel_details, dispatch_operation, properties,
            add_dispatch_operation_cb,
            context,
            mcd_dispatcher_context_unref_6,
            (GObject *)context->dispatcher);

        g_boxed_free (TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST, channel_details);
    }

    /* This matches the approvers count set to 1 at the beginning of the
     * function */
    _mcd_dispatch_operation_dec_ado_pending (context->operation);
}

/* Happens at the end of successful filter chain execution (empty chain
 * is always successful)
 */
static void
mcd_dispatcher_run_clients (McdDispatcherContext *context)
{
    mcd_dispatcher_context_ref (context, "CTXREF07");
    _mcd_dispatch_operation_set_invoking_early_clients (context->operation,
                                                        TRUE);

    mcd_dispatcher_run_observers (context);

    /* if the dispatch operation thinks the channels were not
     * requested, start the Approvers */
    if (_mcd_dispatch_operation_needs_approval (context->operation))
    {
        /* but if the handlers have the BypassApproval flag set, then don't
         *
         * FIXME: we should really run BypassApproval handlers as a separate
         * stage, rather than considering the existence of a BypassApproval
         * handler to constitute approval - this is fd.o #23687 */
        if (_mcd_dispatch_operation_handlers_can_bypass_approval
            (context->operation))
            _mcd_dispatch_operation_set_approved (context->operation);

        if (!_mcd_dispatch_operation_is_approved (context->operation))
            mcd_dispatcher_run_approvers (context);
    }

    _mcd_dispatch_operation_set_invoking_early_clients (context->operation,
                                                        FALSE);
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
     * we'll be trying to iterate over the list at the same time
     * that mcd_mission_abort results in modifying it, which would be bad */
    list = _mcd_dispatch_operation_dup_channels (context->operation);

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
on_operation_finished (McdDispatchOperation *operation,
                       McdDispatcher *self)
{
    /* don't emit the signal if the CDO never appeared on D-Bus */
    if (self->priv->operation_list_active &&
        _mcd_dispatch_operation_needs_approval (operation))
    {
        tp_svc_channel_dispatcher_interface_operation_list_emit_dispatch_operation_finished (
            self, _mcd_dispatch_operation_get_path (operation));
    }
}

static void
mcd_dispatcher_op_ready_to_dispatch_cb (McdDispatchOperation *operation,
                                        McdDispatcherContext *context)
{
    /* This is emitted when the HandleWith() or Claimed() are invoked on the
     * CDO: according to which of these have happened, we run the choosen
     * handler or we don't. */

    mcd_dispatcher_context_ref (context, "CTXREF15");

    /* Because of our calls to _mcd_dispatch_operation_block_finished,
     * this cannot happen until all observers and all approvers have
     * returned from ObserveChannels or AddDispatchOperation, respectively. */
    g_assert (!_mcd_dispatch_operation_has_ado_pending (context->operation));
    g_assert (!_mcd_dispatch_operation_has_observers_pending
              (context->operation));

    if (_mcd_dispatch_operation_peek_channels (context->operation) == NULL)
    {
        DEBUG ("Nothing left to dispatch");

        _mcd_dispatch_operation_set_channels_handled (context->operation,
                                                      TRUE);
    }

    if (_mcd_dispatch_operation_is_awaiting_approval (context->operation))
    {
        _mcd_dispatch_operation_set_awaiting_approval (context->operation,
                                                       FALSE);
        _mcd_dispatch_operation_set_approved (context->operation);
        _mcd_dispatch_operation_check_client_locks (context->operation);
        mcd_dispatcher_context_unref (context, "CTXREF14");
    }

    mcd_dispatcher_context_unref (context, "CTXREF15");
}

/* ownership of @channels is stolen */
static void
_mcd_dispatcher_enter_state_machine (McdDispatcher *dispatcher,
                                     GList *channels,
                                     const gchar * const *possible_handlers,
                                     gboolean requested)
{
    McdDispatcherContext *context;
    McdDispatcherPrivate *priv;
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
    context->chain = priv->filters;

    DEBUG ("new dispatcher context %p for %s channel %p (%s): %s",
           context, requested ? "requested" : "unrequested",
           channels->data,
           channels->next == NULL ? "only" : "and more",
           mcd_channel_get_object_path (channels->data));

    priv->contexts = g_list_prepend (priv->contexts, context);

    /* FIXME: what should we do when the channels are a mixture of Requested
     * and unRequested? At the moment we act as though they're all Requested;
     * perhaps we should act as though they're all unRequested, or split up the
     * bundle? */

    context->operation = _mcd_dispatch_operation_new (priv->clients,
        priv->handler_map, !requested, channels,
        (const gchar * const *) possible_handlers);
    /* ownership of @channels is stolen, but the GObject references are not */

    priv->operations = g_list_prepend (priv->operations, context->operation);

    g_signal_connect (context->operation, "run-handlers",
                      G_CALLBACK (mcd_dispatcher_run_handlers), context);

    if (!requested)
    {
        if (priv->operation_list_active)
        {
            tp_svc_channel_dispatcher_interface_operation_list_emit_new_dispatch_operation (
                dispatcher,
                _mcd_dispatch_operation_get_path (context->operation),
                _mcd_dispatch_operation_get_properties (context->operation));
        }

        g_signal_connect (context->operation, "finished",
                          G_CALLBACK (on_operation_finished), dispatcher);
        g_signal_connect (context->operation, "ready-to-dispatch",
                          G_CALLBACK (mcd_dispatcher_op_ready_to_dispatch_cb),
                          context);
    }

    DEBUG ("entering state machine for context %p", context);

    sp_timestamp ("invoke internal filters");

    mcd_dispatcher_context_ref (context, "CTXREF01");
    mcd_dispatcher_context_proceed (context);

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

            for (iter = priv->operations; iter != NULL; iter = iter->next)
            {
                McdDispatchOperation *op = iter->data;

                if (_mcd_dispatch_operation_needs_approval (op) &&
                    !_mcd_dispatch_operation_is_finished (op))
                {
                    GValueArray *va = g_value_array_new (2);

                    g_value_array_append (va, NULL);
                    g_value_array_append (va, NULL);

                    g_value_init (va->values + 0, DBUS_TYPE_G_OBJECT_PATH);
                    g_value_init (va->values + 1,
                                  TP_HASH_TYPE_STRING_VARIANT_MAP);

                    g_value_set_boxed (va->values + 0,
                        _mcd_dispatch_operation_get_path (op));
                    g_value_set_boxed (va->values + 1,
                        _mcd_dispatch_operation_get_properties (op));

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
mcd_dispatcher_client_handling_channel_cb (McdClientProxy *client,
                                           const gchar *object_path,
                                           McdDispatcher *self)
{
    const gchar *bus_name = tp_proxy_get_bus_name (client);
    const gchar *unique_name = _mcd_client_proxy_get_unique_name (client);

    if (unique_name == NULL || unique_name[0] == '\0')
    {
        /* if it said it was handling channels but it doesn't seem to exist
         * (or worse, doesn't know whether it exists) then we don't believe
         * it */
        DEBUG ("%s doesn't seem to exist, assuming it's not handling %s",
               bus_name, object_path);
        return;
    }

    DEBUG ("%s (%s) is handling %s", bus_name, unique_name,
           object_path);

    _mcd_handler_map_set_path_handled (self->priv->handler_map,
                                       object_path, unique_name);
}

static void mcd_dispatcher_update_client_caps (McdDispatcher *self,
                                               McdClientProxy *client);

static void
mcd_dispatcher_client_capabilities_changed_cb (McdClientProxy *client,
                                               McdDispatcher *self)
{
    mcd_dispatcher_update_client_caps (self, client);
}

static void mcd_dispatcher_client_gone_cb (McdClientProxy *client,
                                           McdDispatcher *self);

static void
mcd_dispatcher_discard_client (McdDispatcher *self,
                               McdClientProxy *client)
{
    g_signal_handlers_disconnect_by_func (client,
        mcd_dispatcher_client_capabilities_changed_cb, self);

    g_signal_handlers_disconnect_by_func (client,
        mcd_dispatcher_client_handling_channel_cb, self);

    g_signal_handlers_disconnect_by_func (client,
                                          mcd_dispatcher_client_gone_cb,
                                          self);
}

static void
mcd_dispatcher_client_gone_cb (McdClientProxy *client,
                               McdDispatcher *self)
{
    mcd_dispatcher_discard_client (self, client);
}

static void
mcd_dispatcher_client_added_cb (McdClientRegistry *clients,
                                McdClientProxy *client,
                                McdDispatcher *self)
{
    g_signal_connect (client, "gone",
                      G_CALLBACK (mcd_dispatcher_client_gone_cb),
                      self);

    g_signal_connect (client, "is-handling-channel",
                      G_CALLBACK (mcd_dispatcher_client_handling_channel_cb),
                      self);

    g_signal_connect (client, "handler-capabilities-changed",
                      G_CALLBACK (mcd_dispatcher_client_capabilities_changed_cb),
                      self);
}

static void
mcd_dispatcher_client_registry_ready_cb (McdClientRegistry *clients,
                                         McdDispatcher *self)
{
    GHashTableIter iter;
    gpointer p;
    GPtrArray *vas;

    DEBUG ("All initial clients have been inspected");

    vas = _mcd_client_registry_dup_client_caps (clients);

    g_hash_table_iter_init (&iter, self->priv->connections);

    while (g_hash_table_iter_next (&iter, &p, NULL))
    {
        _mcd_connection_start_dispatching (p, vas);
    }

    g_ptr_array_foreach (vas, (GFunc) g_value_array_free, NULL);
    g_ptr_array_free (vas, TRUE);
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

    if (priv->clients != NULL)
    {
        gpointer client_p;
        GHashTableIter iter;

        _mcd_client_registry_init_hash_iter (priv->clients, &iter);

        while (g_hash_table_iter_next (&iter, NULL, &client_p))
        {
            mcd_dispatcher_discard_client ((McdDispatcher *) object, client_p);
        }

        g_signal_handlers_disconnect_by_func (priv->clients,
                                              mcd_dispatcher_client_added_cb,
                                              object);

        g_signal_handlers_disconnect_by_func (priv->clients,
            mcd_dispatcher_client_registry_ready_cb, object);

        g_object_unref (priv->clients);
        priv->clients = NULL;
    }

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

static void
mcd_dispatcher_update_client_caps (McdDispatcher *self,
                                   McdClientProxy *client)
{
    GPtrArray *vas;
    GHashTableIter iter;
    gpointer k;

    /* If we haven't finished inspecting initial clients yet, we'll push all
     * the client caps into all connections when we do, so do nothing.
     *
     * If we don't have any connections, on the other hand, then there's
     * nothing to do. */
    if (!_mcd_client_registry_is_ready (self->priv->clients)
        || g_hash_table_size (self->priv->connections) == 0)
    {
        return;
    }

    vas = g_ptr_array_sized_new (1);
    g_ptr_array_add (vas, _mcd_client_proxy_dup_handler_capabilities (client));

    g_hash_table_iter_init (&iter, self->priv->connections);

    while (g_hash_table_iter_next (&iter, &k, NULL))
    {
        _mcd_connection_update_client_caps (k, vas);
    }

    g_ptr_array_foreach (vas, (GFunc) g_value_array_free, NULL);
    g_ptr_array_free (vas, TRUE);
}

static void
mcd_dispatcher_constructed (GObject *object)
{
    DBusGConnection *dgc;
    McdDispatcherPrivate *priv = MCD_DISPATCHER_PRIV (object);
    GError *error = NULL;

    priv->handler_map = _mcd_handler_map_new (priv->dbus_daemon);

    priv->clients = _mcd_client_registry_new (priv->dbus_daemon);
    g_signal_connect (priv->clients, "client-added",
                      G_CALLBACK (mcd_dispatcher_client_added_cb), object);
    g_signal_connect (priv->clients, "ready",
                      G_CALLBACK (mcd_dispatcher_client_registry_ready_cb),
                      object);

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

    if (_mcd_dispatch_operation_get_cancelled (context->operation))
    {
        error.code = TP_ERROR_CANCELLED;
        error.message = "Channel request cancelled";
        _mcd_dispatcher_context_abort (context, &error);
        goto no_more;
    }

    if (_mcd_dispatch_operation_peek_channels (context->operation) == NULL)
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
     * we'll be trying to iterate over the list at the same time
     * that mcd_mission_abort results in modifying it, which would be bad */
    list = _mcd_dispatch_operation_dup_channels (context->operation);

    while (list != NULL)
    {
        mcd_mission_abort (list->data);
        g_object_unref (list->data);
        list = g_list_delete_link (list, list);
    }

    /* There should now be none left (they all aborted) */
    g_return_if_fail (_mcd_dispatch_operation_peek_channels
                      (context->operation) == NULL);
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

    list = _mcd_dispatch_operation_dup_channels (context->operation);

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

    list = _mcd_dispatch_operation_dup_channels (context->operation);

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
 * mcd_dispatcher_context_process (c, FALSE) is exactly equivalent to
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

    /* FIXME: check for leaks */
    g_return_if_fail (context);
    g_return_if_fail (context->ref_count > 0);

    DEBUG ("%s on %p (ref = %d)", tag, context, context->ref_count);
    context->ref_count--;
    if (context->ref_count == 0)
    {
        DEBUG ("freeing the context %p", context);

        g_signal_handlers_disconnect_by_func (context->operation,
            mcd_dispatcher_run_handlers, context);

        g_signal_handlers_disconnect_by_func (context->operation,
            mcd_dispatcher_op_ready_to_dispatch_cb, context);

        /* may emit finished */
        _mcd_dispatch_operation_finish (context->operation);

        g_signal_handlers_disconnect_by_func (context->operation,
                                              on_operation_finished,
                                              context->dispatcher);

        priv = MCD_DISPATCHER_PRIV (context->dispatcher);

        /* remove the context from the list of active contexts */
        priv->operations = g_list_remove (priv->operations, context->operation);

        g_object_unref (context->operation);

        /* remove the context from the list of active contexts */
        priv = MCD_DISPATCHER_PRIV (context->dispatcher);
        priv->contexts = g_list_remove (priv->contexts, context);

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
    const GList *channels = mcd_dispatcher_context_get_channels (context);

    g_return_val_if_fail (channels != NULL, NULL);
    return MCD_CONNECTION (mcd_mission_get_parent
                           (MCD_MISSION (channels->data)));
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
    const GList *channels = mcd_dispatcher_context_get_channels (ctx);

    return channels ? MCD_CHANNEL (channels->data) : NULL;
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
    return _mcd_dispatch_operation_peek_channels (context->operation);
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
    const GList *list;

    g_return_val_if_fail (context != NULL, NULL);
    for (list = mcd_dispatcher_context_get_channels (context);
         list != NULL;
         list = list->next)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);

        if (mcd_channel_get_channel_type_quark (channel) == type)
            return channel;
    }
    return NULL;
}

GPtrArray *
_mcd_dispatcher_get_channel_capabilities (McdDispatcher *dispatcher)
{
    McdDispatcherPrivate *priv = dispatcher->priv;
    GPtrArray *channel_handler_caps;
    GHashTableIter iter;
    gpointer key, value;

    channel_handler_caps = g_ptr_array_new ();

    /* Add the capabilities from the new-style clients */
    _mcd_client_registry_init_hash_iter (priv->clients, &iter);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        McdClientProxy *client = value;
        const GList *list;

        for (list = _mcd_client_proxy_get_handler_filters (client);
             list != NULL;
             list = list->next)
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

    _mcd_client_registry_init_hash_iter (priv->clients, &iter);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        McdClientProxy *client = value;
        const GList *list;

        for (list = _mcd_client_proxy_get_handler_filters (client);
             list != NULL;
             list = list->next)
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
    McdClientProxy *handler = NULL;
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

    handler = mcd_dispatcher_guess_request_handler (dispatcher, channel);
    if (!handler)
    {
        /* No handler found. But it's possible that by the time that the
         * channel will be created some handler will have popped up, so we
         * must not destroy it. */
        DEBUG ("No handler for request %s",
               _mcd_channel_get_request_path (channel));
        return;
    }

    if (!tp_proxy_has_interface_by_id (handler,
        TP_IFACE_QUARK_CLIENT_INTERFACE_REQUESTS))
    {
        DEBUG ("Default handler %s for request %s doesn't want AddRequest",
               tp_proxy_get_bus_name (handler),
               _mcd_channel_get_request_path (channel));
        return;
    }

    DEBUG ("Calling AddRequest on default handler %s for request %s",
           tp_proxy_get_bus_name (handler),
           _mcd_channel_get_request_path (channel));

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
        (TpClient *) handler, -1,
        _mcd_channel_get_request_path (channel), properties,
        NULL, NULL, NULL, NULL);

    g_hash_table_unref (properties);
    g_ptr_array_free (requests, TRUE);

    /* Prepare for a RemoveRequest */
    rrd = g_slice_new (McdRemoveRequestData);
    /* store the request path, because it might not be available when the
     * channel status changes */
    rrd->request_path = g_strdup (_mcd_channel_get_request_path (channel));
    rrd->handler = (TpClient *) handler;
    g_object_ref (handler);
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
        DEBUG ("trivial case - no channels");
        return;
    }

    DEBUG ("%s channel %p (%s): %s",
           requested ? "requested" : "unrequested",
           channels->data,
           channels->next == NULL ? "only" : "and more",
           mcd_channel_get_object_path (channels->data));

    /* See if there are any handlers that can take all these channels */
    possible_handlers = mcd_dispatcher_dup_possible_handlers (dispatcher,
                                                              channels,
                                                              NULL);

    if (possible_handlers == NULL)
    {
        if (channels->next == NULL)
        {
            DEBUG ("One channel, which cannot be handled");
            _mcd_channel_undispatchable (channels->data);
            g_list_free (channels);
        }
        else
        {
            DEBUG ("Two or more channels, which cannot all be handled - "
                   "will split up the batch and try again");

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
        DEBUG ("possible handlers found, dispatching");

        for (list = channels; list != NULL; list = list->next)
            _mcd_channel_set_status (MCD_CHANNEL (list->data),
                                     MCD_CHANNEL_STATUS_DISPATCHING);

        _mcd_dispatcher_enter_state_machine (dispatcher, channels,
            (const gchar * const *) possible_handlers, requested);
    }

    g_strfreev (possible_handlers);
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
mcd_dispatcher_finish_reinvocation (McdChannel *request)
{
    _mcd_channel_set_status (request, MCD_CHANNEL_STATUS_DISPATCHED);
    /* no need to keep it around any more */
    mcd_mission_abort (MCD_MISSION (request));
}

static void
reinvoke_handle_channels_cb (TpClient *client,
                             const GError *error,
                             gpointer user_data G_GNUC_UNUSED,
                             GObject *weak_object)
{
    McdChannel *request = MCD_CHANNEL (weak_object);

    if (error != NULL)
    {
        /* The handler is already dealing with this channel, but refuses to
         * re-handle it. Oh well, we tried. */
        DEBUG ("handler %s refused re-notification about channel %p:%s: "
               "%s:%d: %s", tp_proxy_get_bus_name (client),
               request, mcd_channel_get_object_path (request),
               g_quark_to_string (error->domain), error->code, error->message);
    }
    else
    {
        DEBUG ("handler %s successfully notified about channel %p:%s",
               tp_proxy_get_bus_name (client),
               request, mcd_channel_get_object_path (request));
    }

    /* either way, consider the request to have been dispatched */
    mcd_dispatcher_finish_reinvocation (request);
}

/*
 * _mcd_dispatcher_reinvoke_handler:
 * @dispatcher: The #McdDispatcher.
 * @request: a #McdChannel.
 *
 * Re-invoke the channel handler for @request.
 */
static void
_mcd_dispatcher_reinvoke_handler (McdDispatcher *dispatcher,
                                  McdChannel *request)
{
    GList *request_as_list;
    const gchar *handler_unique;
    GStrv possible_handlers;
    GPtrArray *details;
    GPtrArray *satisfied_requests;
    GHashTable *handler_info;
    TpChannel *channel;
    TpConnection *connection;
    const gchar *connection_path;
    McdAccount *account;
    const gchar *account_path;
    const GList *requests;
    guint64 user_action_time;
    McdClientProxy *handler;

    request_as_list = g_list_append (NULL, request);

    /* the unique name (process) of the current handler */
    handler_unique = _mcd_handler_map_get_handler (
        dispatcher->priv->handler_map,
        mcd_channel_get_object_path (request));

    /* work out how to invoke that process - any of its well-known names
     * will do */
    possible_handlers = mcd_dispatcher_dup_possible_handlers (dispatcher,
                                                              request_as_list,
                                                              handler_unique);

    if (possible_handlers == NULL || possible_handlers[0] == NULL)
    {
        /* The process is still running (otherwise it wouldn't be in the
         * handler map), but none of its well-known names is still
         * interested in channels of that sort. Oh well, not our problem.
         */
        DEBUG ("process %s no longer interested in this channel, not "
               "reinvoking", handler_unique);
        mcd_dispatcher_finish_reinvocation (request);
        goto finally;
    }

    handler = _mcd_client_registry_lookup (dispatcher->priv->clients,
                                           possible_handlers[0]);

    if (handler == NULL)
    {
        DEBUG ("Handler %s does not exist in client registry, not "
               "reinvoking", possible_handlers[0]);
        mcd_dispatcher_finish_reinvocation (request);
        goto finally;
    }

    /* This is deliberately not the same call as for normal dispatching,
     * and it doesn't go through a dispatch operation - the error handling
     * is completely different, because the channel is already being
     * handled perfectly well. */

    /* FIXME: gathering the arguments for HandleChannels is duplicated between
     * this function and mcd_dispatcher_handle_channels */

    account = mcd_channel_get_account (request);
    account_path = account == NULL ? "/"
        : mcd_account_get_object_path (account);

    if (G_UNLIKELY (account_path == NULL))    /* can't happen? */
        account_path = "/";

    channel = mcd_channel_get_tp_channel (request);
    g_assert (channel != NULL);
    connection = tp_channel_borrow_connection (channel);
    g_assert (connection != NULL);
    connection_path = tp_proxy_get_object_path (connection);
    g_assert (connection_path != NULL);

    details = _mcd_channel_details_build_from_list (request_as_list);

    satisfied_requests = g_ptr_array_new ();

    for (requests = _mcd_channel_get_satisfied_requests (request);
         requests != NULL;
         requests = requests->next)
    {
        g_ptr_array_add (satisfied_requests, requests->data);
    }

    user_action_time = _mcd_channel_get_request_user_action_time (request);
    handler_info = g_hash_table_new (g_str_hash, g_str_equal);

    _mcd_channel_set_status (request, MCD_CHANNEL_STATUS_HANDLER_INVOKED);

    tp_cli_client_handler_call_handle_channels ((TpClient *) handler,
        -1, account_path, connection_path, details,
        satisfied_requests, user_action_time, handler_info,
        reinvoke_handle_channels_cb, NULL, NULL, (GObject *) request);

    g_ptr_array_free (satisfied_requests, TRUE);
    _mcd_channel_details_free (details);
    g_hash_table_unref (handler_info);

finally:
    g_list_free (request_as_list);
    g_strfreev (possible_handlers);
}

static McdDispatchOperation *
find_operation_from_channel (McdDispatcher *dispatcher,
                             McdChannel *channel)
{
    GList *list;

    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    for (list = dispatcher->priv->operations; list != NULL; list = list->next)
    {
        McdDispatchOperation *op = list->data;

        if (_mcd_dispatch_operation_has_channel (op, channel))
            return op;
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

        _mcd_dispatcher_reinvoke_handler (dispatcher, request);
    }
    else
    {
        _mcd_channel_set_request_proxy (request, channel);
        if (status == MCD_CHANNEL_STATUS_DISPATCHING)
        {
            McdDispatchOperation *op = find_operation_from_channel (dispatcher,
                                                                    channel);

            g_return_if_fail (op != NULL);

            DEBUG ("channel %p is in CDO %p", channel, op);
            if (_mcd_dispatch_operation_has_ado_pending (op)
                || _mcd_dispatch_operation_is_awaiting_approval (op))
            {
                /* the existing channel is waiting for approval; but since the
                 * same channel has been requested, the approval operation must
                 * terminate */
                _mcd_dispatch_operation_approve (op);
            }
            else
            {
                _mcd_dispatch_operation_set_approved (op);
            }
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
    TpChannel *tp_channel;

    /* we must check if the channel is already being handled by some client; to
     * do this, we can examine the active handlers' "HandledChannel" property.
     * By now, we should already have done this, because startup has completed.
     */
    g_return_if_fail (MCD_IS_DISPATCHER (dispatcher));
    priv = dispatcher->priv;
    g_return_if_fail (_mcd_client_registry_is_ready (
        dispatcher->priv->clients));

    path = mcd_channel_get_object_path (channel);
    tp_channel = mcd_channel_get_tp_channel (channel);
    g_return_if_fail (tp_channel != NULL);

    unique_name = _mcd_handler_map_get_handler (priv->handler_map, path);

    if (unique_name != NULL)
    {
        DEBUG ("Channel %s is already handled by process %s",
               path, unique_name);
        _mcd_channel_set_status (channel,
                                 MCD_CHANNEL_STATUS_DISPATCHED);
        _mcd_handler_map_set_channel_handled (priv->handler_map, tp_channel,
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

/* FIXME: this only needs to exist because McdConnection calls it in order
 * to preload caps before Connect */
GPtrArray *
_mcd_dispatcher_dup_client_caps (McdDispatcher *self)
{
    g_return_val_if_fail (MCD_IS_DISPATCHER (self), NULL);

    /* if we're not ready, return NULL to tell the connection not to preload */
    if (!_mcd_client_registry_is_ready (self->priv->clients))
    {
        return NULL;
    }

    return _mcd_client_registry_dup_client_caps (self->priv->clients);
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

    if (_mcd_client_registry_is_ready (self->priv->clients))
    {
        GPtrArray *vas =
            _mcd_client_registry_dup_client_caps (self->priv->clients);

        _mcd_connection_start_dispatching (connection, vas);

        g_ptr_array_foreach (vas, (GFunc) g_value_array_free, NULL);
        g_ptr_array_free (vas, TRUE);
    }
    /* else _mcd_connection_start_dispatching will be called when we're ready
     * for it */
}
