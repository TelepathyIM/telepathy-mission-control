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

#ifndef MCD_PRESENCE_FRAME_H
#define MCD_PRESENCE_FRAME_H

#include <glib.h>
#include <glib-object.h>
#include <libmissioncontrol/mc-account.h>
#include <libmissioncontrol/mission-control.h>

#include "mcd-mission.h"

G_BEGIN_DECLS
#define MCD_TYPE_PRESENCE_FRAME         (mcd_presence_frame_get_type ())
#define MCD_PRESENCE_FRAME(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_PRESENCE_FRAME, McdPresenceFrame))
#define MCD_PRESENCE_FRAME_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_PRESENCE_FRAME, McdPresenceFrameClass))
#define MCD_IS_PRESENCE_FRAME(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_PRESENCE_FRAME))
#define MCD_IS_PRESENCE_FRAME_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_PRESENCE_FRAME))
#define MCD_PRESENCE_FRAME_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_PRESENCE_FRAME, McdPresenceFrameClass))
typedef struct _McdPresenceFrame McdPresenceFrame;
typedef struct _McdPresenceFrameClass McdPresenceFrameClass;

#include "mcd-account.h"
#include "mcd-account-manager.h"

struct _McdPresenceFrame
{
    McdMission parent;
};

struct _McdPresenceFrameClass
{
    McdMissionClass parent_class;

    /* Signals */
    void (*presence_requested_signal) (McdPresenceFrame * presence_frame,
				       McPresence presence,
				       const gchar * presence_message);
    
    /* Account specific signals */
    void (*presence_set_signal) (McdPresenceFrame * presence_frame,
				 McdAccount * account,
				 McPresence presence,
				 const gchar * presence_message);
    void (*status_changed_signal) (McdPresenceFrame * presence_frame,
				   McdAccount * account,
				   TpConnectionStatus connection_status,
				   TpConnectionStatusReason
				   connection_reason);
    
    /* Aggregate signals */
    void (*presence_actual_signal) (McdPresenceFrame * presence_frame,
				    McPresence presence,
				    const gchar * presence_message);
    void (*status_actual_signal) (McdPresenceFrame * presence_frame,
				  TpConnectionStatus status);
};

GType mcd_presence_frame_get_type (void);
McdPresenceFrame *mcd_presence_frame_new (void);

void mcd_presence_frame_request_presence (McdPresenceFrame * presence_frame,
					  McPresence
					  presence,
					  const gchar * presence_message);

McPresence mcd_presence_frame_get_requested_presence
    (McdPresenceFrame * presence_frame);
const gchar *mcd_presence_frame_get_requested_presence_message
    (McdPresenceFrame * presence_frame);

McPresence mcd_presence_frame_get_actual_presence
    (McdPresenceFrame * presence_frame);
const gchar *mcd_presence_frame_get_actual_presence_message
    (McdPresenceFrame * presence_frame);

void mcd_presence_frame_set_account_presence (McdPresenceFrame *
					      presence_frame,
					      McAccount * account,
					      McPresence
					      presence,
					      const gchar * presence_message);

McPresence mcd_presence_frame_get_account_presence
    (McdPresenceFrame * presence_frame, McAccount * account);
const gchar *mcd_presence_frame_get_account_presence_message
    (McdPresenceFrame * presence_frame, McAccount * account);
TpConnectionStatus mcd_presence_frame_get_account_status
    (McdPresenceFrame * presence_frame, McdAccount * account);
TpConnectionStatusReason mcd_presence_frame_get_account_status_reason
    (McdPresenceFrame * presence_frame, McdAccount * account);

void mcd_presence_frame_set_accounts (McdPresenceFrame * presence_frame,
				      const GList * accounts);

gboolean mcd_presence_frame_add_account (McdPresenceFrame * presence_frame,
                                         McdAccount * account);
gboolean mcd_presence_frame_remove_account (McdPresenceFrame * presence_frame,
                                         McdAccount * account);

gboolean mcd_presence_frame_cancel_last_request (McdPresenceFrame *
						 presence_frame);

gboolean mcd_presence_frame_is_stable (McdPresenceFrame *presence_frame);

void mcd_presence_frame_set_account_manager (McdPresenceFrame *presence_frame,
					     McdAccountManager *account_manager);

G_END_DECLS
#endif /* MCD_PRESENCE_FRAME_H */
