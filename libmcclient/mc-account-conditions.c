/*
 * mc-account-conditions.c - Telepathy Account D-Bus interface (client side)
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

struct _McAccountConditionsProps {
    GHashTable *conditions;
};


void
_mc_account_conditions_props_free (McAccountConditionsProps *props)
{
    if (props->conditions)
	g_hash_table_destroy (props->conditions);
    g_free (props);
}

static void
update_property (gpointer key, gpointer ht_value, gpointer user_data)
{
    McAccount *account = user_data;
    McAccountConditionsProps *props = account->priv->conditions_props;
    GValue *value = ht_value;
    const gchar *name = key;

    if (strcmp (name, "Condition") == 0)
    {
	if (props->conditions)
	    g_hash_table_destroy (props->conditions);
	props->conditions = g_value_get_boxed (value);
	_mc_gvalue_stolen (value);
    }
}

static void
create_props (TpProxy *proxy, GHashTable *props)
{
    McAccount *account = MC_ACCOUNT (proxy);
    McAccountPrivate *priv = account->priv;

    priv->conditions_props = g_malloc0 (sizeof (McAccountConditionsProps));
    g_hash_table_foreach (props, update_property, account);
}

void
mc_account_conditions_call_when_ready (McAccount *account,
				       McAccountWhenReadyCb callback,
				       gpointer user_data)
{
    McIfaceData iface_data;

    iface_data.id = MC_IFACE_QUARK_ACCOUNT_INTERFACE_CONDITIONS;
    iface_data.props_data_ptr = (gpointer)&account->priv->conditions_props;
    iface_data.create_props = create_props;

    _mc_iface_call_when_ready_int ((TpProxy *)account,
				   (McIfaceWhenReadyCb)callback, user_data,
				   &iface_data);
}

const GHashTable *
mc_account_conditions_get (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);

    if (G_UNLIKELY (!account->priv->conditions_props)) return NULL;
    return account->priv->conditions_props->conditions;
}

