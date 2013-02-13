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

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-conditions.h"
#include "mcd-account-manager.h"


static void
store_condition (gpointer key, gpointer value, gpointer userdata)
{
    McdAccount *account = MCD_ACCOUNT (userdata);
    McdStorage *storage = _mcd_account_get_storage (account);
    const gchar *account_name = mcd_account_get_unique_name (account);
    const gchar *name = key, *condition = value;
    gchar condition_key[256];

    g_snprintf (condition_key, sizeof (condition_key), "condition-%s", name);
    mcd_storage_set_string (storage, account_name, condition_key, condition);
}

static gboolean
set_condition (TpSvcDBusProperties *self,
               const gchar *name,
               const GValue *value,
               McdDBusPropSetFlags flags,
               GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdStorage *storage = _mcd_account_get_storage (account);
    const gchar *account_name = mcd_account_get_unique_name (account);
    gchar **keys, **key;
    GHashTable *conditions;

    /* FIXME: some sort of validation beyond just the type? */

    if (!G_VALUE_HOLDS (value, TP_HASH_TYPE_STRING_STRING_MAP))
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "Expected a{s:s} for Condition, but got %s",
                     G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    if (_mcd_account_get_always_on (account))
    {
        g_set_error (error, TP_ERROR, TP_ERROR_PERMISSION_DENIED,
                     "Account %s conditions cannot be changed",
                     mcd_account_get_unique_name (account));
        return FALSE;
    }

    conditions = g_value_get_boxed (value);

    /* first, delete existing conditions */
    keys = mcd_storage_dup_attributes (storage, account_name, NULL);

    for (key = keys; *key != NULL; key++)
    {
        if (strncmp (*key, "condition-", 10) != 0)
            continue;

        mcd_storage_set_attribute (storage, account_name, *key, NULL);
    }

    g_strfreev (keys);

    if (!(flags & MCD_DBUS_PROP_SET_FLAG_ALREADY_IN_STORAGE))
    {
        g_hash_table_foreach (conditions, store_condition, account);

        mcd_storage_commit (storage, account_name);
    }

    return TRUE;
}

static void
get_condition (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    GHashTable *conditions;

    conditions = mcd_account_get_conditions (MCD_ACCOUNT (self));

    g_value_init (value, DBUS_TYPE_G_STRING_STRING_HASHTABLE);
    g_value_take_boxed (value, conditions);
}


const McdDBusProp account_conditions_properties[] = {
    { "Condition", set_condition, get_condition },
    { 0 },
};

void
account_conditions_iface_init (McSvcAccountInterfaceConditionsClass *iface,
			       gpointer iface_data)
{
}

GHashTable *mcd_account_get_conditions (McdAccount *account)
{
    gchar **keys, **key, *condition;
    GHashTable *conditions;
    McdStorage *storage = _mcd_account_get_storage (account);
    const gchar *account_name = mcd_account_get_unique_name (account);

    conditions = g_hash_table_new_full (g_str_hash, g_str_equal,
					g_free, g_free);

    keys = mcd_storage_dup_attributes (storage, account_name, NULL);

    for (key = keys; *key != NULL; key++)
    {
        if (strncmp (*key, "condition-", 10) != 0)
            continue;

        condition = mcd_storage_dup_string (storage, account_name, *key);
        DEBUG ("Condition: %s = %s", *key, condition);
        g_hash_table_insert (conditions, g_strdup (*key + 10), condition);
    }

    g_strfreev (keys);

    return conditions;
}

