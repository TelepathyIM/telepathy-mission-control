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

#ifndef __MCD_CONNECTION_H__
#define __MCD_CONNECTION_H__

#include <glib.h>
#include <glib-object.h>
#include <libmissioncontrol/mc-account.h>
#include <libtelepathy/tp-connmgr.h>

#include "mcd-operation.h"

G_BEGIN_DECLS
#define MCD_TYPE_CONNECTION            (mcd_connection_get_type())
#define MCD_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MCD_TYPE_CONNECTION, McdConnection))
#define MCD_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MCD_TYPE_CONNECTION, McdConnectionClass))
#define MCD_IS_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCD_TYPE_CONNECTION))
#define MCD_IS_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), MCD_TYPE_CONNECTION))
#define MCD_CONNECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_CONNECTION, McdConnectionClass))
    typedef struct
{
    McdOperation parent;
} McdConnection;

typedef struct
{
    McdOperationClass parent_class;
} McdConnectionClass;

#include "mcd-presence-frame.h"
#include "mcd-dispatcher.h"

GType mcd_connection_get_type (void);

McdConnection *mcd_connection_new (DBusGConnection * dbus_connection,
				   const gchar * bus_name,
				   TpConnMgr * tp_conn_mgr,
				   McAccount * account,
				   McdPresenceFrame * presence_frame,
				   McdDispatcher *dispatcher);

/* Return the connection's account */
const McAccount *mcd_connection_get_account (McdConnection * connection);

TelepathyConnectionStatus mcd_connection_get_connection_status (McdConnection
								* connection);

gboolean mcd_connection_request_channel (McdConnection *connection,
					 const struct mcd_channel_request *req,
					 GError ** error);

gboolean mcd_connection_cancel_channel_request (McdConnection *connection,
					       	guint operation_id,
						const gchar *requestor_client_id,
					       	GError **error);

gboolean mcd_connection_get_telepathy_details (McdConnection * id,
					       gchar ** ret_servname,
					       gchar ** ret_objpath);

gboolean mcd_connection_remote_avatar_changed (McdConnection *connection,
					       guint contact_id,
					       const gchar *token);
void mcd_connection_account_changed (McdConnection *connection);

void mcd_connection_close (McdConnection *connection);

G_END_DECLS
#endif /* __MCD_CONNECTION_H__ */
