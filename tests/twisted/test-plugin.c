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

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

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

static void
permission_cb (DBusPendingCall *pc,
    gpointer data)
{
    McdDispatcherContext *ctx = data;
    DBusMessage *message = dbus_pending_call_steal_reply (pc);

    if (dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_ERROR)
    {
        DEBUG ("Permission denied for %p", ctx);
        mcd_dispatcher_context_close_all (ctx,
            TP_CHANNEL_GROUP_CHANGE_REASON_PERMISSION_DENIED,
            "Computer says no");
    }
    else
    {
        DEBUG ("Permission granted for %p", ctx);
    }

    dbus_message_unref (message);
    dbus_pending_call_unref (pc);
}

static void
ask_for_permission (McdDispatcherContext *ctx, gpointer user_data)
{
    McdChannel *channel = mcd_dispatcher_context_get_channel (ctx);

    DEBUG ("%p", ctx);

    if (!tp_strdiff (mcd_channel_get_name (channel), "policy@example.com"))
    {
        TpDBusDaemon *dbus_daemon = tp_dbus_daemon_dup (NULL);
        DBusGConnection *gconn = tp_proxy_get_dbus_connection (dbus_daemon);
        DBusConnection *libdbus = dbus_g_connection_get_connection (gconn);
        DBusPendingCall *pc = NULL;
        DBusMessage *message;

        /* in a real policy-mechanism you'd give some details, like the
         * channel's properties or object path */
        message = dbus_message_new_method_call ("com.example.Policy",
            "/com/example/Policy", "com.example.Policy", "RequestPermission");

        if (!dbus_connection_send_with_reply (libdbus, message,
              &pc, -1))
            g_error ("out of memory");

        dbus_message_unref (message);

        if (pc == NULL)
        {
            DEBUG ("got disconnected from D-Bus...");
            goto proceed;
        }

        /* pc is unreffed by permission_cb */

        DEBUG ("Waiting for permission for %p", ctx);

        if (dbus_pending_call_get_completed (pc))
        {
            permission_cb (pc, ctx);
            goto proceed;
        }

        if (!dbus_pending_call_set_notify (pc, permission_cb, ctx,
              (DBusFreeFunction) mcd_dispatcher_context_proceed))
            g_error ("Out of memory");

        return;
    }

proceed:
    mcd_dispatcher_context_proceed (ctx);
}

static const McdFilter my_filters[] = {
      { reject_rickrolling, MCD_FILTER_PRIORITY_CRITICAL,
      "Never gonna give you up" },
      { reject_with_reason, MCD_FILTER_PRIORITY_CRITICAL,
      "Can't touch this" },
      { reject_mc_hammer, MCD_FILTER_PRIORITY_CRITICAL,
      "Stop! Hammer time" },
      { ask_for_permission, MCD_FILTER_PRIORITY_SYSTEM, "May I?" },
      { NULL }
};

void
mcd_plugin_init (McdPlugin *plugin)
{
  McdDispatcher *dispatcher = mcd_plugin_get_dispatcher (plugin);

  DEBUG ("Initializing test-plugin");

  mcd_dispatcher_add_filters (dispatcher, my_filters);
}
