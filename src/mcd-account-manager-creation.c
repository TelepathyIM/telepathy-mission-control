/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
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

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <config.h>

#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>
#include "mcd-account.h"
#include "mcd-account-manager.h"
#include "mcd-account-manager-creation.h"
#include "_gen/interfaces.h"

typedef struct
{
    McdAccount *account;
    GError *error;
} McdCreationData;

const McdDBusProp account_manager_creation_properties[] = {
    { 0 },
};


static void
set_property (gpointer key, gpointer val, gpointer userdata)
{
    McdCreationData *cd = userdata;
    gchar *name = key, *dot, *iface, *pname;
    GValue *value = val;

    if (cd->error) return;

    if ((dot = strrchr (name, '.')) != NULL)
    {
	iface = g_strndup (name, dot - name);
	pname = dot + 1;
	mcd_dbusprop_set_property (TP_SVC_DBUS_PROPERTIES (cd->account),
				   iface, pname, value, &cd->error);
	g_free (iface);
    }
    else
    {
	g_set_error (&cd->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "Unrecognized property: %s", name);
    }
}

static void
account_manager_create_account (McSvcAccountManagerInterfaceCreation *self,
				const gchar *manager,
				const gchar *protocol,
				const gchar *display_name,
				GHashTable *parameters,
				GHashTable *properties,
				DBusGMethodInvocation *context)
{
    McdCreationData cd;
    GError *error = NULL;
    const gchar *object_path;

    cd.error = NULL;
    cd.account =
	mcd_account_manager_create_account (MCD_ACCOUNT_MANAGER (self),
					    manager, protocol, display_name,
					    parameters, &object_path, &error);
    if (!cd.account)
    {
	if (!error)
	    g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
			 "Internal error");
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }

    g_hash_table_foreach (properties, set_property, &cd);
    if (cd.error)
    {
	dbus_g_method_return_error (context, cd.error);
	g_error_free (cd.error);
	return;
    }
    mc_svc_account_manager_interface_creation_return_from_create_account (context, object_path);
}


void
account_manager_creation_iface_init (McSvcAccountManagerInterfaceCreationClass *iface,
				  gpointer iface_data)
{
#define IMPLEMENT(x) mc_svc_account_manager_interface_creation_implement_##x (\
    iface, account_manager_##x)
    IMPLEMENT(create_account);
#undef IMPLEMENT
}

