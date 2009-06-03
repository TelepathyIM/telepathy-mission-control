/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * mc-account-stats.c - Telepathy Account D-Bus interface (client side)
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
#include "mc-signals-marshal.h"

struct _McAccountStatsProps {
    GHashTable *channel_count;
};

static void create_props (TpProxy *proxy, GHashTable *props);
static void setup_props_monitor (TpProxy *proxy, GQuark interface);

static McIfaceDescription iface_description = {
    G_STRUCT_OFFSET (McAccountPrivate, stats_props),
    create_props,
    setup_props_monitor,
};


void
_mc_account_stats_props_free (McAccountStatsProps *props)
{
    g_hash_table_unref (props->channel_count);
    g_slice_free (McAccountStatsProps, props);
}

static void
channel_count_changed (McAccount *account, GHashTable *channel_count)
{
    McAccountStatsProps *props = account->priv->stats_props;

    if (props->channel_count)
        g_hash_table_unref (props->channel_count);
    props->channel_count = channel_count;
}

void
_mc_account_stats_class_init (McAccountClass *klass)
{
    klass->stats_channel_count_changed = channel_count_changed;

    _mc_iface_add (MC_TYPE_ACCOUNT,
		   MC_IFACE_QUARK_ACCOUNT_INTERFACE_STATS,
		   &iface_description);

    /**
     * McAccount::channel-count-changed:
     * @account: the #McAccount.
     * @channel_count: a #GHashTable with the new channel counters.
     *
     * Emitted when the stats changes.
     * The McAccount member data are updated in the signal closure, so use
     * g_signal_connect_after() if you need them to reflect the new status.
     */
    _mc_account_signals[CHANNEL_COUNT_CHANGED] =
        g_signal_new ("channel-count-changed",
                      G_OBJECT_CLASS_TYPE (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (McAccountClass,
                                       stats_channel_count_changed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__BOXED,
                      G_TYPE_NONE,
                      1, G_TYPE_HASH_TABLE);
}

static void
update_channel_count (const gchar *name, const GValue *value,
                      gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountStatsProps *props = account->priv->stats_props;
    GHashTable *channel_count;

    channel_count = g_value_dup_boxed (value);
    if (props->channel_count)
        /* the signal closure will update the props->channel_count */
        g_signal_emit (account, _mc_account_signals[CHANNEL_COUNT_CHANGED],
                       0, channel_count);
    else
        props->channel_count = channel_count;
}

static const McIfaceProperty account_stats_properties[] =
{
    { "ChannelCount", "a{su}", update_channel_count },
    { NULL, NULL, NULL }
};

static void
create_props (TpProxy *proxy, GHashTable *props)
{
    McAccount *account = MC_ACCOUNT (proxy);
    McAccountPrivate *priv = account->priv;

    priv->stats_props = g_slice_new0 (McAccountStatsProps);
    _mc_iface_update_props (account_stats_properties, props, account);
}

static void
on_stats_changed (TpProxy *proxy, GHashTable *properties, gpointer user_data,
                  GObject *weak_object)
{
    McAccount *account = MC_ACCOUNT (proxy);
    McAccountPrivate *priv = account->priv;

    /* if the GetAll method hasn't returned yet, we do nothing */
    if (G_UNLIKELY (!priv->stats_props)) return;

    _mc_iface_update_props (account_stats_properties, properties,
                            account);
}

static void
setup_props_monitor (TpProxy *proxy, GQuark interface)
{
    McAccount *account = MC_ACCOUNT (proxy);

    mc_cli_account_interface_stats_connect_to_stats_changed (account,
                                                             on_stats_changed,
                                                             NULL, NULL,
                                                             NULL, NULL);
}

/**
 * mc_account_stats_get_channel_count:
 * @account: the #McAccount.
 *
 * Retrieves the number of account active channels, by channel type. This also
 * includes channel requests.
 *
 * Returns: a #GHashTable which maps channel types to their usage count (use
 * GPOINTER_TO_UINT() to convert the values into integers).
 */
GHashTable *
mc_account_stats_get_channel_count (McAccount *account)
{
    McAccountStatsProps *props;

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    props = account->priv->stats_props;
    if (G_UNLIKELY (!props))
        return NULL;

    return props->channel_count;
}

