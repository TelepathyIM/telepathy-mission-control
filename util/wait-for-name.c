/*
 * Run until a bus name appears. This can be used as a service-activation
 * helper for a bus name that is not directly activatable, but will be provided
 * automatically (after a while) by the desktop session.
 *
 * Usage, in
 * $XDG_DATA_DIRS/dbus-1/services/....Client.Something.service:
 *
 * [D-BUS Service]
 * Name=....Client.Something
 * Exec=/usr/lib/telepathy/mc-wait-for-name ....Client.Something
 *
 * Alternatively, it can be used to activate something via an alternative
 * name, e.g. in
 * $XDG_DATA_DIRS/dbus-1/services/....AccountManager.service:
 *
 * [D-BUS Service]
 * Name=....AccountManager
 * Exec=/usr/lib/telepathy/mc-wait-for-name --activate ....MissionControl5 ....AccountManager
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009, 2012 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifdef HAVE_SYSEXITS_H
# include <sysexits.h>
#else
# define EX_USAGE 64
# define EX_UNAVAILABLE 69
# define EX_SOFTWARE 70
# define EX_TEMPFAIL 75
#endif

#include <glib.h>
#include <locale.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

static int exit_status = EX_SOFTWARE;
static guint timeout_id = 0;
static guint idle_id = 0;

static gboolean
quit_because_found (gpointer data)
{
  idle_id = 0;
  g_main_loop_quit (data);
  g_main_loop_unref (data);
  exit_status = 0;
  return FALSE;
}

static gboolean
quit_because_timeout (gpointer data)
{
  timeout_id = 0;
  g_main_loop_quit (data);
  g_main_loop_unref (data);
  exit_status = EX_TEMPFAIL;
  return FALSE;
}

static void
noc_cb (TpDBusDaemon *bus_daemon,
        const gchar *name,
        const gchar *new_owner,
        gpointer data)
{
  if (new_owner[0] == '\0')
    {
      g_debug ("Waiting for %s", name);
    }
  else
    {
      g_debug ("%s now owned by %s", name, new_owner);

      if (idle_id == 0)
        idle_id = g_idle_add (quit_because_found, g_main_loop_ref (data));
    }
}

static void
start_service_cb (TpDBusDaemon *bus_daemon,
    guint ret,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GMainLoop *loop = user_data;

  if (error != NULL)
    {
      g_message ("%s", error->message);
      g_main_loop_quit (loop);
      exit_status = EX_TEMPFAIL;
    }
  else
    {
      switch (ret)
        {
          case 1: /* DBUS_START_REPLY_SUCCESS */
            g_debug ("activated name successfully started");
            break;

          case 2: /* DBUS_START_REPLY_ALREADY_RUNNING */
            g_debug ("activated name already running");
            break;

          default:
            g_message ("ignoring unknown result from StartServiceByName: %u", ret);
            break;
        }
    }
}

#define WFN_TIMEOUT (5 * 60) /* 5 minutes */

static gchar *activate = NULL;
static GOptionEntry entries[] = {
      { "activate", 0, 0, G_OPTION_ARG_STRING, &activate, "Activate NAME before waiting for the other name", "NAME" },
      { NULL }
};

int
main (int argc,
      char **argv)
{
  TpDBusDaemon *bus_daemon;
  GMainLoop *loop;
  GError *error = NULL;
  GOptionContext *context;

  setlocale (LC_ALL, "");

  g_set_prgname ("mc-wait-for-name");

  context = g_option_context_new ("- wait for a bus name");
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_message ("%s", error->message);
      g_error_free (error);
      return EX_USAGE;
    }

  if (activate != NULL &&
      !tp_dbus_check_valid_bus_name (activate, TP_DBUS_NAME_TYPE_WELL_KNOWN,
        NULL))
    {
      g_message ("Not a valid bus name: %s", activate);
      return EX_USAGE;
    }

  if (argc != 2 ||
      !tp_dbus_check_valid_bus_name (argv[1], TP_DBUS_NAME_TYPE_WELL_KNOWN,
        NULL))
    {
      g_message ("Usage: mc-wait-for-name [OPTIONS] com.example.SomeBusName");
      return EX_USAGE;
    }

  g_type_init ();
  bus_daemon = tp_dbus_daemon_dup (&error);

  if (bus_daemon == NULL)
    {
      g_message ("%s", error->message);
      g_error_free (error);
      return EX_UNAVAILABLE;
    }

  loop = g_main_loop_new (NULL, FALSE);

  if (activate != NULL)
    {
      tp_cli_dbus_daemon_call_start_service_by_name (bus_daemon, -1,
          activate, 0 /* no flags */, start_service_cb, g_main_loop_ref (loop),
          (GDestroyNotify) g_main_loop_unref, NULL);
    }

  tp_dbus_daemon_watch_name_owner (bus_daemon, argv[1],
      noc_cb, g_main_loop_ref (loop), (GDestroyNotify) g_main_loop_unref);

  g_timeout_add_seconds (WFN_TIMEOUT, quit_because_timeout,
      g_main_loop_ref (loop));

  g_main_loop_run (loop);

  if (timeout_id != 0)
    {
      g_source_remove (timeout_id);
      g_main_loop_unref (loop);
    }

  if (idle_id != 0)
    {
      g_source_remove (idle_id);
      g_main_loop_unref (loop);
    }

  g_main_loop_unref (loop);
  g_object_unref (bus_daemon);

  return exit_status;
}
