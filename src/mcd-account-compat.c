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

#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#include <libmcclient/mc-interfaces.h>

#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-compat.h"
#include "mcd-account-manager.h"
#include "mcd-misc.h"
#include "mcd-service.h"

#define COMPAT_REQ_DATA "compat_req"

static guint _mcd_account_signal_profile_set = 0;

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
    g_hash_table_destroy (properties);
}

static gboolean
set_profile (TpSvcDBusProperties *self, const gchar *name,
             const GValue *value, GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    const gchar *string, *unique_name;
    GKeyFile *keyfile;

    if (!G_VALUE_HOLDS_STRING (value))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Expected string for Profile, but got %s",
                     G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    /* FIXME: should we reject profile changes after account creation? */
    /* FIXME: some sort of validation beyond just the type? */

    keyfile = _mcd_account_get_keyfile (account);
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
    _mcd_account_write_conf (account);

    g_signal_emit (account, _mcd_account_signal_profile_set, 0);

    return TRUE;
}

static void
get_profile (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    const gchar *unique_name;
    GKeyFile *keyfile;
    gchar *string;

    keyfile = _mcd_account_get_keyfile (account);
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

    string = _mcd_account_get_avatar_filename (account);
    g_value_init (value, G_TYPE_STRING);
    g_value_take_string (value, string);
}

static gboolean
set_secondary_vcard_fields (TpSvcDBusProperties *self, const gchar *name,
                            const GValue *value, GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    const gchar *unique_name, **fields, **field;
    GKeyFile *keyfile;

    /* FIXME: some sort of validation beyond just the type? */

    if (!G_VALUE_HOLDS (value, G_TYPE_STRV))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Expected string-array for SecondaryVCardFields, but "
                     "got %s", G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    keyfile = _mcd_account_get_keyfile (account);
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
    _mcd_account_write_conf (account);

    emit_compat_property_changed (account, name, value);
    return TRUE;
}

static void
get_secondary_vcard_fields (TpSvcDBusProperties *self, const gchar *name,
			    GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    GKeyFile *keyfile;
    const gchar *unique_name;
    gchar **fields;

    keyfile = _mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    fields = g_key_file_get_string_list (keyfile, unique_name,
					 name, NULL, NULL);
    g_value_init (value, G_TYPE_STRV);
    g_value_take_boxed (value, fields);
}


const McdDBusProp account_compat_properties[] = {
    { "Profile", set_profile, get_profile },
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

/**
 * mcd_account_compat_get_profile:
 * @account: the #McdAccount.
 *
 * Returns: the #McProfile for the account. Unreference it when done.
 */
McProfile *
mcd_account_compat_get_mc_profile (McdAccount *account)
{
    const gchar *unique_name;
    GKeyFile *keyfile;
    gchar *profile_name;
    McProfile *profile = NULL;

    keyfile = _mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    profile_name = g_key_file_get_string (keyfile, unique_name,
                                          "Profile", NULL);
    if (profile_name)
    {
        profile = mc_profile_lookup (profile_name);
        g_free (profile_name);
    }
    return profile;
}

inline void
_mcd_account_compat_class_init (McdAccountClass *klass)
{
    _mcd_account_signal_profile_set =
	g_signal_new ("profile-set",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, g_cclosure_marshal_VOID__VOID,
		      G_TYPE_NONE, 0);
}

