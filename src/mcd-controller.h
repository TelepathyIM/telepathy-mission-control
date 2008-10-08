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

#ifndef MCD_CONTROLLER_H
#define MCD_CONTROLLER_H

#include <glib.h>
#include <glib-object.h>

#include "mcd-operation.h"

G_BEGIN_DECLS

#define MCD_TYPE_CONTROLLER         (mcd_controller_get_type ())
#define MCD_CONTROLLER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_CONTROLLER, McdController))
#define MCD_CONTROLLER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_CONTROLLER, McdControllerClass))
#define MCD_IS_CONTROLLER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_CONTROLLER))
#define MCD_IS_CONTROLLER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_CONTROLLER))
#define MCD_CONTROLLER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_CONTROLLER, McdControllerClass))

typedef struct _McdController McdController;
typedef struct _McdControllerClass McdControllerClass;

struct _McdController
{
    McdOperation parent;
};

struct _McdControllerClass
{
    McdOperationClass parent_class;
};

GType mcd_controller_get_type (void);
McdController *mcd_controller_new (void);
void mcd_controller_shutdown (McdController *controller, const gchar *reason);
void mcd_controller_cancel_shutdown (McdController *controller);

G_END_DECLS

#endif /* MCD_CONTROLLER_H */
