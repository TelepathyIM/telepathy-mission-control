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

#ifndef MCD_MISSION_H
#define MCD_MISSION_H

#include <glib.h>
#include <glib-object.h>
#include "mcd-debug.h"

G_BEGIN_DECLS

#define MCD_TYPE_MISSION         (mcd_mission_get_type ())
#define MCD_MISSION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_MISSION, McdMission))
#define MCD_MISSION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_MISSION, McdMissionClass))
#define MCD_IS_MISSION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_MISSION))
#define MCD_IS_MISSION_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_MISSION))
#define MCD_MISSION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_MISSION, McdMissionClass))

#define MCD_MISSION_GET_FLAGS_MASKED(mission, flags) \
    (mcd_mission_get_flags (mission) & flags)

#define MCD_MISSION_SET_FLAGS_MASKED(mission, flags) \
    (mcd_mission_set_flags (mission, mcd_mission_get_flags (mission) | flags))

#define MCD_MISSION_UNSET_FLAGS_MASKED(mission, flags) \
    (mcd_mission_set_flags (mission, mcd_mission_get_flags (mission) & (~flags)))

typedef enum
{
    MCD_MODE_UNKNOWN,
    MCD_MODE_NORMAL,
    MCD_MODE_RESTRICTED,
    MCD_MODE_CALL
} McdMode;

typedef enum
{
    MCD_SYSTEM_CONNECTED          = 1,
    MCD_SYSTEM_MEMORY_CONSERVED   = 1 << 1,
    MCD_SYSTEM_POWER_CONSERVED    = 1 << 2,
    MCD_SYSTEM_SCREEN_BLANKED     = 1 << 3,
    MCD_SYSTEM_LOCKED             = 1 << 4,
    MCD_SYSTEM_IDLE               = 1 << 5
} McdSystemFlags;

typedef struct _McdMission McdMission;
typedef struct _McdMissionClass McdMissionClass;

struct _McdMission
{
    GObject parent;
};

struct _McdMissionClass
{
    GObjectClass parent_class;

    /* Signals */
    void (*parent_set_signal) (McdMission * mission, McdMission * parent);
    void (*connected_signal) (McdMission * mission);
    void (*disconnected_signal) (McdMission * mission);
    
    void (*flags_changed_signal) (McdMission *mission, McdSystemFlags flags);
    void (*mode_set_signal) (McdMission * mission, McdMode mode);

    void (*abort_signal) (McdMission * mission);
    
    /* Virtual methods */
    void (*set_parent) (McdMission * mission, McdMission * parent);
    
    void (*connect) (McdMission * mission);
    void (*disconnect) (McdMission * mission);
    
    void (*set_flags) (McdMission *mission, McdSystemFlags flags);
    McdSystemFlags (*get_flags) (McdMission *mission);
    
    void (*set_mode) (McdMission * mission, McdMode mode);
    McdMode (*get_mode) (McdMission * mission);
    
    void (*abort) (McdMission * mission);
};

GType mcd_mission_get_type (void);
McdMission *mcd_mission_new (void);

gboolean mcd_mission_is_connected (McdMission * mission);

McdMission *mcd_mission_get_parent (McdMission * mission);

void mcd_mission_abort (McdMission * mission);
void mcd_mission_set_parent (McdMission * mission, McdMission * parent);

void mcd_mission_connect (McdMission * mission);
void mcd_mission_disconnect (McdMission * mission);

void mcd_mission_set_flags (McdMission * mission, McdSystemFlags flags);
McdSystemFlags mcd_mission_get_flags (McdMission * mission);

void mcd_mission_set_mode (McdMission * mission, McdMode mode);
McdMode mcd_mission_get_mode (McdMission * mission);

G_END_DECLS
#endif /* MCD_MISSION_H */
