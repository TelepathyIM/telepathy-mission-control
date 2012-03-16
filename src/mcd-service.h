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

#ifndef MCD_OBJECT_H
#define MCD_OBJECT_H

#include <glib.h>
#include <dbus/dbus-glib.h>
#include "mcd-master.h"

#define MCD_TYPE_SERVICE            (mcd_service_get_type ())

#define MCD_OBJECT(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
                                        ((obj), MCD_TYPE_SERVICE, \
					McdService))

#define MCD_OBJECT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
                                        ((klass), MCD_TYPE_SERVICE, \
					McdServiceClass))

#define MCD_IS_SERVICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
                                        ((obj), MCD_TYPE_SERVICE))

#define MCD_IS_SERVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
                                        ((klass), MCD_TYPE_SERVICE))

#define MCD_OBJECT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
				        ((obj), MCD_TYPE_SERVICE, \
					 McdServiceClass))

typedef struct _McdService McdService;
typedef struct _McdServiceClass McdServiceClass;

struct _McdServiceClass
{
    McdMasterClass parent_class;
};


struct _McdService
{
    McdMaster parent;
    GMainLoop *main_loop;
};

GType mcd_service_get_type (void);

McdService *mcd_service_new (void);
void mcd_service_run (McdService * self);
void mcd_service_stop (McdService * self);

#endif
