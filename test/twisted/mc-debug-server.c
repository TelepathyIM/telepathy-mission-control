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

#include <stdlib.h>
#include <unistd.h>
#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/debug.h>
#include <telepathy-glib/util.h>

#include "mcd-service.h"

static McdService *mcd = NULL;

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

    g_object_unref (mcd);
    mcd = NULL;
}

static DBusHandlerResult
dbus_filter_function (DBusConnection *connection,
                      DBusMessage *message,
                      void *user_data)
{
  if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") &&
      !tp_strdiff (dbus_message_get_path (message), DBUS_PATH_LOCAL))
    {
      /* MC initialization sets exit on disconnect - turn it off again, so we
       * get a graceful exit instead (to keep gcov happy) */
      dbus_connection_set_exit_on_disconnect (connection, FALSE);

      g_message ("Got disconnected from the session bus");

      mcd_mission_abort ((McdMission *) mcd);
    }
  else if (dbus_message_is_method_call (message,
        "org.freedesktop.Telepathy.MissionControl5.RegressionTests",
        "ChangeSystemFlags"))
    {
      DBusMessage *reply;
      DBusError e;
      dbus_uint32_t set, unset;

      dbus_error_init (&e);

      if (!dbus_message_get_args (message, &e,
            'u', &set,
            'u', &unset,
            DBUS_TYPE_INVALID))
        {
          reply = dbus_message_new_error (message, e.name, e.message);
          dbus_error_free (&e);
        }
      else
        {
          McdMission *mission = MCD_MISSION (mcd_master_get_default ());
          McdSystemFlags flags;

          flags = mcd_mission_get_flags (mission);
          flags |= set;
          flags &= ~unset;
          mcd_mission_set_flags (mission, flags);

          reply = dbus_message_new_method_return (message);
        }

      if (reply == NULL || !dbus_connection_send (connection, reply, NULL))
        g_error ("Out of memory");

      dbus_message_unref (reply);

      return DBUS_HANDLER_RESULT_HANDLED;
    }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int
main (int argc, char **argv)
{
    TpDBusDaemon *bus_daemon = NULL;
    GError *error = NULL;
    DBusConnection *connection;
    int ret = 1;
    GMainLoop *teardown_loop;
    guint linger_time = 5;

    g_type_init ();

    mcd_debug_init ();
    tp_debug_set_flags (g_getenv ("MC_TP_DEBUG"));

    bus_daemon = tp_dbus_daemon_dup (&error);

    if (bus_daemon == NULL)
    {
        g_warning ("%s", error->message);
        g_error_free (error);
        error = NULL;
        goto out;
    }

    /* It appears that dbus-glib registers a filter that wrongly returns
     * DBUS_HANDLER_RESULT_HANDLED for signals, so for *our* filter to have any
     * effect, we need to install it as soon as possible */
    connection = dbus_g_connection_get_connection (
        ((TpProxy *) bus_daemon)->dbus_connection);
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

out:

    if (bus_daemon != NULL)
    {
        dbus_connection_flush (connection);
        g_object_unref (bus_daemon);
    }

    dbus_shutdown ();

    g_message ("Exiting with %d", ret);

    return ret;
}
