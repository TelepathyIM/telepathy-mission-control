/*
 * mc-account-avatar.c - Telepathy Account D-Bus interface (client side)
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

struct _McAccountAvatarProps {
    GArray *avatar;
    gchar *mime_type;
};

#define MC_ACCOUNT_IS_READY(account) (MC_ACCOUNT(account)->priv->props != NULL)

void
_mc_account_avatar_props_free (McAccountAvatarProps *props)
{
    g_free (props->mime_type);
    if (props->avatar)
	g_array_free (props->avatar, TRUE);
}

static void
update_property (gpointer key, gpointer ht_value, gpointer user_data)
{
    McAccount *account = user_data;
    McAccountAvatarProps *props = account->priv->avatar_props;
    GValue *value = ht_value;
    const gchar *name = key;
    GValueArray *va;
    GType type;

    g_debug ("%s: got property %s", G_STRFUNC, name);
    if (strcmp (name, "Avatar") == 0)
    {
	g_free (props->mime_type);
	if (props->avatar)
	    g_array_free (props->avatar, TRUE);
	va = g_value_get_boxed (value);
	props->avatar = g_value_get_boxed (va->values);
	props->mime_type = g_value_dup_string (va->values + 1);
	/* HACK: clear the GValue so that the GArray will not be freed */
	type = G_VALUE_TYPE (va->values); 
	memset (va->values, 0, sizeof (GValue)); 
	g_value_init (va->values, type); 
    }
}

static void
create_props (McAccount *account, GHashTable *props)
{
    McAccountPrivate *priv = account->priv;

    priv->avatar_props = g_malloc0 (sizeof (McAccountAvatarProps));
    g_hash_table_foreach (props, update_property, account);
}

void
mc_account_avatar_call_when_ready (McAccount *account, McAccountWhenReadyCb callback,
				   gpointer user_data)
{
    McAccountIfaceData iface_data;

    iface_data.name = MC_IFACE_ACCOUNT_INTERFACE_AVATAR;
    iface_data.props_data_ptr = (gpointer *)&account->priv->avatar_props;
    iface_data.create_props = create_props;

    _mc_account_call_when_ready_int (account, callback, user_data, &iface_data);
}

void
mc_account_avatar_get (McAccount *account,
		       const gchar **avatar, gsize *length,
		       const gchar **mime_type)
{
    McAccountAvatarProps *props;

    g_return_if_fail (MC_IS_ACCOUNT (account));
    props = account->priv->avatar_props;
    if (G_UNLIKELY (!props))
    {
	*avatar = NULL;
	*length = 0;
	*mime_type = NULL;
	return;
    }

    *avatar = props->avatar->data;
    *length = props->avatar->len;
    *mime_type = props->mime_type;
}

