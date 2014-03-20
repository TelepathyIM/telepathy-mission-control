/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright © 2007-2011 Nokia Corporation.
 * Copyright © 2009-2011 Collabora Ltd.
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

#include "config.h"

#include <dlfcn.h>
#include <glib.h>
#include <glib/gprintf.h>

#include <dbus/dbus-glib-lowlevel.h>

#include "mission-control-plugins/mission-control-plugins.h"

#include "client-registry.h"
#include "mcd-account-priv.h"
#include "mcd-client-priv.h"
#include "mcd-connection.h"
#include "mcd-connection-priv.h"
#include "mcd-channel.h"
#include "mcd-master.h"
#include "mcd-channel-priv.h"
#include "mcd-dispatcher-priv.h"
#include "mcd-dispatch-operation-priv.h"
#include "mcd-handler-map-priv.h"
#include "mcd-misc.h"
#include "plugin-loader.h"

#include "_gen/svc-dispatcher.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <telepathy-glib/proxy-subclass.h>

#include <stdlib.h>
#include <string.h>
#include "sp_timestamp.h"

#define CREATE_CHANNEL TP_IFACE_CONNECTION_INTERFACE_REQUESTS ".CreateChannel"
#define ENSURE_CHANNEL TP_IFACE_CONNECTION_INTERFACE_REQUESTS ".EnsureChannel"
#define SEND_MESSAGE \
  TP_IFACE_CHANNEL_DISPATCHER ".Interface.Messages.DRAFT.SendMessage"

#define MCD_DISPATCHER_PRIV(dispatcher) (MCD_DISPATCHER (dispatcher)->priv)

static void dispatcher_iface_init (gpointer, gpointer);
static void messages_iface_init (gpointer, gpointer);


G_DEFINE_TYPE_WITH_CODE (McdDispatcher, mcd_dispatcher, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_DISPATCHER,
                           dispatcher_iface_init);
    G_IMPLEMENT_INTERFACE (MC_TYPE_SVC_CHANNEL_DISPATCHER_INTERFACE_MESSAGES_DRAFT,
                           messages_iface_init);
    G_IMPLEMENT_INTERFACE (
        TP_TYPE_SVC_CHANNEL_DISPATCHER_INTERFACE_OPERATION_LIST,
        NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
                           tp_dbus_properties_mixin_iface_init))

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
    PROP_SUPPORTS_REQUEST_HINTS,
    PROP_DISPATCH_OPERATIONS,
};

static void on_operation_finished (McdDispatchOperation *operation,
                                   McdDispatcher *self);

static void
on_master_abort (McdMaster *master, McdDispatcherPrivate *priv)
{
    tp_clear_object (&priv->master);
}

static GStrv
mcd_dispatcher_dup_internal_handlers (void)
{
    const gchar * const internal_handlers[] = { CDO_INTERNAL_HANDLER, NULL };

    return g_strdupv ((GStrv) internal_handlers);
}

static GStrv
mcd_dispatcher_dup_possible_handlers (McdDispatcher *self,
                                      McdRequest *request,
                                      TpChannel *channel,
                                      const gchar *must_have_unique_name)
{
    GList *handlers;
    guint n_handlers;
    guint i;
    GStrv ret;
    const GList *iter;
    GVariant *request_properties = NULL;

    if (request != NULL)
        request_properties = mcd_request_dup_properties (request);

    handlers = _mcd_client_registry_list_possible_handlers (
        self->priv->clients,
        request != NULL ? _mcd_request_get_preferred_handler (request) : NULL,
        request_properties,
        channel, must_have_unique_name);
    n_handlers = g_list_length (handlers);

    tp_clear_pointer (&request_properties, g_variant_unref);

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
                                     McdChannel *channel,
                                     const gchar * const *possible_handlers,
                                     gboolean requested,
                                     gboolean only_observe)
{
    McdDispatchOperation *operation;
    McdDispatcherPrivate *priv;
    McdAccount *account;

    g_return_if_fail (MCD_IS_DISPATCHER (dispatcher));
    g_return_if_fail (MCD_IS_CHANNEL (channel));
    g_return_if_fail (requested || !only_observe);

    account = mcd_channel_get_account (channel);
    if (G_UNLIKELY (!account))
    {
        g_warning ("%s called with no account", G_STRFUNC);
        return;
    }

    priv = dispatcher->priv;

    DEBUG ("new dispatch operation for %s channel %p: %s",
           requested ? "requested" : "unrequested",
           channel,
           mcd_channel_get_object_path (channel));

    operation = _mcd_dispatch_operation_new (priv->clients,
        priv->handler_map, !requested, only_observe, channel,
        (const gchar * const *) possible_handlers);

    if (!requested)
    {
        if (priv->operation_list_active)
        {
            tp_svc_channel_dispatcher_interface_operation_list_emit_new_dispatch_operation (
                dispatcher,
                _mcd_dispatch_operation_get_path (operation),
                _mcd_dispatch_operation_get_properties (operation));
        }

        priv->operations = g_list_prepend (priv->operations,
                                           g_object_ref (operation));

        g_signal_connect (operation, "finished",
                          G_CALLBACK (on_operation_finished), dispatcher);
    }

    if (_mcd_dispatch_operation_get_cancelled (operation))
    {
        GError error = { TP_ERROR, TP_ERROR_CANCELLED,
            "Channel request cancelled" };
        McdChannel *cancelled;

        cancelled = _mcd_dispatch_operation_dup_channel (operation);

        if (cancelled != NULL)
        {
            if (mcd_channel_get_error (cancelled) == NULL)
                mcd_channel_take_error (cancelled, g_error_copy (&error));

            _mcd_channel_undispatchable (cancelled);

            g_object_unref (cancelled);
        }
    }
    else if (_mcd_dispatch_operation_peek_channel (operation) == NULL)
    {
        DEBUG ("No channels left");
    }
    else
    {
        _mcd_dispatch_operation_run_clients (operation);
    }

    g_object_unref (operation);
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

    case PROP_SUPPORTS_REQUEST_HINTS:
        g_value_set_boolean (val, TRUE);
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

/*
 * @channel: a non-null TpChannel which has already been dispatched
 * @request: (allow-none): if not NULL, a request that resulted in
 *     @channel
 *
 * Return a Handler to which @channel could be re-dispatched,
 * for instance as a result of a re-request or a PresentChannel call.
 *
 * If @channel was dispatched to a Handler, return that Handler.
 * Otherwise, if it was Claimed by a process, and that process
 * has a Handler to which the channel could have been dispatched,
 * return that Handler. Otherwise return NULL.
 */
static McdClientProxy *
_mcd_dispatcher_lookup_handler (McdDispatcher *self,
                                TpChannel *channel,
                                McdRequest *request)
{
    McdClientProxy *handler = NULL;
    const gchar *object_path;
    const gchar *unique_name;
    const gchar *well_known_name;

    object_path = tp_proxy_get_object_path (channel);

    unique_name = _mcd_handler_map_get_handler (self->priv->handler_map,
                                                object_path,
                                                &well_known_name);

    if (unique_name == NULL)
    {
        DEBUG ("No process is handling channel %s", object_path);
        return NULL;
    }

    if (well_known_name != NULL)
    {
        /* We know which Handler well-known name was responsible: use it if it
         * still exists */
        DEBUG ("Channel %s is handler by %s", object_path, well_known_name);
        handler = _mcd_client_registry_lookup (self->priv->clients,
                                               well_known_name);
    }

    if (handler == NULL)
    {
        GList *possible_handlers;
        GVariant *request_properties = NULL;

        /* Failing that, maybe the Handler it was dispatched to was temporary;
         * try to pick another Handler that can deal with it, on the same
         * unique name (i.e. in the same process).
         * It can also happen in the case an Observer/Approver Claimed the
         * channel; in that case we did not get its handler well known name.
         */
        if (request != NULL)
            request_properties = mcd_request_dup_properties (request);

        possible_handlers = _mcd_client_registry_list_possible_handlers (
                self->priv->clients,
                request != NULL ? _mcd_request_get_preferred_handler (request) : NULL,
                request_properties, channel, unique_name);
        tp_clear_pointer (&request_properties, g_variant_unref);

        if (possible_handlers != NULL)
        {
            DEBUG ("Pick first possible handler for channel %s", object_path);
            handler = possible_handlers->data;
        }
        else
        {
            /* The process is still running (otherwise it wouldn't be in the
             * handler map), but none of its well-known names is still
             * interested in channels of that sort. Oh well, not our problem.
             */
            DEBUG ("process %s no longer interested in channel %s",
                   unique_name, object_path);
        }

        g_list_free (possible_handlers);
    }

    return handler;
}

static void
mcd_dispatcher_client_needs_recovery_cb (McdClientProxy *client,
                                         McdDispatcher *self)
{
    const GList *channels =
        _mcd_handler_map_get_handled_channels (self->priv->handler_map);
    const GList *observer_filters;
    const GList *list;

    DEBUG ("called");

    observer_filters = _mcd_client_proxy_get_observer_filters (client);

    for (list = channels; list; list = list->next)
    {
        TpChannel *channel = list->data;
        const gchar *object_path = tp_proxy_get_object_path (channel);
        GVariant *properties;
        McdClientProxy *handler;

        /* FIXME: This is not exactly the right behaviour, see fd.o#40305 */
        handler = _mcd_dispatcher_lookup_handler (self, channel, NULL);
        if (handler && _mcd_client_proxy_get_bypass_observers (handler))
        {
            DEBUG ("skipping unobservable channel %s", object_path);
            continue;
        }

        properties = tp_channel_dup_immutable_properties (channel);

        if (_mcd_client_match_filters (properties, observer_filters,
            FALSE))
        {
            const gchar *account_path =
                _mcd_handler_map_get_channel_account (self->priv->handler_map,
                    tp_proxy_get_object_path (channel));

            _mcd_client_recover_observer (client, channel, account_path);
        }

        g_variant_unref (properties);
    }

    /* we also need to think about channels that are still being dispatched,
     * but have got far enough that this client wouldn't otherwise see them */
    for (list = self->priv->operations; list != NULL; list = list->next)
    {
        McdDispatchOperation *op = list->data;

        if (_mcd_dispatch_operation_has_invoked_observers (op))
        {
            McdChannel *mcd_channel =
                _mcd_dispatch_operation_peek_channel (op);

            if (mcd_channel != NULL)
            {
                GVariant *properties =
                    mcd_channel_dup_immutable_properties (mcd_channel);

                if (_mcd_client_match_filters (properties, observer_filters,
                        FALSE))
                {
                    _mcd_client_recover_observer (client,
                        mcd_channel_get_tp_channel (mcd_channel),
                        _mcd_dispatch_operation_get_account_path (op));
                }

                g_variant_unref (properties);
            }
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
    g_ptr_array_unref (vas);
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

    tp_clear_pointer (&priv->connections, g_hash_table_unref);
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
    g_ptr_array_unref (vas);
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

    dgc = tp_proxy_get_dbus_connection (TP_PROXY (priv->dbus_daemon));

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
        { "SupportsRequestHints", "supports-request-hints", NULL },
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

    g_object_class_install_property (
        object_class, PROP_SUPPORTS_REQUEST_HINTS,
        g_param_spec_boolean ("supports-request-hints", "SupportsRequestHints",
                              "Yes, we support CreateChannelWithHints etc.",
                              TRUE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

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

/*
 * _mcd_dispatcher_add_channel:
 * @dispatcher: the #McdDispatcher.
 * @channel: (transfer none): a #McdChannel which must own a #TpChannel
 * @requested: whether the channels were requested by MC.
 *
 * Add @channel to the dispatching state machine.
 */
void
_mcd_dispatcher_add_channel (McdDispatcher *dispatcher,
                             McdChannel *channel,
                             gboolean requested,
                             gboolean only_observe)
{
    TpChannel *tp_channel = NULL;
    GStrv possible_handlers;
    McdRequest *request = NULL;
    gboolean internal_request = FALSE;

    g_return_if_fail (MCD_IS_DISPATCHER (dispatcher));
    g_return_if_fail (MCD_IS_CHANNEL (channel));

    DEBUG ("%s channel %p: %s",
           requested ? "requested" : "unrequested",
           channel,
           mcd_channel_get_object_path (channel));

    if (only_observe)
    {
        g_return_if_fail (requested);

        /* these channels were requested "behind our back", so only call
         * ObserveChannels on them */
        _mcd_dispatcher_enter_state_machine (dispatcher, channel, NULL,
                                             TRUE, TRUE);
        return;
    }

    /* The channel must have the TpChannel part of McdChannel's double life.
     * It might also have the McdRequest part. */
    tp_channel = mcd_channel_get_tp_channel (channel);
    g_assert (tp_channel != NULL);

    request = _mcd_channel_get_request (channel);
    internal_request = _mcd_request_is_internal (request);

    /* See if there are any handlers that can take all these channels */
    if (internal_request)
        possible_handlers = mcd_dispatcher_dup_internal_handlers ();
    else
        possible_handlers = mcd_dispatcher_dup_possible_handlers (dispatcher,
                                                                  request,
                                                                  tp_channel,
                                                                  NULL);

    if (possible_handlers == NULL)
    {
        DEBUG ("Channel cannot be handled - making a CDO "
               "anyway, to get Observers run");
    }
    else
    {
        DEBUG ("%s handler(s) found, dispatching channel",
               internal_request ? "internal" : "possible");
    }

    _mcd_channel_set_status (channel, MCD_CHANNEL_STATUS_DISPATCHING);

    _mcd_dispatcher_enter_state_machine (dispatcher, channel,
        (const gchar * const *) possible_handlers, requested, FALSE);

    g_strfreev (possible_handlers);
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
    McdClientProxy *handler = NULL;
    McdRequest *real_request = _mcd_channel_get_request (request);
    TpChannel *tp_channel = mcd_channel_get_tp_channel (request);
    GHashTable *handler_info;
    GHashTable *request_properties;

    g_assert (real_request != NULL);
    g_assert (tp_channel != NULL);

    request_as_list = g_list_append (NULL, request);

    request_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, (GDestroyNotify) g_hash_table_unref);
    g_hash_table_insert (request_properties,
        g_strdup (_mcd_request_get_object_path (real_request)),
        _mcd_request_dup_immutable_properties (real_request));

    handler_info = tp_asv_new (NULL, NULL);
    /* hand over ownership of request_properties */
    tp_asv_take_boxed (handler_info, "request-properties",
                       TP_HASH_TYPE_OBJECT_IMMUTABLE_PROPERTIES_MAP,
                       request_properties);
    request_properties = NULL;

    handler = _mcd_dispatcher_lookup_handler (dispatcher,
            tp_channel, real_request);
    if (handler == NULL)
    {
        mcd_dispatcher_finish_reinvocation (request);
        goto finally;
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
    McdRequest *origin = _mcd_channel_get_request (request);
    gboolean internal = _mcd_request_is_internal (origin);

    status = mcd_channel_get_status (channel);

    /* if the channel is already dispatched, just reinvoke the handler; if it
     * is not, @request must mirror the status of @channel */
    if (status == MCD_CHANNEL_STATUS_DISPATCHED)
    {
        DEBUG ("reinvoking handler on channel %p", request);

        /* copy the object path and the immutable properties from the
         * existing channel */
        _mcd_channel_copy_details (request, channel);

        if (internal)
          _mcd_request_handle_internally (origin, request, FALSE);
        else
          _mcd_dispatcher_reinvoke_handler (dispatcher, request);
    }
    else
    {
        DEBUG ("non-reinvoked handling of channel %p", request);
        _mcd_channel_set_request_proxy (request, channel);

        if (internal)
        {
            _mcd_request_handle_internally (origin, request, FALSE);
        }
        else if (status == MCD_CHANNEL_STATUS_DISPATCHING)
        {
            McdDispatchOperation *op = find_operation_from_channel (dispatcher,
                                                                    channel);
            const gchar *preferred_handler =
              _mcd_channel_get_request_preferred_handler (request);

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
        _mcd_dispatcher_add_channel (dispatcher, channel, requested, FALSE);
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
      (*error)->domain = TP_ERROR;
      (*error)->code = TP_ERROR_INVALID_ARGUMENT;
      return FALSE;
  }

  if (!g_str_has_prefix (preferred_handler, TP_CLIENT_BUS_NAME_BASE))
  {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
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
        g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
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

    g_hash_table_unref (params);
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
        g_ptr_array_unref (vas);
    }
    /* else _mcd_connection_start_dispatching will be called when we're ready
     * for it */
}

/* org.freedesktop.Telepathy.ChannelDispatcher.Messages */
typedef struct
{
    McdDispatcher *dispatcher;
    gchar *account_path;
    gchar *target_id;
    GPtrArray *payload;
    guint flags;
    guint tries;
    gboolean close_after;
    DBusGMethodInvocation *dbus_context;
} MessageContext;

static MessageContext *
message_context_steal (MessageContext *from)
{
    MessageContext *stolen = g_slice_new0 (MessageContext);

    g_memmove (stolen, from, sizeof (MessageContext));
    memset (from, 0, sizeof (MessageContext));

    return stolen;
}

static MessageContext *
message_context_new (McdDispatcher *dispatcher,
                     const gchar *account_path,
                     const gchar *target_id,
                     const GPtrArray *payload,
                     guint flags)
{
    guint i;
    const guint size = payload->len;
    MessageContext *context = g_slice_new0 (MessageContext);
    GPtrArray *msg_copy = g_ptr_array_sized_new (size);

    g_ptr_array_set_free_func (msg_copy, (GDestroyNotify) g_hash_table_unref);

    for (i = 0; i < size; i++)
    {
        GHashTable *part = g_ptr_array_index (payload, i);

        g_ptr_array_add (msg_copy, _mcd_deepcopy_asv (part));
    }

    context->dispatcher = g_object_ref (dispatcher);
    context->account_path = g_strdup (account_path);
    context->target_id = g_strdup (target_id);
    context->payload = msg_copy;
    context->flags = flags;
    context->dbus_context = NULL;

    return context;
}

static void
message_context_return_error (MessageContext *context, const GError *error)
{
    if (context->dbus_context == NULL)
        return;

    dbus_g_method_return_error (context->dbus_context, error);
    context->dbus_context = NULL;
}

static void
message_context_set_return_context (MessageContext *context,
                                    DBusGMethodInvocation *dbus_context)
{
    context->dbus_context = dbus_context;
}

static void
message_context_free (gpointer ctx)
{
    MessageContext *context = ctx;

    tp_clear_pointer (&context->payload, g_ptr_array_unref);
    tp_clear_pointer (&context->account_path, g_free);
    tp_clear_pointer (&context->target_id, g_free);

    if (context->dbus_context != NULL)
    {
        GError *error;

        error = g_error_new_literal (TP_ERROR, TP_ERROR_TERMINATED,
                                     "Channel request failed");
        dbus_g_method_return_error (context->dbus_context, error);
        g_error_free (error);
    }

    tp_clear_object (&context->dispatcher);

    g_slice_free (MessageContext, context);
}

static void
send_message_submitted (TpChannel *proxy,
                        const gchar *token,
                        const GError *error,
                        gpointer data,
                        GObject *weak)
{
    MessageContext *message = data;
    DBusGMethodInvocation *context = message->dbus_context;
    McdChannel *channel = MCD_CHANNEL (weak);
    McdRequest *request = _mcd_channel_get_request (channel);
    gboolean close_after = message->close_after;

    /* this frees the dbus context, so clear it from our cache afterwards */
    if (error == NULL)
    {
        mc_svc_channel_dispatcher_interface_messages_draft_return_from_send_message (context, token);
        message_context_set_return_context (message, NULL);
    }
    else
    {
        DEBUG ("error: %s", error->message);
        message_context_return_error (message, error);
    }

    _mcd_request_unblock_account (message->account_path);
    _mcd_request_clear_internal_handler (request);

    if (close_after)
        _mcd_channel_close (channel);
}

static void messages_send_message_start (DBusGMethodInvocation *context,
                                         MessageContext *message);

static void
send_message_got_channel (McdRequest *request,
                          McdChannel *channel,
                          gpointer data,
                          gboolean close_after)
{
    MessageContext *message = data;

    DEBUG ("received internal request/channel");

    /* successful channel creation */
    if (channel != NULL)
    {
        message->close_after = close_after;

        DEBUG ("calling send on channel interface");
        tp_cli_channel_interface_messages_call_send_message
          (mcd_channel_get_tp_channel (channel),
           -1,
           message->payload,
           message->flags,
           send_message_submitted,
           message,
           NULL,
           G_OBJECT (channel));
    }
    else /* doom and despair: no channel */
    {
        if (message->tries++ == 0)
        {
            messages_send_message_start (message->dbus_context, message);
            /* we created a new lock above, we can now release the old one: */
            _mcd_request_unblock_account (message->account_path);
        }
        else
        {
            GError *error = g_error_new_literal (TP_ERROR, TP_ERROR_CANCELLED,
                                                 "Channel closed by owner");

            _mcd_request_unblock_account (message->account_path);
            message_context_return_error (message, error);
            _mcd_request_clear_internal_handler (request);
            g_error_free (error);
        }
    }
}

static void
messages_send_message_acl_success (DBusGMethodInvocation *dbus_context,
                                   gpointer data)
{
    /* steal the contents of the message context from the ACL framework: *
     * this avoids a nasty double-free (and means we don't have to dup   *
     * the message payload memory twice)                                 */
    messages_send_message_start (dbus_context, message_context_steal (data));
}

static void
messages_send_message_start (DBusGMethodInvocation *dbus_context,
                             MessageContext *message)
{
    McdAccountManager *am;
    McdAccount *account;
    McdChannel *channel = NULL;
    McdRequest *request = NULL;
    GError *error = NULL;
    GHashTable *props = NULL;
    GValue c_type = G_VALUE_INIT;
    GValue h_type = G_VALUE_INIT;
    GValue target = G_VALUE_INIT;
    McdDispatcher *self = message->dispatcher;

    DEBUG ("messages_send_message_acl_success [attempt #%u]", message->tries);
    /* the message request can now take posession of the dbus method context */
    message_context_set_return_context (message, dbus_context);

    if (tp_str_empty (message->account_path))
    {
        g_set_error_literal (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                             "Account path not specified");
        goto failure;
    }

    g_object_get (self->priv->master, "account-manager", &am, NULL);

    g_assert (am != NULL);

    account =
      mcd_account_manager_lookup_account_by_path (am, message->account_path);

    if (account == NULL)
    {
        g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "No such account: %s", message->account_path);
        goto failure;
    }

    props = g_hash_table_new_full (g_str_hash, g_str_equal,
        NULL, (GDestroyNotify) g_value_unset);

    g_value_init (&c_type, G_TYPE_STRING);
    g_value_init (&h_type, G_TYPE_UINT);
    g_value_init (&target, G_TYPE_STRING);

    g_value_set_static_string (&c_type, TP_IFACE_CHANNEL_TYPE_TEXT);
    g_value_set_uint (&h_type, TP_HANDLE_TYPE_CONTACT);
    g_value_set_string (&target, message->target_id);

    g_hash_table_insert (props, TP_PROP_CHANNEL_CHANNEL_TYPE, &c_type);
    g_hash_table_insert (props, TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, &h_type);
    g_hash_table_insert (props, TP_PROP_CHANNEL_TARGET_ID, &target);

    /* compare dispatcher_request_channel: we _are_ the handler for     *
     * this channel so we don't need to check_preferred_handler here    *
     * Also: this deep-copies the props hash, so we can throw ours away */
    channel = _mcd_account_create_request (self->priv->clients,
                                           account, props, time (NULL),
                                           NULL, NULL, TRUE,
                                           &request, &error);
    g_hash_table_unref (props);

    if (channel == NULL || request == NULL)
    {
        g_set_error (&error, TP_ERROR, TP_ERROR_RESOURCE_UNAVAILABLE,
                     "Could not create channel request");
        goto failure;
    }

    _mcd_request_set_internal_handler (request,
                                       send_message_got_channel,
                                       message_context_free,
                                       message);

    /* we don't need to predict the handler either, same reason as above  *
     * we do, however, want to call proceed on the request, as it is ours */
    _mcd_request_proceed (request, NULL);

    goto finished;

failure:
    message_context_return_error (message, error);
    message_context_free (message);
    g_error_free (error);

finished:
    /* these are reffed and held open by the request infrastructure */
    tp_clear_object (&channel);
    tp_clear_object (&request);
}

static void
messages_send_message_acl_cleanup (gpointer data)
{
    MessageContext *message = data;

    /* At this point either the messages framework or the ACL framework   *
     * is expected to have handled the DBus return, so we must not try to */
    message_context_set_return_context (message, NULL);
    message_context_free (message);
}

static void
messages_send_message (McSvcChannelDispatcherInterfaceMessagesDraft *iface,
                       const gchar *account_path,
                       const gchar *target_id,
                       const GPtrArray *payload,
                       guint flags,
                       DBusGMethodInvocation *context)
{
    McdDispatcher *self= MCD_DISPATCHER (iface);
    MessageContext *message =
      message_context_new (self, account_path, target_id, payload, flags);

    /* these are for the ACL itself */
    GValue *account = g_slice_new0 (GValue);
    GHashTable *params =
      g_hash_table_new_full (g_str_hash, g_str_equal, NULL, free_gvalue);

    g_value_init (account, G_TYPE_STRING);
    g_value_set_string (account, account_path);
    g_hash_table_insert (params, "account-path", account);

    mcp_dbus_acl_authorised_async (self->priv->dbus_daemon,
                                   context,
                                   DBUS_ACL_TYPE_METHOD,
                                   SEND_MESSAGE,
                                   params,
                                   messages_send_message_acl_success,
                                   message,
                                   messages_send_message_acl_cleanup);
}

static void
messages_iface_init (gpointer iface, gpointer data G_GNUC_UNUSED)
{
#define IMPLEMENT(x) \
  mc_svc_channel_dispatcher_interface_messages_draft_implement_##x (iface, messages_##x)
    IMPLEMENT (send_message);
#undef IMPLEMENT
}

typedef struct
{
    McdDispatcher *self;
    gint64 user_action_time;
    DBusGMethodInvocation *context;
    /* List of owned ChannelToDelegate */
    GList *channels;
    /* array of owned channel path */
    GPtrArray *delegated;
    /* owned channel path -> owned GValueArray representing a
     * TP_STRUCT_TYPE_NOT_DELEGATED_ERROR  */
    GHashTable *not_delegated;
} DelegateChannelsCtx;

typedef struct
{
    /* borrowed reference */
    DelegateChannelsCtx *ctx;
    McdAccount *account;
    McdChannel *channel;
    /* Queue of reffed McdClientProxy */
    GQueue *handlers;
    GError *error;
}   ChannelToDelegate;

static ChannelToDelegate *
channel_to_delegate_new (DelegateChannelsCtx *ctx,
  McdAccount *account,
  McdChannel *channel)
{
    ChannelToDelegate *chan = g_slice_new0 (ChannelToDelegate);

    chan->ctx = ctx;
    chan->account = g_object_ref (account);
    chan->channel = g_object_ref (channel);
    chan->handlers = g_queue_new ();
    chan->error = NULL;
    return chan;
}

static void
channel_to_delegate_free (ChannelToDelegate *chan)
{
    g_object_unref (chan->account);
    g_object_unref (chan->channel);
    g_queue_foreach (chan->handlers, (GFunc) g_object_unref, NULL);
    g_queue_free (chan->handlers);
    tp_clear_pointer (&chan->error, g_error_free);
    g_slice_free (ChannelToDelegate, chan);
}

static void
free_not_delegated_error (gpointer data)
{
    g_boxed_free (TP_STRUCT_TYPE_NOT_DELEGATED_ERROR, data);
}

static DelegateChannelsCtx *
delegate_channels_ctx_new (McdDispatcher *self,
    gint64 user_action_time,
    DBusGMethodInvocation *context)
{
    DelegateChannelsCtx *ctx = g_slice_new0 (DelegateChannelsCtx);

    ctx->self = g_object_ref (self);
    ctx->user_action_time = user_action_time;
    ctx->context = context;
    ctx->channels = NULL;
    ctx->delegated = g_ptr_array_new_with_free_func (g_free);
    ctx->not_delegated = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, free_not_delegated_error);
    return ctx;
}

static void
delegate_channels_ctx_free (DelegateChannelsCtx *ctx)
{
    g_object_unref (ctx->self);
    g_ptr_array_unref (ctx->delegated);
    g_hash_table_unref (ctx->not_delegated);
    g_list_free_full (ctx->channels, (GDestroyNotify) channel_to_delegate_free);
    g_slice_free (DelegateChannelsCtx, ctx);
}

static void try_delegating (ChannelToDelegate *to_delegate);

static void
delegation_done (ChannelToDelegate *to_delegate)
{
    DelegateChannelsCtx *ctx = to_delegate->ctx;

    ctx->channels = g_list_remove (ctx->channels, to_delegate);
    channel_to_delegate_free (to_delegate);

    if (ctx->channels == NULL)
      {
        /* We are done */
        tp_svc_channel_dispatcher_return_from_delegate_channels (
            ctx->context, ctx->delegated, ctx->not_delegated);

        delegate_channels_ctx_free (ctx);
      }
}

static void
delegate_channels_cb (TpClient *client,
    const GError *error,
    gpointer user_data G_GNUC_UNUSED,
    GObject *weak_object)
{
    ChannelToDelegate *to_delegate = user_data;
    DelegateChannelsCtx *ctx = to_delegate->ctx;
    McdClientProxy *clt_proxy = MCD_CLIENT_PROXY (client);

    /* If the delegation succeeded, the channel has a new handler. If
     * the delegation failed, the channel still has the old
     * handler. Either way, the channel still has a handler, so it has
     * been successfully dispatched (from 'handler invoked'). */
    _mcd_channel_set_status (to_delegate->channel,
        MCD_CHANNEL_STATUS_DISPATCHED);

    if (error != NULL)
      {
        DEBUG ("Handler refused delegated channels");

        if (to_delegate->error == NULL)
            to_delegate->error = g_error_copy (error);

        try_delegating (to_delegate);
        return;
      }

    DEBUG ("Channel %s has been delegated", mcd_channel_get_object_path (
        to_delegate->channel));

    _mcd_handler_map_set_path_handled (ctx->self->priv->handler_map,
        mcd_channel_get_object_path (to_delegate->channel),
        _mcd_client_proxy_get_unique_name (clt_proxy),
        tp_proxy_get_bus_name (client));

    g_ptr_array_add (ctx->delegated, g_strdup (
        mcd_channel_get_object_path (to_delegate->channel)));

    delegation_done (to_delegate);
}

static void
try_delegating (ChannelToDelegate *to_delegate)
{
    McdClientProxy *client;
    GList *channels = NULL;

    DEBUG ("%s",
        mcd_channel_get_object_path (to_delegate->channel));

    if (g_queue_get_length (to_delegate->handlers) == 0)
      {
        GValueArray *v;
        const gchar *dbus_error;

        if (to_delegate->error == NULL)
          {
            g_set_error (&to_delegate->error, TP_ERROR, TP_ERROR_NOT_CAPABLE,
                "There is no other suitable handler");
          }

        if (to_delegate->error->domain == TP_ERROR)
          dbus_error = tp_error_get_dbus_name (to_delegate->error->code);
        else
          dbus_error = TP_ERROR_STR_NOT_AVAILABLE;

        /* We failed to delegate this channel */
        v = tp_value_array_build (2,
          G_TYPE_STRING, dbus_error,
          G_TYPE_STRING, to_delegate->error->message,
          G_TYPE_INVALID);

        g_hash_table_insert (to_delegate->ctx->not_delegated,
            g_strdup (mcd_channel_get_object_path (to_delegate->channel)),
            v);

        DEBUG ("...but failed to delegate %s: %s",
            mcd_channel_get_object_path (to_delegate->channel),
            to_delegate->error->message);

        delegation_done (to_delegate);
        return;
      }

    client = g_queue_pop_head (to_delegate->handlers);

    DEBUG ("...trying client %s", _mcd_client_proxy_get_unique_name (
        client));

    channels = g_list_prepend (channels, to_delegate->channel);

    _mcd_client_proxy_handle_channels (client, -1, channels,
        to_delegate->ctx->user_action_time, NULL, delegate_channels_cb,
        to_delegate, NULL, NULL);

    g_object_unref (client);
    g_list_free (channels);
}

static void
add_possible_handlers (McdDispatcher *self,
    ChannelToDelegate *to_delegate,
    TpChannel *tp_channel,
    const gchar *sender,
    const gchar *preferred_handler)
{
    GStrv possible_handlers;
    guint i;

    possible_handlers = mcd_dispatcher_dup_possible_handlers (self,
        NULL, tp_channel, NULL);

    for (i = 0; possible_handlers[i] != NULL; i++)
      {
        McdClientProxy *client;
        const gchar *unique_name;

        client = _mcd_client_registry_lookup (self->priv->clients,
            possible_handlers[i]);
        g_return_if_fail (client != NULL);

        unique_name = _mcd_client_proxy_get_unique_name (client);

        /* Skip the caller */
        if (!tp_strdiff (unique_name, sender))
            continue;

        /* Put the preferred handler at the head of the list so it will be tried
         * first */
        if (!tp_strdiff (possible_handlers[i], preferred_handler))
          g_queue_push_head (to_delegate->handlers, g_object_ref (client));
        else
          g_queue_push_tail (to_delegate->handlers, g_object_ref (client));
      }

    g_strfreev (possible_handlers);
}

static void
dispatcher_delegate_channels (
    TpSvcChannelDispatcher *iface,
    const GPtrArray *channels,
    gint64 user_action_time,
    const gchar *preferred_handler,
    DBusGMethodInvocation *context)
{
    McdDispatcher *self = (McdDispatcher *) iface;
    GError *error = NULL;
    gchar *sender = NULL;
    McdConnection *conn = NULL;
    DelegateChannelsCtx *ctx = NULL;
    McdAccountManager *am = NULL;
    guint i;
    GList *l;

    DEBUG ("called");

    if (!check_preferred_handler (preferred_handler, &error))
        goto error;

    if (channels->len == 0)
      {
        g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
            "Need at least one channel to delegate");
        goto error;
      }

    ctx = delegate_channels_ctx_new (self, user_action_time, context);

    sender = dbus_g_method_get_sender (context);

    g_object_get (self->priv->master, "account-manager", &am, NULL);
    g_assert (am != NULL);

    for (i = 0; i < channels->len; i++)
      {
        const gchar *chan_path = g_ptr_array_index (channels, i);
        const gchar *chan_account;
        const gchar *handler;
        McdChannel *mcd_channel;
        TpChannel *tp_channel;
        McdAccount *account;
        ChannelToDelegate *to_delegate;

        chan_account = _mcd_handler_map_get_channel_account (
            self->priv->handler_map, chan_path);

        if (chan_account == NULL)
          {
            g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                "Unknown channel: %s", chan_path);
            goto error;
          }

        account = mcd_account_manager_lookup_account_by_path (am,
            chan_account);
        g_return_if_fail (account != NULL);

        /* Check the caller is handling the channel */
        handler = _mcd_handler_map_get_handler (self->priv->handler_map,
            chan_path, NULL);
        if (tp_strdiff (sender, handler))
         {
            g_set_error (&error, TP_ERROR, TP_ERROR_NOT_YOURS,
                "Your are not handling channel %s", chan_path);
            goto error;
         }

        conn = mcd_account_get_connection (account);
        g_return_if_fail (conn != NULL);

        mcd_channel = mcd_connection_find_channel_by_path (conn, chan_path);
        g_return_if_fail (mcd_channel != NULL);

        tp_channel = mcd_channel_get_tp_channel (mcd_channel);
        g_return_if_fail (tp_channel != NULL);

        to_delegate = channel_to_delegate_new (ctx, account, mcd_channel);

        add_possible_handlers (self, to_delegate, tp_channel, sender,
            preferred_handler);

        ctx->channels = g_list_prepend (ctx->channels, to_delegate);
      }

    /* All the channels were ok, we can start delegating */
    for (l = ctx->channels; l != NULL; l = g_list_next (l))
      {
        ChannelToDelegate *to_delegate = l->data;

        try_delegating (to_delegate);
      }

    g_free (sender);
    g_object_unref (am);

    return;

error:
    g_free (sender);
    dbus_g_method_return_error (context, error);
    g_error_free (error);

    tp_clear_pointer (&ctx, delegate_channels_ctx_free);
    tp_clear_object (&am);
}

static void
present_handle_channels_cb (TpClient *client,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
    DBusGMethodInvocation *context = user_data;
    McdChannel *mcd_channel = MCD_CHANNEL (weak_object);

    /* Whether presenting the channel succeeded or failed, the
     * channel's handler hasn't been altered, so it must be set back
     * to the dispatched state (from 'handler invoked'). */
    _mcd_channel_set_status (mcd_channel,
        MCD_CHANNEL_STATUS_DISPATCHED);

    if (error != NULL)
      {
        dbus_g_method_return_error (context, error);
        return;
      }

    tp_svc_channel_dispatcher_return_from_present_channel (context);
}

static void
dispatcher_present_channel (
    TpSvcChannelDispatcher *iface,
    const gchar *channel_path,
    gint64 user_action_time,
    DBusGMethodInvocation *context)
{
    McdDispatcher *self = (McdDispatcher *) iface;
    McdAccountManager *am;
    const gchar *chan_account;
    McdAccount *account;
    McdConnection *conn;
    McdChannel *mcd_channel;
    GError *error = NULL;
    McdClientProxy *client;
    GList *channels = NULL;

    chan_account = _mcd_handler_map_get_channel_account (
        self->priv->handler_map, channel_path);

    if (chan_account == NULL)
      {
        g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
            "Unknown channel: %s", channel_path);
        goto error;
      }

    g_object_get (self->priv->master, "account-manager", &am, NULL);
    g_assert (am != NULL);

    account = mcd_account_manager_lookup_account_by_path (am, chan_account);
    g_return_if_fail (account != NULL);
    g_object_unref (am);

    conn = mcd_account_get_connection (account);
    g_return_if_fail (conn != NULL);

    mcd_channel = mcd_connection_find_channel_by_path (conn, channel_path);
    g_return_if_fail (mcd_channel != NULL);

    /* We take mcd_channel's request to base the search for a suitable Handler
     * on the handler that was preferred by the request that initially created
     * the Channel, if any.
     * Actually not, because of fd.o#41031 */
    client = _mcd_dispatcher_lookup_handler (self,
            mcd_channel_get_tp_channel (mcd_channel),
            _mcd_channel_get_request (mcd_channel));
    if (client == NULL)
      {
        g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
            "Channel %s is currently not handled", channel_path);
        goto error;
      }

    channels = g_list_append (channels, mcd_channel);

    _mcd_client_proxy_handle_channels (client, -1, channels,
        user_action_time, NULL, present_handle_channels_cb,
        context, NULL, G_OBJECT (mcd_channel));

    g_list_free (channels);
    return;

error:
    dbus_g_method_return_error (context, error);
    g_error_free (error);
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
    IMPLEMENT (delegate_channels);
    IMPLEMENT (present_channel);
#undef IMPLEMENT
}
