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

#ifndef __MCD_DISPATCHER_CONTEXT_H__
#define __MCD_DISPATCHER_CONTEXT_H__

#include "mcd-dispatcher.h"
#include "mcd-connection.h"
#include "mcd-chan-handler.h"

G_BEGIN_DECLS

#define MCD_PLUGIN_INIT_FUNC  "mcd_filters_init"

/* Filter flag definitions */
#define MCD_FILTER_IN  1<<0
#define MCD_FILTER_OUT 1<<1

/* The context of the current filter chain execution. Should be kept
 * intact by filter implementation and passed transparently to
 * getters/setters and state machine functions
 */
typedef struct _McdDispatcherContext McdDispatcherContext;

/* Filter function type */
typedef void (*McdFilterFunc) (McdDispatcherContext * ctx, gpointer user_data);

/* Filter priorities */
#define MCD_FILTER_PRIORITY_CRITICAL 10000
#define MCD_FILTER_PRIORITY_SYSTEM   20000
#define MCD_FILTER_PRIORITY_USER     30000
#define MCD_FILTER_PRIORITY_NOTICE   40000
#define MCD_FILTER_PRIORITY_LOW	     50000

typedef struct filter_t {
    McdFilterFunc func;
    guint priority;
    gpointer user_data;
} McdFilter;

void mcd_dispatcher_register_filter (McdDispatcher *dispatcher,
				     McdFilterFunc filter,
				     GQuark channel_type_quark,
				     guint filter_flags,
				     guint priority,
				     gpointer user_data);

void mcd_dispatcher_unregister_filter (McdDispatcher *dispatcher,
				       McdFilterFunc filter,
				       GQuark channel_type_quark,
				       guint filter_flags);

void mcd_dispatcher_register_filters (McdDispatcher *dispatcher,
				      McdFilter *filters,
				      GQuark channel_type_quark,
				      guint filter_flags);

/* Context API section
 *
 * The use of gpointer is intentional; we want to make accessing the
 * internals of the context restricted to make it unlikely that
 * somebody shoots [him|her]self in the foot while doing fancy
 * tricks. This also minimizes the amount of necessary includes.
 */

/* Getters */

McdDispatcher* mcd_dispatcher_context_get_dispatcher (McdDispatcherContext * ctx);

TpChan *mcd_dispatcher_context_get_channel_object (McdDispatcherContext * ctx);

TpConn * mcd_dispatcher_context_get_connection_object (McdDispatcherContext * ctx);

McdChannel * mcd_dispatcher_context_get_channel (McdDispatcherContext * ctx);

McdConnection * mcd_dispatcher_context_get_connection (McdDispatcherContext * ctx);

McdChannelHandler * mcd_dispatcher_context_get_chan_handler (McdDispatcherContext * ctx);

/*Returns an array of the gchar *  addresses of participants in the channel*/
GPtrArray *mcd_dispatcher_context_get_members (McdDispatcherContext * ctx);


/* Statemachine API section */

/* Will step through the state machine.
 * @param ctx: The context
 * @param result: The return code
 */

void mcd_dispatcher_context_process (McdDispatcherContext * ctx, gboolean result);

const gchar *mcd_dispatcher_context_get_protocol_name (McdDispatcherContext *);

G_END_DECLS

#endif
