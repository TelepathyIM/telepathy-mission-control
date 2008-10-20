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

#ifndef __MCD_CONNECTION_H__
#define __MCD_CONNECTION_H__

#include <glib.h>
#include <glib-object.h>
#include <telepathy-glib/connection-manager.h>

#include "mcd-operation.h"

G_BEGIN_DECLS
#define MCD_TYPE_CONNECTION            (mcd_connection_get_type())
#define MCD_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MCD_TYPE_CONNECTION, McdConnection))
#define MCD_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MCD_TYPE_CONNECTION, McdConnectionClass))
#define MCD_IS_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCD_TYPE_CONNECTION))
#define MCD_IS_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), MCD_TYPE_CONNECTION))
#define MCD_CONNECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_CONNECTION, McdConnectionClass))

typedef struct _McdConnection McdConnection;
typedef struct _McdConnectionPrivate McdConnectionPrivate;
typedef struct _McdConnectionClass McdConnectionClass;

struct _McdConnection
{
    McdOperation parent;
    McdConnectionPrivate *priv;
};

struct _McdConnectionClass
{
    McdOperationClass parent_class;
};

#include "mcd-dispatcher.h"

GType mcd_connection_get_type (void);

McdConnection *mcd_connection_new (TpDBusDaemon *dbus_daemon,
				   const gchar * bus_name,
				   TpConnectionManager * tp_conn_mgr,
				   McdAccount * account,
				   McdDispatcher *dispatcher);

const gchar *mcd_connection_get_object_path (McdConnection *connection);

/* Return the connection's account */
McdAccount *mcd_connection_get_account (McdConnection * connection);

TpConnectionStatus mcd_connection_get_connection_status (McdConnection *connection);
TpConnectionStatusReason mcd_connection_get_connection_status_reason (McdConnection *connection);

gboolean mcd_connection_request_channel (McdConnection *connection,
					 McdChannel *channel,
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
void mcd_connection_restart (McdConnection *connection);

void mcd_connection_connect (McdConnection *connection, GHashTable *params);
void mcd_connection_close (McdConnection *connection);

G_END_DECLS
#endif /* __MCD_CONNECTION_H__ */
