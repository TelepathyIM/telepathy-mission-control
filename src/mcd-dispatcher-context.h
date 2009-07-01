/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
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

G_BEGIN_DECLS

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

void mcd_dispatcher_add_filter (McdDispatcher *dispatcher,
                                McdFilterFunc filter,
                                guint priority,
                                gpointer user_data);
void mcd_dispatcher_add_filters (McdDispatcher *dispatcher,
                                 const McdFilter *filters);

/* Context API section
 *
 * The use of gpointer is intentional; we want to make accessing the
 * internals of the context restricted to make it unlikely that
 * somebody shoots [him|her]self in the foot while doing fancy
 * tricks. This also minimizes the amount of necessary includes.
 */

/* Getters */

McdDispatcher* mcd_dispatcher_context_get_dispatcher (McdDispatcherContext * ctx);

TpChannel *mcd_dispatcher_context_get_channel_object (McdDispatcherContext * ctx) G_GNUC_DEPRECATED;

TpConnection *mcd_dispatcher_context_get_connection_object (McdDispatcherContext * ctx) G_GNUC_DEPRECATED;

McdChannel * mcd_dispatcher_context_get_channel (McdDispatcherContext * ctx);
const GList *mcd_dispatcher_context_get_channels
    (McdDispatcherContext *context);
McdChannel *mcd_dispatcher_context_get_channel_by_type
    (McdDispatcherContext *context, GQuark type);

McdConnection *mcd_dispatcher_context_get_connection
    (McdDispatcherContext *context);

void mcd_dispatcher_context_close_all (McdDispatcherContext *context,
                                       TpChannelGroupChangeReason reason,
                                       const gchar *message);

void mcd_dispatcher_context_destroy_all (McdDispatcherContext *context);

void mcd_dispatcher_context_forget_all (McdDispatcherContext *context);

/* Statemachine API section */

/* Will step through the state machine.
 * @param ctx: The context
 * @param result: The return code
 */

void mcd_dispatcher_context_proceed (McdDispatcherContext *context);

void mcd_dispatcher_context_process (McdDispatcherContext * ctx, gboolean result);

const gchar *mcd_dispatcher_context_get_protocol_name (McdDispatcherContext *) G_GNUC_DEPRECATED;

G_END_DECLS

#endif
