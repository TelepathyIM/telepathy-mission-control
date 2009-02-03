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
    GHashTable *properties;
    DBusGMethodInvocation *context;
} McdCreationData;

const McdDBusProp account_manager_creation_properties[] = {
    { 0 },
};


static inline void
mcd_creation_data_free (McdCreationData *cd)
{
    g_hash_table_unref (cd->properties);
    g_slice_free (McdCreationData, cd);
}

static void
create_account_cb (McdAccountManager *account_manager, McdAccount *account,
                   const GError *error, gpointer user_data)
{
    McdCreationData *cd = user_data;
    const gchar *object_path;
    GHashTableIter iter;
    gchar *name;
    GValue *value;
    GError *err = NULL;

    if (G_UNLIKELY (error))
    {
	dbus_g_method_return_error (cd->context, (GError *)error);
	return;
    }

    g_return_if_fail (MCD_IS_ACCOUNT (account));

    g_hash_table_iter_init (&iter, cd->properties);
    while (g_hash_table_iter_next (&iter, (gpointer)&name, (gpointer)&value) &&
           err == NULL)
    {
        gchar *dot, *iface, *pname;

        if ((dot = strrchr (name, '.')) != NULL)
        {
            iface = g_strndup (name, dot - name);
            pname = dot + 1;
            mcd_dbusprop_set_property (TP_SVC_DBUS_PROPERTIES (account),
                                       iface, pname, value, &err);
            g_free (iface);
        }
        else
            err = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                               "Unrecognized property: %s", name);
    }

    if (err)
    {
        dbus_g_method_return_error (cd->context, err);
        g_error_free (err);
	return;
    }
    object_path = mcd_account_get_object_path (account);
    mc_svc_account_manager_interface_creation_return_from_create_account
        (cd->context, object_path);
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
    McdCreationData *cd;

    cd = g_slice_new (McdCreationData);
    cd->properties = g_hash_table_ref (properties);
    cd->context = context;
    mcd_account_manager_create_account (MCD_ACCOUNT_MANAGER (self),
                                        manager, protocol, display_name,
                                        parameters, create_account_cb, cd,
                                        (GDestroyNotify)mcd_creation_data_free);
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

