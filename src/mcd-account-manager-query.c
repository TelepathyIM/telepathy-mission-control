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

#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>
#include "mcd-account.h"
#include "mcd-account-manager.h"
#include "mcd-account-manager-priv.h"
#include "mcd-account-priv.h"

typedef struct
{
    const gchar *name;
    const GValue *value;
} McdFindParam;

typedef struct
{
    gchar *iface;
    const gchar *name;
    const GValue *value;
} McdIfaceProperty;

typedef struct
{
    const gchar *manager;
    const gchar *protocol;
    guint requested_presence;
    const gchar *requested_status;
    guint current_presence;
    const gchar *current_status;
    GArray *params;
    GArray *properties;
    gint n_accounts;
    GPtrArray *accounts;
    GError *error;
} McdFindData;

static const gchar *supported_keywords[] = {
    "Manager", "Protocol",
    "RequestedPresence", "RequestedStatus", 
    "CurrentPresence", "CurrentStatus",
    NULL
}; 

static void
get_keywords (TpSvcDBusProperties *self, const gchar *name,
	      GValue *value)
{
    g_value_init (value, G_TYPE_STRV);
    g_value_set_static_boxed (value, supported_keywords);
}


const McdDBusProp account_manager_query_properties[] = {
    { "Keywords", NULL, get_keywords },
    { 0 },
};


static gboolean
match_account_parameter (McdAccount *account, const gchar *name,
			 const GValue *value)
{
    gboolean match = FALSE;
    McdStorage *storage = _mcd_account_get_storage (account);
    const gchar *account_name = mcd_account_get_unique_name (account);
    GType vtype = G_VALUE_TYPE (value);

    if (mcd_storage_has_value (storage, account_name, name))
    {
        GValue *conf = NULL;

        switch (vtype)
        {
          case G_TYPE_STRING:
          case G_TYPE_UINT:
          case G_TYPE_BOOLEAN:
            conf = mcd_storage_dup_value (storage, account_name, name, vtype,
                                          NULL);
            break;
          default:
            g_warning ("Unexpected type %s", G_VALUE_TYPE_NAME (value));
        }

        if (conf != NULL)
        {
            if (G_VALUE_TYPE (conf) == vtype)
            {
                switch (vtype)
                {
                  case G_TYPE_STRING:
                    match = g_strcmp0 (g_value_get_string (value),
                                       g_value_get_string (conf)) == 0;
                    break;

                  case G_TYPE_UINT:
                    match = g_value_get_uint (value) == g_value_get_uint (conf);
                    break;

                  case G_TYPE_BOOLEAN:
                    match =
                      g_value_get_boolean (value) == g_value_get_boolean (conf);
                    break;

                  default:
                    break;
                }
            }

            tp_g_value_slice_free (conf);
        }
    }

    return match;
}

static gboolean
match_account_property (McdAccount *account, McdIfaceProperty *prop)
{
    const gchar *unique_name;
    gboolean match = FALSE;
    GValue value = { 0 };
    GError *error = NULL;

    DEBUG ("prop %s, value type %s", prop->name, G_VALUE_TYPE_NAME (prop->value));
    unique_name = mcd_account_get_unique_name (account);
    mcd_dbusprop_get_property (TP_SVC_DBUS_PROPERTIES (account),
			       prop->iface, prop->name, &value,
			       &error);
    if (error)
    {
	g_warning ("%s on %s: %s", G_STRFUNC, unique_name, error->message);
	g_error_free (error);
	return FALSE;
    }

    if (G_VALUE_TYPE (&value) == G_VALUE_TYPE (prop->value))
    {
	switch (G_VALUE_TYPE (&value))
	{
	case G_TYPE_CHAR:
	case G_TYPE_UCHAR:
	case G_TYPE_BOOLEAN:
	case G_TYPE_INT:
	case G_TYPE_UINT:
	case G_TYPE_LONG:
	case G_TYPE_ULONG:
	case G_TYPE_INT64:
	case G_TYPE_UINT64:
	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
	case G_TYPE_POINTER:
	    /* this assumes the GValue was previously initialized to 0, which
	     * should always be the case */
	    match = (value.data[0].v_uint64 == prop->value->data[0].v_uint64);
	    break;
	case G_TYPE_STRING:
	    match = !tp_strdiff (g_value_get_string (&value),
				 g_value_get_string (prop->value));
	    break;
	default:
	    g_warning ("%s: unsupported value type: %s",
		       G_STRFUNC, G_VALUE_TYPE_NAME (&value));
	}
    }
    g_value_unset (&value);
    return match;
}

static void
find_accounts (gpointer key, gpointer value, gpointer userdata)
{
    McdAccount *account = MCD_ACCOUNT (value);
    McdFindData *fd = userdata;
    TpConnectionPresenceType presence;
    const gchar *object_path, *string, *status, *message;
    guint i;

    DEBUG ("%s", (gchar *)key);
    if (fd->manager)
    {
	string = mcd_account_get_manager_name (account);
	if (!string || strcmp (fd->manager, string) != 0) return;
    }
    if (fd->protocol)
    {
	string = mcd_account_get_protocol_name (account);
	if (!string || strcmp (fd->protocol, string) != 0) return;
    }
    if (fd->requested_presence > 0)
    {
	mcd_account_get_requested_presence (account, &presence,
					    &status, &message);
	if (fd->requested_presence != presence) return;
    }
    if (fd->requested_status)
    {
	mcd_account_get_requested_presence (account, &presence,
					    &status, &message);
	if (!status || strcmp (fd->requested_status, status) != 0) return;
    }
    if (fd->current_presence > 0)
    {
	mcd_account_get_current_presence (account, &presence,
					  &status, &message);
	if (fd->current_presence != presence) return;
    }
    if (fd->current_status)
    {
	mcd_account_get_current_presence (account, &presence,
					  &status, &message);
	if (!status || strcmp (fd->current_status, status) != 0) return;
    }

    DEBUG ("checking parameters");
    for (i = 0; i < fd->params->len; i++)
    {
	McdFindParam *param;
	param = &g_array_index (fd->params, McdFindParam, i);
	if (!match_account_parameter (account, param->name, param->value))
	    return;
    }

    DEBUG ("checking properties");
    for (i = 0; i < fd->properties->len; i++)
    {
	McdIfaceProperty *prop;
	prop = &g_array_index (fd->properties, McdIfaceProperty, i);
	if (!match_account_property (account, prop))
	    return;
    }
    object_path = mcd_account_get_object_path (account);
    DEBUG ("%s", object_path);
    g_ptr_array_add (fd->accounts, (gpointer)object_path);
}

static void
parse_query (gpointer key, gpointer val, gpointer userdata)
{
    McdFindData *fd = userdata;
    gchar *name = key, *dot;
    GValue *value = val;

    if (fd->error) return;

    if (strcmp (name, "Manager") == 0)
	fd->manager = g_value_get_string (value);
    else if (strcmp (name, "Protocol") == 0)
	fd->protocol = g_value_get_string (value);
    else if (strcmp (name, "RequestedPresence") == 0)
	fd->requested_presence = g_value_get_uint (value);
    else if (strcmp (name, "RequestedStatus") == 0)
	fd->requested_status = g_value_get_string (value);
    else if (strcmp (name, "CurrentPresence") == 0)
	fd->current_presence = g_value_get_uint (value);
    else if (strcmp (name, "CurrentStatus") == 0)
	fd->current_status = g_value_get_string (value);
    else if (strncmp (name, "param-", 6) == 0)
    {
	McdFindParam param;

	param.name = name;
	param.value = value;
	g_array_append_val (fd->params, param);
    }
    else if ((dot = strrchr (name, '.')) != NULL)
    {
	McdIfaceProperty prop;

	prop.iface = g_strndup (name, dot - name);
	prop.name = dot + 1;
	prop.value = value;
	g_array_append_val (fd->properties, prop);
    }
    else
    {
	g_set_error (&fd->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "Unrecognized query parameter: %s", name);
    }
}

static void
account_manager_find_accounts (McSvcAccountManagerInterfaceQuery *self,
			       GHashTable *query,
			       DBusGMethodInvocation *context)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (self);
    McdFindData fd;
    guint i;

    DEBUG ("called");
    memset (&fd, 0, sizeof (fd));
    fd.params = g_array_new (FALSE, FALSE, sizeof (McdFindParam));
    fd.properties = g_array_new (FALSE, FALSE, sizeof (McdIfaceProperty));

    /* break the hash table into the McdFindData struct, to avoid having to
     * iterate over it for every account */
    g_hash_table_foreach (query, parse_query, &fd);
    if (!fd.error)
    {
        GHashTable *accounts;

        fd.accounts = g_ptr_array_sized_new (16);
        accounts = _mcd_account_manager_get_accounts (account_manager);
        g_hash_table_foreach (accounts, find_accounts, &fd);
    }
    g_array_unref (fd.params);
    for (i = 0; i < fd.properties->len; i++)
    {
	McdIfaceProperty *prop;
	prop = &g_array_index (fd.properties, McdIfaceProperty, i);
	g_free (prop->iface);
    }
    g_array_unref (fd.properties);

    if (fd.error)
    {
	dbus_g_method_return_error (context, fd.error);
	g_error_free (fd.error);
	return;
    }

    mc_svc_account_manager_interface_query_return_from_find_accounts (context,
								      fd.accounts);
    g_ptr_array_unref (fd.accounts);
}


void
account_manager_query_iface_init (McSvcAccountManagerInterfaceQueryClass *iface,
				  gpointer iface_data)
{
#define IMPLEMENT(x) mc_svc_account_manager_interface_query_implement_##x (\
    iface, account_manager_##x)
    IMPLEMENT(find_accounts);
#undef IMPLEMENT
}

