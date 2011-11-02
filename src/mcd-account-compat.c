/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
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
#include <config.h>

#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-manager.h"
#include "mcd-misc.h"
#include "mcd-service.h"

#define COMPAT_REQ_DATA "compat_req"

typedef struct
{
    guint requestor_serial;
    gchar *requestor_client_id;
} McdAccountCompatReq;

static void
emit_compat_property_changed (McdAccount *account, const gchar *key,
			      const GValue *value)
{
    GHashTable *properties;

    properties = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_insert (properties, (gpointer)key, (gpointer)value);

    mc_svc_account_interface_compat_emit_compat_property_changed (account,
                                                                  properties);
    g_hash_table_unref (properties);
}

static void
get_avatar_file (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    gchar *string;

    string = _mcd_account_get_avatar_filename (account);
    g_value_init (value, G_TYPE_STRING);
    g_value_take_string (value, string);
}

static gboolean
set_secondary_vcard_fields (TpSvcDBusProperties *self, const gchar *name,
                            const GValue *value, GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdStorage *storage = _mcd_account_get_storage (account);
    const gchar *account_name = mcd_account_get_unique_name (account);
    GStrv fields;
    const GValue *set = NULL;

    /* FIXME: some sort of validation beyond just the type? */

    if (!G_VALUE_HOLDS (value, G_TYPE_STRV))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Expected string-array for SecondaryVCardFields, but "
                     "got %s", G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    fields = g_value_get_boxed (value);

    if (fields != NULL)
      set = value;

    mcd_storage_set_value (storage, account_name, name, set, FALSE);
    mcd_storage_commit (storage, account_name);

    emit_compat_property_changed (account, name, value);

    return TRUE;
}

static void
get_secondary_vcard_fields (TpSvcDBusProperties *self, const gchar *name,
			    GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdStorage *storage = _mcd_account_get_storage (account);
    const gchar *account_name = mcd_account_get_unique_name (account);
    GValue *fetched;

    g_value_init (value, G_TYPE_STRV);
    fetched =
      mcd_storage_dup_value (storage, account_name, name, G_TYPE_STRV, NULL);

    if (fetched != NULL)
    {
        GStrv fields = g_value_get_boxed (fetched);

        g_value_take_boxed (value, fields);
        g_slice_free (GValue, fetched);
        fetched = NULL;
    }
    else
    {
        g_value_take_boxed (value, NULL);
    }
}


const McdDBusProp account_compat_properties[] = {
    { "AvatarFile", NULL, get_avatar_file },
    { "SecondaryVCardFields", set_secondary_vcard_fields, get_secondary_vcard_fields },
    { 0 },
};

static void
compat_set_has_been_online (McSvcAccountInterfaceCompat *iface,
                            DBusGMethodInvocation *context)
{
    _mcd_account_set_has_been_online (MCD_ACCOUNT (iface));
    mc_svc_account_interface_compat_return_from_set_has_been_online (context);
}

void
account_compat_iface_init (McSvcAccountInterfaceCompatClass *iface,
                           gpointer iface_data)
{
#define IMPLEMENT(x) mc_svc_account_interface_compat_implement_##x (\
    iface, compat_##x)
    IMPLEMENT (set_has_been_online);
#undef IMPLEMENT
}
