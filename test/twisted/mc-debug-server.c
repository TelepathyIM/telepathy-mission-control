/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
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


static void
on_abort (McdService * mcd)
{
    g_debug ("Exiting now ...");

    mcd_debug_print_tree (mcd);

    g_object_unref (mcd);
    g_debug ("MC now exits .. bye bye");
    exit (0);
}

static DBusHandlerResult
dbus_filter_function (DBusConnection *connection,
                      DBusMessage *message,
                      void *user_data)
{
  if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") &&
      !tp_strdiff (dbus_message_get_path (message), DBUS_PATH_LOCAL))
    {
      g_message ("Got disconnected from the session bus");
      exit (69); /* EX_UNAVAILABLE */
    }
  else if (dbus_message_is_method_call (message,
        "org.freedesktop.Telepathy.MissionControl.RegressionTests",
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

      return DBUS_HANDLER_RESULT_HANDLED;
    }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int
main (int argc, char **argv)
{
    TpDBusDaemon *bus_daemon = NULL;
    McdService *mcd = NULL;
    GError *error = NULL;
    DBusConnection *connection;
    int ret = 1;

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
    g_signal_connect_after (mcd, "abort", G_CALLBACK (on_abort), mcd);

    /* connect */
    mcd_mission_connect (MCD_MISSION (mcd));

    /* MC initialization sets exit on disconnect - turn it off again, so we
     * get a graceful exit from the above handler instead (to keep gcov
     * happy) */
    dbus_connection_set_exit_on_disconnect (connection, FALSE);

    mcd_service_run (MCD_OBJECT (mcd));

    ret = 0;

out:
    if (bus_daemon != NULL)
    {
        g_object_unref (bus_daemon);
    }

    return ret;
}
