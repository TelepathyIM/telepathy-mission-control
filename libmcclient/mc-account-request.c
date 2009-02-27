/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * mc-account-request.c - Telepathy Account D-Bus interface (client side)
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Alberto Mardegan  <alberto.mardegan@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define MC_INTERNAL

#include <stdio.h>
#include <string.h>
#include "mc-account.h"
#include "mc-account-priv.h"
#include "dbus-api.h"
#include "mc-errors.h"

#include <telepathy-glib/interfaces.h>

typedef struct
{
    guint id;
    McAccount *account;
    gchar *request_path;
    GError *error;
    gboolean cancelled;

    /* caller data */
    McAccountChannelrequestCb callback;
    gpointer user_data;
    GDestroyNotify destroy;
    GObject *weak_object;
} McChannelRequest;

static GHashTable *requests = NULL;
static guint last_request_id = 0;

#define REQUEST_FROM_ID(id) request_from_id(id)
#define REQUEST_ID(req) (req->id)

static inline McChannelRequest *
request_from_id (guint id)
{
    if (G_UNLIKELY (!requests)) return NULL;

    return g_hash_table_lookup (requests, GUINT_TO_POINTER (id));
}

static void
on_weak_object_destroy (guint id, GObject *weak_object)
{
    McChannelRequest *req;

    g_debug ("%s called (%u, %p)", G_STRFUNC, id, weak_object);
    req = REQUEST_FROM_ID (id);
    if (G_UNLIKELY (!req)) return;

    g_return_if_fail (req->weak_object == weak_object);
    req->weak_object = NULL;
    g_hash_table_remove (requests, GUINT_TO_POINTER (id));
}

static void
mc_request_free (McChannelRequest *req)
{
    if (req->weak_object)
        g_object_weak_unref (req->weak_object,
                             (GWeakNotify)on_weak_object_destroy,
                             GUINT_TO_POINTER (req->id));
    if (req->destroy)
        req->destroy (req->user_data);
    g_free (req->request_path);
    if (req->error)
        g_error_free (req->error);
    g_slice_free (McChannelRequest, req);
}

static void
emit_request_event (McChannelRequest *req, McAccountChannelrequestEvent event)
{
    guint id;

    id = REQUEST_ID (req);

    if (req->callback)
        req->callback (req->account, id, event,
                       req->user_data, req->weak_object);

    if (event == MC_ACCOUNT_CR_SUCCEEDED ||
        event == MC_ACCOUNT_CR_FAILED ||
        event == MC_ACCOUNT_CR_CANCELLED)
    {
        g_hash_table_remove (requests, GUINT_TO_POINTER (id));
    }
}

static void
request_create_cb (TpProxy *proxy, const gchar *request_path,
                   const GError *error, gpointer user_data,
                   GObject *weak_object)
{
    McChannelRequest *req = user_data;

    if (req->cancelled)
    {
        g_debug ("%s: cancelling %s", G_STRFUNC, request_path);
        if (!error)
            mc_cli_account_interface_channelrequests_call_cancel
                (proxy, -1, request_path, NULL, NULL, NULL, NULL);
        emit_request_event (req, MC_ACCOUNT_CR_CANCELLED);
        return;
    }

    if (error)
    {
        /* the request hasn't even been created */
        req->error = g_error_copy (error);
        emit_request_event (req, MC_ACCOUNT_CR_FAILED);
        return;
    }
    g_debug ("%s called with %s", G_STRFUNC, request_path);
    req->request_path = g_strdup (request_path);
}

static void
on_request_failed (TpProxy *proxy, const gchar *request_path,
                   const gchar *error_name, const gchar *error_message,
                   gpointer user_data, GObject *weak_object)
{
    McChannelRequest *req;
    guint id;

    g_debug ("%s called for %s", G_STRFUNC, request_path);
    id = mc_channelrequest_get_from_path (request_path);
    if (id == 0) /* not our request, ignore it */
        return;

    req = REQUEST_FROM_ID (id);
    if (G_UNLIKELY (!req)) return;

    tp_proxy_dbus_error_to_gerror (proxy, error_name, error_message,
                                   &req->error);
    emit_request_event (req, MC_ACCOUNT_CR_FAILED);
}

static void
on_request_succeeded (TpProxy *proxy, const gchar *request_path,
                      gpointer user_data, GObject *weak_object)
{
    McChannelRequest *req;
    guint id;

    g_debug ("%s called for %s", G_STRFUNC, request_path);
    id = mc_channelrequest_get_from_path (request_path);
    if (id == 0) /* not our request, ignore it */
        return;

    req = REQUEST_FROM_ID (id);
    if (G_UNLIKELY (!req)) return;

    emit_request_event (req, MC_ACCOUNT_CR_SUCCEEDED);
}

static McChannelRequest *
create_request_struct (McAccount *account,
                       McAccountChannelrequestCb callback,
                       gpointer user_data, GDestroyNotify destroy,
                       GObject *weak_object)
{
    McChannelRequest *req;

    if (!requests)
    {
        requests = g_hash_table_new_full (NULL, NULL, NULL,
                                          (GDestroyNotify)mc_request_free);

        mc_cli_account_interface_channelrequests_connect_to_failed (account,
            on_request_failed, NULL, NULL, NULL, NULL);
        mc_cli_account_interface_channelrequests_connect_to_succeeded (account,
            on_request_succeeded, NULL, NULL, NULL, NULL);
    }

    req = g_slice_new0 (McChannelRequest);
    req->id = ++last_request_id;
    req->account = account;
    req->callback = callback;
    req->user_data = user_data;
    req->destroy = destroy;
    if (weak_object)
    {
        req->weak_object = weak_object;
        g_object_weak_ref (weak_object,
                           (GWeakNotify)on_weak_object_destroy,
                           GUINT_TO_POINTER (req->id));
    }

    g_hash_table_insert (requests, GUINT_TO_POINTER (req->id), req);

    return req;
}

void
_mc_account_channelrequests_class_init (McAccountClass *klass)
{
    /* nothing here, as we don't have any properties on the interface */
}

/**
 * McAccountChannelrequestCb:
 * @account: the #McAccount.
 * @request_id: unique identifier of the channel request.
 * @event: one #McAccountChannelrequestEvent.
 * @user_data: the user data that was passed when registering the callback.
 * @weak_object: the #GObject that was passed when registering the callback.
 *
 * This callback will be called when some event occurs on the channel request.
 * If the event is one of %MC_ACCOUNT_CR_SUCCEEDED, %MC_ACCOUNT_CR_FAILED or
 * %MC_ACCOUNT_CR_CANCELLED, the channel request should be considered
 * completed, and @request_id will be no longer valid.
 * This callback could be called multiple times, in case some other events than
 * %MC_ACCOUNT_CR_SUCCEEDED, %MC_ACCOUNT_CR_FAILED and %MC_ACCOUNT_CR_CANCELLED
 * occur.
 */

/**
 * McAccountChannelrequestFlags:
 * @MC_ACCOUNT_CR_FLAG_USE_EXISTING: allow requesting of an existing channel
 * (EnsureChannel will be called).
 */

/**
 * mc_account_channelrequest:
 * @account: the #McAccount.
 * @req_data: a #McAccountChannelrequestData struct with the requested fields
 * set.
 * @user_action_time: the time at which user action occurred, or %0.
 * @handler: well-known name of the preferred handler, or %NULL.
 * @flags: a combination of #McAccountChannelrequestFlags.
 * @callback: called when something happens to the request.
 * @user_data: user data to be passed to @callback.
 * @destroy: called with the user_data as argument, after the request has
 * succeeded, failed or been cancelled.
 * @weak_object: If not %NULL, a #GObject which will be weakly referenced; if
 * it is destroyed, this call will automatically be cancelled.
 *
 * This is a convenience function that internally calls
 * mc_account_channelrequest_ht(). The only difference between the two
 * functions is that this one takes the requested properties in the form of a
 * #McAccountChannelrequestData structure.
 *
 * Returns: the unique ID of the channel request.
 */
guint
mc_account_channelrequest (McAccount *account,
                           const McAccountChannelrequestData *req_data,
                           time_t user_action_time, const gchar *handler,
                           McAccountChannelrequestFlags flags,
                           McAccountChannelrequestCb callback,
                           gpointer user_data, GDestroyNotify destroy,
                           GObject *weak_object)
{
    GHashTable *properties;
    GValue v_channel_type;
    GValue v_target_handle;
    GValue v_target_handle_type;
    GValue v_target_id;
    guint id;

    properties = g_hash_table_new (g_str_hash, g_str_equal);

    /* If more fields are added, it might be worth refactoring this code */
    if (MC_ACCOUNT_CRD_IS_SET (req_data, channel_type))
    {
        GQuark channel_type;

        channel_type = MC_ACCOUNT_CRD_GET (req_data, channel_type);
        v_channel_type.g_type = 0;
        g_value_init (&v_channel_type, G_TYPE_STRING);
        g_value_set_static_string (&v_channel_type,
                                   g_quark_to_string (channel_type));
        g_hash_table_insert (properties,
                             TP_IFACE_CHANNEL ".ChannelType",
                             &v_channel_type);
    }

    if (MC_ACCOUNT_CRD_IS_SET (req_data, target_handle))
    {
        v_target_handle.g_type = 0;
        g_value_init (&v_target_handle, G_TYPE_UINT);
        g_value_set_uint (&v_target_handle,
                          MC_ACCOUNT_CRD_GET (req_data, target_handle));
        g_hash_table_insert (properties,
                             TP_IFACE_CHANNEL ".TargetHandle",
                             &v_target_handle);
    }

    if (MC_ACCOUNT_CRD_IS_SET (req_data, target_handle_type))
    {
        v_target_handle_type.g_type = 0;
        g_value_init (&v_target_handle_type, G_TYPE_UINT);
        g_value_set_uint (&v_target_handle_type,
                          MC_ACCOUNT_CRD_GET (req_data, target_handle_type));
        g_hash_table_insert (properties,
                             TP_IFACE_CHANNEL ".TargetHandleType",
                             &v_target_handle_type);
    }

    if (MC_ACCOUNT_CRD_IS_SET (req_data, target_id))
    {
        v_target_id.g_type = 0;
        g_value_init (&v_target_id, G_TYPE_STRING);
        g_value_set_static_string (&v_target_id,
                                   MC_ACCOUNT_CRD_GET (req_data, target_id));
        g_hash_table_insert (properties,
                             TP_IFACE_CHANNEL ".TargetID",
                             &v_target_id);
    }

    id = mc_account_channelrequest_ht (account, properties, user_action_time,
                                        handler, flags, callback, user_data,
                                        destroy, weak_object);
    g_hash_table_destroy (properties);
    return id;
}

/**
 * mc_account_channelrequest_ht:
 * @account: the #McAccount.
 * @properties: a #GHashTable with the requested channel properties.
 * @user_action_time: the time at which user action occurred, or %0.
 * @handler: well-known name of the preferred handler, or %NULL.
 * @flags: a combination of #McAccountChannelrequestFlags.
 * @callback: called when something happens to the request.
 * @user_data: user data to be passed to @callback.
 * @destroy: called with the user_data as argument, after the request has
 * succeeded, failed or been cancelled.
 * @weak_object: If not %NULL, a #GObject which will be weakly referenced; if
 * it is destroyed, this call will automatically be cancelled.
 *
 * Requests a channel matching all the requested @properties. The channel
 * request is uniquely identified (inside the process that called this method)
 * by an unsigned integer ID, which is the return value of the method and also
 * the second parameter passed to @callback.
 *
 * Unless the @weak_object is destroyed, @callback will be called to notify the
 * requestor of the progress of its request. The only events supported so far
 * are %MC_ACCOUNT_CR_SUCCEEDED, %MC_ACCOUNT_CR_FAILED and
 * %MC_ACCOUNT_CR_CANCELLED, which also happen to signal the end of the request:
 * after one of these events occur, the request ID is no longer valid, and
 * @destroy (if it was not %NULL) is called on @user_data.
 *
 * If the @weak_object is destroyed before the channel request is completed,
 * @callback will not be called anymore, but @destroy (if it was not %NULL) is
 * called on @user_data; the channel request is left at whatever state it was:
 * if you want it to be cancelled, you need to call
 * mc_account_channelrequest_cancel() explicitly.
 *
 * Returns: the unique ID of the channel request.
 */
guint
mc_account_channelrequest_ht (McAccount *account,
                              GHashTable *properties,
                              time_t user_action_time, const gchar *handler,
                              McAccountChannelrequestFlags flags,
                              McAccountChannelrequestCb callback,
                              gpointer user_data, GDestroyNotify destroy,
                              GObject *weak_object)
{
    McChannelRequest *req;

    g_return_val_if_fail (MC_IS_ACCOUNT (account), 0);
    req = create_request_struct (account, callback, user_data, destroy,
                                 weak_object);

    if (flags & MC_ACCOUNT_CR_FLAG_USE_EXISTING)
        mc_cli_account_interface_channelrequests_call_ensure_channel
            (account, -1, properties, user_action_time, handler,
             request_create_cb, req, NULL, NULL);
    else
        mc_cli_account_interface_channelrequests_call_create
            (account, -1, properties, user_action_time, handler,
             request_create_cb, req, NULL, NULL);

    return REQUEST_ID (req);
}

/**
 * mc_account_channelrequest_add:
 * @account: the #McAccount.
 * @object_path: the D-Bus object path of a channel request.
 * @properties: a D-Bus a{sv} of properties.
 * @callback: called when something happens to the request.
 * @user_data: user data to be passed to @callback.
 * @destroy: called with the user_data as argument, after the request has
 * succeeded, failed or been cancelled.
 * @weak_object: If not %NULL, a #GObject which will be weakly referenced; if
 * it is destroyed, this call will automatically be cancelled.
 *
 * This function adds an existing request, created from another process and
 * described by @object_path and @properties, to those to be monitored.
 *
 * Returns: the unique ID of the channel request, or %0 if the request was
 * already being monitored by another callback.
 */
guint
mc_account_channelrequest_add (McAccount *account, const gchar *object_path,
                               GHashTable *properties,
                               McAccountChannelrequestCb callback,
                               gpointer user_data, GDestroyNotify destroy,
                               GObject *weak_object)
{
    McChannelRequest *req;
    guint id;

    g_return_val_if_fail (MC_IS_ACCOUNT (account), 0);

    /* check whether this request is already monitored by us */
    id = mc_account_channelrequest_get_from_path (account, object_path);
    if (id != 0)
    {
        req = REQUEST_FROM_ID (id);
        /* either we properly invoke this callback too, or we must return an
         * error to inform that it will not be called */
        if (callback != NULL &&
            (callback != req->callback || user_data != req->user_data ||
             destroy != req->destroy))
        {
            g_warning ("%s: request %s is already monitored", G_STRFUNC,
                       object_path);
            return 0;
        }
        return id;
    }

    req = create_request_struct (account, callback, user_data, destroy,
                                 weak_object);
    req->request_path = g_strdup (object_path);
    /* at the moment there isn't even a method for retrieving the properties,
     * so let's ignore them */
    return REQUEST_ID (req);
}

/**
 * mc_account_channelrequest_cancel:
 * @account: the #McAccount.
 * @request_id: the ID of the request to be cancelled.
 *
 * Cancel the channel request identified by @request_id.
 */
void
mc_account_channelrequest_cancel (McAccount *account, guint request_id)
{
    McChannelRequest *req;

    g_return_if_fail (MC_IS_ACCOUNT (account));
    g_return_if_fail (request_id != 0);
    req = REQUEST_FROM_ID (request_id);
    if (G_UNLIKELY (!req))
    {
        g_warning ("%s: invalid request ID: %u", G_STRFUNC, request_id);
        return;
    }

    if (req->request_path)
    {
        g_debug ("%s: %s", G_STRFUNC, req->request_path);
        mc_cli_account_interface_channelrequests_call_cancel
            (account, -1, req->request_path, NULL, NULL, NULL, NULL);
        emit_request_event (req, MC_ACCOUNT_CR_CANCELLED);
    }
    else
    {
        req->cancelled = TRUE;
    }
}

/**
 * mc_account_channelrequest_get_error:
 * @account: the #McAccount.
 * @request_id: the ID of the channel request.
 *
 * Get the last error which occurred on the channel request identified by
 * @request_id.
 *
 * Returns: a #GError (not to be freed), or %NULL.
 */
const GError *
mc_account_channelrequest_get_error (McAccount *account, guint request_id)
{
    McChannelRequest *req;

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    g_return_val_if_fail (request_id != 0, NULL);
    req = REQUEST_FROM_ID (request_id);
    if (G_UNLIKELY (!req))
    {
        g_warning ("%s: invalid request ID: %u", G_STRFUNC, request_id);
        return NULL;
    }
    return req->error;
}

/**
 * mc_account_channelrequest_get_path:
 * @account: the #McAccount.
 * @request_id: the ID of the channel request.
 *
 * Get the object path of the channel request identified by @request_id.
 * The channel request D-Bus object is currently not implemented, but this
 * object path can be used consistently with the
 * org.freedesktop.Telepathy.Client.Handler interface.
 *
 * Returns: the object path of the channel request.
 */
const gchar *
mc_account_channelrequest_get_path (McAccount *account, guint request_id)
{
    return mc_channelrequest_get_path (request_id);
}

/**
 * mc_account_channelrequest_get_from_path:
 * @account: the #McAccount.
 * @object_path: the D-Bus object path of a channel request.
 *
 * Finds the request ID whose D-Bus object path matches @object_path.
 * This only works if the request was created by this process.
 *
 * Returns: the unique ID of the channel request, or %0.
 */
guint
mc_account_channelrequest_get_from_path (McAccount *account,
                                         const gchar *object_path)
{
    return mc_channelrequest_get_from_path (object_path);
}

/**
 * mc_channelrequest_get_path:
 * @request_id: the ID of the channel request.
 *
 * Get the object path of the channel request identified by @request_id.
 * The channel request D-Bus object is currently not implemented, but this
 * object path can be used consistently with the
 * org.freedesktop.Telepathy.Client.Handler interface.
 *
 * Returns: the object path of the channel request.
 */
const gchar *
mc_channelrequest_get_path (guint request_id)
{
    McChannelRequest *req;

    g_return_val_if_fail (request_id != 0, NULL);
    req = REQUEST_FROM_ID (request_id);
    return req ? req->request_path : NULL;
}

/**
 * mc_channelrequest_get_from_path:
 * @object_path: the D-Bus object path of a channel request.
 *
 * Finds the request ID whose D-Bus object path matches @object_path.
 * This only works if the request was created by this process.
 *
 * Returns: the unique ID of the channel request, or %0.
 */
guint
mc_channelrequest_get_from_path (const gchar *object_path)
{
    McChannelRequest *req;
    GHashTableIter iter;

    g_return_val_if_fail (object_path != NULL, 0);
    if (G_UNLIKELY (!requests)) return 0;

    g_hash_table_iter_init (&iter, requests);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer)&req))
    {
        if (req->request_path && strcmp (req->request_path, object_path) == 0)
            return REQUEST_ID (req);
    }
    return 0;
}

/**
 * mc_channelrequest_get_account:
 * @request_id: the ID of the channel request.
 *
 * Gets the account on which this channel request was made.
 *
 * Returns: the #McAccount (reference count is not increased), or %NULL if the
 * request is not valid.
 */
McAccount *
mc_channelrequest_get_account (guint request_id)
{
    McChannelRequest *req;

    g_return_val_if_fail (request_id != 0, NULL);
    req = REQUEST_FROM_ID (request_id);
    return req ? req->account : NULL;
}

