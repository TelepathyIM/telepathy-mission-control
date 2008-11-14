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

#ifndef MCD_DISPATCHER_H
#define MCD_DISPATCHER_H

#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>

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
    McdMission parent;
    McdDispatcherPrivate *priv;
};

struct _McdDispatcherClass
{
    McdMissionClass parent_class;

    /* signals */
    void (*channel_added_signal) (McdDispatcher *dispatcher,
				  McdChannel *channel);
    void (*channel_removed_signal) (McdDispatcher *dispatcher,
				    McdChannel *channel);
    void (*dispatched_signal) (McdDispatcher * dispatcher,
			       McdChannel * channel);
    void (*dispatch_failed_signal) (McdDispatcher * dispatcher,
				    McdChannel * channel,
				    GError *error);

    /* virtual methods */
    void (*send) (McdDispatcher * dispatcher, McdChannel *channel);
};

GType mcd_dispatcher_get_type (void);

McdDispatcher *mcd_dispatcher_new (TpDBusDaemon *dbus_daemon,
				   McdMaster * master);

gboolean mcd_dispatcher_send (McdDispatcher * dispatcher, McdChannel *channel);

gint mcd_dispatcher_get_channel_type_usage (McdDispatcher * dispatcher,
					    GQuark chan_type_quark);

/* retrieves the channel handlers' capabilities, in a format suitable for being
 * used as a parameter for the telepathy "AdvertiseCapabilities" method */
GPtrArray *mcd_dispatcher_get_channel_capabilities (McdDispatcher * dispatcher,
						    const gchar *protocol);

/* retrieves the channel handlers' capabilities, in a format suitable for being
 * used as a parameter for the telepathy "SetSelfCapabilities" method */
GPtrArray *mcd_dispatcher_get_channel_enhanced_capabilities (
    McdDispatcher * dispatcher,
    const gchar *protocol);

/* not exported */
void _mcd_dispatcher_add_request (McdDispatcher *dispatcher,
                                  McdAccount *account, McdChannel *channel);
void _mcd_dispatcher_send_channels (McdDispatcher *dispatcher,
                                    GList *channels, gboolean requested);

G_END_DECLS

#endif /* MCD_DISPATCHER_H */
