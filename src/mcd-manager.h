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

#ifndef MCD_MANAGER_H
#define MCD_MANAGER_H

#include <glib.h>
#include <glib-object.h>
#include <libmissioncontrol/mc-account.h>
#include <libmissioncontrol/mc-manager.h>

#include "mcd-connection.h"
#include "mcd-operation.h"
#include "mcd-presence-frame.h"
#include "mcd-dispatcher.h"

G_BEGIN_DECLS

#define MCD_TYPE_MANAGER         (mcd_manager_get_type ())
#define MCD_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_MANAGER, McdManager))
#define MCD_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_MANAGER, McdManagerClass))
#define MCD_IS_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_MANAGER))
#define MCD_IS_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_MANAGER))
#define MCD_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_MANAGER, McdManagerClass))

typedef struct _McdManager McdManager;
typedef struct _McdManagerClass McdManagerClass;

struct _McdManager
{
    McdOperation parent;
};

struct _McdManagerClass
{
    McdOperationClass parent_class;

    /* signals */
    void (*account_added_signal) (McdManager * manager, McAccount * account);
    void (*account_removed_signal) (McdManager * manager,
				    McAccount * account);
};

GType mcd_manager_get_type (void);
McdManager *mcd_manager_new (McManager * mc_manager,
			     McdPresenceFrame * pframe,
			     McdDispatcher *dispatcher,
			     TpDBusDaemon *dbus_daemon);

gboolean mcd_manager_add_account (McdManager * manager, McAccount * account);
gboolean mcd_manager_can_handle_account (McdManager * manager,
					 McAccount *account);
McAccount* mcd_manager_get_account_by_name (McdManager * manager,
					       const gchar * account_name);
gboolean mcd_manager_remove_account (McdManager * manager,
				     McAccount * account);
const GList *mcd_manager_get_accounts (McdManager * manager);
McdConnection *mcd_manager_get_account_connection (McdManager * manager,
						   McAccount * account);
gboolean mcd_manager_request_channel (McdManager *manager,
				      const struct mcd_channel_request *req,
				      GError ** error);

gboolean mcd_manager_cancel_channel_request (McdManager *manager, guint operation_id,
					     const gchar *requestor_client_pid, GError **error);

McdConnection *mcd_manager_get_connection (McdManager *manager,
					   const gchar *object_path);
void mcd_manager_reconnect_account (McdManager *manager, McAccount *account);

G_END_DECLS
#endif /* MCD_MANAGER_H */
