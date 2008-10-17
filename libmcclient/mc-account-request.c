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

#include <stdio.h>
#include <string.h>
#include "mc-account.h"
#include "mc-account-priv.h"
#include "dbus-api.h"

#include <telepathy-glib/interfaces.h>

/**
 * McAccountChannelRequestCb:
 * @account: the #McAccount.
 * @request_id: unique identifier of the channel request.
 * @event: one #McAccountChannelRequestEvent.
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
 * mc_account_channel_request:
 * @account: the #McAccount.
 * @req_data: a #McAccountChannelRequestData struct with the requested fields
 * set.
 * @user_action_time: the time at which user action occurred, or %0.
 * @handler: well-known name of the preferred handler, or %NULL.
 * @callback: called when something happens to the request.
 * @user_data: user data to be passed to @callback.
 * @destroy: called with the user_data as argument, after the request has
 * succeeded, failed or been cancelled.
 * @weak_object: If not %NULL, a #GObject which will be weakly referenced; if
 * it is destroyed, this call will automatically be cancelled.
 *
 * This is a convenience function that internally calls
 * mc_account_channel_request_ht(). The only difference between the two
 * functions is that this one takes the requested properties in the form of a
 * #McAccountChannelRequestData structure.
 *
 * Returns: the unique ID of the channel request.
 */
guint
mc_account_channel_request (McAccount *account,
                            const McAccountChannelRequestData *req_data,
                            time_t user_action_time, const gchar *handler,
                            McAccountChannelRequestCb callback,
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
                             &v_channel_type);
    }

    if (MC_ACCOUNT_CRD_IS_SET (req_data, target_handle_type))
    {
        v_target_handle_type.g_type = 0;
        g_value_init (&v_target_handle_type, G_TYPE_UINT);
        g_value_set_uint (&v_target_handle_type,
                          MC_ACCOUNT_CRD_GET (req_data, target_handle_type));
        g_hash_table_insert (properties,
                             TP_IFACE_CHANNEL ".TargetHandleType",
                             &v_channel_type);
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

    id = mc_account_channel_request_ht (account, properties, user_action_time,
                                        handler, callback, user_data, destroy,
                                        weak_object);
    g_hash_table_destroy (properties);
    return id;
}

/**
 * mc_account_channel_request_ht:
 * @account: the #McAccount.
 * @properties: a #GHashTable with the requested channel properties.
 * @user_action_time: the time at which user action occurred, or %0.
 * @handler: well-known name of the preferred handler, or %NULL.
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
 * mc_account_channel_request_cancel() explicitly.
 *
 * Returns: the unique ID of the channel request.
 */
guint
mc_account_channel_request_ht (McAccount *account,
                               GHashTable *properties,
                               time_t user_action_time, const gchar *handler,
                               McAccountChannelRequestCb callback,
                               gpointer user_data, GDestroyNotify destroy,
                               GObject *weak_object)
{
    g_warning ("%s is not implemented yet", G_STRFUNC);
    return 0;
}

/**
 * mc_account_channel_request_cancel:
 * @account: the #McAccount.
 * @request_id: the ID of the request to be cancelled.
 *
 * Cancel the channel request identified by @request_id.
 */
void
mc_account_channel_request_cancel (McAccount *account, guint request_id)
{
    g_warning ("%s is not implemented yet", G_STRFUNC);
}

/**
 * mc_account_channel_request_get_error:
 * @account: the #McAccount.
 * @request_id: the ID of the channel request.
 *
 * Get the last error which occurred on the channel request identified by
 * @request_id.
 *
 * Returns: a #GError (not to be freed), or %NULL.
 */
const GError *
mc_account_channel_request_get_error (McAccount *account, guint request_id)
{
    g_warning ("%s is not implemented yet", G_STRFUNC);
    return NULL;
}

/**
 * mc_account_channel_request_get_path:
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
mc_account_channel_request_get_path (McAccount *account, guint request_id)
{
    g_warning ("%s is not implemented yet", G_STRFUNC);
    return NULL;
}

/**
 * mc_account_channel_request_get_from_path:
 * @account: the #McAccount.
 * @object_path: the D-Bus object path of a channel request.
 *
 * Finds the request ID whose D-Bus object path matches @object_path.
 * This only works if the request was created by this process.
 *
 * Returns: the unique ID of the channel request, or %0.
 */
guint
mc_account_channel_request_get_from_path (McAccount *account,
                                          const gchar *object_path)
{
    g_warning ("%s is not implemented yet", G_STRFUNC);
    return 0;
}

