/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

#define MC_INTERNAL

#include <stdio.h>
#include <string.h>
#include "mc-account.h"
#include "mc-account-priv.h"
#include "dbus-api.h"

struct _McAccountCompatProps {
    gchar *avatar_file;
    gchar *profile;
    gchar **secondary_vcard_fields;
};

static void create_props (TpProxy *proxy, GHashTable *props);
static void setup_props_monitor (TpProxy *proxy, GQuark interface);

static McIfaceDescription iface_description = {
    G_STRUCT_OFFSET (McAccountPrivate, compat_props),
    create_props,
    setup_props_monitor,
};


void
_mc_account_compat_class_init (McAccountClass *klass)
{
    _mc_iface_add (MC_TYPE_ACCOUNT,
		   MC_IFACE_QUARK_ACCOUNT_INTERFACE_COMPAT,
		   &iface_description);
}

void
_mc_account_compat_props_free (McAccountCompatProps *props)
{
    g_free (props->profile);
    g_free (props->avatar_file);
    g_strfreev (props->secondary_vcard_fields);
    g_slice_free (McAccountCompatProps, props);
}

static void
update_profile (const gchar *name, const GValue *value, gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountCompatProps *props = account->priv->compat_props;

    g_free (props->profile);
    props->profile = g_value_dup_string (value);
}

static void
update_avatar_file (const gchar *name, const GValue *value, gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountCompatProps *props = account->priv->compat_props;

    g_free (props->avatar_file);
    props->avatar_file = g_value_dup_string (value);
}

static void
update_secondary_vcard_fields (const gchar *name, const GValue *value,
                               gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountCompatProps *props = account->priv->compat_props;

    g_strfreev (props->secondary_vcard_fields);
    props->secondary_vcard_fields = g_value_dup_boxed (value);
}

static const McIfaceProperty account_compat_properties[] =
{
    { "Profile", "s", update_profile },
    { "AvatarFile", "s", update_avatar_file },
    { "SecondaryVCardFields", "as", update_secondary_vcard_fields },
    { NULL, NULL, NULL }
};

static void
create_props (TpProxy *proxy, GHashTable *props)
{
    McAccount *account = MC_ACCOUNT (proxy);
    McAccountPrivate *priv = account->priv;

    priv->compat_props = g_slice_new0 (McAccountCompatProps);
    _mc_iface_update_props (account_compat_properties, props, account);
}

/**
 * mc_account_compat_call_when_ready:
 * @account: the #McAccount.
 * @callback: called when the interface becomes ready or invalidated, whichever
 * happens first.
 * @user_data: user data to be passed to @callback.
 *
 * Start retrieving and monitoring the properties of the Compat interface of
 * @account. If they have already been retrieved, call @callback immediately,
 * then return. Otherwise, @callback will be called when the properties are
 * ready.
 */
void
mc_account_compat_call_when_ready (McAccount *account,
				   McAccountWhenReadyCb callback,
				   gpointer user_data)
{
    McIfaceData iface_data;

    iface_data.id = MC_IFACE_QUARK_ACCOUNT_INTERFACE_COMPAT;
    iface_data.props_data_ptr = (gpointer)&account->priv->compat_props;
    iface_data.create_props = create_props;

    if (_mc_iface_call_when_ready_int ((TpProxy *)account,
				   (McIfaceWhenReadyCb)callback, user_data,
                                       &iface_data))
    {
        setup_props_monitor ((TpProxy *)account,
                             MC_IFACE_QUARK_ACCOUNT_INTERFACE_COMPAT);
    }
}

/**
 * mc_account_compat_get_profile:
 * @account: the #McAccount.
 *
 * Retrieves the profile name of @account.
 *
 * Returns: a constant string representing the name of the profile.
 */
const gchar *
mc_account_compat_get_profile (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);

    if (G_UNLIKELY (!account->priv->compat_props)) return NULL;
    return account->priv->compat_props->profile;
}

/**
 * mc_account_compat_get_avatar_file:
 * @account: the #McAccount.
 *
 * Returns: a constant string representing the filename of the avatar.
 */
const gchar *
mc_account_compat_get_avatar_file (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);

    if (G_UNLIKELY (!account->priv->compat_props)) return NULL;
    return account->priv->compat_props->avatar_file;
}

/**
 * mc_account_compat_get_secondary_vcard_fields:
 * @account: the #McAccount.
 *
 * Returns: an array of strings representing the secondary vcard fields set for
 * @account.
 */
const gchar * const *
mc_account_compat_get_secondary_vcard_fields (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);

    if (G_UNLIKELY (!account->priv->compat_props)) return NULL;
    return (const gchar * const *)
        account->priv->compat_props->secondary_vcard_fields;
}

static void
on_compat_property_changed (TpProxy *proxy, GHashTable *properties,
                            gpointer user_data, GObject *weak_object)
{
    McAccount *account = MC_ACCOUNT (proxy);
    McAccountPrivate *priv = account->priv;

    /* if the GetAll method hasn't returned yet, we do nothing */
    if (G_UNLIKELY (!priv->compat_props)) return;

    _mc_iface_update_props (account_compat_properties, properties, account);
}

static void
setup_props_monitor (TpProxy *proxy, GQuark interface)
{
    McAccount *account = MC_ACCOUNT (proxy);

    mc_cli_account_interface_compat_connect_to_compat_property_changed
        (account, on_compat_property_changed, NULL, NULL, NULL, NULL);
}

/**
 * mc_account_avatar_set:
 * @account: the #McAccount.
 * @profile: the name of the profile to set.
 * @callback: callback to be invoked when the operation completes, or %NULL.
 * @user_data: user data for @callback.
 * @destroy: #GDestroyNotify function for @user_data.
 * @weak_object: if not NULL, a GObject which will be weakly referenced; if it
 * is destroyed, this call will automatically be cancelled. Must be NULL if
 * callback is NULL.
 *
 * Set the profile name for @account.
 *
 * Returns: a #TpProxyPendingCall for the underlying D-Bus call.
 */
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

/**
 * mc_account_avatar_set:
 * @account: the #McAccount.
 * @fields: array of VCard fields to set.
 * @callback: callback to be invoked when the operation completes, or %NULL.
 * @user_data: user data for @callback.
 * @destroy: #GDestroyNotify function for @user_data.
 * @weak_object: if not NULL, a GObject which will be weakly referenced; if it
 * is destroyed, this call will automatically be cancelled. Must be NULL if
 * callback is NULL.
 *
 * Set the secondary VCard fields for @account.
 *
 * Returns: a #TpProxyPendingCall for the underlying D-Bus call.
 */
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

