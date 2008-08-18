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

#include <glib.h>
#include <dbus/dbus-glib.h>
#include "libmissioncontrol/mission-control.h"


static void
on_service_ended (MissionControl *mc)
{
    g_debug ("Mission control has ended");
}

static void
account_status_changed_cb (GObject *object,
                           TelepathyConnectionStatus status,
                           McPresence presence,
                           TelepathyConnectionStatusReason reason,
                           const gchar *account,
                           gpointer data)
{
    g_debug ("Account status changed: %s, status = %u, presence = %u, reason = %u",
	     account, status, presence, reason);
}

static void mc_callback (MissionControl *mc, GError *error, gpointer data)
{
    if (error)
    {
	g_debug ("%s: got error code %u (%s), data is %p", G_STRFUNC,
		 error->code, error->message, data);
	g_error_free (error);
    }
    else
	g_debug ("%s: data is %p", G_STRFUNC, data);
}

int
main (int argc, char **argv)
{
    MissionControl *mc;
    DBusGConnection *dbus_conn = NULL;
    GMainLoop *main_loop;

    g_type_init ();
    dbus_conn = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);

    mc = mission_control_new (dbus_conn);
    g_signal_connect (mc, "ServiceEnded", G_CALLBACK (on_service_ended), NULL);
    dbus_g_proxy_connect_signal (DBUS_G_PROXY (mc),
				 "AccountStatusChanged",
				 G_CALLBACK (account_status_changed_cb),
				 NULL, NULL); /* NULL is for a DestroyNotify */

    mission_control_connect_all_with_default_presence (mc, mc_callback, NULL);
    main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (main_loop);

    return 0;
}


