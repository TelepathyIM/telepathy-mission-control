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

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/debug-sender.h>

#include "mcd-service.h"

static TpDebugSender *debug_sender;

#ifdef BUILD_AS_ANDROID_SERVICE
int telepathy_mission_control_main (int argc, char **argv);
#endif

static void
on_abort (McdService * mcd)
{
    g_debug ("Exiting now ...");

    mcd_debug_print_tree (mcd);

    g_object_unref (mcd);
    g_debug ("MC now exits .. bye bye");
    exit (0);
}

int
#ifdef BUILD_AS_ANDROID_SERVICE
telepathy_mission_control_main (int argc, char **argv)
#else
main (int argc, char **argv)
#endif
{
    McdService *mcd;

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

    /* Listen for suicide notification */
    g_signal_connect_after (mcd, "abort", G_CALLBACK (on_abort), mcd);

    /* connect */
    mcd_mission_connect (MCD_MISSION (mcd));

    mcd_service_run (MCD_OBJECT (mcd));

    tp_clear_object (&debug_sender);

    return 0;
}
