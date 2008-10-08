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

#ifndef MCD_OPERATION_H
#define MCD_OPERATION_H

#include <glib.h>
#include <glib-object.h>

#include "mcd-mission.h"

G_BEGIN_DECLS
#define MCD_TYPE_OPERATION         (mcd_operation_get_type ())
#define MCD_OPERATION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_OPERATION, McdOperation))
#define MCD_OPERATION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_OPERATION, McdOperationClass))
#define MCD_IS_OPERATION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_OPERATION))
#define MCD_IS_OPERATION_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_OPERATION))
#define MCD_OPERATION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_OPERATION, McdOperationClass))
typedef struct _McdOperation McdOperation;
typedef struct _McdOperationClass McdOperationClass;

struct _McdOperation
{
    McdMission parent;
};

struct _McdOperationClass
{
    McdMissionClass parent_class;

    /* signals */
    void (*mission_taken_signal) (McdOperation * operation,
				  McdMission * mission);
    void (*mission_removed_signal) (McdOperation * operation,
				    McdMission * mission);

    /* virtual methods */
    void (*take_mission) (McdOperation * operation, McdMission * mission);
    void (*remove_mission) (McdOperation * operation, McdMission * mission);
};

GType mcd_operation_get_type (void);
McdOperation *mcd_operation_new (void);

/* Takes the ownership of mission */
void mcd_operation_take_mission (McdOperation * operation,
				 McdMission * mission);
void mcd_operation_remove_mission (McdOperation * operation,
				   McdMission * mission);
void mcd_operation_foreach (McdOperation * operation,
			    GFunc func, gpointer user_data);
const GList * mcd_operation_get_missions (McdOperation * operation);

G_END_DECLS
#endif /* MCD_OPERATION_H */
