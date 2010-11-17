/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright © 2008-2009 Nokia Corporation.
 * Copyright © 2009-2010 Collabora Ltd.
 *
 * Contact: Alberto Mardegan  <alberto.mardegan@nokia.com>
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

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <config.h>

#include <dbus/dbus-glib-lowlevel.h>

#include <libmcclient/mc-errors.h>
#include <libmcclient/mc-interfaces.h>

#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/svc-channel-request.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>

#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-manager.h"
#include "mcd-dispatcher-priv.h"
#include "mcd-channel-priv.h"
#include "mcd-misc.h"
#include "request.h"

static void
online_request_cb (McdAccount *account, gpointer userdata, const GError *error)
{
    McdChannel *channel = MCD_CHANNEL (userdata);
    McdConnection *connection;

    if (error)
    {
        g_warning ("got error: %s", error->message);
        mcd_channel_take_error (channel, g_error_copy (error));
        g_object_unref (channel);
        return;
    }
    DEBUG ("called");
    connection = mcd_account_get_connection (account);
    g_return_if_fail (connection != NULL);
    g_return_if_fail (mcd_account_get_connection_status (account)
                      == TP_CONNECTION_STATUS_CONNECTED);

    if (mcd_channel_get_status (channel) == MCD_CHANNEL_STATUS_FAILED)
    {
        DEBUG ("channel %p is failed", channel);
        g_object_unref (channel);
        return;
    }

    /* the connection will take ownership of the channel if and only if it
     * has no parent; we expect it to have no parent, and the connection will
     * become its parent */
    g_assert (mcd_mission_get_parent ((McdMission *) channel) == NULL);
    mcd_connection_request_channel (connection, channel);
}

static McdChannel *
get_channel_from_request (McdAccount *account, const gchar *request_id)
{
    McdConnection *connection;
    const GList *channels, *list;


    connection = mcd_account_get_connection (account);
    if (connection)
    {
        channels = mcd_operation_get_missions (MCD_OPERATION (connection));
        for (list = channels; list != NULL; list = list->next)
        {
            McdChannel *channel = MCD_CHANNEL (list->data);
            McdRequest *request = _mcd_channel_get_request (channel);

            if (request != NULL &&
                !tp_strdiff (_mcd_request_get_object_path (request),
                             request_id))
                return channel;
        }
    }

    /* if we don't have a connection in connected state yet, the channel might
     * be in the online requests queue */
    list = _mcd_account_get_online_requests (account);
    while (list)
    {
        McdOnlineRequestData *data = list->data;

        if (data->callback == online_request_cb)
        {
            McdChannel *channel = MCD_CHANNEL (data->user_data);
            McdRequest *request = _mcd_channel_get_request (channel);

            if (request != NULL &&
                !tp_strdiff (_mcd_request_get_object_path (request),
                             request_id))
                return channel;
        }

        list = list->next;
    }
    return NULL;
}

static void mcd_account_channel_request_disconnect (McdRequest *request);

static void
on_request_succeeded_with_channel (McdRequest *request,
    const gchar *conn_path,
    GHashTable *conn_props,
    const gchar *chan_path,
    GHashTable *chan_props,
    McdChannel *channel)
{
    McdAccount *account = _mcd_request_get_account (request);

    /* Backwards-compatible version for the old API */
    mc_svc_account_interface_channelrequests_emit_succeeded (account,
        _mcd_request_get_object_path (request));

    mcd_account_channel_request_disconnect (request);
}

static void
on_request_failed (McdRequest *request,
    const gchar *err_string,
    const gchar *message,
    McdChannel *channel)
{
    McdAccount *account = _mcd_request_get_account (request);

    g_warning ("Channel request %s failed, error: %s",
               _mcd_request_get_object_path (request), message);

    /* Backwards-compatible version for the old API */
    mc_svc_account_interface_channelrequests_emit_failed (account,
        _mcd_request_get_object_path (request), err_string, message);

    mcd_account_channel_request_disconnect (request);
}

static void ready_to_request_cb (McdRequest *request, McdChannel *channel);

static void
mcd_account_channel_request_disconnect (McdRequest *request)
{
    g_signal_handlers_disconnect_matched (request, G_SIGNAL_MATCH_FUNC,
                                          0,        /* signal_id ignored */
                                          0,        /* detail ignored */
                                          NULL,     /* closure ignored */
                                          on_request_failed,
                                          NULL      /* user data ignored */);
    g_signal_handlers_disconnect_matched (request, G_SIGNAL_MATCH_FUNC,
                                          0,        /* signal_id ignored */
                                          0,        /* detail ignored */
                                          NULL,     /* closure ignored */
                                          on_request_succeeded_with_channel,
                                          NULL      /* user data ignored */);
    g_signal_handlers_disconnect_matched (request, G_SIGNAL_MATCH_FUNC,
                                          0,        /* signal_id ignored */
                                          0,        /* detail ignored */
                                          NULL,     /* closure ignored */
                                          ready_to_request_cb,
                                          NULL      /* user data ignored */);
}

McdChannel *
_mcd_account_create_request (McdClientRegistry *clients,
                             McdAccount *account, GHashTable *properties,
                             gint64 user_time, const gchar *preferred_handler,
                             GHashTable *hints, gboolean use_existing,
                             McdRequest **request_out, GError **error)
{
    McdChannel *channel;
    GHashTable *props;
    McdRequest *request;

    if (!mcd_account_check_request (account, properties, error))
    {
        return NULL;
    }

    /* We MUST deep-copy the hash-table, as we don't know how dbus-glib will
     * free it */
    props = _mcd_deepcopy_asv (properties);
    request = _mcd_request_new (clients, use_existing, account, props,
                                user_time, preferred_handler, hints);
    g_assert (request != NULL);
    g_hash_table_unref (props);

    channel = _mcd_channel_new_request (request);

    /* FIXME: this isn't ideal - if the account is deleted, Proceed will fail,
     * whereas what we want to happen is that Proceed will succeed but
     * immediately cause a failure to be signalled. It'll do for now though. */

    /* This can't actually be emitted until Proceed() is called; it'll always
     * come before succeeded-with-channel or failed */
    g_signal_connect_data (request,
                           "ready-to-request",
                           G_CALLBACK (ready_to_request_cb),
                           g_object_ref (channel),
                           (GClosureNotify) g_object_unref,
                           0);

    /* we use connect_after, to make sure that other signals (such as
     * RemoveRequest) are emitted before the Failed signal */
    g_signal_connect_data (request,
                           "succeeded-with-channel",
                           G_CALLBACK (on_request_succeeded_with_channel),
                           g_object_ref (channel),
                           (GClosureNotify) g_object_unref,
                           G_CONNECT_AFTER);
    g_signal_connect_data (request,
                           "failed",
                           G_CALLBACK (on_request_failed),
                           g_object_ref (channel),
                           (GClosureNotify) g_object_unref,
                           G_CONNECT_AFTER);

    if (request_out != NULL)
    {
        *request_out = g_object_ref (request);
    }

    return channel;
}

const McdDBusProp account_channelrequests_properties[] = {
    { 0 },
};

static void
ready_to_request_cb (McdRequest *request,
                     McdChannel *channel)
{
    GError *error = _mcd_request_dup_failure (request);

    /* if we didn't ref the channel, disconnecting the signal could
     * destroy it */
    g_object_ref (channel);
    g_signal_handlers_disconnect_by_func (request, ready_to_request_cb,
                                          channel);

    if (error != NULL)
    {
        g_message ("request denied by plugin: %s", error->message);
        mcd_channel_take_error (channel, error);
    }
    else
    {
        DEBUG ("Starting online request");
        /* Put the account online if necessary, and when that's finished,
         * make the actual request. (The callback releases this reference.) */
        _mcd_account_online_request (_mcd_request_get_account (request),
                                     online_request_cb,
                                     g_object_ref (channel));
    }

    g_object_unref (channel);
}

static void
account_request_common (McdAccount *account, GHashTable *properties,
                        gint64 user_time, const gchar *preferred_handler,
                        DBusGMethodInvocation *context, gboolean use_existing)
{
    GError *error = NULL;
    const gchar *request_id;
    McdChannel *channel;
    McdDispatcher *dispatcher;
    McdClientRegistry *clients;
    McdRequest *request = NULL;

    dispatcher = mcd_master_get_dispatcher (mcd_master_get_default ());
    clients = _mcd_dispatcher_get_client_registry (dispatcher);

    channel = _mcd_account_create_request (clients,
                                           account, properties, user_time,
                                           preferred_handler, NULL,
                                           use_existing,
                                           &request, &error);

    if (error)
    {
        g_assert (channel == NULL);
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    g_assert (request != NULL);

    request_id = _mcd_request_get_object_path (request);
    DEBUG ("returning %s", request_id);
    if (use_existing)
        mc_svc_account_interface_channelrequests_return_from_ensure_channel
            (context, request_id);
    else
        mc_svc_account_interface_channelrequests_return_from_create
            (context, request_id);

    _mcd_request_predict_handler (request);

    /* we only just created the request, so Proceed() shouldn't fail */
    _mcd_request_proceed (request, NULL);

    /* we still have refs returned by _mcd_account_create_request(), which
     * are no longer necessary at this point */
    g_object_unref (request);
    g_object_unref (channel);
}

static void
account_request_create (McSvcAccountInterfaceChannelRequests *self,
                        GHashTable *properties, guint64 user_time,
                        const gchar *preferred_handler,
                        DBusGMethodInvocation *context)
{
    account_request_common (MCD_ACCOUNT (self), properties, user_time,
                            preferred_handler, context, FALSE);
}

static void
account_request_ensure_channel (McSvcAccountInterfaceChannelRequests *self,
                                GHashTable *properties, guint64 user_time,
                                const gchar *preferred_handler,
                                DBusGMethodInvocation *context)
{
    account_request_common (MCD_ACCOUNT (self), properties, user_time,
                            preferred_handler, context, TRUE);
}

static void
account_request_cancel (McSvcAccountInterfaceChannelRequests *self,
                        const gchar *request_id,
                        DBusGMethodInvocation *context)
{
    GError *error = NULL;
    McdChannel *channel;

    DEBUG ("called for %s", request_id);
    g_return_if_fail (request_id != NULL);
    channel = get_channel_from_request (MCD_ACCOUNT (self), request_id);
    if (!channel)
    {
        error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                             "Request %s not found", request_id);
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    if (!_mcd_request_cancel (_mcd_channel_get_request (channel), &error))
    {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    mc_svc_account_interface_channelrequests_return_from_cancel (context);
}

void
account_channelrequests_iface_init (McSvcAccountInterfaceChannelRequestsClass *iface,
                                    gpointer iface_data)
{
#define IMPLEMENT(x) mc_svc_account_interface_channelrequests_implement_##x (\
    iface, account_request_##x)
    IMPLEMENT(create);
    IMPLEMENT(ensure_channel);
    IMPLEMENT(cancel);
#undef IMPLEMENT
}

gboolean
mcd_account_check_request (McdAccount *account, GHashTable *request,
                           GError **error)
{
    gboolean (*impl) (McdAccount *account, GHashTable *request,
                      GError **error);

    g_return_val_if_fail (MCD_IS_ACCOUNT (account), FALSE);
    g_return_val_if_fail (request != NULL, FALSE);

    impl = MCD_ACCOUNT_GET_CLASS (account)->check_request;

    if (impl == NULL)
        return TRUE;

    return impl (account, request, error);
}

/* Default implementation of check_request */
gboolean
_mcd_account_check_request_real (McdAccount *account, GHashTable *request,
                                 GError **error)
{
    return TRUE;
}
