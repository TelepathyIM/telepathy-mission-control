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

#ifndef MCD_MASTER_H
#define MCD_MASTER_H

#include <glib.h>
#include <glib-object.h>
#include "mcd-controller.h"

G_BEGIN_DECLS
#define MCD_TYPE_MASTER         (mcd_master_get_type ())
#define MCD_MASTER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_MASTER, McdMaster))
#define MCD_MASTER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_MASTER, McdMasterClass))
#define MCD_IS_MASTER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_MASTER))
#define MCD_IS_MASTER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_MASTER))
#define MCD_MASTER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_MASTER, McdMasterClass))

typedef struct _McdMaster McdMaster;
typedef struct _McdMasterClass McdMasterClass;

#include <mcd-manager.h>
#include <mcd-connection.h>

struct _McdMaster
{
    McdController parent;
};

struct _McdMasterClass
{
    McdControllerClass parent_class;
};

struct mcd_channel_request;

GType mcd_master_get_type (void);
#define mcd_master_new()    mcd_master_get_default()
McdMaster *mcd_master_get_default (void);

McdManager *mcd_master_lookup_manager (McdMaster *master,
				       const gchar *unique_name);

void mcd_master_request_presence (McdMaster * master,
				  McPresence presence,
				  const gchar * presence_message);

McPresence mcd_master_get_actual_presence (McdMaster * master);
gchar *mcd_master_get_actual_presence_message (McdMaster * master);

McPresence mcd_master_get_requested_presence (McdMaster * master);
gchar *mcd_master_get_requested_presence_message (McdMaster * master);

gboolean mcd_master_set_default_presence (McdMaster * master,
					  const gchar *client_id);

void mcd_master_set_default_presence_setting (McdMaster *master,
					      McPresence presence);

TpConnectionStatus mcd_master_get_account_status (McdMaster * master,
						  gchar * account_name);

gboolean mcd_master_get_online_connection_names (McdMaster * master,
						 gchar *** connected_names);

gboolean mcd_master_get_account_connection_details (McdMaster * master,
						    const gchar * account_name,
						    gchar ** servname,
						    gchar ** objpath);

gboolean mcd_master_request_channel (McdMaster *master,
				     const struct mcd_channel_request *req,
				     GError ** error);

gboolean mcd_master_cancel_channel_request (McdMaster *master,
					    guint operation_id,
					    const gchar *requestor_client_id,
					    GError **error);

gboolean mcd_master_get_used_channels_count (McdMaster *master, guint chan_type,
					     guint * ret, GError ** error);
McdConnection *mcd_master_get_connection (McdMaster *master,
					  const gchar *object_path,
					  GError **error);
gboolean mcd_master_get_account_for_connection (McdMaster *master,
						const gchar *object_path,
						gchar **ret_unique_name,
						GError **error);

void mcd_master_add_connection_parameter (McdMaster *master, const gchar *name,
					  const GValue *value);
GHashTable * mcd_master_get_connection_parameters (McdMaster *master);

G_END_DECLS
#endif /* MCD_MASTER_H */
