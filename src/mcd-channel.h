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

#ifndef MCD_CHANNEL_H
#define MCD_CHANNEL_H

#include <glib.h>
#include <glib-object.h>
#include <telepathy-glib/channel.h>

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

typedef enum
{
    MCD_CHANNEL_UNDISPATCHED, /* used for channels created in the NewChannel
                                 signal before the connection is ready */
    MCD_CHANNEL_NO_PROXY,     /* Telepathy channel is not yet created */
    MCD_CHANNEL_DISPATCHING, /* Telepathy channel is created and waiting dispatch */
    MCD_CHANNEL_DISPATCHED,  /* Channel has been dispatched to handler */
    MCD_CHANNEL_FAILED,      /* Channel creation failed, or channel could not
                                be dispached to a handler */
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

McdChannel *mcd_channel_new (TpChannel *channel,
			     const gchar *channel_type,
			     guint channel_handle,
			     TpHandleType channel_handle_type,
			     gboolean outgoing,
			     guint requestor_serial,
                             const gchar *requestor_client_id)
    G_GNUC_DEPRECATED;
McdChannel *mcd_channel_new_from_path (TpConnection *connection,
                                       const gchar *object_path,
                                       const gchar *type, guint handle,
                                       TpHandleType handle_type);
McdChannel *mcd_channel_new_request (GHashTable *properties);

gboolean mcd_channel_set_object_path (McdChannel *channel,
                                      TpConnection *connection,
                                      const gchar *object_path);

void mcd_channel_set_status (McdChannel *channel, McdChannelStatus status);
McdChannelStatus mcd_channel_get_status (McdChannel * channel);
gboolean mcd_channel_get_members_accepted (McdChannel *channel);
const gchar* mcd_channel_get_channel_type (McdChannel *channel);
GQuark mcd_channel_get_channel_type_quark (McdChannel *channel);
const gchar* mcd_channel_get_object_path (McdChannel *channel);
void mcd_channel_set_handle (McdChannel *channel, guint handle);
guint mcd_channel_get_handle (McdChannel *channel);
TpHandleType mcd_channel_get_handle_type (McdChannel *channel);
gint mcd_channel_get_flags (McdChannel *channel);
GPtrArray* mcd_channel_get_members (McdChannel *channel);
const gchar *mcd_channel_get_name (McdChannel *channel);
const gchar *mcd_channel_get_inviter (McdChannel *channel);
guint mcd_channel_get_self_handle (McdChannel *channel);
gboolean mcd_channel_is_missed (McdChannel *channel);
gboolean mcd_channel_leave (McdChannel *channel, const gchar *message,
			    TpChannelGroupChangeReason reason);

/* not exported: */
void _mcd_channel_set_immutable_properties (McdChannel *channel,
                                            GHashTable *properties);
GHashTable *_mcd_channel_get_immutable_properties (McdChannel *channel);

GPtrArray *_mcd_channel_details_build_from_list (GList *channels);
void _mcd_channel_details_free (GPtrArray *channels);

const gchar *_mcd_channel_get_target_id (McdChannel *channel);

void _mcd_channel_set_error (McdChannel *channel, GError *error);
const GError *_mcd_channel_get_error (McdChannel *channel);

G_END_DECLS
#endif /* MCD_CHANNEL_H */
