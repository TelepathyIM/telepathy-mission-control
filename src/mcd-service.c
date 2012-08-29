/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
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

/**
 * SECTION:mcd-service
 * @title: McdService
 * @short_description: Service interface implementation
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-service.h
 * 
 * It is the frontline interface object that exposes mission-control to outside
 * world through a dbus interface. It basically subclasses McdMaster and
 * wraps up everything inside it and translate them into mission-control
 * dbus interface.
 */

#include "config.h"

#include <dbus/dbus.h>
#include <string.h>
#include <dlfcn.h>
#include <sched.h>
#include <stdlib.h>

#include <glib.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>
#include <telepathy-glib/telepathy-glib.h>

#include "mcd-connection.h"
#include "mcd-misc.h"
#include "mcd-service.h"

/* DBus service specifics */
#define MISSION_CONTROL_DBUS_SERVICE "org.freedesktop.Telepathy.MissionControl5"

static GObjectClass *parent_class = NULL;

#define MCD_OBJECT_PRIV(mission) (G_TYPE_INSTANCE_GET_PRIVATE ((mission), \
				   MCD_TYPE_SERVICE, \
				   McdServicePrivate))

G_DEFINE_TYPE (McdService, mcd_service, MCD_TYPE_MASTER);

/* Private */

typedef struct _McdServicePrivate
{
    gboolean is_disposed;
} McdServicePrivate;

static void
mcd_service_obtain_bus_name (McdService * obj)
{
    McdMaster *master = MCD_MASTER (obj);
    GError *error = NULL;

    DEBUG ("Requesting MC dbus service");

    if (!tp_dbus_daemon_request_name (mcd_master_get_dbus_daemon (master),
                                      MISSION_CONTROL_DBUS_SERVICE,
                                      TRUE /* idempotent */, &error))
    {
        g_warning ("Failed registering '%s' service: %s",
                   MISSION_CONTROL_DBUS_SERVICE, error->message);
        g_error_free (error);
        exit (1);
    }
}

static void
mcd_service_disconnect (McdMission *mission)
{
    MCD_MISSION_CLASS (mcd_service_parent_class)->disconnect (mission);
    mcd_master_shutdown (MCD_MASTER (mission), "Disconnected");
}

static void
mcd_dispose (GObject * obj)
{
    McdServicePrivate *priv;
    McdService *self = MCD_OBJECT (obj);

    priv = MCD_OBJECT_PRIV (self);

    if (priv->is_disposed)
    {
	return;
    }

    priv->is_disposed = TRUE;

    if (self->main_loop)
    {
	g_main_loop_quit (self->main_loop);
    }

    tp_clear_pointer (&self->main_loop, g_main_loop_unref);

    if (G_OBJECT_CLASS (parent_class)->dispose)
    {
	G_OBJECT_CLASS (parent_class)->dispose (obj);
    }
}

static void
mcd_service_constructed (GObject *obj)
{
    DEBUG ("called");

    mcd_service_obtain_bus_name (MCD_OBJECT (obj));
    mcd_debug_print_tree (obj);

    if (G_OBJECT_CLASS (parent_class)->constructed)
	G_OBJECT_CLASS (parent_class)->constructed (obj);
}

static void
mcd_service_init (McdService * obj)
{
    obj->main_loop = g_main_loop_new (NULL, FALSE);

    DEBUG ("called");
}

static void
mcd_service_class_init (McdServiceClass * self)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (self);
    McdMissionClass *mission_class = MCD_MISSION_CLASS (self);

    parent_class = g_type_class_peek_parent (self);
    gobject_class->constructed = mcd_service_constructed;
    gobject_class->dispose = mcd_dispose;
    mission_class->disconnect = mcd_service_disconnect;

    g_type_class_add_private (gobject_class, sizeof (McdServicePrivate));
}

McdService *
mcd_service_new (void)
{
    McdService *obj;
    TpDBusDaemon *dbus_daemon;
    GError *error = NULL;

    /* Initialize DBus connection */
    dbus_daemon = tp_dbus_daemon_dup (&error);
    if (dbus_daemon == NULL)
    {
	g_printerr ("Failed to open connection to bus: %s", error->message);
	g_error_free (error);
	return NULL;
    }
    obj = g_object_new (MCD_TYPE_SERVICE,
			"dbus-daemon", dbus_daemon,
			NULL);
    g_object_unref (dbus_daemon);
    return obj;
}

void
mcd_service_run (McdService * self)
{
    g_main_loop_run (self->main_loop);
}

void
mcd_service_stop (McdService * self)
{
    if (self->main_loop != NULL)
        g_main_loop_quit (self->main_loop);
}
