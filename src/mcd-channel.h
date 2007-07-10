/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
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

#ifndef MCD_CHANNEL_H
#define MCD_CHANNEL_H

#include <glib.h>
#include <glib-object.h>
#include <libtelepathy/tp-chan.h>
#include <libtelepathy/tp-constants.h>

#include "mcd-mission.h"

G_BEGIN_DECLS

#define MCD_TYPE_CHANNEL         (mcd_channel_get_type ())
#define MCD_CHANNEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_CHANNEL, McdChannel))
#define MCD_CHANNEL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_CHANNEL, McdChannelClass))
#define MCD_IS_CHANNEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_CHANNEL))
#define MCD_IS_CHANNEL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_CHANNEL))
#define MCD_CHANNEL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_CHANNEL, McdChannelClass))

typedef struct _McdChannel McdChannel;
typedef struct _McdChannelClass McdChannelClass;

typedef enum
{
    MCD_CHANNEL_PENDING,     /* Telepathy channel is not yet created */
    MCD_CHANNEL_DISPATCHING, /* Telepathy channel is created and waiting dispatch */
    MCD_CHANNEL_DISPATCHED,  /* Channel has been dispatched to handler */
    MCD_CHANNEL_FAILED,      /* Channel could not be dispached to handler, dying */
    
} McdChannelStatus;

struct _McdChannel
{
    McdMission parent;
};

struct _McdChannelClass
{
    McdMissionClass parent_class;

    /* signals */
    void (*status_changed_signal) (McdChannel * channel,
				   McdChannelStatus status);
    void (*members_accepted_signal) (McdChannel *channel);
};

struct mcd_channel_request
{
    const gchar *account_name;
    const gchar *channel_type;
    guint channel_handle;
    const gchar *channel_handle_string;
    gint channel_handle_type;
    guint requestor_serial;
    const gchar *requestor_client_id;
};

GType mcd_channel_get_type (void);

McdChannel *mcd_channel_new (TpChan *channel,
			     const gchar *channel_object_path,
			     const gchar *channel_type,
			     guint channel_handle,
			     TelepathyHandleType channel_handle_type,
			     gboolean outgoing,
			     guint requestor_serial,
			     const gchar *requestor_client_id);

void mcd_channel_set_status (McdChannel *channel, McdChannelStatus status);
McdChannelStatus mcd_channel_get_status (McdChannel * channel);
gboolean mcd_channel_get_members_accepted (McdChannel *channel);
const gchar* mcd_channel_get_channel_type (McdChannel *channel);
GQuark mcd_channel_get_channel_type_quark (McdChannel *channel);
const gchar* mcd_channel_get_object_path (McdChannel *channel);
guint mcd_channel_get_handle (McdChannel *channel);
TelepathyHandleType mcd_channel_get_handle_type (McdChannel *channel);
gint mcd_channel_get_flags (McdChannel *channel);
GPtrArray* mcd_channel_get_members (McdChannel *channel);
const gchar *mcd_channel_get_name (McdChannel *channel);
gboolean mcd_channel_is_missed (McdChannel *channel);
gboolean mcd_channel_leave (McdChannel *channel, const gchar *message,
			    TelepathyChannelGroupChangeReason reason);

G_END_DECLS
#endif /* MCD_CHANNEL_H */
