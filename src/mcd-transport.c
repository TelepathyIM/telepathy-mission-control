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

#include "config.h"

#include <glib.h>
#include "mcd-transport.h"
#include "mcd-enum-types.h"

/**
 * SECTION:mcd-transport
 * @title: McdTransportPlugin
 * @short_description: Interface to provide connectivity monitoring plugins.
 *
 * The #McdTransportPlugin interface is to be implemented by objects which can
 * provide information about connectivity status. Such an object can advertise
 * changes in connectivity by emitting the "status-changed" signal.
 *
 * To register an McdTransportPlugin into mission-control, a plugin should use
 * the mcd_plugin_register_transport() function.
 */

enum
{
    STATUS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
mcd_transport_plugin_base_init (gpointer iface)
{
    static gboolean initialized = FALSE;

    if (!initialized)
    {
        /**
         * McdTransportPlugin::status-changed:
         * @plugin: The #McdTransportPlugin.
         * @transport: The #McdTransport.
         * @status: the new status of the transport
         *
	 * Signals that the status of @transport has changed. Signalling
	 * MCD_TRANSPORT_STATUS_CONNECTED and MCD_TRANSPORT_STATUS_DISCONNECTED
	 * is mandatory.
         */
        signals[STATUS_CHANGED] = g_signal_new ("status-changed",
            G_TYPE_FROM_INTERFACE (iface),
            G_SIGNAL_RUN_FIRST,
            G_STRUCT_OFFSET (McdTransportPluginIface, status_changed),
            NULL, NULL, NULL,
            G_TYPE_NONE, 2,
            G_TYPE_POINTER, MCD_TYPE_TRANSPORT_STATUS);
        initialized = TRUE;
    }
}

GType
mcd_transport_plugin_get_type (void)
{
    static GType type = 0;

    if (!type) {
        static const GTypeInfo info = {
            sizeof (McdTransportPluginIface),
            mcd_transport_plugin_base_init,
            NULL, 
            NULL,
            NULL,
            NULL,
            0,
            0,
            NULL
        };
        type = g_type_register_static (G_TYPE_INTERFACE, 
                                       "McdTransportPlugin", &info, 0);
        g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
    }
    return type;
}

/**
 * mcd_transport_plugin_get_transports:
 * @plugin: the #McdTransportPlugin.
 *
 * Get a #list of all the transport known to the plugin. Transports which are
 * in disconnected status can be skipped from the return value.
 *
 * Returns: a #GList of all the known McdTransports. Must not be freed.
 */
const GList *
mcd_transport_plugin_get_transports (McdTransportPlugin *plugin)
{
    McdTransportPluginIface *iface;

    iface = MCD_TRANSPORT_PLUGIN_GET_IFACE (plugin);
    g_return_val_if_fail (iface->get_transports != NULL, NULL);
    return iface->get_transports (plugin);
}


/**
 * mcd_transport_plugin_check_conditions:
 * @plugin: the #McdTransportPlugin.
 * @transport: a #McdTransport.
 * @conditions: a #GHashTable with account conditions.
 *
 * Checks whether @transport satisfies the @conditions.
 *
 * Returns: %TRUE if all conditions are met, %FALSE otherwise.
 */
gboolean
mcd_transport_plugin_check_conditions (McdTransportPlugin *plugin,
				       McdTransport *transport,
				       const GHashTable *conditions)
{
    McdTransportPluginIface *iface;

    iface = MCD_TRANSPORT_PLUGIN_GET_IFACE (plugin);

    if (iface->check_conditions == NULL)
        return TRUE;
    else
        return iface->check_conditions (plugin, transport, conditions);
}

/**
 * mcd_transport_get_name:
 * @plugin: the #McdTransportPlugin.
 * @transport: a #McdTransport.
 *
 * Gets the name of @transport.
 *
 * Returns: the name of the transport.
 */
const gchar *
mcd_transport_get_name (McdTransportPlugin *plugin, McdTransport *transport)
{
    McdTransportPluginIface *iface;

    iface = MCD_TRANSPORT_PLUGIN_GET_IFACE (plugin);
    g_return_val_if_fail (iface->get_transport_name != NULL, NULL);
    return iface->get_transport_name (plugin, transport);
}

/**
 * mcd_transport_get_status:
 * @plugin: the #McdTransportPlugin.
 * @transport: a #McdTransport.
 *
 * Gets the status of @transport.
 *
 * Returns: the #McdTransportStatus of the transport.
 */
McdTransportStatus
mcd_transport_get_status (McdTransportPlugin *plugin, McdTransport *transport)
{
    McdTransportPluginIface *iface;

    iface = MCD_TRANSPORT_PLUGIN_GET_IFACE (plugin);
    g_return_val_if_fail (iface->get_transport_status != NULL,
        MCD_TRANSPORT_STATUS_DISCONNECTED);
    return iface->get_transport_status (plugin, transport);
}

