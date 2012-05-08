/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008 Nokia Corporation. 
 *
 * Contact: Alberto Mardegan  <alberto.mardegan@nokia.com>
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

#ifndef __MCD_TRANSPORT_PLUGIN_H__
#define __MCD_TRANSPORT_PLUGIN_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MCD_TYPE_TRANSPORT_PLUGIN             (mcd_transport_plugin_get_type ())
#define MCD_TRANSPORT_PLUGIN(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), MCD_TYPE_TRANSPORT_PLUGIN, McdTransportPlugin))
#define MCD_IS_TRANSPORT_PLUGIN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCD_TYPE_TRANSPORT_PLUGIN))
#define MCD_TRANSPORT_PLUGIN_GET_IFACE(obj)   (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MCD_TYPE_TRANSPORT_PLUGIN, McdTransportPluginIface))

typedef struct _McdTransportPlugin McdTransportPlugin;
typedef struct _McdTransportPluginIface McdTransportPluginIface;
typedef struct _McdTransport McdTransport;

typedef enum {
    MCD_TRANSPORT_STATUS_CONNECTED,
    MCD_TRANSPORT_STATUS_CONNECTING,
    MCD_TRANSPORT_STATUS_DISCONNECTED,
    MCD_TRANSPORT_STATUS_DISCONNECTING,
} McdTransportStatus;

struct _McdTransportPluginIface
{
    GTypeInterface g_iface;

    /* methods */
    const GList * (*get_transports) (McdTransportPlugin *plugin);
    gboolean (*check_conditions) (McdTransportPlugin *plugin,
				  McdTransport *transport,
				  const GHashTable *conditions);
    const gchar * (*get_transport_name) (McdTransportPlugin *plugin,
					 McdTransport *transport);
    McdTransportStatus (*get_transport_status) (McdTransportPlugin *plugin,
						McdTransport *transport);

    /* signals */
    void (*status_changed) (McdTransportPlugin *plugin, McdTransport *transport,
			    McdTransportStatus status);
};

GType mcd_transport_plugin_get_type (void) G_GNUC_CONST;

const GList *mcd_transport_plugin_get_transports (McdTransportPlugin *plugin);

gboolean mcd_transport_plugin_check_conditions (McdTransportPlugin *plugin,
						McdTransport *transport,
						const GHashTable *conditions);

const gchar *mcd_transport_get_name (McdTransportPlugin *plugin,
				     McdTransport *transport);
McdTransportStatus mcd_transport_get_status (McdTransportPlugin *plugin,
					     McdTransport *transport);

G_END_DECLS
#endif /* __MCD_TRANSPORT_PLUGIN_H__ */
