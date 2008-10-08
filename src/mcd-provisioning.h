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
#ifndef __MCD_PROVISIONING_H__
#define __MCD_PROVISIONING_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define MCD_TYPE_PROVISIONING            (mcd_provisioning_get_type ())
#define MCD_PROVISIONING(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MCD_TYPE_PROVISIONING, McdProvisioning))
#define MCD_IS_PROVISIONING(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCD_TYPE_PROVISIONING))
#define MCD_PROVISIONING_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MCD_TYPE_PROVISIONING, McdProvisioningIface))
#define MCD_PROVISIONING_ERROR           (mcd_provisioning_error_quark())

typedef struct _McdProvisioning      McdProvisioning;
typedef struct _McdProvisioningIface McdProvisioningIface;

typedef enum
{
    MCD_PROVISIONING_ERROR_NOT_FOUND,
    MCD_PROVISIONING_ERROR_NO_RESPONSE,
    MCD_PROVISIONING_ERROR_BAD_RESULT,
} McdProvisioningError;

typedef void (*McdProvisioningCallback) (McdProvisioning *prov,
					 GHashTable *parameters,
					 GError *error,
					 gpointer user_data);

struct _McdProvisioningIface {
    GTypeInterface g_iface;

    void (*request_parameters) (McdProvisioning *prov,
				const gchar *url,
				const gchar *username,
				const gchar *password,
				McdProvisioningCallback callback,
				gpointer user_data);
    void (*cancel_request) (McdProvisioning *prov,
			    McdProvisioningCallback callback,
			    gpointer user_data);
};

GQuark mcd_provisioning_error_quark     (void);
GType  mcd_provisioning_get_type        (void);

void mcd_provisioning_request_parameters (McdProvisioning *prov,
					  const gchar *url,
					  const gchar *username,
					  const gchar *password,
					  McdProvisioningCallback callback,
					  gpointer user_data);

void mcd_provisioning_cancel_request (McdProvisioning *prov,
				      McdProvisioningCallback callback,
				      gpointer user_data);
G_END_DECLS

#endif
