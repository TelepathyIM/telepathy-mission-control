/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008 Nokia Corporation. 
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
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>
#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-requests.h"
#include "mcd-account-manager.h"
#include "mcd-misc.h"
#include "_gen/interfaces.h"

typedef struct
{
    gchar *requestor_client_id;
} McdRequestData;

#define REQUEST_DATA "request_data"

static inline McdRequestData *
get_request_data (McdChannel *channel)
{
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);
    return g_object_get_data ((GObject *)channel, REQUEST_DATA);
}

static void
online_request_cb (McdAccount *account, gpointer userdata, const GError *error)
{
    McdChannel *channel = MCD_CHANNEL (userdata);
    McdConnection *connection;

    if (error)
    {
        g_warning ("%s: got error: %s", G_STRFUNC, error->message);
        _mcd_channel_set_error (channel, g_error_copy (error));
        /* no unref here, as this will invoke our handler which will
         * unreference the channel */
        return;
    }
    g_debug ("%s called", G_STRFUNC);
    connection = mcd_account_get_connection (account);
    g_return_if_fail (connection != NULL);
    g_return_if_fail (mcd_connection_get_connection_status (connection)
                      == TP_CONNECTION_STATUS_CONNECTED);

    /* the connection will take ownership of the channel, so let's keep a
     * reference to it to make sure it's not destroyed while we are using it */
    g_object_ref (channel);
    mcd_connection_request_channel (connection, channel);
}

static void
request_data_free (McdRequestData *rd)
{
    g_free (rd->requestor_client_id);
    g_slice_free (McdRequestData, rd);
}

static void
on_channel_status_changed (McdChannel *channel, McdChannelStatus status,
                           McdAccount *account)
{
    const GError *error;

    if (status == MCD_CHANNEL_FAILED)
    {
        const gchar *err_string;
        error = _mcd_channel_get_error (channel);
        g_warning ("Channel request %s failed, error: %s",
                   _mcd_channel_get_request_path (channel), error->message);

        err_string = _mcd_get_error_string (error);
        mc_svc_account_interface_channelrequests_emit_failed (account,
            _mcd_channel_get_request_path (channel),
            err_string, error->message);

        g_object_unref (channel);
    }
    else if (status == MCD_CHANNEL_DISPATCHED)
    {
        mc_svc_account_interface_channelrequests_emit_succeeded (account,
            _mcd_channel_get_request_path (channel));

        /* free the request data, it's no longer useful */
        g_object_set_data ((GObject *)channel, REQUEST_DATA, NULL);

        g_object_unref (channel);
    }
}

static McdChannel *
create_request (McdAccount *account, GHashTable *properties,
                guint64 user_time, const gchar *preferred_handler,
                DBusGMethodInvocation *context, gboolean use_existing)
{
    McdChannel *channel;
    McdRequestData *rd;
    GError *error = NULL;
    GHashTable *props;
    McdDispatcher *dispatcher;

    /* We MUST deep-copy the hash-table, as we don't know how dbus-glib will
     * free it */
    props = _mcd_deepcopy_asv (properties);
    channel = mcd_channel_new_request (props, user_time,
                                       preferred_handler);
    g_hash_table_unref (props);
    _mcd_channel_set_request_use_existing (channel, use_existing);

    rd = g_slice_new (McdRequestData);
    rd->requestor_client_id = dbus_g_method_get_sender (context);
    g_object_set_data_full ((GObject *)channel, REQUEST_DATA, rd,
                            (GDestroyNotify)request_data_free);

    g_signal_connect (channel, "status-changed",
                      G_CALLBACK (on_channel_status_changed), account);

    dispatcher = mcd_master_get_dispatcher (mcd_master_get_default ());
    _mcd_dispatcher_add_request (dispatcher, account, channel);

    _mcd_account_online_request (account, online_request_cb, channel, &error);
    if (error)
    {
        g_warning ("%s: _mcd_account_online_request: %s", G_STRFUNC,
                   error->message);
        _mcd_channel_set_error (channel, error);
        /* no unref here, as this will invoke our handler which will
         * unreference the channel */
    }
    return channel;
}

const McdDBusProp account_channelrequests_properties[] = {
    { 0 },
};

static void
account_request_create (McSvcAccountInterfaceChannelRequests *self,
                        GHashTable *properties, guint64 user_time,
                        const gchar *preferred_handler,
                        DBusGMethodInvocation *context)
{
    GError *error = NULL;
    const gchar *request_id;
    McdChannel *channel;

    channel = create_request (MCD_ACCOUNT (self), properties, user_time,
                              preferred_handler, context, FALSE);
    if (error)
    {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }
    request_id = _mcd_channel_get_request_path (channel);
    mc_svc_account_interface_channelrequests_return_from_create (context,
                                                                 request_id);
}

static void
account_request_ensure_channel (McSvcAccountInterfaceChannelRequests *self,
                                GHashTable *properties, guint64 user_time,
                                const gchar *preferred_handler,
                                DBusGMethodInvocation *context)
{
    GError *error = NULL;
    const gchar *request_id;
    McdChannel *channel;

    channel = create_request (MCD_ACCOUNT (self), properties, user_time,
                              preferred_handler, context, TRUE);

    if (error)
    {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }
    request_id = _mcd_channel_get_request_path (channel);
    mc_svc_account_interface_channelrequests_return_from_ensure_channel
        (context, request_id);
}

static void
account_request_cancel (McSvcAccountInterfaceChannelRequests *self,
                        const gchar *request_id,
                        DBusGMethodInvocation *context)
{
    GError *error;

    error = g_error_new (TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
                         "%s is currently just a stub", G_STRFUNC);
    dbus_g_method_return_error (context, error);
    g_error_free (error);
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

