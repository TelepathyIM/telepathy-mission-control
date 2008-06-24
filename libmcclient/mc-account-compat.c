/*
 * mc-account-compat.c - Telepathy Account D-Bus interface (client side)
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Alberto Mardegan  <alberto.mardegan@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <string.h>
#include "mc-account.h"
#include "mc-account-priv.h"
#include "dbus-api.h"

struct _McAccountCompatProps {
    gchar *avatar_file;
    gchar *profile;
    const gchar **secondary_vcard_fields;
};


void
_mc_account_compat_props_free (McAccountCompatProps *props)
{
    g_free (props->profile);
    g_free (props->avatar_file);
    g_strfreev ((gchar **)props->secondary_vcard_fields);
    g_free (props);
}

static void
update_property (gpointer key, gpointer ht_value, gpointer user_data)
{
    McAccount *account = user_data;
    McAccountCompatProps *props = account->priv->compat_props;
    GValue *value = ht_value;
    const gchar *name = key;

    if (strcmp (name, "Profile") == 0)
    {
	g_free (props->profile);
	props->profile = g_value_dup_string (value);
    }
    else if (strcmp (name, "AvatarFile") == 0)
    {
	g_free (props->avatar_file);
	props->avatar_file = g_value_dup_string (value);
    }
    else if (strcmp (name, "SecondaryVCardFields") == 0)
    {
	g_strfreev ((gchar **)props->secondary_vcard_fields);
	props->secondary_vcard_fields = g_value_get_boxed (value);
	_mc_gvalue_stolen (value);
    }
}

static void
create_props (TpProxy *proxy, GHashTable *props)
{
    McAccount *account = MC_ACCOUNT (proxy);
    McAccountPrivate *priv = account->priv;

    priv->compat_props = g_malloc0 (sizeof (McAccountCompatProps));
    g_hash_table_foreach (props, update_property, account);
}

void
mc_account_compat_call_when_ready (McAccount *account, McAccountWhenReadyCb callback,
				   gpointer user_data)
{
    McIfaceData iface_data;

    iface_data.id = MC_IFACE_QUARK_ACCOUNT_INTERFACE_COMPAT;
    iface_data.props_data_ptr = (gpointer)&account->priv->compat_props;
    iface_data.create_props = create_props;

    _mc_iface_call_when_ready_int ((TpProxy *)account,
				   (McIfaceWhenReadyCb)callback, user_data,
				   &iface_data);
}

const gchar *
mc_account_compat_get_profile (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);

    if (G_UNLIKELY (!account->priv->compat_props)) return NULL;
    return account->priv->compat_props->profile;
}

const gchar *
mc_account_compat_get_avatar_file (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);

    if (G_UNLIKELY (!account->priv->compat_props)) return NULL;
    return account->priv->compat_props->avatar_file;
}

const gchar * const *
mc_account_compat_get_secondary_vcard_fields (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);

    if (G_UNLIKELY (!account->priv->compat_props)) return NULL;
    return account->priv->compat_props->secondary_vcard_fields;
}

TpProxyPendingCall *
mc_account_compat_set_profile (McAccount *account, const gchar *profile,
			       tp_cli_dbus_properties_callback_for_set callback,
			       gpointer user_data,
			       GDestroyNotify destroy,
			       GObject *weak_object)
{
    GValue value = { 0 };

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, profile);
    return tp_cli_dbus_properties_call_set (account, -1,
	MC_IFACE_ACCOUNT_INTERFACE_COMPAT, "Profile", &value,
	callback, user_data, destroy, weak_object);
}

TpProxyPendingCall *
mc_account_compat_set_secondary_vcard_fields (McAccount *account,
					      const gchar * const *fields,
					      tp_cli_dbus_properties_callback_for_set callback,
					      gpointer user_data,
					      GDestroyNotify destroy,
					      GObject *weak_object)
{
    GValue value = { 0 };

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    g_value_init (&value, G_TYPE_STRV);
    g_value_set_static_boxed (&value, fields);
    return tp_cli_dbus_properties_call_set (account, -1,
	MC_IFACE_ACCOUNT_INTERFACE_COMPAT, "SecondaryVCardFields", &value,
	callback, user_data, destroy, weak_object);
}

