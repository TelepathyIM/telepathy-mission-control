/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

#define MC_INTERNAL

#include <stdio.h>
#include <string.h>
#include "mc-account.h"
#include "mc-account-priv.h"
#include "dbus-api.h"

struct _McAccountConditionsProps {
    GHashTable *conditions;
};

static void create_props (TpProxy *proxy, GHashTable *props);

static McIfaceDescription iface_description = {
    G_STRUCT_OFFSET (McAccountPrivate, conditions_props),
    create_props,
    NULL,
};


void
_mc_account_conditions_class_init (McAccountClass *klass)
{
    _mc_iface_add (MC_TYPE_ACCOUNT,
		   MC_IFACE_QUARK_ACCOUNT_INTERFACE_CONDITIONS,
		   &iface_description);
}

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

/**
 * mc_account_conditions_call_when_ready:
 * @account: the #McAccount.
 * @callback: called when the interface becomes ready or invalidated, whichever
 * happens first.
 * @user_data: user data to be passed to @callback.
 *
 * Start retrieving and monitoring the properties of the Conditions interface
 * of @account. If they have already been retrieved, call @callback
 * immediately, then return. Otherwise, @callback will be called when the
 * properties are ready.
 */
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

/**
 * mc_account_conditions_get:
 * @account: the #McAccount.
 *
 * Retrieves the account conditions.
 *
 * Returns: a #GHashTable containing the account conditions, where both keys
 * and values are NULL-terminated strings. It must not be modified or
 * destroyed.
 */
GHashTable *
mc_account_conditions_get (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);

    if (G_UNLIKELY (!account->priv->conditions_props)) return NULL;
    return account->priv->conditions_props->conditions;
}

/**
 * mc_account_conditions_set:
 * @account: the #McAccount.
 * @conditions: a #GHashTable with the conditions to set.
 * @callback: callback to be invoked when the operation completes, or %NULL.
 * @user_data: user data for @callback.
 * @destroy: #GDestroyNotify function for @user_data.
 * @weak_object: if not NULL, a GObject which will be weakly referenced; if it
 * is destroyed, this call will automatically be cancelled. Must be NULL if
 * callback is NULL.
 *
 * Set the conditions for @account; the @conditions must be in a #GHashTable
 * where both keys and valus are NULL-terminated strings. It will not be
 * modified by this method.
 *
 * Returns: a #TpProxyPendingCall for the underlying D-Bus call.
 */
TpProxyPendingCall *
mc_account_conditions_set (McAccount *account,
			   const GHashTable *conditions,
			   tp_cli_dbus_properties_callback_for_set callback,
			   gpointer user_data,
			   GDestroyNotify destroy,
			   GObject *weak_object)
{
    GValue value = { 0 };

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    g_value_init (&value, DBUS_TYPE_G_STRING_STRING_HASHTABLE);
    g_value_set_static_boxed (&value, conditions);
    return tp_cli_dbus_properties_call_set (account, -1,
	MC_IFACE_ACCOUNT_INTERFACE_CONDITIONS, "Condition", &value,
	callback, user_data, destroy, weak_object);
}

