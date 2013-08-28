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

#ifndef MCD_CHANNEL_H
#define MCD_CHANNEL_H

#include <glib.h>
#include <glib-object.h>
#include <telepathy-glib/telepathy-glib.h>

#include "mcd-mission.h"

G_BEGIN_DECLS

#define MCD_TYPE_CHANNEL         (mcd_channel_get_type ())
#define MCD_CHANNEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_CHANNEL, McdChannel))
#define MCD_CHANNEL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_CHANNEL, McdChannelClass))
#define MCD_IS_CHANNEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_CHANNEL))
#define MCD_IS_CHANNEL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_CHANNEL))
#define MCD_CHANNEL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_CHANNEL, McdChannelClass))

typedef struct _McdChannel McdChannel;
typedef struct _McdChannelPrivate McdChannelPrivate;
typedef struct _McdChannelClass McdChannelClass;

#include "mcd-account.h"

typedef enum
{
    MCD_CHANNEL_STATUS_UNDISPATCHED, /* used for channels created in the
                                        NewChannel signal before the connection
                                        is ready */
    MCD_CHANNEL_STATUS_REQUEST,      /* Telepathy channel is not yet created */
    MCD_CHANNEL_STATUS_REQUESTED,    /* Channel has been requested from the CM
                                      */
    MCD_CHANNEL_STATUS_DISPATCHING,  /* Telepathy channel is created and
                                        waiting dispatch */
    MCD_CHANNEL_STATUS_HANDLER_INVOKED,
    MCD_CHANNEL_STATUS_DISPATCHED,   /* Channel has been dispatched to handler
                                      */
    MCD_CHANNEL_STATUS_FAILED,       /* Channel creation failed, or channel
                                        could not be dispached to a handler */
    MCD_CHANNEL_STATUS_ABORTED,      /* Channel has been aborted */
} McdChannelStatus;

struct _McdChannel
{
    McdMission parent;
    McdChannelPrivate *priv;
};

struct _McdChannelClass
{
    McdMissionClass parent_class;

    /* signals */
    void (*status_changed_signal) (McdChannel * channel,
				   McdChannelStatus status);
};

GType mcd_channel_get_type (void);

McdChannel *
mcd_channel_new_from_properties (TpConnection *connection,
                                 const gchar *object_path,
                                 const GHashTable *properties);
McdChannel *mcd_channel_new_from_path (TpConnection *connection,
                                       const gchar *object_path,
                                       const gchar *type, guint handle,
                                       TpHandleType handle_type);

McdChannelStatus mcd_channel_get_status (McdChannel * channel);
const gchar* mcd_channel_get_object_path (McdChannel *channel);
gboolean mcd_channel_is_requested (McdChannel *channel);
McdAccount *mcd_channel_get_account (McdChannel *channel);
TpChannel *mcd_channel_get_tp_channel (McdChannel *channel);

void mcd_channel_take_error (McdChannel *channel, GError *error);
const GError *mcd_channel_get_error (McdChannel *channel);

GVariant *mcd_channel_dup_immutable_properties (McdChannel *channel);

G_END_DECLS
#endif /* MCD_CHANNEL_H */
