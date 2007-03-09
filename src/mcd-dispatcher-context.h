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
typedef void (*filter_func_t) (McdDispatcherContext * ctx);

/* Data structures and typedefs needed by pluginized filters */
typedef void (*abort_function_t) (McdDispatcherContext * ctx);

/* Requests the chain of filter functions for an unique combination of
 * channel types and filter flags.
 *
 * @param channel_type: A quark representing a particular channel type
 * @param filter_flags: The flags for the filter, such as incoming/outgoing
 * @return A NULL-terminated array of filter function pointers
 */

filter_func_t *mcd_dispatcher_get_filter_chain (McdDispatcher * dispatcher,
						GQuark channel_type_quark,
						guint filter_flags);


/* Indicates to Mission Control that we want to register a filter chain
 * for a unique combination of channel type/filter flags.
 *
 * @param channel_type_quark: Quark indicating the channel type
 * @param filter_flags: The flags for the filter, such as incoming/outgoing
 * @param chain: The chain of filter functions to register
 */

void mcd_dispatcher_register_filter_chain (McdDispatcher * dispatcher,
					   GQuark channel_type_quark,
					   guint filter_flags,
					   filter_func_t * chain);

/* Indicates to Mission Control that we will not want to have a filter chain
 * for particular unique channel type/filter flags combination anymore.
 *
 * @param channel_type_quark: Quark indicating the channel type
 * @param filter_flags: The flags for the filter, such as incoming/outgoing
 */
void mcd_dispatcher_unregister_filter_chain (McdDispatcher * dispatcher,
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

const TpChan *mcd_dispatcher_context_get_channel_object (McdDispatcherContext * ctx);

const TpConn * mcd_dispatcher_context_get_connection_object (McdDispatcherContext * ctx);

McdChannel * mcd_dispatcher_context_get_channel (McdDispatcherContext * ctx);

const McdConnection * mcd_dispatcher_context_get_connection (McdDispatcherContext * ctx);

McdChannelHandler * mcd_dispatcher_context_get_chan_handler (McdDispatcherContext * ctx);

/*Returns an array of the gchar *  addresses of participants in the channel*/
GPtrArray *mcd_dispatcher_context_get_members (McdDispatcherContext * ctx);

/*Filter-specifc data*/
gpointer mcd_dispatcher_context_get_data (McdDispatcherContext * ctx);


/* Setters */

/* Abort function should be known only to the filter function.  When
   executed, filter function MUST set an abort fn as needed (such as
   when implementing an async filter) */

void mcd_dispatcher_context_set_abort_fn (McdDispatcherContext * ctx, abort_function_t abort_fn);

void mcd_dispatcher_context_set_data (McdDispatcherContext * ctx, gpointer data);

/* Statemachine API section */

/* Will step through the state machine.
 * @param ctx: The context
 * @param result: The return code
 */

void mcd_dispatcher_context_process (McdDispatcherContext * ctx, gboolean result);

G_END_DECLS

#endif
