/*
 * A demonstration plugin that acts as a channel filter.
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
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

#include "mcd-dispatcher-context.h"
#include "mcd-plugin.h"
#include "mcd-debug.h"

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

/* forward declaration to keep the compiler happy */
void mcd_plugin_init (McdPlugin *);

static void
reject_rickrolling (McdDispatcherContext *ctx,
                    gpointer user_data)
{
    McdChannel *channel = mcd_dispatcher_context_get_channel (ctx);
    const gchar *inviter = mcd_channel_get_inviter (channel);
    GQuark channel_type = mcd_channel_get_channel_type_quark (channel);
    const gchar *object_path = mcd_channel_get_object_path (channel);

    DEBUG ("called");

    /* we don't actually use the user_data here, so just assert that it's
     * passed to the callback correctly */
    g_assert (!tp_strdiff (user_data, "Never gonna give you up"));

    /* the McdChannel had better have a TpChannel, otherwise something is badly
     * wrong */
    g_assert (channel_type != 0);
    g_assert (object_path != NULL);

    if (!tp_strdiff (inviter, "rick.astley@example.com")
        && (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA ||
            channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_TEXT))
    {
        DEBUG ("rickrolling detected, closing channel %s", object_path);
        mcd_dispatcher_context_destroy_all (ctx);
        return;
    }

    mcd_dispatcher_context_proceed (ctx);
}

static void
reject_with_reason (McdDispatcherContext *ctx,
                    gpointer user_data)
{
    McdChannel *channel = mcd_dispatcher_context_get_channel (ctx);
    const gchar *inviter = mcd_channel_get_inviter (channel);
    GQuark channel_type = mcd_channel_get_channel_type_quark (channel);
    const gchar *object_path = mcd_channel_get_object_path (channel);

    DEBUG ("called");

    /* we don't actually use the user_data here, so just assert that it's
     * passed to the callback correctly */
    g_assert (!tp_strdiff (user_data, "Can't touch this"));

    /* the McdChannel had better have a TpChannel, otherwise something is badly
     * wrong */
    g_assert (channel_type != 0);
    g_assert (object_path != NULL);

    if (!tp_strdiff (inviter, "hammertime@example.com")
        && (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA ||
            channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_TEXT))
    {
        DEBUG ("MC Hammer detected, closing channel %s", object_path);
        mcd_dispatcher_context_close_all (ctx,
            TP_CHANNEL_GROUP_CHANGE_REASON_PERMISSION_DENIED,
            "Can't touch this");
        return;
    }

    mcd_dispatcher_context_proceed (ctx);
}

/* An older API for terminating unwanted channels */
static void
reject_mc_hammer (McdDispatcherContext *ctx,
                  gpointer user_data)
{
    McdChannel *channel = mcd_dispatcher_context_get_channel (ctx);
    const gchar *inviter = mcd_channel_get_inviter (channel);
    GQuark channel_type = mcd_channel_get_channel_type_quark (channel);
    const gchar *object_path = mcd_channel_get_object_path (channel);

    DEBUG ("called");

    /* we don't actually use the user_data here, so just assert that it's
     * passed to the callback correctly */
    g_assert (!tp_strdiff (user_data, "Stop! Hammer time"));

    /* the McdChannel had better have a TpChannel, otherwise something is badly
     * wrong */
    g_assert (channel_type != 0);
    g_assert (object_path != NULL);

    if (!tp_strdiff (inviter, "mc.hammer@example.com")
        && (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA ||
            channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_TEXT))
    {
        DEBUG ("MC Hammer detected, closing channel %s", object_path);
        mcd_dispatcher_context_process (ctx, FALSE);
        return;
    }

    mcd_dispatcher_context_process (ctx, TRUE);
}

static const McdFilter my_filters[] = {
      { reject_rickrolling, MCD_FILTER_PRIORITY_CRITICAL,
      "Never gonna give you up" },
      { reject_with_reason, MCD_FILTER_PRIORITY_CRITICAL,
      "Can't touch this" },
      { reject_mc_hammer, MCD_FILTER_PRIORITY_CRITICAL,
      "Stop! Hammer time" },
      { NULL }
};

void
mcd_plugin_init (McdPlugin *plugin)
{
  McdDispatcher *dispatcher = mcd_plugin_get_dispatcher (plugin);

  DEBUG ("Initializing test-plugin");

  mcd_dispatcher_add_filters (dispatcher, my_filters);
}
