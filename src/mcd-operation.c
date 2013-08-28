/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

/**
 * SECTION:mcd-operation
 * @title: McdOperation
 * @short_description: Container class for holding missions
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-operation.h
 * 
 * This is a simple container class that can hold a list of mission objects
 * as children. McdOperation makes sure that object states (see: McdMission)
 * of the container are all proxied to the children. Children life cycles
 * also managed by this class and parent-child relationship is correctly
 * established.
 */

#include "config.h"

#include "mcd-operation.h"
#include "mcd-mission-priv.h"

#define MCD_OPERATION_PRIV(operation) (G_TYPE_INSTANCE_GET_PRIVATE ((operation), \
				       MCD_TYPE_OPERATION, \
				       McdOperationPrivate))

G_DEFINE_TYPE (McdOperation, mcd_operation, MCD_TYPE_MISSION);

/* Private */

typedef struct _McdOperationPrivate
{
    GList *missions;
    gboolean is_disposed;
} McdOperationPrivate;

enum _McdOperationSignalType
{
    MISSION_TAKEN,
    MISSION_REMOVED,
    LAST_SIGNAL
};

static guint mcd_operation_signals[LAST_SIGNAL] = { 0 };

static void
on_mission_abort (McdMission *mission, McdOperation *operation)
{
    g_return_if_fail (MCD_IS_MISSION (mission));
    g_return_if_fail (MCD_IS_OPERATION (operation));
    mcd_operation_remove_mission (operation, mission);
}

static void
_mcd_operation_disconnect_mission (McdMission *mission, McdOperation *operation)
{
    g_signal_handlers_disconnect_by_func (mission,
					  G_CALLBACK (on_mission_abort),
					  operation);
}

static void
_mcd_operation_finalize (GObject * object)
{
    G_OBJECT_CLASS (mcd_operation_parent_class)->finalize (object);
}

static void
_mcd_operation_child_unref (McdMission *child)
{
    g_object_unref (child);
}

static void
_mcd_operation_abort (McdOperation * operation)
{
    const GList *node;
    
    DEBUG ("Operation abort received, aborting all children");
    node = MCD_OPERATION_PRIV (operation)->missions;
    while (node)
    {
	McdMission *mission = MCD_MISSION (node->data);
	/* We don't want to hear it ourself so that we still hold the
	 * final reference to our children.
	 */
	g_signal_handlers_disconnect_by_func (mission,
					      G_CALLBACK (on_mission_abort),
					      operation);
	mcd_mission_abort (mission);
	
	/* Restore the handler so that we continue listing for destroy
	 * notify for our children.
	 */
	g_signal_connect (mission, "abort",
			  G_CALLBACK (on_mission_abort), operation);
	node = g_list_next (node);
    }
}

static void
_mcd_operation_dispose (GObject * object)
{
    McdOperationPrivate *priv = MCD_OPERATION_PRIV (object);

    if (priv->is_disposed)
    {
	return;
    }

    priv->is_disposed = TRUE;
    DEBUG ("operation disposed");

    g_signal_handlers_disconnect_by_func (object,
					  G_CALLBACK (_mcd_operation_abort),
					  NULL);
    if (priv->missions)
    {
	g_list_foreach (priv->missions,
			(GFunc) _mcd_operation_disconnect_mission,
			object);
	g_list_foreach (priv->missions, (GFunc) _mcd_operation_child_unref,
			NULL);
	g_list_free (priv->missions);
	priv->missions = NULL;
    }
    G_OBJECT_CLASS (mcd_operation_parent_class)->dispose (object);
}

static void
_mcd_operation_connect (McdMission * mission)
{
    McdOperationPrivate *priv = MCD_OPERATION_PRIV (mission);
    g_list_foreach (priv->missions, (GFunc) mcd_mission_connect, NULL);
    MCD_MISSION_CLASS (mcd_operation_parent_class)->connect (mission);
}

static void
_mcd_operation_disconnect (McdMission * mission)
{
    McdOperationPrivate *priv = MCD_OPERATION_PRIV (mission);
    g_list_foreach (priv->missions, (GFunc) mcd_mission_disconnect, NULL);
    MCD_MISSION_CLASS (mcd_operation_parent_class)->disconnect (mission);
}

void
mcd_operation_take_mission (McdOperation * operation, McdMission * mission)
{
    McdOperationPrivate *priv;

    g_return_if_fail (MCD_IS_OPERATION (operation));
    g_return_if_fail (MCD_IS_MISSION (mission));
    priv = MCD_OPERATION_PRIV (operation);

    priv->missions = g_list_prepend (priv->missions, mission);
    _mcd_mission_set_parent (mission, MCD_MISSION (operation));

    if (mcd_mission_is_connected (MCD_MISSION (operation)))
	mcd_mission_connect (mission);

    g_signal_connect (mission, "abort",
		      G_CALLBACK (on_mission_abort), operation);
    g_signal_emit_by_name (G_OBJECT (operation), "mission-taken", mission);
}

void
mcd_operation_remove_mission (McdOperation * operation, McdMission * mission)
{
    McdOperationPrivate *priv;

    g_return_if_fail (MCD_IS_OPERATION (operation));
    g_return_if_fail (MCD_IS_MISSION (mission));
    priv = MCD_OPERATION_PRIV (operation);

    g_return_if_fail (g_list_find (priv->missions, mission) != NULL);
    
    _mcd_operation_disconnect_mission (mission, operation);
    
    priv->missions = g_list_remove (priv->missions, mission);
    _mcd_mission_set_parent (mission, NULL);
    
    g_signal_emit_by_name (G_OBJECT (operation), "mission-removed", mission);

    DEBUG ("removing mission: %p", mission);
    g_object_unref (mission);
}

static void
mcd_operation_class_init (McdOperationClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    McdMissionClass *mission_class = MCD_MISSION_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (McdOperationPrivate));

    object_class->finalize = _mcd_operation_finalize;
    object_class->dispose = _mcd_operation_dispose;

    mission_class->connect = _mcd_operation_connect;
    mission_class->disconnect = _mcd_operation_disconnect;

    mcd_operation_signals[MISSION_TAKEN] =
	g_signal_new ("mission-taken",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdOperationClass,
				       mission_taken_signal),
		      NULL, NULL,
		      g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE, 1, G_TYPE_OBJECT);

    mcd_operation_signals[MISSION_REMOVED] =
	g_signal_new ("mission-removed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdOperationClass,
				       mission_removed_signal),
		      NULL, NULL,
		      g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE, 1, G_TYPE_OBJECT);
}

static void
mcd_operation_init (McdOperation * obj)
{
    McdOperationPrivate *priv = MCD_OPERATION_PRIV (obj);
    priv->missions = NULL;
    
    /* Listen to self abort so that we can propagate it to our
     * children
     */
    g_signal_connect (obj, "abort", G_CALLBACK (_mcd_operation_abort), NULL);
}

/* Public */

McdOperation *
mcd_operation_new (void)
{
    McdOperation *obj;
    obj = MCD_OPERATION (g_object_new (MCD_TYPE_OPERATION, NULL));
    return obj;
}

const GList *
mcd_operation_get_missions (McdOperation * operation)
{
    McdOperationPrivate *priv;

    g_return_val_if_fail (MCD_IS_OPERATION (operation), NULL);
    priv = MCD_OPERATION_PRIV (operation);

    return priv->missions;
}

void
mcd_operation_foreach (McdOperation * operation, GFunc func, gpointer user_data)
{
    McdOperationPrivate *priv;

    g_return_if_fail (MCD_IS_OPERATION (operation));
    priv = MCD_OPERATION_PRIV (operation);

    g_list_foreach (priv->missions, (GFunc) func, user_data);
}
