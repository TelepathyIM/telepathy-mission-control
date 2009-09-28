/*
 * Run until a bus name appears. This can be used as a service-activation
 * helper for a bus name that is not directly activatable, but will be provided
 * automatically (after a while) by the desktop session.
 *
 * Usage, in
 * $XDG_DATA_DIRS/dbus-1/services/org.freedesktop.Client.Something.service:
 *
 * [D-BUS Service]
 * Name=org.freedesktop.Telepathy.Client.Something
 * Exec=/usr/lib/mission-control/mc-wait-for-name org.freedesktop.Telepathy.Client.Something
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
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

#ifdef HAVE_SYSEXITS_H
# include <sysexits.h>
#else
# define EX_USAGE 64
# define EX_UNAVAILABLE 69
# define EX_SOFTWARE 70
# define EX_TEMPFAIL 75
#endif

#include <glib.h>

#include <telepathy-glib/dbus.h>

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

#define WFN_TIMEOUT (5 * 60) /* 5 minutes */

int
main (int argc,
      char **argv)
{
  TpDBusDaemon *bus_daemon;
  GMainLoop *loop;
  GError *error = NULL;

  g_set_prgname ("mc-wait-for-name");

  if (argc != 2 ||
      !tp_dbus_check_valid_bus_name (argv[1], TP_DBUS_NAME_TYPE_WELL_KNOWN,
        NULL))
    {
      g_message ("Usage: mc-wait-for-name com.example.SomeBusName");
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
