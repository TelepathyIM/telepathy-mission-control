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

#include "mcd-provisioning.h"

GQuark 
mcd_provisioning_error_quark (void)
{
    static GQuark quark = 0;
    
    if (quark == 0) {
	quark = g_quark_from_static_string ("mcd-provisioning-error-quark");
    }
    return quark;
}

static void
mcd_provisioning_base_init (gpointer gclass)
{
    static gboolean initialized = FALSE;
    
    if (!initialized) {
	initialized = TRUE;
    }
}

GType
mcd_provisioning_get_type (void)
{
    static GType type = 0;
    
    if (!type) {
	static const GTypeInfo info = {
	    sizeof (McdProvisioningIface),
	    mcd_provisioning_base_init,
	    NULL, 
	    NULL,
	    NULL,
	    NULL,
	    0,
	    0,
	    NULL
	};
	type = g_type_register_static (G_TYPE_INTERFACE, 
				       "McdProvisioning", 
				       &info,
				       0);
	g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
    }
    return type;
}

/**
 * mcd_provisioning_request_parameters:
 * @prov: the #McdProvisioning object.
 * @url: URL of the provisioning server.
 * @username: username for connecting to the server.
 * @password: password for connecting to the server.
 * @callback: #McdProvisioningCallback which will receive the parameters.
 * @user_data: extra argument for @callback.
 *
 * Queries the provisioning service and registers the @callback function for
 * handling the result.
 */
void
mcd_provisioning_request_parameters (McdProvisioning *prov,
				     const gchar *url,
				     const gchar *username,
				     const gchar *password,
				     McdProvisioningCallback callback,
				     gpointer user_data)
{
    g_return_if_fail (MCD_IS_PROVISIONING (prov));

    MCD_PROVISIONING_GET_IFACE (prov)->request_parameters (prov, url,
							   username,
							   password,
							   callback,
							   user_data);
}

/**
 * mcd_provisioning_cancel_request:
 * @prov: the #McdProvisioning object.
 * @callback: #McdProvisioningCallback to disconnect.
 * @user_data: extra argument for @callback.
 *
 * Cancel a provisioning request, preventing @callback from being invoked.
 */
void
mcd_provisioning_cancel_request (McdProvisioning *prov,
				 McdProvisioningCallback callback,
				 gpointer user_data)
{
    g_return_if_fail (MCD_IS_PROVISIONING (prov));

    MCD_PROVISIONING_GET_IFACE (prov)->cancel_request (prov,
						       callback, user_data);
}

