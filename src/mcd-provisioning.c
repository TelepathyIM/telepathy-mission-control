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
