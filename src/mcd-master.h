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
#include <mcd-connection-plugin.h>

struct _McdMaster
{
    McdController parent;
};

struct _McdMasterClass
{
    McdControllerClass parent_class;
    McdManager *(*create_manager) (McdMaster *master,
                                   const gchar *unique_name);
    void (*_mc_reserved1) (void);
    void (*_mc_reserved2) (void);
    void (*_mc_reserved3) (void);
    void (*_mc_reserved4) (void);
    void (*_mc_reserved5) (void);
    void (*_mc_reserved6) (void);
};

GType mcd_master_get_type (void);
McdMaster *mcd_master_get_default (void);

McdDispatcher *mcd_master_get_dispatcher (McdMaster *master);
TpDBusDaemon *mcd_master_get_dbus_daemon (McdMaster *master);

void mcd_master_add_connection_parameter (McdMaster *master, const gchar *name,
					  const GValue *value);

gboolean mcd_master_has_low_memory (McdMaster *master);
void mcd_master_set_low_memory (McdMaster *master, gboolean low_memory);
void mcd_master_set_idle (McdMaster *master, gboolean idle);

G_END_DECLS
#endif /* MCD_MASTER_H */
