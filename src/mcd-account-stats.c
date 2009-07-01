/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

#include <string.h>
#include <glib.h>
#include <config.h>

#include <libmcclient/mc-gtypes.h>
#include <libmcclient/mc-interfaces.h>

#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-manager.h"
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>

static GHashTable *
mcd_account_get_channel_count (McdAccount *account)
{
    GHashTable *stats;
    McdConnection *connection;

    stats = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    connection = mcd_account_get_connection (account);
    if (connection)
    {
        const GList *channels;

        channels = mcd_operation_get_missions (MCD_OPERATION (connection));
        for (; channels != NULL; channels = channels->next)
        {
            McdChannel *channel;
            const gchar *channel_type;
            guint count;

            channel = MCD_CHANNEL (channels->data);
            channel_type = mcd_channel_get_channel_type (channel);
            if (G_UNLIKELY (!channel_type)) continue;

            count = GPOINTER_TO_UINT (g_hash_table_lookup (stats,
                                                           channel_type));
            count++;
            g_hash_table_insert (stats, g_strdup (channel_type),
                                 GUINT_TO_POINTER (count));
        }
    }

    return stats;
}

static void
get_channel_count (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    GHashTable *stats;

    stats = mcd_account_get_channel_count (MCD_ACCOUNT (self));

    g_value_init (value, MC_HASH_TYPE_CHANNEL_COUNT_MAP);
    g_value_take_boxed (value, stats);
}


const McdDBusProp account_stats_properties[] = {
    { "ChannelCount", NULL, get_channel_count },
    { 0 },
};

void
account_stats_iface_init (McSvcAccountInterfaceStatsClass *iface,
                          gpointer iface_data)
{
}

static void
on_channel_count_changed (McdConnection *connection, McdChannel *channel,
                          McdAccount *account)
{
    GHashTable *stats, *properties;
    GValue value = { 0 };

    stats = mcd_account_get_channel_count (account);

    g_value_init (&value, MC_HASH_TYPE_CHANNEL_COUNT_MAP);
    g_value_take_boxed (&value, stats);

    properties = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_insert (properties, (gpointer)"ChannelCount", &value);

    mc_svc_account_interface_stats_emit_stats_changed (account, properties);

    g_hash_table_destroy (properties);
    g_value_unset (&value);
}

static void
watch_connection (McdAccount *account)
{
    McdConnection *connection;

    connection = mcd_account_get_connection (account);
    if (G_UNLIKELY (!connection)) return;

    g_signal_connect (connection, "mission-taken",
                      G_CALLBACK (on_channel_count_changed), account);
    g_signal_connect (connection, "mission-removed",
                      G_CALLBACK (on_channel_count_changed), account);
}

static void
on_account_connection_status_changed (McdAccount *account,
                                      TpConnectionStatus status,
                                      TpConnectionStatusReason reason)
{
    if (status == TP_CONNECTION_STATUS_CONNECTED)
        watch_connection (account);
}

void
account_stats_instance_init (TpSvcDBusProperties *self)
{
    McdAccount *account = MCD_ACCOUNT (self);

    if (mcd_account_get_connection_status (account) ==
        TP_CONNECTION_STATUS_CONNECTED)
    {
        watch_connection (account);
    }

    g_signal_connect (account, "connection-status-changed",
                      G_CALLBACK (on_account_connection_status_changed), NULL);
}

