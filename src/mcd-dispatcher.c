/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2010 Nokia Corporation.
 * Copyright (C) 2009-2010 Collabora Ltd.
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

#include "mission-control-plugins/mission-control-plugins.h"

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
#include "plugin-loader.h"

#include "libmcclient/mc-gtypes.h"
#include "_gen/svc-dispatcher.h"

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

#define CREATE_CHANNEL TP_IFACE_CONNECTION_INTERFACE_REQUESTS ".CreateChannel"
#define ENSURE_CHANNEL TP_IFACE_CONNECTION_INTERFACE_REQUESTS ".EnsureChannel"

#define MCD_DISPATCHER_PRIV(dispatcher) (MCD_DISPATCHER (dispatcher)->priv)

static void dispatcher_iface_init (gpointer, gpointer);
static void redispatch_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (McdDispatcher, mcd_dispatcher, MCD_TYPE_MISSION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_DISPATCHER,
                           dispatcher_iface_init);
    G_IMPLEMENT_INTERFACE (MC_TYPE_SVC_CHANNEL_DISPATCHER_INTERFACE_REDISPATCH,
                           redispatch_iface_init);
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

typedef struct
{
  McdDispatcher *dispatcher;
  gchar *account_path;
  GHashTable *properties;
  gint64 user_action_time;
  gchar *preferred_handler;
  GHashTable *request_metadata;
  gboolean ensure;
} McdChannelRequestACL;

struct _McdDispatcherPrivate
{
    /* Dispatching contexts */
    GList *contexts;

    /* Channel dispatch operations */
    GList *operations;

    TpDBusDaemon *dbus_daemon;

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
    tp_clear_object (&priv->master);
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
        if (!_mcd_client_match_property (channel_class2, property_name,
                                         property_value))
            return FALSE;
    }
    return TRUE;
}

static GStrv
mcd_dispatcher_dup_possible_handlers (McdDispatcher *self,
                                      McdRequest *request,
                                      const GList *channels,
                                      const gchar *must_have_unique_name)
{
    GList *handlers = _mcd_client_registry_list_possible_handlers (
        self->priv->clients,
        request != NULL ? _mcd_request_get_preferred_handler (request) : NULL,
        request != NULL ? _mcd_request_get_properties (request) : NULL,
        channels, must_have_unique_name);
    guint n_handlers = g_list_length (handlers);
    guint i;
    GStrv ret;
    const GList *iter;

    if (handlers == NULL)
        return NULL;

    ret = g_new0 (gchar *, n_handlers + 1);

    for (iter = handlers, i = 0; iter != NULL; iter = iter->next, i++)
    {
        ret[i] = g_strdup (tp_proxy_get_bus_name (iter->data));
    }

    ret[n_handlers] = NULL;

    g_list_free (handlers);

    return ret;
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
    GList *its_link;

    g_signal_handlers_disconnect_by_func (operation,
                                          on_operation_finished,
                                          self);

    /* don't emit the signal if the CDO never appeared on D-Bus */
    if (self->priv->operation_list_active &&
        _mcd_dispatch_operation_needs_approval (operation))
    {
        tp_svc_channel_dispatcher_interface_operation_list_emit_dispatch_operation_finished (
            self, _mcd_dispatch_operation_get_path (operation));
    }

    its_link = g_list_find (self->priv->operations, operation);

    if (its_link != NULL)
    {
        self->priv->operations = g_list_delete_link (self->priv->operations,
                                                     its_link);
        g_object_unref (operation);
    }
}

static void
_mcd_dispatcher_enter_state_machine (McdDispatcher *dispatcher,
                                     GList *channels,
                                     const gchar * const *possible_handlers,
                                     gboolean requested,
                                     gboolean only_observe)
{
    McdDispatcherContext *context;
    McdDispatcherPrivate *priv;
    McdAccount *account;

    g_return_if_fail (MCD_IS_DISPATCHER (dispatcher));
    g_return_if_fail (channels != NULL);
    g_return_if_fail (MCD_IS_CHANNEL (channels->data));
    g_return_if_fail (requested || !only_observe);
    g_return_if_fail (possible_handlers != NULL || only_observe);

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

    /* FIXME: what should we do when the channels are a mixture of Requested
     * and unRequested? At the moment we act as though they're all Requested;
     * perhaps we should act as though they're all unRequested, or split up the
     * bundle? */

    context->operation = _mcd_dispatch_operation_new (priv->clients,
        priv->handler_map, !requested, only_observe, channels,
        (const gchar * const *) possible_handlers);

    if (!requested)
    {
        if (priv->operation_list_active)
        {
            tp_svc_channel_dispatcher_interface_operation_list_emit_new_dispatch_operation (
                dispatcher,
                _mcd_dispatch_operation_get_path (context->operation),
                _mcd_dispatch_operation_get_properties (context->operation));
        }

        priv->operations = g_list_prepend (priv->operations,
                                           g_object_ref (context->operation));

        g_signal_connect (context->operation, "finished",
                          G_CALLBACK (on_operation_finished), dispatcher);
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
	tp_clear_object (&priv->dbus_daemon);
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
                                       object_path, unique_name, bus_name);
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

static void mcd_dispatcher_client_needs_recovery_cb (McdClientProxy *client,
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

    g_signal_handlers_disconnect_by_func (client,
        mcd_dispatcher_client_needs_recovery_cb, self);
}

static void
mcd_dispatcher_client_gone_cb (McdClientProxy *client,
                               McdDispatcher *self)
{
    mcd_dispatcher_discard_client (self, client);
}

static void
mcd_dispatcher_client_needs_recovery_cb (McdClientProxy *client,
                                         McdDispatcher *self)
{
    GList *channels =
        _mcd_handler_map_get_handled_channels (self->priv->handler_map);
    const GList *observer_filters;
    GList *list;

    DEBUG ("called");

    observer_filters = _mcd_client_proxy_get_observer_filters (client);

    for (list = channels; list; list = list->next)
    {
        TpChannel *channel = list->data;
        const gchar *object_path = tp_proxy_get_object_path (channel);
        GHashTable *properties;
        const gchar *hname;

        if (_mcd_handler_map_get_handler (self->priv->handler_map,
              object_path, &hname))
        {
            McdClientProxy *handler =
                _mcd_client_registry_lookup (self->priv->clients, hname);

            if (_mcd_client_proxy_get_bypass_observers (handler))
            {
              DEBUG ("skipping unobservable channel %s", object_path);
              continue;
            }
        }

        properties = tp_channel_borrow_immutable_properties (channel);

        if (_mcd_client_match_filters (properties, observer_filters,
            FALSE))
        {
            const gchar *account_path =
                _mcd_handler_map_get_channel_account (self->priv->handler_map,
                    tp_proxy_get_object_path (channel));

            _mcd_client_recover_observer (client, channel, account_path);
        }

    }
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

    g_signal_connect (client, "need-recovery",
                      G_CALLBACK (mcd_dispatcher_client_needs_recovery_cb),
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
drop_each_operation (gpointer operation,
                     gpointer dispatcher)
{
    g_signal_handlers_disconnect_by_func (operation,
                                          on_operation_finished,
                                          dispatcher);
    g_object_unref (operation);
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

    if (priv->operations != NULL)
    {
        g_list_foreach (priv->operations, drop_each_operation, object);
        tp_clear_pointer (&priv->operations, g_list_free);
    }

    tp_clear_object (&priv->handler_map);

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

        tp_clear_object (&priv->clients);
    }

    tp_clear_pointer (&priv->connections, g_hash_table_destroy);
    tp_clear_object (&priv->master);
    tp_clear_object (&priv->dbus_daemon);

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
                                      TP_CHANNEL_DISPATCHER_BUS_NAME,
                                      TRUE /* idempotent */, &error))
    {
        /* FIXME: put in proper error handling when MC gains the ability to
         * be the AM or the CD but not both */
        g_warning ("Failed registering '%s' service: %s",
                   TP_CHANNEL_DISPATCHER_BUS_NAME, error->message);
        g_error_free (error);
        exit (1);
    }

    dbus_g_connection_register_g_object (dgc,
                                         TP_CHANNEL_DISPATCHER_OBJECT_PATH,
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

    /* idempotent, not guaranteed to have been called yet */
    _mcd_plugin_loader_init ();
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

no_more:    /* either no more filters, or no more channels */
    _mcd_dispatch_operation_run_clients (context->operation);
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
    g_return_if_fail (context);
    _mcd_dispatch_operation_forget_channels (context->operation);
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
    g_return_if_fail (context);
    _mcd_dispatch_operation_destroy_channels (context->operation);
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
    g_return_if_fail (context);
    _mcd_dispatch_operation_leave_channels (context->operation, reason,
                                            message);
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
        _mcd_dispatch_operation_destroy_channels (context->operation);
    }

    mcd_dispatcher_context_proceed (context);
}

static void
mcd_dispatcher_context_unref (McdDispatcherContext * context,
                              const gchar *tag)
{
    /* FIXME: check for leaks */
    g_return_if_fail (context);
    g_return_if_fail (context->ref_count > 0);

    DEBUG ("%s on %p (ref = %d)", tag, context, context->ref_count);
    context->ref_count--;
    if (context->ref_count == 0)
    {
        DEBUG ("freeing the context %p", context);
        g_object_unref (context->operation);
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

/*
 * _mcd_dispatcher_take_channels:
 * @dispatcher: the #McdDispatcher.
 * @channels: a #GList of #McdChannel elements, each of which must own a
 *  #TpChannel
 * @requested: whether the channels were requested by MC.
 *
 * Dispatch @channels. The #GList @channels will be no longer valid after this
 * function has been called.
 */
void
_mcd_dispatcher_take_channels (McdDispatcher *dispatcher, GList *channels,
                               gboolean requested, gboolean only_observe)
{
    GList *list;
    GList *tp_channels = NULL;
    GStrv possible_handlers;
    McdRequest *request = NULL;

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

    if (only_observe)
    {
        g_return_if_fail (requested);

        /* these channels were requested "behind our back", so only call
         * ObserveChannels on them */
        _mcd_dispatcher_enter_state_machine (dispatcher, channels, NULL,
                                             TRUE, TRUE);
        g_list_free (channels);
        return;
    }

    /* These channels must have the TpChannel part of McdChannel's double life.
     * They might also have the McdRequest part. */
    for (list = channels; list != NULL; list = list->next)
    {
        TpChannel *tp_channel = mcd_channel_get_tp_channel (list->data);

        g_assert (tp_channel != NULL);
        tp_channels = g_list_prepend (tp_channels, g_object_ref (tp_channel));

        /* We take the channel request from the first McdChannel that (has|is)
         * one.*/
        if (request == NULL)
        {
            request = _mcd_channel_get_request (list->data);
        }
    }

    /* See if there are any handlers that can take all these channels */
    possible_handlers = mcd_dispatcher_dup_possible_handlers (dispatcher,
                                                              request,
                                                              tp_channels,
                                                              NULL);

    g_list_foreach (tp_channels, (GFunc) g_object_unref, NULL);
    g_list_free (tp_channels);

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
                _mcd_dispatcher_take_channels (dispatcher, list, requested,
                                               FALSE);
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
            (const gchar * const *) possible_handlers, requested, FALSE);
        g_list_free (channels);
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
 * @request: a #McdChannel that has both a #TpChannel and a #McdRequest
 *
 * Re-invoke the channel handler for @request.
 */
static void
_mcd_dispatcher_reinvoke_handler (McdDispatcher *dispatcher,
                                  McdChannel *request)
{
    GList *request_as_list;
    const gchar *handler_unique;
    const gchar *well_known_name = NULL;
    GStrv possible_handlers = NULL;
    McdClientProxy *handler = NULL;
    McdRequest *real_request = _mcd_channel_get_request (request);
    GList *tp_channels = g_list_append (NULL,
        mcd_channel_get_tp_channel (request));
    GHashTable *handler_info;
    GHashTable *request_properties;

    g_assert (real_request != NULL);
    g_assert (tp_channels->data != NULL);

    request_as_list = g_list_append (NULL, request);

    request_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, (GDestroyNotify) g_hash_table_unref);
    g_hash_table_insert (request_properties,
        g_strdup (_mcd_request_get_object_path (real_request)),
        _mcd_request_dup_immutable_properties (real_request));

    handler_info = tp_asv_new (NULL, NULL);
    /* hand over ownership of request_properties */
    /* FIXME: use telepathy-glib version when available */
    tp_asv_take_boxed (handler_info, "request-properties",
                       MC_HASH_TYPE_OBJECT_IMMUTABLE_PROPERTIES_MAP,
                       request_properties);
    request_properties = NULL;

    /* the unique name (process) of the current handler */
    handler_unique = _mcd_handler_map_get_handler (
        dispatcher->priv->handler_map,
        tp_proxy_get_object_path (tp_channels->data), &well_known_name);

    if (well_known_name != NULL)
    {
        /* We know which Handler well-known name was responsible: if it
         * still exists, we want to call HandleChannels on it */
        handler = _mcd_client_registry_lookup (dispatcher->priv->clients,
                                               well_known_name);
    }

    if (handler == NULL)
    {
        /* Failing that, maybe the Handler it was dispatched to was temporary;
         * try to pick another Handler that can deal with it, on the same
         * unique name (i.e. in the same process) */
        possible_handlers = mcd_dispatcher_dup_possible_handlers (dispatcher,
            real_request, tp_channels, handler_unique);

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
    }

    /* This is deliberately not the same call as for normal dispatching,
     * and it doesn't go through a dispatch operation - the error handling
     * is completely different, because the channel is already being
     * handled perfectly well. */

    _mcd_client_proxy_handle_channels (handler,
        -1, request_as_list,
        0, /* the request's user action time will be used automatically */
        handler_info,
        reinvoke_handle_channels_cb, NULL, NULL, (GObject *) request);

finally:
    g_hash_table_unref (handler_info);
    g_list_free (request_as_list);
    g_list_free (tp_channels);
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
        const gchar *preferred_handler =
            _mcd_channel_get_request_preferred_handler (request);

        _mcd_channel_set_request_proxy (request, channel);
        if (status == MCD_CHANNEL_STATUS_DISPATCHING)
        {
            McdDispatchOperation *op = find_operation_from_channel (dispatcher,
                                                                    channel);

            g_return_if_fail (op != NULL);

            DEBUG ("channel %p is in CDO %p", channel, op);
            _mcd_dispatch_operation_approve (op, preferred_handler);
        }
        DEBUG ("channel %p is proxying %p", request, channel);
    }
}

void
_mcd_dispatcher_recover_channel (McdDispatcher *dispatcher,
                                 McdChannel *channel,
                                 const gchar *account_path)
{
    McdDispatcherPrivate *priv;
    const gchar *path;
    const gchar *unique_name;
    const gchar *well_known_name = NULL;
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

    unique_name = _mcd_handler_map_get_handler (priv->handler_map, path,
                                                &well_known_name);

    if (unique_name != NULL)
    {
        DEBUG ("Channel %s is already handled by process %s",
               path, unique_name);
        _mcd_channel_set_status (channel,
                                 MCD_CHANNEL_STATUS_DISPATCHED);
        _mcd_handler_map_set_channel_handled (priv->handler_map, tp_channel,
                                              unique_name, well_known_name,
                                              account_path);
    }
    else
    {
        DEBUG ("%s is unhandled, redispatching", path);

        requested = mcd_channel_is_requested (channel);
        _mcd_dispatcher_take_channels (dispatcher,
                                       g_list_prepend (NULL, channel),
                                       requested,
                                       FALSE);
    }
}

static gboolean
check_preferred_handler (const gchar *preferred_handler,
    GError **error)
{
  g_assert (error != NULL);

  if (preferred_handler[0] == '\0')
      return TRUE;

  if (!tp_dbus_check_valid_bus_name (preferred_handler,
                                     TP_DBUS_NAME_TYPE_WELL_KNOWN,
                                     error))
  {
      /* The error is TP_DBUS_ERROR_INVALID_BUS_NAME, which has no D-Bus
       * representation; re-map to InvalidArgument. */
      (*error)->domain = TP_ERRORS;
      (*error)->code = TP_ERROR_INVALID_ARGUMENT;
      return FALSE;
  }

  if (!g_str_has_prefix (preferred_handler, TP_CLIENT_BUS_NAME_BASE))
  {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "Not a Telepathy Client: %s", preferred_handler);
      return FALSE;
  }

  return TRUE;
}

static void
dispatcher_request_channel (McdDispatcher *self,
                            const gchar *account_path,
                            GHashTable *requested_properties,
                            gint64 user_action_time,
                            const gchar *preferred_handler,
                            GHashTable *request_metadata,
                            DBusGMethodInvocation *context,
                            gboolean ensure)
{
    McdAccountManager *am;
    McdAccount *account;
    McdChannel *channel = NULL;
    McdRequest *request = NULL;
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

    if (!check_preferred_handler (preferred_handler, &error))
        goto despair;

    channel = _mcd_account_create_request (self->priv->clients,
                                           account, requested_properties,
                                           user_action_time, preferred_handler,
                                           request_metadata, ensure,
                                           &request, &error);

    if (channel == NULL)
    {
        /* FIXME: ideally this would be emitted as a Failed signal after
         * Proceed is called, but for the particular failure case here (low
         * memory) perhaps we don't want to */
        goto despair;
    }

    g_assert (request != NULL);
    path = _mcd_request_get_object_path (request);
    g_assert (path != NULL);

    /* This is OK because the signatures of CreateChannel and EnsureChannel
     * are the same */
    tp_svc_channel_dispatcher_return_from_create_channel (context, path);

    _mcd_request_predict_handler (request);

    /* We've done all we need to with this channel: the ChannelRequests code
     * keeps it alive as long as is necessary. The finally clause will
     * free it */
    goto finally;

despair:
    dbus_g_method_return_error (context, error);
    g_error_free (error);

finally:
    tp_clear_object (&channel);
    tp_clear_object (&request);
    g_object_unref (am);
}

static void
dispatcher_channel_request_acl_cleanup (gpointer data)
{
    McdChannelRequestACL *crd = data;

    DEBUG ("cleanup acl (%p)", data);

    g_free (crd->account_path);
    g_free (crd->preferred_handler);
    g_hash_table_unref (crd->properties);
    g_object_unref (crd->dispatcher);
    tp_clear_pointer (&crd->request_metadata, g_hash_table_unref);

    g_slice_free (McdChannelRequestACL, crd);
}

static void
dispatcher_channel_request_acl_success (DBusGMethodInvocation *context,
                                        gpointer data)
{
    McdChannelRequestACL *crd = data;

    DEBUG ("complete acl (%p)", crd);

    dispatcher_request_channel (MCD_DISPATCHER (crd->dispatcher),
                                crd->account_path,
                                crd->properties,
                                crd->user_action_time,
                                crd->preferred_handler,
                                crd->request_metadata,
                                context,
                                crd->ensure);
}

static void
free_gvalue (gpointer gvalue)
{
    GValue *gv = gvalue;

    g_value_unset (gv);
    g_slice_free (GValue, gv);
}

static void
dispatcher_channel_request_acl_start (McdDispatcher *dispatcher,
                                      const gchar *method,
                                      const gchar *account_path,
                                      GHashTable *requested_properties,
                                      gint64 user_action_time,
                                      const gchar *preferred_handler,
                                      GHashTable *request_metadata,
                                      DBusGMethodInvocation *context,
                                      gboolean ensure)
{
    McdChannelRequestACL *crd = g_slice_new0 (McdChannelRequestACL);
    GValue *account = g_slice_new0 (GValue);
    GHashTable *params =
      g_hash_table_new_full (g_str_hash, g_str_equal, NULL, free_gvalue);

    g_value_init (account, G_TYPE_STRING);
    g_value_set_string (account, account_path);
    g_hash_table_insert (params, "account-path", account);

    crd->dispatcher = g_object_ref (dispatcher);
    crd->account_path = g_strdup (account_path);
    crd->preferred_handler = g_strdup (preferred_handler);
    crd->properties = g_hash_table_ref (requested_properties);
    crd->user_action_time = user_action_time;
    crd->ensure = ensure;
    crd->request_metadata = request_metadata != NULL ?
        g_hash_table_ref (request_metadata) : NULL;

    DEBUG ("start %s.%s acl (%p)", account_path, method, crd);

    mcp_dbus_acl_authorised_async (dispatcher->priv->dbus_daemon,
                                   context,
                                   DBUS_ACL_TYPE_METHOD,
                                   method,
                                   params,
                                   dispatcher_channel_request_acl_success,
                                   crd,
                                   dispatcher_channel_request_acl_cleanup);

}

static void
dispatcher_create_channel (TpSvcChannelDispatcher *iface,
                           const gchar *account_path,
                           GHashTable *requested_properties,
                           gint64 user_action_time,
                           const gchar *preferred_handler,
                           DBusGMethodInvocation *context)
{
    dispatcher_channel_request_acl_start (MCD_DISPATCHER (iface),
                                          CREATE_CHANNEL,
                                          account_path,
                                          requested_properties,
                                          user_action_time,
                                          preferred_handler,
                                          NULL,
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
    dispatcher_channel_request_acl_start (MCD_DISPATCHER (iface),
                                          ENSURE_CHANNEL,
                                          account_path,
                                          requested_properties,
                                          user_action_time,
                                          preferred_handler,
                                          NULL,
                                          context,
                                          TRUE);
}

static void
dispatcher_create_channel_with_hints (TpSvcChannelDispatcher *iface,
    const gchar *account_path,
    GHashTable *requested_properties,
    gint64 user_action_time,
    const gchar *preferred_handler,
    GHashTable *hints,
    DBusGMethodInvocation *context)
{
    dispatcher_channel_request_acl_start (MCD_DISPATCHER (iface),
                                          CREATE_CHANNEL,
                                          account_path,
                                          requested_properties,
                                          user_action_time,
                                          preferred_handler,
                                          hints,
                                          context,
                                          FALSE);
}

static void
dispatcher_ensure_channel_with_hints (TpSvcChannelDispatcher *iface,
    const gchar *account_path,
    GHashTable *requested_properties,
    gint64 user_action_time,
    const gchar *preferred_handler,
    GHashTable *hints,
    DBusGMethodInvocation *context)
{
    dispatcher_channel_request_acl_start (MCD_DISPATCHER (iface),
                                          ENSURE_CHANNEL,
                                          account_path,
                                          requested_properties,
                                          user_action_time,
                                          preferred_handler,
                                          hints,
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
    IMPLEMENT (create_channel_with_hints);
    IMPLEMENT (ensure_channel_with_hints);
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

McdClientRegistry *
_mcd_dispatcher_get_client_registry (McdDispatcher *self)
{
    g_return_val_if_fail (MCD_IS_DISPATCHER (self), NULL);
    return self->priv->clients;
}

typedef struct
{
  McdDispatcher *self;
  McdAccount *account;
  gint64 user_action_time;
  GHashTable *hints;
  DBusGMethodInvocation *context;
  /* List of reffed McdChannel */
  GList *channels;
  /* Queue of reffed McdClientProxy */
  GQueue *handlers;
} RedispatchChannelsCtx;

static RedispatchChannelsCtx *
redispatch_channels_ctx_new (McdDispatcher *self,
    McdAccount *account,
    gint64 user_action_time,
    GHashTable *hints,
    DBusGMethodInvocation *context)
{
  RedispatchChannelsCtx *ctx = g_slice_new0 (RedispatchChannelsCtx);

  ctx->self = g_object_ref (self);
  ctx->account = g_object_ref (account);
  ctx->user_action_time = user_action_time;
  ctx->hints = g_hash_table_ref (hints);
  ctx->context = context;
  ctx->handlers = g_queue_new ();
  return ctx;
}

static void
redispatch_channels_ctx_free (RedispatchChannelsCtx *ctx)
{
  g_object_unref (ctx->self);
  g_object_unref (ctx->account);
  g_list_foreach (ctx->channels, (GFunc) g_object_unref, NULL);
  g_list_free (ctx->channels);
  g_hash_table_unref (ctx->hints);
  g_queue_foreach (ctx->handlers, (GFunc) g_object_unref, NULL);
  g_queue_free (ctx->handlers);
  g_slice_free (RedispatchChannelsCtx, ctx);
}

static void try_redispatching (RedispatchChannelsCtx *ctx);

static void
redispatch_handle_channels_cb (TpClient *client,
    const GError *error,
    gpointer user_data G_GNUC_UNUSED,
    GObject *weak_object)
{
  RedispatchChannelsCtx *ctx = user_data;
  GList *l;
  McdClientProxy *clt_proxy = MCD_CLIENT_PROXY (client);

  if (error != NULL)
    {
        DEBUG ("Handler refused redispatching channels");

        try_redispatching (ctx);
        return;
    }

  DEBUG ("Channels have been redispatched");

  for (l = ctx->channels; l != NULL; l = g_list_next (l))
    {
      McdChannel *channel = l->data;

      _mcd_handler_map_set_path_handled (ctx->self->priv->handler_map,
          mcd_channel_get_object_path (channel),
          _mcd_client_proxy_get_unique_name (clt_proxy),
          tp_proxy_get_bus_name (client));
    }

  mc_svc_channel_dispatcher_interface_redispatch_return_from_redispatch_channels (
      ctx->context);

  redispatch_channels_ctx_free (ctx);
}

static void
try_redispatching (RedispatchChannelsCtx *ctx)
{
  McdClientProxy *client;

  if (g_queue_get_length (ctx->handlers) == 0)
    {
      GError *error = NULL;

      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_CAPABLE,
          "There is no other suitable handler");

      dbus_g_method_return_error (ctx->context, error);
      g_error_free (error);

      redispatch_channels_ctx_free (ctx);
      return;
    }

  client = g_queue_pop_head (ctx->handlers);

  DEBUG ("Try redispatching channels to %s", _mcd_client_proxy_get_unique_name (
      client));

  _mcd_client_proxy_handle_channels (client, -1, ctx->channels,
      ctx->user_action_time, NULL, redispatch_handle_channels_cb,
      ctx, NULL, NULL);

  g_object_unref (client);
}

static void
dispatcher_redispatch_channels (
    McSvcChannelDispatcherInterfaceRedispatch *iface,
    const gchar *account_path,
    const GPtrArray *channels,
    gint64 user_action_time,
    const gchar *preferred_handler,
    GHashTable *hints,
    DBusGMethodInvocation *context)
{
  McdDispatcher *self = (McdDispatcher *) iface;
  guint i;
  GError *error = NULL;
  const gchar *sender;
  McdAccountManager *am;
  McdAccount *account;
  McdConnection *conn;
  GStrv possible_handlers;
  GList *tp_channels = NULL;
  RedispatchChannelsCtx *ctx = NULL;

  if (!check_preferred_handler (preferred_handler, &error))
      goto error;

  g_object_get (self->priv->master, "account-manager", &am, NULL);
  g_assert (am != NULL);

  account = mcd_account_manager_lookup_account_by_path (am, account_path);
  g_object_unref (am);

  if (account == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "No such account: %s", account_path);
      goto error;
    }

  conn = mcd_account_get_connection (account);
  if (conn == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "No connection for account: %s", account_path);
      goto error;
    }

  if (channels->len == 0)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Need at least one channel to redispatch");
      goto error;
    }

  ctx = redispatch_channels_ctx_new (self, account, user_action_time, hints,
      context);

  sender = dbus_g_method_get_sender (context);

  for (i = 0; i < channels->len; i++)
    {
      const gchar *chan_path = g_ptr_array_index (channels, i);
      const gchar *chan_account;
      const gchar *handler;
      McdChannel *mcd_channel;
      TpChannel *tp_channel;

      /* Check account of the channel */
      chan_account = _mcd_handler_map_get_channel_account (
          self->priv->handler_map, chan_path);

      if (tp_strdiff (account_path, chan_account))
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Channel %s has %s as account, not %s", chan_path, chan_account,
              account_path);
          goto error;
        }

      /* Check the caller is handling the channel */
      handler = _mcd_handler_map_get_handler (self->priv->handler_map,
          chan_path, NULL);
      if (tp_strdiff (sender, handler))
       {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_YOURS,
              "Your are not handling channel %s", chan_path);
          goto error;
       }

      mcd_channel = mcd_connection_find_channel_by_path (conn, chan_path);
      g_assert (mcd_channel != NULL);

      tp_channel = mcd_channel_get_tp_channel (mcd_channel);
      g_assert (tp_channel != NULL);

      tp_channels = g_list_prepend (tp_channels, tp_channel);
      ctx->channels = g_list_prepend (ctx->channels,
          g_object_ref (mcd_channel));
    }

  possible_handlers = mcd_dispatcher_dup_possible_handlers (self,
      NULL, tp_channels, NULL);

  g_list_free (tp_channels);

  for (i = 0; possible_handlers[i] != NULL; i++)
    {
      McdClientProxy *client;
      const gchar *unique_name;

      client = _mcd_client_registry_lookup (self->priv->clients,
          possible_handlers[i]);
      g_assert (client != NULL);

      unique_name = _mcd_client_proxy_get_unique_name (client);

      /* Skip the caller */
      if (!tp_strdiff (unique_name, sender))
          continue;

      /* Put the preferred handler at the head of the list so it will be tried
       * first */
      if (!tp_strdiff (possible_handlers[i], preferred_handler))
        g_queue_push_head (ctx->handlers, g_object_ref (client));
      else
        g_queue_push_tail (ctx->handlers, g_object_ref (client));
    }

  g_strfreev (possible_handlers);

  try_redispatching (ctx);

  return;

error:
  dbus_g_method_return_error (context, error);
  g_error_free (error);

  tp_clear_pointer (&ctx, redispatch_channels_ctx_free);
}

static void
redispatch_iface_init (gpointer g_iface,
                       gpointer iface_data G_GNUC_UNUSED)
{
#define IMPLEMENT(x) mc_svc_channel_dispatcher_interface_redispatch_implement_##x (\
    g_iface, dispatcher_##x)
  IMPLEMENT(redispatch_channels);
}
