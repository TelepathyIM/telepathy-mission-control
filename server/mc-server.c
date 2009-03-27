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

#include <telepathy-glib/debug.h>

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

int
main (int argc, char **argv)
{
    McdService *mcd;

    g_type_init ();

    mcd_debug_init ();
    tp_debug_set_flags (g_getenv ("MC_TP_DEBUG"));

    mcd = mcd_service_new ();

    /* Listen for suicide notification */
    g_signal_connect_after (mcd, "abort", G_CALLBACK (on_abort), mcd);

    /* connect */
    mcd_mission_connect (MCD_MISSION (mcd));

    mcd_service_run (MCD_OBJECT (mcd));

    return 0;
}
