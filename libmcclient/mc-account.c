/*
 * mc-account.c - Telepathy Account D-Bus interface (client side)
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
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

#include <telepathy-glib/proxy-subclass.h>

#include "_gen/cli-account-body.h"

/**
 * SECTION:mc-account
 * @title: McAccount
 * @short_description: proxy object for the Telepathy Account D-Bus API
 *
 * This module provides a client-side proxy object for the Telepathy
 * Account D-Bus API.
 *
 * Since: FIXME
 */

/**
 * McAccountClass:
 *
 * The class of a #McAccount.
 *
 * Since: FIXME
 */
struct _McAccountClass {
    TpProxyClass parent_class;
    /*<private>*/
    gpointer priv;
};

struct _McAccountProps {
    gchar *display_name;
    gchar *icon;
    guint valid : 1;
    guint enabled : 1;
    guint connect_automatically : 1;
    gchar *nickname;
    GHashTable *parameters;
    TpConnectionPresenceType auto_presence_type;
    gchar *auto_presence_status;
    gchar *auto_presence_message;
    gchar *connection;
    TpConnectionStatus connection_status;
    TpConnectionStatusReason connection_status_reason;
    TpConnectionPresenceType curr_presence_type;
    gchar *curr_presence_status;
    gchar *curr_presence_message;
    TpConnectionPresenceType req_presence_type;
    gchar *req_presence_status;
    gchar *req_presence_message;
    gchar *normalized_name;
};

#define MC_ACCOUNT_IS_READY(account) (MC_ACCOUNT(account)->priv->props != NULL)
#define MC_ACCOUNT_IFACE_IS_READY(iface_data) (*(iface_data->props_data_ptr) != NULL)

/**
 * McAccount:
 *
 * A proxy object for the Telepathy Account D-Bus API. This is a subclass of
 * #TpProxy.
 *
 * Since: FIXME
 */

G_DEFINE_TYPE (McAccount, mc_account, TP_TYPE_PROXY);

enum
{
    PROP_0,
    PROP_OBJECT_PATH,
};

static inline gboolean
parse_object_path (McAccount *account)
{
    gchar manager[64], protocol[64], name[256];
    gchar *object_path = account->parent.object_path;
    gint n;

    n = sscanf (object_path, MC_ACCOUNT_DBUS_OBJECT_BASE "%[^/]/%[^/]/%s",
		manager, protocol, name);
    if (n != 3) return FALSE;

    account->manager_name = g_strdup (manager);
    account->protocol_name = g_strdup (protocol);
    account->name = object_path +
       	(sizeof (MC_ACCOUNT_DBUS_OBJECT_BASE) - 1);
    return TRUE;
}

static void
mc_account_init (McAccount *account)
{
    McAccountPrivate *priv;

    priv = account->priv =
       	G_TYPE_INSTANCE_GET_PRIVATE(account, MC_TYPE_ACCOUNT,
				    McAccountPrivate);

    tp_proxy_add_interface_by_id ((TpProxy *)account,
				  MC_IFACE_QUARK_ACCOUNT_INTERFACE_AVATAR);
    tp_proxy_add_interface_by_id ((TpProxy *)account,
				  MC_IFACE_QUARK_ACCOUNT_INTERFACE_COMPAT);
    tp_proxy_add_interface_by_id ((TpProxy *)account,
				  MC_IFACE_QUARK_ACCOUNT_INTERFACE_CONDITIONS);
}

static GObject *
constructor (GType type,
	     guint n_params,
	     GObjectConstructParam *params)
{
    GObjectClass *object_class = (GObjectClass *) mc_account_parent_class;
    McAccount *account;
   
    account =  MC_ACCOUNT (object_class->constructor (type, n_params, params));

    g_return_val_if_fail (account != NULL, NULL);

    if (!parse_object_path (account))
	return NULL;

    return (GObject *) account;
}

static void
account_props_free (McAccountProps *props)
{
    g_free (props->display_name);
    g_free (props->icon);
    g_free (props->nickname);
    if (props->parameters)
	g_hash_table_destroy (props->parameters);
    g_free (props->auto_presence_status);
    g_free (props->auto_presence_message);
    g_free (props->connection);
    g_free (props->curr_presence_status);
    g_free (props->curr_presence_message);
    g_free (props->req_presence_status);
    g_free (props->req_presence_message);
    g_free (props->normalized_name);
}

static void
finalize (GObject *object)
{
    McAccount *account = MC_ACCOUNT (object);

    if (account->priv->props)
	account_props_free (account->priv->props);

    g_free (account->manager_name);
    g_free (account->protocol_name);

    G_OBJECT_CLASS (mc_account_parent_class)->finalize (object);
}

static void
mc_account_class_init (McAccountClass *klass)
{
    GType type = MC_TYPE_ACCOUNT;
    GObjectClass *object_class = (GObjectClass *)klass;
    TpProxyClass *proxy_class = (TpProxyClass *)klass;

    g_type_class_add_private (object_class, sizeof (McAccountPrivate));
    object_class->constructor = constructor;
    object_class->finalize = finalize;

    /* the API is stateless, so we can keep the same proxy across restarts */
    proxy_class->must_have_unique_name = FALSE;

    _mc_ext_register_dbus_glib_marshallers ();

    proxy_class->interface = MC_IFACE_QUARK_ACCOUNT;
    tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_account_add_signals);

    tp_proxy_subclass_add_error_mapping (type, TP_ERROR_PREFIX, TP_ERRORS,
					 TP_TYPE_ERROR);
}

/**
 * mc_account_new:
 * @dbus: a D-Bus daemon; may not be %NULL
 *
 * <!-- -->
 *
 * Returns: a new NMC 4.x proxy
 *
 * Since: FIXME
 */
McAccount *
mc_account_new (TpDBusDaemon *dbus, const gchar *object_path)
{
    McAccount *account;

    account = g_object_new (MC_TYPE_ACCOUNT,
			    "dbus-daemon", dbus,
			    "bus-name", MC_ACCOUNT_MANAGER_DBUS_SERVICE,
			    "object-path", object_path,
			    NULL);
    return account;
}

static void
call_when_ready_context_free (gpointer ptr)
{
    g_slice_free (CallWhenReadyContext, ptr);
}

static void
update_property (gpointer key, gpointer ht_value, gpointer user_data)
{
    McAccount *account = user_data;
    McAccountProps *props = account->priv->props;
    GValue *value = ht_value;
    const gchar *name = key;
    GValueArray *va;
    GType type;

    if (strcmp (name, "DisplayName") == 0)
    {
	g_free (props->display_name);
	props->display_name = g_value_dup_string (value);
    }
    else if (strcmp (name, "Icon") == 0)
    {
	g_free (props->icon);
	props->icon = g_value_dup_string (value);
    }
    else if (strcmp (name, "Valid") == 0)
    {
	props->valid = g_value_get_boolean (value);
    }
    else if (strcmp (name, "Enabled") == 0)
    {
	props->enabled = g_value_get_boolean (value);
    }
    else if (strcmp (name, "Nickname") == 0)
    {
	g_free (props->nickname);
	props->nickname = g_value_dup_string (value);
    }
    else if (strcmp (name, "Parameters") == 0)
    {
	if (props->parameters)
	    g_hash_table_destroy (props->parameters);
	props->parameters = g_value_get_boxed (value);
	/* HACK: clear the GValue so that the hashtable will not be freed */
	type = G_VALUE_TYPE (value); 
	memset (value, 0, sizeof (GValue)); 
	g_value_init (value, type); 
    }
    else if (strcmp (name, "AutomaticPresence") == 0)
    {
	g_free (props->auto_presence_status);
	g_free (props->auto_presence_message);
	va = g_value_get_boxed (value);
	props->auto_presence_type = (gint)g_value_get_uint (va->values);
	props->auto_presence_status = g_value_dup_string (va->values + 1);
	props->auto_presence_message = g_value_dup_string (va->values + 2);
    }
    else if (strcmp (name, "ConnectAutomatically") == 0)
    {
	props->connect_automatically = g_value_get_boolean (value);
    }
    else if (strcmp (name, "Connection") == 0)
    {
	g_free (props->connection);
	props->connection = g_value_dup_string (value);
    }
    else if (strcmp (name, "ConnectionStatus") == 0)
    {
	props->connection_status = g_value_get_uint (value);
    }
    else if (strcmp (name, "ConnectionStatusReason") == 0)
    {
	props->connection_status_reason = g_value_get_uint (value);
    }
    else if (strcmp (name, "CurrentPresence") == 0)
    {
	g_free (props->curr_presence_status);
	g_free (props->curr_presence_message);
	va = g_value_get_boxed (value);
	props->curr_presence_type = (gint)g_value_get_uint (va->values);
	props->curr_presence_status = g_value_dup_string (va->values + 1);
	props->curr_presence_message = g_value_dup_string (va->values + 2);
    }
    else if (strcmp (name, "RequestedPresence") == 0)
    {
	g_free (props->req_presence_status);
	g_free (props->req_presence_message);
	va = g_value_get_boxed (value);
	props->req_presence_type = (gint)g_value_get_uint (va->values);
	props->req_presence_status = g_value_dup_string (va->values + 1);
	props->req_presence_message = g_value_dup_string (va->values + 2);
    }
    else if (strcmp (name, "NormalizedName") == 0)
    {
	g_free (props->normalized_name);
	props->normalized_name = g_value_dup_string (value);
    }
}

static void
properties_get_all_cb (TpProxy *proxy, GHashTable *props, 
		       const GError *error, gpointer user_data, 
		       GObject *weak_object) 
{
    McAccount *account = MC_ACCOUNT (proxy);
    CallWhenReadyContext *ctx = user_data;

    if (error)
    {
	ctx->callback (account, error, ctx->user_data);
    }
    else
    {
	ctx->create_props (account, props);
	ctx->callback (account, NULL, ctx->user_data);
    }
}

void
_mc_account_call_when_ready_int (McAccount *account,
				 McAccountWhenReadyCb callback,
				 gpointer user_data,
				 McAccountIfaceData *iface_data)
{
    TpProxy *proxy = (TpProxy *) account;

    g_return_if_fail (callback != NULL);
    
    if (MC_ACCOUNT_IFACE_IS_READY (iface_data) || proxy->invalidated)
    {
	callback (account, proxy->invalidated, user_data);
    }
    else
    {
	CallWhenReadyContext *ctx = g_slice_new (CallWhenReadyContext);

	ctx->callback = callback;
	ctx->user_data = user_data;
	ctx->create_props = iface_data->create_props;
	if (!tp_cli_dbus_properties_call_get_all (account, -1, 
						  iface_data->name, 
						  properties_get_all_cb, 
						  ctx,
						  call_when_ready_context_free,
						  NULL)) 
	{
	    GError *error = g_error_new_literal (TP_ERRORS,
						 TP_ERROR_NOT_AVAILABLE,
						 "DBus call failed");
	    callback (account, error, user_data);
	    g_error_free (error);
	} 
    }
}

static void
create_props (McAccount *account, GHashTable *props)
{
    McAccountPrivate *priv = account->priv;

    priv->props = g_malloc0 (sizeof (McAccountProps));
    g_hash_table_foreach (props, update_property, account);
}

void
mc_account_call_when_ready (McAccount *account, McAccountWhenReadyCb callback,
			    gpointer user_data)
{
    McAccountIfaceData iface_data;

    iface_data.name = MC_IFACE_ACCOUNT;
    iface_data.props_data_ptr = (gpointer *)&account->priv->props;
    iface_data.create_props = create_props;

    _mc_account_call_when_ready_int (account, callback, user_data, &iface_data);
}

const gchar *
mc_account_get_display_name (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->display_name;
}

const gchar *
mc_account_get_icon (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->icon;
}

gboolean
mc_account_is_valid (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), FALSE);
    if (G_UNLIKELY (!account->priv->props)) return FALSE;
    return account->priv->props->valid;
}

gboolean
mc_account_is_enabled (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), FALSE);
    if (G_UNLIKELY (!account->priv->props)) return FALSE;
    return account->priv->props->enabled;
}

gboolean
mc_account_connects_automatically (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), FALSE);
    if (G_UNLIKELY (!account->priv->props)) return FALSE;
    return account->priv->props->connect_automatically;
}

const gchar *
mc_account_get_nickname (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->nickname;
}

const GHashTable *
mc_account_get_parameters (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->parameters;
}

void
mc_account_get_automatic_presence (McAccount *account,
				   TpConnectionPresenceType *type,
				   const gchar **status,
				   const gchar **message)
{
    McAccountProps *props;

    g_return_if_fail (MC_IS_ACCOUNT (account));
    props = account->priv->props;
    if (G_UNLIKELY (!props))
    {
	*type = TP_CONNECTION_PRESENCE_TYPE_UNSET;
	*status = *message = NULL;
	return;
    }
    *type = props->auto_presence_type;
    *status = props->auto_presence_status;
    *message = props->auto_presence_message;
}

const gchar *
mc_account_get_connection_name (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->connection;
}

TpConnectionStatus
mc_account_get_connection_status (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account),
			  TP_CONNECTION_STATUS_DISCONNECTED);
    if (G_UNLIKELY (!account->priv->props))
       	return TP_CONNECTION_STATUS_DISCONNECTED;
    return account->priv->props->connection_status;
}

TpConnectionStatusReason
mc_account_get_connection_status_reason (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account),
			  TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
    if (G_UNLIKELY (!account->priv->props))
       	return TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;
    return account->priv->props->connection_status_reason;
}

void
mc_account_get_current_presence (McAccount *account,
				      TpConnectionPresenceType *type,
				      const gchar **status,
				      const gchar **message)
{
    McAccountProps *props;

    g_return_if_fail (MC_IS_ACCOUNT (account));
    props = account->priv->props;
    if (G_UNLIKELY (!props))
    {
	*type = TP_CONNECTION_PRESENCE_TYPE_UNSET;
	*status = *message = NULL;
	return;
    }
    *type = props->curr_presence_type;
    *status = props->curr_presence_status;
    *message = props->curr_presence_message;
}

void
mc_account_get_requested_presence (McAccount *account,
					TpConnectionPresenceType *type,
					const gchar **status,
					const gchar **message)
{
    McAccountProps *props;

    g_return_if_fail (MC_IS_ACCOUNT (account));
    props = account->priv->props;
    if (G_UNLIKELY (!props))
    {
	*type = TP_CONNECTION_PRESENCE_TYPE_UNSET;
	*status = *message = NULL;
	return;
    }
    *type = props->req_presence_type;
    *status = props->req_presence_status;
    *message = props->req_presence_message;
}

const gchar *
mc_account_get_normalized_name (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->normalized_name;
}

