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
#include <glib/gi18n.h>
#include <config.h>

#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>

#include <libmcclient/mc-interfaces.h>

#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-conditions.h"
#include "mcd-account-manager.h"


static void
store_condition (gpointer key, gpointer value, gpointer userdata)
{
    McdAccount *account = userdata;
    const gchar *name = key, *condition = value;
    const gchar *unique_name;
    gchar condition_key[256];
    GKeyFile *keyfile;

    keyfile = _mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    g_snprintf (condition_key, sizeof (condition_key), "condition-%s", name);
    g_key_file_set_string (keyfile, unique_name, condition_key, condition);
}

static gboolean
set_condition (TpSvcDBusProperties *self, const gchar *name,
               const GValue *value, GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    const gchar *unique_name;
    GKeyFile *keyfile;
    gchar **keys, **key;
    GHashTable *conditions;

    /* FIXME: some sort of validation beyond just the type? */

    if (!G_VALUE_HOLDS (value, TP_HASH_TYPE_STRING_STRING_MAP))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Expected a{s:s} for Condition, but got %s",
                     G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    unique_name = mcd_account_get_unique_name (account);
    conditions = g_value_get_boxed (value);

    if (_mcd_account_get_always_on (account))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                     "Account %s conditions cannot be changed",
                     unique_name);
        return FALSE;
    }

    keyfile = _mcd_account_get_keyfile (account);
    /* first, delete existing conditions */
    keys = g_key_file_get_keys (keyfile, unique_name, NULL, NULL);
    for (key = keys; *key != NULL; key++)
    {
	if (strncmp (*key, "condition-", 10) != 0) continue;
	g_key_file_remove_key (keyfile, unique_name,
			       *key, NULL);
    }
    g_strfreev (keys);

    g_hash_table_foreach (conditions, store_condition, account);

    _mcd_account_write_conf (account);
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
    const gchar *unique_name;
    GKeyFile *keyfile;
    gchar **keys, **key, *condition;
    GHashTable *conditions;

    keyfile = _mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    conditions = g_hash_table_new_full (g_str_hash, g_str_equal,
					g_free, g_free);
    keys = g_key_file_get_keys (keyfile, unique_name, NULL, NULL);
    for (key = keys; *key != NULL; key++)
    {
	if (strncmp (*key, "condition-", 10) != 0) continue;
	condition = g_key_file_get_string (keyfile, unique_name, *key, NULL);
        DEBUG ("Condition: %s = %s", *key, condition);
	g_hash_table_insert (conditions, g_strdup (*key + 10), condition);
    }
    g_strfreev (keys);
    return conditions;
}

