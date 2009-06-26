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

#include <glib.h>
#include <telepathy-glib/dbus.h>

static gboolean
quit (gpointer data)
{
  g_main_loop_quit (data);
  g_main_loop_unref (data);
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
      g_idle_add (quit, g_main_loop_ref (data));
    }
}

int
main (int argc,
      char **argv)
{
  TpDBusDaemon *bus_daemon;
  GMainLoop *loop;
  GError *error = NULL;

  g_set_prgname ("mc-wait-for-name");

  if (argc != 2)
    {
      g_message ("Usage: mc-wait-for-name com.example.SomeBusName");
      return 2;
    }

  g_type_init ();
  bus_daemon = tp_dbus_daemon_dup (&error);

  if (bus_daemon == NULL)
    {
      g_message ("%s", error->message);
      g_error_free (error);
      return 1;
    }

  loop = g_main_loop_new (NULL, FALSE);
  tp_dbus_daemon_watch_name_owner (bus_daemon, argv[1],
      noc_cb, g_main_loop_ref (loop), (GDestroyNotify) g_main_loop_unref);
  g_main_loop_run (loop);

  g_main_loop_unref (loop);
  g_object_unref (bus_daemon);

  return 0;
}
