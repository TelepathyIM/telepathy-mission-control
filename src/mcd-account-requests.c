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
#include "plugin-loader.h"
#include "plugin-request.h"
#include "request.h"

#include "_gen/svc-Channel_Request_Future.h"

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

            if (g_strcmp0 (_mcd_channel_get_request_path (channel),
                           request_id) == 0)
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

            if (g_strcmp0 (_mcd_channel_get_request_path (channel),
                           request_id) == 0)
                return channel;
        }

        list = list->next;
    }
    return NULL;
}

static void
on_request_completed (McdRequest *request,
                      gboolean successful,
                      McdChannel *channel)
{
    McdAccount *account = _mcd_request_get_account (request);

    if (!successful)
    {
        GError *error = _mcd_request_dup_failure (request);
        gchar *err_string;

        g_warning ("Channel request %s failed, error: %s",
                   _mcd_channel_get_request_path (channel), error->message);

        err_string = _mcd_build_error_string (error);
        /* FIXME: ideally the McdRequest should emit this signal itself, and
         * the Account.Interface.ChannelRequests should catch and re-emit it */
        tp_svc_channel_request_emit_failed (channel, err_string,
                                            error->message);
        mc_svc_account_interface_channelrequests_emit_failed (account,
            _mcd_channel_get_request_path (channel),
            err_string, error->message);
        g_free (err_string);

        g_error_free (error);
    }
    else
    {
        /* FIXME: ideally the McdRequest should emit this signal itself, and
         * the Account.Interface.ChannelRequests should catch and re-emit it */
        TpChannel *tp_chan;
        TpConnection *tp_conn;

        /* SucceededWithChannel has to be fired first */
        tp_chan = mcd_channel_get_tp_channel (channel);
        g_assert (tp_chan != NULL);

        tp_conn = tp_channel_borrow_connection (tp_chan);
        g_assert (tp_conn != NULL);

        mc_svc_channel_request_future_emit_succeeded_with_channel (channel,
            tp_proxy_get_object_path (tp_conn),
            tp_proxy_get_object_path (tp_chan));

        tp_svc_channel_request_emit_succeeded (channel);
        mc_svc_account_interface_channelrequests_emit_succeeded (account,
            _mcd_channel_get_request_path (channel));
    }

    g_signal_handlers_disconnect_by_func (request, on_request_completed,
                                          channel);
}

McdChannel *
_mcd_account_create_request (McdAccount *account, GHashTable *properties,
                             gint64 user_time, const gchar *preferred_handler,
                             GHashTable *request_metadata,
                             gboolean use_existing, gboolean proceeding,
                             GError **error)
{
    McdChannel *channel;
    GHashTable *props;
    TpDBusDaemon *dbus_daemon = mcd_account_get_dbus_daemon (account);

    DBusGConnection *dgc = tp_proxy_get_dbus_connection (dbus_daemon);

    if (!mcd_account_check_request (account, properties, error))
    {
        return NULL;
    }

    /* We MUST deep-copy the hash-table, as we don't know how dbus-glib will
     * free it */
    props = _mcd_deepcopy_asv (properties);
    channel = mcd_channel_new_request (account, dgc, props, user_time,
                                       preferred_handler, request_metadata, use_existing,
                                       proceeding);
    g_hash_table_unref (props);

    /* FIXME: this isn't ideal - if the account is deleted, Proceed will fail,
     * whereas what we want to happen is that Proceed will succeed but
     * immediately cause a failure to be signalled. It'll do for now though. */

    /* we use connect_after, to make sure that other signals (such as
     * RemoveRequest) are emitted before the Failed signal */
    g_signal_connect_data (_mcd_channel_get_request (channel),
                           "completed",
                            G_CALLBACK (on_request_completed),
                            g_object_ref (channel),
                            (GClosureNotify) g_object_unref,
                            G_CONNECT_AFTER);

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

void
_mcd_account_proceed_with_request (McdAccount *account,
                                   McdChannel *channel)
{
    McdPluginRequest *plugin_api = NULL;
    const GList *mini_plugins;

    g_object_ref (channel);

    for (mini_plugins = mcp_list_objects ();
         mini_plugins != NULL;
         mini_plugins = mini_plugins->next)
    {
        if (MCP_IS_REQUEST_POLICY (mini_plugins->data))
        {
            DEBUG ("Checking request with policy");

            /* Lazily create a plugin-API object if anything cares */
            if (plugin_api == NULL)
            {
                plugin_api = _mcd_plugin_request_new (account,
                    _mcd_channel_get_request (channel));
            }

            mcp_request_policy_check (mini_plugins->data,
                                      MCP_REQUEST (plugin_api));
        }
    }

    g_signal_connect_data (_mcd_channel_get_request (channel),
                           "ready-to-request",
                           G_CALLBACK (ready_to_request_cb),
                           g_object_ref (channel),
                           (GClosureNotify) g_object_unref,
                           0);

    /* this is paired with the delay set when the request was created */
    _mcd_request_end_delay (_mcd_channel_get_request (channel));

    tp_clear_object (&plugin_api);
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

    channel = _mcd_account_create_request (account, properties, user_time,
                                           preferred_handler, NULL, use_existing,
                                           TRUE /* proceeding */, &error);

    if (error)
    {
        g_assert (channel == NULL);
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    _mcd_account_proceed_with_request (account, channel);

    request_id = _mcd_channel_get_request_path (channel);
    DEBUG ("returning %s", request_id);
    if (use_existing)
        mc_svc_account_interface_channelrequests_return_from_ensure_channel
            (context, request_id);
    else
        mc_svc_account_interface_channelrequests_return_from_create
            (context, request_id);

    dispatcher = mcd_master_get_dispatcher (mcd_master_get_default ());
    _mcd_dispatcher_add_request (dispatcher, account, channel);

    /* we still have a ref returned by _mcd_account_create_request(), which
     * is no longer necessary at this point */
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

    if (!_mcd_channel_request_cancel (channel, &error))
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
