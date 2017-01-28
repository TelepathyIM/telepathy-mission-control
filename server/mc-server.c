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

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <glib.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#endif

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-service.h"

static TpDebugSender *debug_sender;
static McdService *mcd = NULL;

#ifdef G_OS_UNIX
static int quit_pipe[2];
#define QUIT_READ_END 0
#define QUIT_WRITE_END 1
#endif

#ifdef BUILD_AS_ANDROID_SERVICE
int telepathy_mission_control_main (int argc, char **argv);
#endif

static void
on_abort (McdService * _mcd)
{
    g_debug ("Exiting now ...");

    mcd_debug_print_tree (_mcd);

    g_debug ("MC now exits .. bye bye");
    mcd_service_stop (_mcd);
}

#ifdef G_OS_UNIX
static void
signal_handler (int sig)
{
    switch (sig)
      {
        case SIGINT:
            if ((quit_pipe[QUIT_WRITE_END] > 0) &&
                write (quit_pipe[QUIT_WRITE_END], "\0", 1) != 1)
              {
                /* If we can't write to the socket, dying seems a good
                 * response to SIGINT. We'd use exit(), but that's not
                 * async-signal-safe, so we'll have to resort to _exit().
                 * We use write() because it is async-signal-safe. */
                static const char message[] =
                  "Unable to write to quit pipe - buffer full?\n"
                  "Will exit instead.\n";

                if (write (STDERR_FILENO, message, strlen (message)) == -1)
                  {
                    /* Ignore, we are returning anyway */
                  }
                _exit (1);
              }
            break;
      }
}

static gboolean
quit_idle_cb (gpointer user_data)
{
    mcd_mission_abort (MCD_MISSION (mcd));
    return FALSE;
}

static gboolean
quit_event_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    g_idle_add_full (G_PRIORITY_LOW, quit_idle_cb, NULL, NULL);
    return FALSE;
}

static void
init_quit_pipe (void)
{
    int i;
    GIOChannel *channel;
    GError *error = NULL;

    if (!g_unix_open_pipe (quit_pipe, FD_CLOEXEC, &error))
      {
        g_warning ("Failed to get a pipe: %s", error->message);
        g_clear_error (&error);
        return;
      }
    for (i = 0 ; i < 2 ; i++)
      {
        int val;
        val = fcntl (quit_pipe[i], F_GETFL, 0);
        if (val < 0)
          {
            g_warning ("Failed to get flags from file descriptor %d: %s",
                       quit_pipe[i], strerror (errno));
            continue;
          }
        val = fcntl (quit_pipe[i], F_SETFL, val | O_NONBLOCK);
        if (val < 0)
          {
            g_warning ("Failed to set flags from file descriptor %d: %s",
                       quit_pipe[i], strerror (errno));
            continue;
          }
      }
    channel = g_io_channel_unix_new (quit_pipe[QUIT_READ_END]);
    g_io_add_watch (channel, G_IO_IN, quit_event_cb, NULL);
}
#endif

int
#ifdef BUILD_AS_ANDROID_SERVICE
telepathy_mission_control_main (int argc, char **argv)
#else
main (int argc, char **argv)
#endif
{
#ifdef G_OS_UNIX
    struct sigaction act;
    sigset_t empty_mask;
#endif

    g_type_init ();
    g_set_application_name ("Account manager");

    /* Keep a ref to the default TpDebugSender for the lifetime of the
     * McdMaster, so it will persist for the lifetime of MC, and subsequent
     * calls to tp_debug_sender_dup() will return it again */
    debug_sender = tp_debug_sender_dup ();

    /* Send all debug messages through the Telepathy infrastructure.
     *
     * Unlike CMs, we don't have "subdomains" within MC yet, so we don't want
     * to exclude any domains. */
    g_log_set_default_handler (tp_debug_sender_log_handler, NULL);

    mcd_debug_init ();
    tp_debug_set_flags (g_getenv ("MC_TP_DEBUG"));

    mcd = mcd_service_new ();
    if (mcd == NULL)
      return 1;

    /* Listen for suicide notification */
    g_signal_connect_after (mcd, "abort", G_CALLBACK (on_abort), mcd);

    /* Set up signals */
#ifdef G_OS_UNIX
    init_quit_pipe ();
    sigemptyset (&empty_mask);
    act.sa_handler = signal_handler;
    act.sa_mask    = empty_mask;
    act.sa_flags   = 0;
    sigaction (SIGINT, &act, NULL);
#endif

    /* connect */
    mcd_mission_connect (MCD_MISSION (mcd));

    mcd_service_run (MCD_OBJECT (mcd));

    g_clear_object (&mcd);
    tp_clear_object (&debug_sender);

    return 0;
}
