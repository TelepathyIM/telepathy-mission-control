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

#ifndef MCD_DISPATCHER_H
#define MCD_DISPATCHER_H

#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

#define MCD_TYPE_DISPATCHER         (mcd_dispatcher_get_type ())
#define MCD_DISPATCHER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_DISPATCHER, McdDispatcher))
#define MCD_DISPATCHER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_DISPATCHER, McdDispatcherClass))
#define MCD_IS_DISPATCHER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_DISPATCHER))
#define MCD_IS_DISPATCHER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_DISPATCHER))
#define MCD_DISPATCHER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_DISPATCHER, McdDispatcherClass))

typedef struct _McdDispatcher McdDispatcher;
typedef struct _McdDispatcherClass McdDispatcherClass;
typedef struct _McdDispatcherPrivate McdDispatcherPrivate;

#include "mcd-channel.h"
#include "mcd-master.h"

struct _McdDispatcher
{
    GObject parent;
    McdDispatcherPrivate *priv;
};

struct _McdDispatcherClass
{
    GObjectClass parent_class;
    TpDBusPropertiesMixinClass dbus_properties_class;
};

GType mcd_dispatcher_get_type (void);

McdDispatcher *mcd_dispatcher_new (TpDBusDaemon *dbus_daemon,
				   McdMaster * master);

G_END_DECLS

#endif /* MCD_DISPATCHER_H */
