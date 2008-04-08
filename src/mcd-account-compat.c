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
#include "mcd-account-priv.h"
#include "mcd-account-compat.h"
#include "mcd-account-manager.h"
#include "_gen/interfaces.h"


static void
set_profile (TpSvcDBusProperties *self, const gchar *name,
	     const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    const gchar *string, *unique_name;
    GKeyFile *keyfile;

    keyfile = mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    string = g_value_get_string (value);
    if (string && string[0] != 0)
	g_key_file_set_string (keyfile, unique_name,
			       name, string);
    else
    {
	g_key_file_remove_key (keyfile, unique_name,
			       name, NULL);
    }
    mcd_account_manager_write_conf (keyfile);
}

static void
get_profile (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    const gchar *unique_name;
    GKeyFile *keyfile;
    gchar *string;

    keyfile = mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    string = g_key_file_get_string (keyfile, unique_name,
				    name, NULL);
    g_value_init (value, G_TYPE_STRING);
    g_value_take_string (value, string);
}

static void
get_avatar_file (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    gchar *string;

    string = mcd_account_get_avatar_filename (account);
    g_value_init (value, G_TYPE_STRING);
    g_value_take_string (value, string);
}

static void
set_secondary_vcard_fields (TpSvcDBusProperties *self, const gchar *name,
			    const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    const gchar *unique_name, **fields, **field;
    GKeyFile *keyfile;

    keyfile = mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    fields = g_value_get_boxed (value);
    if (fields)
    {
	gsize len;

	for (field = fields, len = 0; *field; field++, len++);
	g_key_file_set_string_list (keyfile, unique_name,
				    name, fields, len);
    }
    else
    {
	g_key_file_remove_key (keyfile, unique_name,
			       name, NULL);
    }
    mcd_account_manager_write_conf (keyfile);
}

static void
get_secondary_vcard_fields (TpSvcDBusProperties *self, const gchar *name,
			    GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    GKeyFile *keyfile;
    const gchar *unique_name;
    gchar **fields;

    keyfile = mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    fields = g_key_file_get_string_list (keyfile, unique_name,
					 name, NULL, NULL);
    g_value_init (value, G_TYPE_STRV);
    g_value_take_boxed (value, fields);
}


McdDBusProp account_compat_properties[] = {
    { "Profile", set_profile, get_profile },
    { "AvatarFile", NULL, get_avatar_file },
    { "SecondaryVCardFields", set_secondary_vcard_fields, get_secondary_vcard_fields },
    { 0 },
};

const McdDBusProp *
_mcd_account_compat_get_properties (void)
{
    return account_compat_properties;
}

void
_mcd_account_compat_iface_init (McSvcAccountInterfaceCompatClass *iface,
				gpointer iface_data)
{
}

