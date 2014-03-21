/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
 *
 * Contact: Naba Kumar  <naba.kumar@nokia.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-service.h"

TpDBusDaemon *bus_daemon = NULL;
static McdService *mcd = NULL;

static void
bus_closed (GDBusConnection *connection,
    gboolean remote_peer_vanished,
    GError *error,
    gpointer user_data)
{
  const gchar *which = user_data;

  if (error == NULL)
    g_message ("disconnected from the %s bus", which);
  else if (remote_peer_vanished)
    g_message ("%s bus vanished: %s #%d: %s", which,
        g_quark_to_string (error->domain), error->code, error->message);
  else
    g_message ("error communicating with %s bus: %s #%d: %s", which,
        g_quark_to_string (error->domain), error->code, error->message);

  g_dbus_connection_set_exit_on_close (connection, FALSE);
  mcd_mission_abort ((McdMission *) mcd);
}

static gboolean
the_end (gpointer data)
{
    g_main_loop_quit (data);

    return FALSE;
}

static void
on_abort (gpointer unused G_GNUC_UNUSED)
{
    g_debug ("McdService aborted, unreffing it");
    mcd_debug_print_tree (mcd);
    tp_clear_object (&mcd);
}

static gboolean
delayed_abort (gpointer data G_GNUC_UNUSED)
{
    g_message ("Aborting by popular request");
    mcd_mission_abort ((McdMission *) mcd);
    return FALSE;
}

static gboolean
billy_idle (gpointer user_data)
{
  DBusMessage *reply = user_data;
  DBusConnection *connection = dbus_g_connection_get_connection (
      tp_proxy_get_dbus_connection (bus_daemon));

  if (!dbus_connection_send (connection, reply, NULL))
    g_error ("Out of memory");

  return FALSE;
}

#define MCD_SYSTEM_MEMORY_CONSERVED (1 << 1)
#define MCD_SYSTEM_IDLE (1 << 5)

static DBusHandlerResult
dbus_filter_function (DBusConnection *connection,
                      DBusMessage *message,
                      void *user_data)
{
  if (dbus_message_is_method_call (message,
        "im.telepathy.v1.MissionControl6.RegressionTests",
        "Abort"))
    {
      DBusMessage *reply;

      g_idle_add (delayed_abort, NULL);

      reply = dbus_message_new_method_return (message);

      if (reply == NULL || !dbus_connection_send (connection, reply, NULL))
        g_error ("Out of memory");

      dbus_message_unref (reply);

      return DBUS_HANDLER_RESULT_HANDLED;
    }
  else if (dbus_message_is_method_call (message,
        "im.telepathy.v1.MissionControl6.RegressionTests",
        "BillyIdle"))
    {
      /* Used to drive a souped-up version of sync_dbus(), where we need to
       * ensure that all idles have fired, on top of the D-Bus queue being
       * drained.
       */
      DBusMessage *reply = dbus_message_new_method_return (message);
      GVariant *variant;
      GDBusConnection *system_bus;

      if (reply == NULL)
        g_error ("Out of memory");

      /* Sync GDBus, too, to make sure we have received any pending
       * FakeNetworkMonitor messages. */
      system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
      g_assert (system_bus != NULL);
      variant = g_dbus_connection_call_sync (system_bus,
          "org.freedesktop.DBus", "/org/freedesktop/DBus",
          "org.freedesktop.DBus", "ListNames",
          NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
      g_assert (variant != NULL);
      g_variant_unref (variant);
      g_object_unref (system_bus);

      g_idle_add_full (G_PRIORITY_LOW, billy_idle, reply,
          (GDestroyNotify) dbus_message_unref);

      return DBUS_HANDLER_RESULT_HANDLED;
    }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int
main (int argc, char **argv)
{
    GError *error = NULL;
    GDBusConnection *gdbus = NULL;
    GDBusConnection *gdbus_system = NULL;
    DBusConnection *connection = NULL;
    int ret = 1;
    GMainLoop *teardown_loop;
    guint linger_time = 5;

    g_type_init ();

    g_set_application_name ("Mission Control regression tests");

    mcd_debug_init ();
    tp_debug_set_flags (g_getenv ("MC_TP_DEBUG"));

    /* Not all warnings are fatal due to MC spamming warnings (fd.o #23486),
     * but GLib and GObject warnings are pretty serious */
    g_log_set_fatal_mask ("GLib",
        G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);
    g_log_set_fatal_mask ("GLib-GObject",
        G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

    gdbus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    g_assert_no_error (error);
    g_assert (gdbus != NULL);
    g_dbus_connection_set_exit_on_close (gdbus, FALSE);
    g_signal_connect (gdbus, "closed", G_CALLBACK (bus_closed), "session");

    gdbus_system = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    g_assert_no_error (error);
    g_assert (gdbus_system != NULL);
    g_dbus_connection_set_exit_on_close (gdbus_system, FALSE);
    g_signal_connect (system_bus, "closed", G_CALLBACK (bus_closed), "system");

    bus_daemon = tp_dbus_daemon_dup (&error);
    g_assert_no_error (error);
    g_assert (bus_daemon != NULL);

    /* It appears that dbus-glib registers a filter that wrongly returns
     * DBUS_HANDLER_RESULT_HANDLED for signals, so for *our* filter to have any
     * effect, we need to install it as soon as possible */
    connection = dbus_g_connection_get_connection (
	tp_proxy_get_dbus_connection (bus_daemon));
    dbus_connection_add_filter (connection, dbus_filter_function, NULL, NULL);

    mcd = mcd_service_new ();

    /* Listen for suicide notification */
    g_signal_connect_after (mcd, "abort", G_CALLBACK (on_abort), NULL);

    /* connect */
    mcd_mission_connect (MCD_MISSION (mcd));

    dbus_connection_set_exit_on_disconnect (connection, FALSE);

    mcd_service_run (MCD_OBJECT (mcd));

    ret = 0;

    teardown_loop = g_main_loop_new (NULL, FALSE);

    if (g_getenv ("MC_LINGER_TIME") != NULL)
      {
        linger_time = g_ascii_strtoull (g_getenv ("MC_LINGER_TIME"), NULL, 10);
      }

    /* Keep running in the background until it's all over. This means valgrind
     * and refdbg can get complete information. */
    g_timeout_add_seconds_full (G_PRIORITY_DEFAULT, linger_time, the_end,
        teardown_loop, (GDestroyNotify) g_main_loop_unref);

    g_main_loop_run (teardown_loop);

    if (connection != NULL)
    {
        dbus_connection_flush (connection);
    }

    tp_clear_object (&gdbus);
    tp_clear_object (&gdbus_system);
    tp_clear_object (&bus_daemon);

    dbus_shutdown ();

    g_message ("Exiting with %d", ret);

    return ret;
}
