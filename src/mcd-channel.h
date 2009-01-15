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
    void (*_mc_reserved1) (void);
    void (*_mc_reserved2) (void);
    void (*_mc_reserved3) (void);
    void (*_mc_reserved4) (void);
    void (*_mc_reserved5) (void);
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
McdChannel *
mcd_channel_new_from_properties (TpConnection *connection,
                                 const gchar *object_path,
                                 const GHashTable *properties);
McdChannel *mcd_channel_new_from_path (TpConnection *connection,
                                       const gchar *object_path,
                                       const gchar *type, guint handle,
                                       TpHandleType handle_type);
McdChannel *mcd_channel_new_request (GHashTable *properties,
                                     guint64 user_time,
                                     const gchar *preferred_handler);

gboolean mcd_channel_set_object_path (McdChannel *channel,
                                      TpConnection *connection,
                                      const gchar *object_path)
    G_GNUC_DEPRECATED;
G_GNUC_INTERNAL
gboolean _mcd_channel_create_proxy (McdChannel *channel,
                                    TpConnection *connection,
                                    const gchar *object_path,
                                    const GHashTable *properties);

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
GPtrArray* mcd_channel_get_members (McdChannel *channel)
    G_GNUC_DEPRECATED;
const gchar *mcd_channel_get_name (McdChannel *channel);
const gchar *mcd_channel_get_inviter (McdChannel *channel);
guint mcd_channel_get_self_handle (McdChannel *channel);
gboolean mcd_channel_is_missed (McdChannel *channel);
gboolean mcd_channel_leave (McdChannel *channel, const gchar *message,
			    TpChannelGroupChangeReason reason)
    G_GNUC_DEPRECATED;
gboolean mcd_channel_is_requested (McdChannel *channel);
McdAccount *mcd_channel_get_account (McdChannel *channel);
TpChannel *mcd_channel_get_tp_channel (McdChannel *channel);

/* not exported: */
G_GNUC_INTERNAL
gboolean _mcd_channel_create_proxy_old (McdChannel *channel,
                                        TpConnection *connection,
                                        const gchar *object_path,
                                        const gchar *type, guint handle,
                                        TpHandleType handle_type);
G_GNUC_INTERNAL
GHashTable *_mcd_channel_get_immutable_properties (McdChannel *channel);

G_GNUC_INTERNAL
GPtrArray *_mcd_channel_details_build_from_list (GList *channels);
G_GNUC_INTERNAL
void _mcd_channel_details_free (GPtrArray *channels);

G_GNUC_INTERNAL
const gchar *_mcd_channel_get_target_id (McdChannel *channel);
G_GNUC_INTERNAL
GHashTable *_mcd_channel_get_requested_properties (McdChannel *channel);
G_GNUC_INTERNAL
const gchar *_mcd_channel_get_request_path (McdChannel *channel);
G_GNUC_INTERNAL
const GList *_mcd_channel_get_satisfied_requests (McdChannel *channel);
G_GNUC_INTERNAL
guint64 _mcd_channel_get_request_user_action_time (McdChannel *channel);
G_GNUC_INTERNAL
const gchar *_mcd_channel_get_request_preferred_handler (McdChannel *channel);
G_GNUC_INTERNAL
void _mcd_channel_set_request_use_existing (McdChannel *channel,
                                            gboolean use_existing);
G_GNUC_INTERNAL
gboolean _mcd_channel_get_request_use_existing (McdChannel *channel);

G_GNUC_INTERNAL
void _mcd_channel_copy_details (McdChannel *channel, McdChannel *source);
G_GNUC_INTERNAL
void _mcd_channel_set_request_proxy (McdChannel *channel, McdChannel *source);

G_GNUC_INTERNAL
void _mcd_channel_set_error (McdChannel *channel, GError *error);
G_GNUC_INTERNAL
const GError *_mcd_channel_get_error (McdChannel *channel);

G_END_DECLS
#endif /* MCD_CHANNEL_H */
