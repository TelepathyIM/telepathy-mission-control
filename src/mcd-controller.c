/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
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
 * SECTION:mcd-controller
 * @title: McdController
 * @short_description: Server controller class
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-controller.h
 * 
 * This class implements the logic to control mission-control based on all
 * external device events and states. It also controls mission-control
 * life-cycle based on such events.
 */

#include "mcd-controller.h"

/* Milliseconds to wait for Connectivity coming back up before exiting MC */
#define EXIT_COUNTDOWN_TIME 5000

#define MCD_CONTROLLER_PRIV(controller) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((controller), \
				  MCD_TYPE_CONTROLLER, \
				  McdControllerPrivate))

G_DEFINE_TYPE (McdController, mcd_controller, MCD_TYPE_OPERATION);

/* Private */
typedef struct _McdControllerPrivate
{
    /* Current pending sleep timer */
    gint shutdown_timeout_id;

    gboolean is_disposed;
} McdControllerPrivate;

static void
mcd_controller_class_init (McdControllerClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdControllerPrivate));
}

static void
mcd_controller_init (McdController * obj)
{
}

/* Public */

McdController *
mcd_controller_new ()
{
    McdController *obj;
    obj = MCD_CONTROLLER (g_object_new (MCD_TYPE_CONTROLLER, NULL));
    return obj;
}

static gboolean
_mcd_controller_exit_by_timeout (gpointer data)
{
    McdController *controller;
    McdControllerPrivate *priv;
    
    controller = MCD_CONTROLLER (data);
    priv = MCD_CONTROLLER_PRIV (controller);
    
    priv->shutdown_timeout_id = 0;
    
    /* Notify sucide */
    mcd_mission_abort (MCD_MISSION (controller));
    
    return FALSE;
}

void
mcd_controller_shutdown (McdController *controller, const gchar *reason)
{
    McdControllerPrivate *priv;

    g_return_if_fail (MCD_IS_CONTROLLER (controller));
    priv = MCD_CONTROLLER_PRIV (controller);

    if(!priv->shutdown_timeout_id)
    {
        DEBUG ("MC will bail out because of \"%s\" out exit after %i",
               reason ? reason : "No reason specified",
               EXIT_COUNTDOWN_TIME);
	
	priv->shutdown_timeout_id = g_timeout_add (EXIT_COUNTDOWN_TIME,
						   _mcd_controller_exit_by_timeout,
						   controller);
    }
    else
    {
        DEBUG ("Already shutting down. This one has the reason %s",
               reason ? reason:"No reason specified");
    }
    mcd_debug_print_tree (controller);
}

void
mcd_controller_cancel_shutdown (McdController *controller)
{
    McdControllerPrivate *priv;

    g_return_if_fail (MCD_IS_CONTROLLER (controller));
    priv = MCD_CONTROLLER_PRIV (controller);

    if (priv->shutdown_timeout_id)
    {
        DEBUG ("Cancelling exit timeout");
	g_source_remove (priv->shutdown_timeout_id);
	priv->shutdown_timeout_id = 0;
    }
}
