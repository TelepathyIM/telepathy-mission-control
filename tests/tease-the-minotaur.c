/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/*
 * tease-the-minotaur: a simple interactive test app for McdConnectivityMonitor
 *
 * Copyright © 2009–2011 Collabora Ltd.
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
 *
 * Authors:
 *   Will Thompson <will.thompson@collabora.co.uk>
 */

#include "config.h"

#include "connectivity-monitor.h"

static void
state_change_cb (
    McdConnectivityMonitor *minotaur,
    gboolean connected,
    McdInhibit *inhibit,
    gpointer user_data)
{
  g_print (connected ? "connected\n" : "disconnected\n");
}

int
main (
    int argc,
    char *argv[])
{
  McdConnectivityMonitor *minotaur;
  GMainLoop *im_feeling_loopy;

  g_type_init ();

  minotaur = mcd_connectivity_monitor_new ();
  g_signal_connect (minotaur, "state-change", (GCallback) state_change_cb, NULL);
  state_change_cb (minotaur, mcd_connectivity_monitor_is_online (minotaur),
      NULL, NULL);

  im_feeling_loopy = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (im_feeling_loopy);
  return 0;
}
