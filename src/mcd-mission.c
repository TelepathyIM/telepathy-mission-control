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
 * SECTION:mcd-mission
 * @title: McdMission
 * @short_description: Base class for server classes
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-mission.h
 * 
 * It is the base class for every object in mission-control. It defines
 * a set of virtual functions and set of corresponding action signals.
 * all virtual functions results in emission of their corresponding action
 * signals. The virtual functions define states of the object, such
 * as memory conserved state, connected state, locked state, low power state,
 * lit state, sleeping state etc. Each of the object states can also be queried
 * independently as properties.
 * 
 * There are also some action signals such as abort, which is used to notify
 * other objects holding hard references to it to release them (this object
 * should then automatically die since all held references are released). It
 * is mandatory for all other objects that hold a hard reference to it to
 * listen for this signal and release the reference in signal handler.
 * 
 * Concrete derived classes should override the sate  methods to implement
 * object specific state managements.
 */

#include "config.h"

#include "mcd-mission-priv.h"

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-enum-types.h"

#define MCD_MISSION_PRIV(mission) (G_TYPE_INSTANCE_GET_PRIVATE ((mission), \
				   MCD_TYPE_MISSION, \
				   McdMissionPrivate))

G_DEFINE_TYPE (McdMission, mcd_mission, G_TYPE_OBJECT);

/* Private */

typedef struct _McdMissionPrivate
{
    McdMission *parent;

    gboolean connected;
    gboolean is_disposed;

} McdMissionPrivate;

enum _McdMissionSignalType
{
    CONNECTED,
    DISCONNECTED,
    PARENT_SET,
    ABORT,
    LAST_SIGNAL
};

enum _McdMissionPropertyType
{
    PROP_0,
    PROP_PARENT
};

static guint mcd_mission_signals[LAST_SIGNAL] = { 0 };

static void
_mcd_mission_connect (McdMission * mission)
{
    McdMissionPrivate *priv;

    g_return_if_fail (MCD_IS_MISSION (mission));
    priv = MCD_MISSION_PRIV (mission);

    if (!priv->connected)
    {
        priv->connected = TRUE;
        g_signal_emit_by_name (mission, "connected");
    }
}

static void
_mcd_mission_disconnect (McdMission * mission)
{
    McdMissionPrivate *priv;

    g_return_if_fail (MCD_IS_MISSION (mission));
    priv = MCD_MISSION_PRIV (mission);

    if (priv->connected)
    {
        priv->connected = FALSE;
        g_signal_emit_by_name (mission, "disconnected");
    }
}

static void
on_parent_abort (McdMission *parent, McdMission *mission)
{
    DEBUG ("called");
    _mcd_mission_set_parent (mission, NULL);
}

void
_mcd_mission_set_parent (McdMission * mission, McdMission * parent)
{
    McdMissionPrivate *priv;

    g_return_if_fail (MCD_IS_MISSION (mission));
    g_return_if_fail ((parent == NULL) || MCD_IS_MISSION (parent));

    priv = MCD_MISSION_PRIV (mission);

    DEBUG ("child = %p, parent = %p", mission, parent);

    if (priv->parent)
    {
	g_signal_handlers_disconnect_by_func (priv->parent,
					      on_parent_abort,
					      mission);
    }
    
    if (parent)
    {
	g_signal_connect (parent, "abort",
			  G_CALLBACK (on_parent_abort),
			  mission);
	g_object_ref (parent);
    }
    
    tp_clear_object (&priv->parent);
    priv->parent = parent;
}

static void
_mcd_mission_abort (McdMission * mission)
{
    g_signal_emit_by_name (G_OBJECT (mission), "abort");
}

static void
_mcd_mission_dispose (GObject * object)
{
    McdMissionPrivate *priv;
    g_return_if_fail (MCD_IS_MISSION (object));

    priv = MCD_MISSION_PRIV (object);

    if (priv->is_disposed)
    {
	return;
    }

    priv->is_disposed = TRUE;

    DEBUG ("mission disposed %p", object);
    if (priv->parent)
    {
	g_signal_handlers_disconnect_by_func (priv->parent,
					      on_parent_abort,
					      object);
    }

    tp_clear_object (&priv->parent);

    G_OBJECT_CLASS (mcd_mission_parent_class)->dispose (object);
}

static void
_mcd_mission_finalize (GObject * object)
{
    DEBUG ("mission finalized %p", object);
    G_OBJECT_CLASS (mcd_mission_parent_class)->finalize (object);
}

static void
_mcd_set_property (GObject * object, guint prop_id, const GValue * val,
		   GParamSpec * pspec)
{
    McdMission *mission = MCD_MISSION (object);

    switch (prop_id)
    {
    case PROP_PARENT:
	_mcd_mission_set_parent (mission, g_value_get_object (val));
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	break;
    }
}

static void
_mcd_get_property (GObject * object, guint prop_id, GValue * val,
		   GParamSpec * pspec)
{
    McdMission *mission = MCD_MISSION (object);

    switch (prop_id)
    {
    case PROP_PARENT:
	g_value_set_object (val, mcd_mission_get_parent (mission));
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	break;
    }
}

static void
mcd_mission_class_init (McdMissionClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdMissionPrivate));

    /* virtual medthods */
    object_class->finalize = _mcd_mission_finalize;
    object_class->dispose = _mcd_mission_dispose;
    object_class->set_property = _mcd_set_property;
    object_class->get_property = _mcd_get_property;

    /* virtual medthods */
    klass->abort = _mcd_mission_abort;
    klass->connect = _mcd_mission_connect;
    klass->disconnect = _mcd_mission_disconnect;

    /* signals */
    mcd_mission_signals[ABORT] =
	g_signal_new ("abort",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST,
		      G_STRUCT_OFFSET (McdMissionClass, abort_signal),
		      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
		      0);
    mcd_mission_signals[CONNECTED] =
	g_signal_new ("connected", G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (McdMissionClass,
							   connected_signal),
		      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
		      0);
    mcd_mission_signals[DISCONNECTED] =
	g_signal_new ("disconnected", G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (McdMissionClass,
							   disconnected_signal),
		      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
		      0);
}

static void
mcd_mission_init (McdMission * obj)
{
}

/* Public methods */

void
mcd_mission_connect (McdMission * mission)
{
    g_return_if_fail (MCD_IS_MISSION (mission));
    MCD_MISSION_GET_CLASS (mission)->connect (mission);
}

void
mcd_mission_disconnect (McdMission * mission)
{
    g_return_if_fail (MCD_IS_MISSION (mission));
    MCD_MISSION_GET_CLASS (mission)->disconnect (mission);
}

void
mcd_mission_abort (McdMission * mission)
{
    g_return_if_fail (MCD_IS_MISSION (mission));
    MCD_MISSION_GET_CLASS (mission)->abort (mission);
}

gboolean
mcd_mission_is_connected (McdMission * mission)
{
    McdMissionPrivate *priv;

    g_return_val_if_fail (MCD_IS_MISSION (mission), FALSE);
    priv = MCD_MISSION_PRIV (mission);

    return priv->connected;
}

McdMission *
mcd_mission_get_parent (McdMission * mission)
{
    McdMissionPrivate *priv;

    g_return_val_if_fail (MCD_IS_MISSION (mission), NULL);
    priv = MCD_MISSION_PRIV (mission);

    return priv->parent;
}
