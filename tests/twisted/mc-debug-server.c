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
  GDBusMethodInvocation *invocation = user_data;

  g_dbus_method_invocation_return_value (invocation, NULL);
  return FALSE;
}

static GDBusMethodInfo test_interface_abort = {
    -1, /* no refcount */
    "Abort",
    NULL, /* no in args */
    NULL, /* no out args */
    NULL /* no annotations */
};

static GDBusMethodInfo test_interface_billy_idle = {
    -1, /* no refcount */
    "BillyIdle",
    NULL, /* no in args */
    NULL, /* no out args */
    NULL /* no annotations */
};

static GDBusMethodInfo *test_interface_method_pointers[] = {
    &test_interface_abort,
    &test_interface_billy_idle,
    NULL
};

static GDBusInterfaceInfo test_interface = {
    -1, /* no refcount */
    "im.telepathy.v1.MissionControl6.RegressionTests",
    test_interface_method_pointers,
    NULL, /* signals */
    NULL, /* properties */
    NULL /* annotations */
};

static void
test_interface_method_call (GDBusConnection *connection G_GNUC_UNUSED,
    const gchar *sender G_GNUC_UNUSED,
    const gchar *object_path G_GNUC_UNUSED,
    const gchar *interface_name G_GNUC_UNUSED,
    const gchar *method_name,
    GVariant *parameters G_GNUC_UNUSED,
    GDBusMethodInvocation *invocation,
    gpointer user_data G_GNUC_UNUSED)
{
  if (!tp_strdiff (method_name, test_interface_abort.name))
    {
      g_idle_add (delayed_abort, NULL);
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
  else if (!tp_strdiff (method_name, test_interface_billy_idle.name))
    {
      /* Used to drive a souped-up version of sync_dbus(), where we need to
       * ensure that all idles have fired, on top of the D-Bus queue being
       * drained.
       */
      GDBusConnection *system_bus;
      GVariant *variant;

      /* Sync the system bus, too, to make sure we have received any pending
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

      g_idle_add_full (G_PRIORITY_LOW, billy_idle, invocation, NULL);
    }
  else
    {
      g_assert_not_reached ();
    }
}

static const GDBusInterfaceVTable test_interface_vtable = {
    test_interface_method_call,
    NULL, /* get property */
    NULL /* set property */
};

int
main (int argc, char **argv)
{
    GError *error = NULL;
    GDBusConnection *gdbus = NULL;
    GDBusConnection *gdbus_system = NULL;
    int ret = 1;
    GMainLoop *teardown_loop;
    guint linger_time = 5;
    guint test_interface_id = 0;
    TpDebugSender *debug_sender = NULL;

    g_type_init ();

    g_set_application_name ("Mission Control regression tests");

    debug_sender = tp_debug_sender_dup ();

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
    g_signal_connect (gdbus_system, "closed", G_CALLBACK (bus_closed),
        "system");

    test_interface_id = g_dbus_connection_register_object (gdbus,
        TP_ACCOUNT_MANAGER_OBJECT_PATH, &test_interface,
        &test_interface_vtable, NULL, NULL, &error);
    g_assert_no_error (error);

    mcd = mcd_service_new ();

    /* Listen for suicide notification */
    g_signal_connect_after (mcd, "abort", G_CALLBACK (on_abort), NULL);

    /* connect */
    mcd_mission_connect (MCD_MISSION (mcd));

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

    if (gdbus != NULL)
        g_dbus_connection_flush_sync (gdbus, NULL, NULL);

    if (test_interface_id != 0)
        g_dbus_connection_unregister_object (gdbus, test_interface_id);

    tp_clear_object (&gdbus);
    tp_clear_object (&gdbus_system);

    g_message ("Exiting with %d", ret);
    tp_clear_object (&debug_sender);

    return ret;
}
