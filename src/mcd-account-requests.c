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
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>

#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

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

static void mcd_account_channel_request_disconnect (McdRequest *request);

static void
on_request_succeeded_with_channel (McdRequest *request,
    const gchar *conn_path,
    GHashTable *conn_props,
    const gchar *chan_path,
    GHashTable *chan_props,
    McdChannel *channel)
{
    mcd_account_channel_request_disconnect (request);
}

static void
on_request_failed (McdRequest *request,
    const gchar *err_string,
    const gchar *message,
    McdChannel *channel)
{
    g_warning ("Channel request %s failed, error: %s",
               _mcd_request_get_object_path (request), message);

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
