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
 * Exec=/usr/lib/telepathy/mc6-wait-for-name ....Client.Something
 *
 * Alternatively, it can be used to activate something via an alternative
 * name, e.g. in
 * $XDG_DATA_DIRS/dbus-1/services/....AccountManager.service:
 *
 * [D-BUS Service]
 * Name=....AccountManager
 * Exec=/usr/lib/telepathy/mc6-wait-for-name --activate ....MissionControl6 ....AccountManager
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
#include <gio/gio.h>

#include <telepathy-glib/telepathy-glib.h>

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
name_appeared_cb (GDBusConnection *connection,
    const gchar *name,
    const gchar *name_owner,
    gpointer user_data)
{
  g_debug ("%s now owned by %s", name, name_owner);

  if (idle_id == 0)
    idle_id = g_idle_add (quit_because_found, g_main_loop_ref (user_data));
}

static void
name_vanished_cb (GDBusConnection *connection,
    const gchar *name,
    gpointer user_data)
{
  g_debug ("Waiting for %s", name);
}

static void
start_service_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  GMainLoop *loop = user_data;
  GVariant *tuple;
  guint32 ret;
  GError *error = NULL;

  tuple = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
      result, &error);

  if (tuple == NULL)
    {
      g_message ("%s", error->message);
      g_error_free (error);
      g_main_loop_quit (loop);
      exit_status = EX_TEMPFAIL;
    }
  else
    {
      g_variant_get (tuple, "(u)", &ret);

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

      g_variant_unref (tuple);
    }

  g_main_loop_unref (loop);
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
  GDBusConnection *bus;
  GMainLoop *loop;
  GError *error = NULL;
  GOptionContext *context;
  guint watch;

  g_set_prgname ("mc6-wait-for-name");

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
      g_message ("Usage: mc6-wait-for-name [OPTIONS] com.example.SomeBusName");
      return EX_USAGE;
    }

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

  if (bus == NULL)
    {
      g_message ("%s", error->message);
      g_error_free (error);
      return EX_UNAVAILABLE;
    }

  loop = g_main_loop_new (NULL, FALSE);

  if (activate != NULL)
    {
      g_dbus_connection_call (bus,
          "org.freedesktop.DBus",
          "/org/freedesktop/DBus",
          "org.freedesktop.DBus",
          "StartServiceByName",
          g_variant_new ("(su)", activate, 0 /* no flags */),
          G_VARIANT_TYPE ("(u)"),
          G_DBUS_CALL_FLAGS_NONE, -1, NULL,
          start_service_cb,
          g_main_loop_ref (loop));
    }

  watch = g_bus_watch_name_on_connection (bus,
    argv[1],
    G_BUS_NAME_WATCHER_FLAGS_NONE,
    name_appeared_cb,
    name_vanished_cb,
    g_main_loop_ref (loop),
    (GDestroyNotify) g_main_loop_unref);

  g_timeout_add_seconds (WFN_TIMEOUT, quit_because_timeout,
      g_main_loop_ref (loop));

  g_main_loop_run (loop);

  g_bus_unwatch_name (watch);

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
  g_object_unref (bus);

  return exit_status;
}
