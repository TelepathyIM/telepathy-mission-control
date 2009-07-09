/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

#define MC_INTERNAL

#include <stdio.h>
#include <string.h>
#include "mc-account.h"
#include "mc-account-manager.h"
#include "mc-account-priv.h"
#include "mc-interfaces.h"
#include "dbus-api.h"
#include "mc-signals-marshal.h"

#include <telepathy-glib/proxy-subclass.h>

#include "_gen/cli-account-body.h"

/**
 * SECTION:mc-account
 * @title: McAccount
 * @short_description: proxy object for the Telepathy Account D-Bus API
 *
 * This module provides a client-side proxy object for the Telepathy
 * Account D-Bus API.
 */

typedef struct {
    TpConnectionPresenceType type;
    gchar *status;
    gchar *message;
} McPresence;

struct _McAccountProps {
    gchar *display_name;
    gchar *icon;
    guint valid : 1;
    guint enabled : 1;
    guint has_been_online : 1;
    guint connect_automatically : 1;
    guint emit_changed : 1;
    guint emit_connection_status_changed : 1;
    gchar *nickname;
    GHashTable *parameters;
    McPresence auto_presence;
    gchar *connection;
    TpConnectionStatus connection_status;
    TpConnectionStatusReason connection_status_reason;
    McPresence curr_presence;
    McPresence req_presence;
    gchar *normalized_name;
};

#define mc_presence_free(presence) \
    { g_free ((presence)->status); g_free ((presence)->message); }

/**
 * McAccount:
 * @parent: the #TpProxy for the account object.
 * @name: the name of the account; currently it's the variable part of the
 * D-Bus object path. (read-only)
 * @manager_name: the name of the Telepathy manager. (read-only)
 * @protocol_name: the name of the protocol. (read-only)
 *
 * A proxy object for the Telepathy Account D-Bus API. This is a subclass of
 * #TpProxy.
 */

G_DEFINE_TYPE (McAccount, mc_account, TP_TYPE_PROXY);

enum
{
    PROP_0,
    PROP_OBJECT_PATH,
};

guint _mc_account_signals[LAST_SIGNAL] = { 0 };

static void create_props (TpProxy *proxy, GHashTable *props);
static void setup_props_monitor (TpProxy *proxy, GQuark interface);

static McIfaceDescription iface_description = {
    G_STRUCT_OFFSET (McAccountPrivate, props),
    create_props,
    setup_props_monitor,
};


static void
on_account_removed (TpProxy *proxy, gpointer user_data, GObject *weak_object)
{
    GError e = { TP_DBUS_ERRORS, TP_DBUS_ERROR_OBJECT_REMOVED,
	"Account was removed" };
    tp_proxy_invalidate (proxy, &e);
}

static inline void
set_presence_gvalue (GValue *value, TpConnectionPresenceType type,
		     const gchar *status, const gchar *message)
{
    GType gtype;
    gtype = TP_STRUCT_TYPE_SIMPLE_PRESENCE;
    g_value_init (value, gtype);
    g_value_take_boxed (value, dbus_g_type_specialized_construct (gtype));
    GValueArray *va = (GValueArray *) g_value_get_boxed (value);
    g_value_set_uint (va->values, type);
    g_value_set_static_string (va->values + 1, status);
    g_value_set_static_string (va->values + 2, message);
}

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
    account->name = object_path + MC_ACCOUNT_DBUS_OBJECT_BASE_LEN;
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
        MC_IFACE_QUARK_ACCOUNT_INTERFACE_CHANNELREQUESTS);
    tp_proxy_add_interface_by_id ((TpProxy *)account,
				  MC_IFACE_QUARK_ACCOUNT_INTERFACE_COMPAT);
    tp_proxy_add_interface_by_id ((TpProxy *)account,
				  MC_IFACE_QUARK_ACCOUNT_INTERFACE_CONDITIONS);
    tp_proxy_add_interface_by_id ((TpProxy *)account,
				  MC_IFACE_QUARK_ACCOUNT_INTERFACE_STATS);
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

    mc_cli_account_connect_to_removed (account, on_account_removed,
				       NULL, NULL, NULL, NULL);
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
    mc_presence_free (&props->auto_presence);
    g_free (props->connection);
    mc_presence_free (&props->curr_presence);
    mc_presence_free (&props->req_presence);
    g_free (props->normalized_name);
    g_free (props);
}

static void
finalize (GObject *object)
{
    McAccount *account = MC_ACCOUNT (object);

    if (account->priv->props)
	account_props_free (account->priv->props);

    if (account->priv->avatar_props)
	_mc_account_avatar_props_free (account->priv->avatar_props);

    if (account->priv->compat_props)
	_mc_account_compat_props_free (account->priv->compat_props);

    if (account->priv->conditions_props)
	_mc_account_conditions_props_free (account->priv->conditions_props);

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
    tp_proxy_init_known_interfaces ();
    tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_account_add_signals);

    tp_proxy_subclass_add_error_mapping (type, TP_ERROR_PREFIX, TP_ERRORS,
					 TP_TYPE_ERROR);
    tp_proxy_subclass_add_error_mapping (type, MC_ERROR_PREFIX, MC_ERROR,
                                         MC_TYPE_ERROR);

    /**
     * McAccount::presence-changed:
     * @account: the #McAccount.
     * @detail: a #GQuark specifying which presence has changed.
     * @type: the presence type.
     * @status: the presence status string.
     * @message: the presence status message.
     *
     * Emitted when the current, requested or automatic presence changes.
     *
     * This signal will be emitted only once mc_account_call_when_ready() has
     * been successfully invoked.
     */
    _mc_account_signals[PRESENCE_CHANGED] =
	g_signal_new ("presence-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL,
		      mc_signals_marshal_VOID__UINT_UINT_STRING_STRING,
		      G_TYPE_NONE,
		      4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING,
		      G_TYPE_STRING);

    /**
     * McAccount::string-changed:
     * @account: the #McAccount.
     * @detail: a #GQuark specifying which string has changed.
     * @message: the new vaule for the string.
     *
     * Emitted when a string property changes (such as display name, icon...).
     *
     * This signal will be emitted only once mc_account_call_when_ready() has
     * been successfully invoked.
     */
    _mc_account_signals[STRING_CHANGED] =
	g_signal_new ("string-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL,
		      mc_signals_marshal_VOID__UINT_STRING,
		      G_TYPE_NONE,
		      2, G_TYPE_UINT, G_TYPE_STRING);

    /**
     * McAccount::string-changed:
     * @account: the #McAccount.
     * @status: the connection status.
     * @reason: the connection status reason.
     *
     * Emitted when the connection status changes.
     *
     * This signal will be emitted only once mc_account_call_when_ready() has
     * been successfully invoked.
     */
    _mc_account_signals[CONNECTION_STATUS_CHANGED] =
	g_signal_new ("connection-status-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST,
		      0,
		      NULL, NULL,
		      mc_signals_marshal_VOID__UINT_UINT,
		      G_TYPE_NONE,
		      2, G_TYPE_UINT, G_TYPE_UINT);

    /**
     * McAccount::flag-changed:
     * @account: the #McAccount.
     * @detail: a #GQuark specifying which flag has changed.
     * @value: the new vaule for the boolean property.
     *
     * Emitted when a boolean property changes (such as valid, enabled).
     *
     * This signal will be emitted only once mc_account_call_when_ready() has
     * been successfully invoked.
     */
    _mc_account_signals[FLAG_CHANGED] =
	g_signal_new ("flag-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL,
		      mc_signals_marshal_VOID__UINT_BOOLEAN,
		      G_TYPE_NONE,
		      2, G_TYPE_UINT, G_TYPE_BOOLEAN);

    /**
     * McAccount::parameters-changed:
     * @account: the #McAccount.
     * @old: the #GHashTable of the old account parameters.
     * @new: the #GHashTable of the new account parameters.
     *
     * Emitted when the account parameters change. Don't modify the passed-in
     * hash tables.
     *
     * This signal will be emitted only once mc_account_call_when_ready() has
     * been successfully invoked.
     */
    _mc_account_signals[PARAMETERS_CHANGED] =
	g_signal_new ("parameters-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL,
		      mc_signals_marshal_VOID__BOXED_BOXED,
		      G_TYPE_NONE,
		      2, G_TYPE_HASH_TABLE, G_TYPE_HASH_TABLE);

    _mc_iface_add (MC_TYPE_ACCOUNT, MC_IFACE_QUARK_ACCOUNT,
		   &iface_description);
    _mc_account_avatar_class_init (klass);
    _mc_account_compat_class_init (klass);
    _mc_account_conditions_class_init (klass);
    _mc_account_stats_class_init (klass);
}

/**
 * mc_account_new:
 * @dbus: a D-Bus daemon; may not be %NULL
 * @object_path: the path of the D-Bus account object.
 *
 * Returns: a new #McAccount object.
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
update_string (McAccount *account, gchar **ptr, const GValue *value,
               GQuark quark, gboolean emit_changed)
{
    g_free (*ptr);
    *ptr = g_value_dup_string (value);
    if (emit_changed)
        g_signal_emit (account, _mc_account_signals[STRING_CHANGED],
                       quark,
                       quark,
                       *ptr);
}

static void
update_display_name (const gchar *name, const GValue *value,
                     gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    update_string (account, &props->display_name, value,
                   MC_QUARK_DISPLAY_NAME, props->emit_changed);
}

static void
update_icon (const gchar *name, const GValue *value, gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    update_string (account, &props->icon, value,
                   MC_QUARK_ICON, props->emit_changed);
}

static void
update_valid (const gchar *name, const GValue *value, gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    props->valid = g_value_get_boolean (value);
    if (props->emit_changed)
        g_signal_emit (account, _mc_account_signals[FLAG_CHANGED],
                       MC_QUARK_VALID,
                       MC_QUARK_VALID,
                       props->valid);
}

static void
update_enabled (const gchar *name, const GValue *value, gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    props->enabled = g_value_get_boolean (value);
    if (props->emit_changed)
        g_signal_emit (account, _mc_account_signals[FLAG_CHANGED],
                       MC_QUARK_ENABLED,
                       MC_QUARK_ENABLED,
                       props->enabled);
}

static void
update_has_been_online (const gchar *name, const GValue *value,
                        gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    props->has_been_online = g_value_get_boolean (value);
    if (props->emit_changed)
        g_signal_emit (account, _mc_account_signals[FLAG_CHANGED],
                       MC_QUARK_HAS_BEEN_ONLINE,
                       MC_QUARK_HAS_BEEN_ONLINE,
                       props->has_been_online);
}

static void
update_nickname (const gchar *name, const GValue *value, gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    update_string (account, &props->nickname, value,
                   MC_QUARK_NICKNAME, props->emit_changed);
}

static void
update_parameters (const gchar *name, const GValue *value,
                   gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;
    GHashTable *old_parameters = props->parameters;

    props->parameters = g_value_dup_boxed (value);
    if (props->emit_changed)
        g_signal_emit (account, _mc_account_signals[PARAMETERS_CHANGED],
                       0,
                       old_parameters, props->parameters);
    if (old_parameters)
        g_hash_table_destroy (old_parameters);
}

static void
update_presence (McAccount *account, McPresence *presence, const GValue *value,
                 GQuark quark, gboolean emit_changed)
{
    GValueArray *va;

    mc_presence_free (presence);
    va = g_value_get_boxed (value);
    presence->type = (gint)g_value_get_uint (va->values);
    presence->status = g_value_dup_string (va->values + 1);
    presence->message = g_value_dup_string (va->values + 2);
    if (emit_changed)
        g_signal_emit (account, _mc_account_signals[PRESENCE_CHANGED],
                       quark,
                       quark,
                       presence->type,
                       presence->status,
                       presence->message);
}

static void
update_automatic_presence (const gchar *name, const GValue *value,
                           gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    update_presence (account, &props->auto_presence, value,
                     MC_QUARK_AUTOMATIC_PRESENCE, props->emit_changed);
}

static void
update_connect_automatically (const gchar *name, const GValue *value,
                              gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    props->connect_automatically = g_value_get_boolean (value);
    if (props->emit_changed)
        g_signal_emit (account, _mc_account_signals[FLAG_CHANGED],
                       MC_QUARK_CONNECT_AUTOMATICALLY,
                       MC_QUARK_CONNECT_AUTOMATICALLY,
                       props->connect_automatically);
}

static void
update_connection (const gchar *name, const GValue *value,
                   gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;
    const gchar *object_path;

    g_free (props->connection);
    object_path = g_value_get_boxed (value);
    if (object_path && strcmp (object_path, "/") != 0)
        props->connection = g_strdup (object_path);
    else
        props->connection = NULL;
}

static void
update_connection_status (const gchar *name, const GValue *value,
                          gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    props->connection_status = g_value_get_uint (value);
    if (props->emit_changed)
        props->emit_connection_status_changed = TRUE;
}

static void
update_connection_status_reason (const gchar *name, const GValue *value,
                                 gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    props->connection_status_reason = g_value_get_uint (value);
    if (props->emit_changed)
        props->emit_connection_status_changed = TRUE;
}

static void
update_current_presence (const gchar *name, const GValue *value,
                         gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    update_presence (account, &props->curr_presence, value,
                     MC_QUARK_CURRENT_PRESENCE, props->emit_changed);
}

static void
update_requested_presence (const gchar *name, const GValue *value,
                           gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    update_presence (account, &props->req_presence, value,
                     MC_QUARK_REQUESTED_PRESENCE, props->emit_changed);
}

static void
update_normalized_name (const gchar *name, const GValue *value,
                        gpointer user_data)
{
    McAccount *account = MC_ACCOUNT (user_data);
    McAccountProps *props = account->priv->props;

    update_string (account, &props->normalized_name, value,
                   MC_QUARK_NORMALIZED_NAME, props->emit_changed);
}

static const McIfaceProperty account_properties[] =
{
    { "DisplayName", "s", update_display_name },
    { "Icon", "s", update_icon },
    { "Valid", "b", update_valid },
    { "Enabled", "b", update_enabled },
    { "HasBeenOnline", "b", update_has_been_online },
    { "Nickname", "s", update_nickname },
    { "Parameters", "a{sv}", update_parameters },
    { "AutomaticPresence", "(uss)", update_automatic_presence },
    { "ConnectAutomatically", "b", update_connect_automatically },
    { "Connection", "o", update_connection },
    { "ConnectionStatus", "u", update_connection_status },
    { "ConnectionStatusReason", "u", update_connection_status_reason },
    { "CurrentPresence", "(uss)", update_current_presence },
    { "RequestedPresence", "(uss)", update_requested_presence },
    { "NormalizedName", "s", update_normalized_name },
    { NULL, NULL, NULL }
};

static void
create_props (TpProxy *proxy, GHashTable *props)
{
    McAccount *account = MC_ACCOUNT (proxy);
    McAccountPrivate *priv = account->priv;

    priv->props = g_malloc0 (sizeof (McAccountProps));
    _mc_iface_update_props (account_properties, props, account);
    priv->props->emit_changed = TRUE;
}

static void 
on_account_property_changed (TpProxy *proxy, GHashTable *props, 
			     gpointer user_data, GObject *weak_object) 
{ 
    McAccount *account = MC_ACCOUNT (proxy); 
    McAccountPrivate *priv = account->priv; 

    /* if the GetAll method hasn't returned yet, we do nothing */
    if (G_UNLIKELY (!priv->props)) return;

    _mc_iface_update_props (account_properties, props, account);
    if (priv->props->emit_connection_status_changed)
    {
	g_signal_emit (account,
		       _mc_account_signals[CONNECTION_STATUS_CHANGED], 0,
		       priv->props->connection_status,
		       priv->props->connection_status_reason);
        priv->props->emit_connection_status_changed = FALSE;
    }
}

static void
setup_props_monitor (TpProxy *proxy, GQuark interface)
{
    McAccount *account = MC_ACCOUNT (proxy);

    mc_cli_account_connect_to_account_property_changed (account,
							on_account_property_changed,
							NULL, NULL,
							NULL, NULL);
}

/**
 * McAccountWhenReadyCb:
 * @account: the #McAccount.
 * @error: %NULL if the interface is ready for use, or the error with which it
 * was invalidated if it is now invalid.
 * @user_data: the user data that was passed to mc_account_call_when_ready().
 */

/**
 * mc_account_call_when_ready:
 * @account: the #McAccount.
 * @callback: called when the interface becomes ready or invalidated, whichever
 * happens first.
 * @user_data: user data to be passed to @callback.
 *
 * Start retrieving and monitoring the properties of the base interface of
 * @account. If they have already been retrieved, call @callback immediately,
 * then return. Otherwise, @callback will be called when the properties are
 * ready.
 */
void
mc_account_call_when_ready (McAccount *account, McAccountWhenReadyCb callback,
			    gpointer user_data)
{
    McIfaceData iface_data;

    iface_data.id = MC_IFACE_QUARK_ACCOUNT;
    iface_data.props_data_ptr = (gpointer)&account->priv->props;
    iface_data.create_props = create_props;

    if (_mc_iface_call_when_ready_int ((TpProxy *)account,
				       (McIfaceWhenReadyCb)callback, user_data,
				       &iface_data))
    {
	mc_cli_account_connect_to_account_property_changed (account,
							    on_account_property_changed,
							    NULL, NULL,
							    NULL, NULL);
    }
}

/**
 * McAccountWhenReadyObjectCb:
 * @account: the #McAccount.
 * @error: %NULL if the interface is ready for use, or the error with which it
 * was invalidated if it is now invalid.
 * @user_data: the user data that was passed to
 * mc_account_call_when_iface_ready() or mc_account_call_when_all_ready().
 * @weak_object: the #GObject that was passed to
 * mc_account_call_when_iface_ready() or mc_account_call_when_all_ready().
 */

/**
 * mc_account_call_when_iface_ready:
 * @account: the #McAccount.
 * @interface: a #GQuark representing the interface to process.
 * @callback: called when the interface becomes ready or invalidated, whichever
 * happens first.
 * @user_data: user data to be passed to @callback.
 * @destroy: called with the user_data as argument, after the call has
 * succeeded, failed or been cancelled.
 * @weak_object: If not %NULL, a #GObject which will be weakly referenced; if
 * it is destroyed, this call will automatically be cancelled. Must be %NULL if
 * @callback is %NULL
 *
 * Start retrieving and monitoring the properties of the interface @interface
 * of @account. If they have already been retrieved, call @callback
 * immediately, then return. Otherwise, @callback will be called when the
 * properties are ready.
 */
void
mc_account_call_when_iface_ready (McAccount *account,
				  GQuark interface,
				  McAccountWhenReadyObjectCb callback,
				  gpointer user_data,
				  GDestroyNotify destroy,
				  GObject *weak_object)
{
    _mc_iface_call_when_ready ((TpProxy *)account,
			       MC_TYPE_ACCOUNT,
			       interface,
			       (McIfaceWhenReadyCb)callback,
			       user_data, destroy, weak_object);
}

/**
 * mc_account_call_when_all_ready:
 * @account: the #McAccount.
 * @callback: called when the interfaces becomes ready or invalidated,
 * whichever happens first.
 * @user_data: user data to be passed to @callback.
 * @destroy: called with the user_data as argument, after the call has
 * succeeded, failed or been cancelled.
 * @weak_object: If not %NULL, a #GObject which will be weakly referenced; if
 * it is destroyed, this call will automatically be cancelled. Must be %NULL if
 * @callback is %NULL
 * @Varargs: a list of #GQuark types representing the interfaces to process,
 * followed by %0.
 *
 * Start retrieving and monitoring the properties of the specified interfaces
 * of @account. This is a convenience function built around
 * mc_account_call_when_iface_ready(), to have @callback called only once all
 * the specified interfaces are ready. In case more than one interface fail to
 * be processed, the #GError passed to the callback function will be the one of
 * the first interface that failed.
 */
void
mc_account_call_when_all_ready (McAccount *account,
				McAccountWhenReadyObjectCb callback,
				gpointer user_data,
				GDestroyNotify destroy,
				GObject *weak_object, ...)
{
    GPtrArray *ifaces;
    GQuark iface;
    va_list ifaces_va;

    ifaces = g_ptr_array_sized_new (8);

    va_start (ifaces_va, weak_object);
    for (iface = va_arg (ifaces_va, GQuark); iface != 0;
	 iface = va_arg (ifaces_va, GQuark))
    {
        g_ptr_array_add (ifaces, GUINT_TO_POINTER (iface));
    }
    va_end (ifaces_va);

    _mc_iface_call_when_all_readyv ((TpProxy *)account, MC_TYPE_ACCOUNT,
                                    (McIfaceWhenReadyCb)callback,
                                    user_data, destroy, weak_object,
                                    ifaces->len, (GQuark *)ifaces->pdata);

    g_ptr_array_free (ifaces, TRUE);
}

/**
 * mc_account_get_display_name:
 * @account: the #McAccount.
 *
 * Returns: a constant string representing the account display name.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
const gchar *
mc_account_get_display_name (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->display_name;
}

/**
 * mc_account_get_icon:
 * @account: the #McAccount.
 *
 * Returns: a constant string representing the account icon name.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
const gchar *
mc_account_get_icon (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->icon;
}

/**
 * mc_account_is_valid:
 * @account: the #McAccount.
 *
 * Returns: %TRUE if the account is valid, %FALSE otherwise.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
gboolean
mc_account_is_valid (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), FALSE);
    if (G_UNLIKELY (!account->priv->props)) return FALSE;
    return account->priv->props->valid;
}

/**
 * mc_account_is_enabled:
 * @account: the #McAccount.
 *
 * Returns: %TRUE if the account is enabled, %FALSE otherwise.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
gboolean
mc_account_is_enabled (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), FALSE);
    if (G_UNLIKELY (!account->priv->props)) return FALSE;
    return account->priv->props->enabled;
}

/**
 * mc_account_has_been_online:
 * @account: the #McAccount.
 *
 * Returns: %TRUE if the account has ever been online, %FALSE otherwise.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
gboolean
mc_account_has_been_online (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), FALSE);
    if (G_UNLIKELY (!account->priv->props)) return FALSE;
    return account->priv->props->has_been_online;
}

/**
 * mc_account_connects_automatically:
 * @account: the #McAccount.
 *
 * Returns: %TRUE if the account automatically connects when possible, %FALSE
 * otherwise.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
gboolean
mc_account_connects_automatically (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), FALSE);
    if (G_UNLIKELY (!account->priv->props)) return FALSE;
    return account->priv->props->connect_automatically;
}

/**
 * mc_account_get_nickname:
 * @account: the #McAccount.
 *
 * Returns: the nickname (alias) of @account.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
const gchar *
mc_account_get_nickname (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->nickname;
}

/**
 * mc_account_get_parameters:
 * @account: the #McAccount.
 *
 * Returns: a constant #GHashTable (do not destroy or modify it) listing the
 * account parameters. The keys in the hash table are strings representing the
 * parameter names, and the values are stored in #GValue types.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
GHashTable *
mc_account_get_parameters (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->parameters;
}

/**
 * mc_account_get_automatic_presence:
 * @account: the #McAccount.
 * @type: pointer to a #TpConnectionPresenceType to receive the presence type.
 * @status: pointer that will receive the presence status string (to be not
 * modified or free'd).
 * @message: pointer that will receive the presence status message string (to
 * be not modified or free'd).
 *
 * Retrieves the automatic presence (the presence this account will request
 * when going automatically online).
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
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
	if (type)
	    *type = TP_CONNECTION_PRESENCE_TYPE_UNSET;
	if (status)
	    *status = NULL;
	if (message)
	    *message = NULL;
	return;
    }
    if (type)
	*type = props->auto_presence.type;
    if (status)
	*status = props->auto_presence.status;
    if (message)
	*message = props->auto_presence.message;
}

/**
 * mc_account_get_connection_path:
 * @account: the #McAccount.
 *
 * Returns: a constant string representing the D-Bus path of the Telepathy
 * connection object, or %NULL if the account is disconnected.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
const gchar *
mc_account_get_connection_path (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->connection;
}

/**
 * mc_account_get_connection_name:
 * @account: the #McAccount.
 *
 * @deprecated: use mc_account_get_connection_path() instead.
 */
const gchar *
mc_account_get_connection_name (McAccount *account)
{
    return mc_account_get_connection_path (account);
}

/**
 * mc_account_get_connection_status:
 * @account: the #McAccount.
 *
 * Returns: the connection status of the Telepathy connection object.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
TpConnectionStatus
mc_account_get_connection_status (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account),
			  TP_CONNECTION_STATUS_DISCONNECTED);
    if (G_UNLIKELY (!account->priv->props))
       	return TP_CONNECTION_STATUS_DISCONNECTED;
    return account->priv->props->connection_status;
}

/**
 * mc_account_get_connection_status_reason:
 * @account: the #McAccount.
 *
 * Returns: the connection status reason of the Telepathy connection object.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
TpConnectionStatusReason
mc_account_get_connection_status_reason (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account),
			  TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
    if (G_UNLIKELY (!account->priv->props))
       	return TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;
    return account->priv->props->connection_status_reason;
}

/**
 * mc_account_get_current_presence:
 * @account: the #McAccount.
 * @type: pointer to a #TpConnectionPresenceType to receive the presence type.
 * @status: pointer that will receive the presence status string (to be not
 * modified or free'd).
 * @message: pointer that will receive the presence status message string (to
 * be not modified or free'd).
 *
 * Retrieves the current presence of @account.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
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
	if (type)
	    *type = TP_CONNECTION_PRESENCE_TYPE_UNSET;
	if (status)
	    *status = NULL;
	if (message)
	    *message = NULL;
	return;
    }
    if (type)
	*type = props->curr_presence.type;
    if (status)
	*status = props->curr_presence.status;
    if (message)
	*message = props->curr_presence.message;
}

/**
 * mc_account_get_requested_presence:
 * @account: the #McAccount.
 * @type: pointer to a #TpConnectionPresenceType to receive the presence type.
 * @status: pointer that will receive the presence status string (to be not
 * modified or free'd).
 * @message: pointer that will receive the presence status message string (to
 * be not modified or free'd).
 *
 * Retrieves the requested presence of @account.
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
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
	if (type)
	    *type = TP_CONNECTION_PRESENCE_TYPE_UNSET;
	if (status)
	    *status = NULL;
	if (message)
	    *message = NULL;
	return;
    }
    if (type)
	*type = props->req_presence.type;
    if (status)
	*status = props->req_presence.status;
    if (message)
	*message = props->req_presence.message;
}

/**
 * mc_account_get_normalized_name:
 * @account: the #McAccount.
 *
 * Returns: a constant string representing the normalized name of @account.
 * This is the value returned from Telepathy when inspecting the self handle,
 * and will be %NULL if the account never went online.
 * connection object, or %NULL if the account is disconnected.
 *
 * mc_account_call_when_ready() must have been successfully invoked prior to
 * calling this function.
 */
const gchar *
mc_account_get_normalized_name (McAccount *account)
{
    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    if (G_UNLIKELY (!account->priv->props)) return NULL;
    return account->priv->props->normalized_name;
}

/**
 * mc_account_set_display_name:
 * @account: the #McAccount.
 * @display_name: display name to be set.
 * @callback: callback to be invoked when the operation completes, or %NULL.
 * @user_data: user data for @callback.
 * @destroy: #GDestroyNotify function for @user_data.
 * @weak_object: if not NULL, a GObject which will be weakly referenced; if it
 * is destroyed, this call will automatically be cancelled. Must be NULL if
 * callback is NULL.
 *
 * Set the display name of @account.
 *
 * Returns: a #TpProxyPendingCall for the underlying D-Bus call.
 */
TpProxyPendingCall *
mc_account_set_display_name (McAccount *account, const gchar *display_name,
			     tp_cli_dbus_properties_callback_for_set callback,
			     gpointer user_data,
			     GDestroyNotify destroy,
			     GObject *weak_object)
{
    GValue value = { 0 };

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, display_name);
    return tp_cli_dbus_properties_call_set (account, -1,
	MC_IFACE_ACCOUNT, "DisplayName", &value,
	callback, user_data, destroy, weak_object);
}

/**
 * mc_account_set_icon:
 * @account: the #McAccount.
 * @icon: icon name to be set.
 * @callback: callback to be invoked when the operation completes, or %NULL.
 * @user_data: user data for @callback.
 * @destroy: #GDestroyNotify function for @user_data.
 * @weak_object: if not NULL, a GObject which will be weakly referenced; if it
 * is destroyed, this call will automatically be cancelled. Must be NULL if
 * callback is NULL.
 *
 * Set the icon of @account.
 *
 * Returns: a #TpProxyPendingCall for the underlying D-Bus call.
 */
TpProxyPendingCall *
mc_account_set_icon (McAccount *account, const gchar *icon,
		     tp_cli_dbus_properties_callback_for_set callback,
		     gpointer user_data,
		     GDestroyNotify destroy,
		     GObject *weak_object)
{
    GValue value = { 0 };

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, icon);
    return tp_cli_dbus_properties_call_set (account, -1,
	MC_IFACE_ACCOUNT, "Icon", &value,
	callback, user_data, destroy, weak_object);
}

/**
 * mc_account_set_enabled:
 * @account: the #McAccount.
 * @enabled: whether the @account must be enabled.
 * @callback: callback to be invoked when the operation completes, or %NULL.
 * @user_data: user data for @callback.
 * @destroy: #GDestroyNotify function for @user_data.
 * @weak_object: if not NULL, a GObject which will be weakly referenced; if it
 * is destroyed, this call will automatically be cancelled. Must be NULL if
 * callback is NULL.
 *
 * Enables or disables @account.
 *
 * Returns: a #TpProxyPendingCall for the underlying D-Bus call.
 */
TpProxyPendingCall *
mc_account_set_enabled (McAccount *account, gboolean enabled,
			tp_cli_dbus_properties_callback_for_set callback,
			gpointer user_data,
			GDestroyNotify destroy,
			GObject *weak_object)
{
    GValue value = { 0 };

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    g_value_init (&value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&value, enabled);
    return tp_cli_dbus_properties_call_set (account, -1,
	MC_IFACE_ACCOUNT, "Enabled", &value,
	callback, user_data, destroy, weak_object);
}

/**
 * mc_account_set_connect_automatically:
 * @account: the #McAccount.
 * @connect: whether the @account must connect automatically.
 * @callback: callback to be invoked when the operation completes, or %NULL.
 * @user_data: user data for @callback.
 * @destroy: #GDestroyNotify function for @user_data.
 * @weak_object: if not NULL, a GObject which will be weakly referenced; if it
 * is destroyed, this call will automatically be cancelled. Must be NULL if
 * callback is NULL.
 *
 * Enables or disables automatic connection for @account.
 *
 * Returns: a #TpProxyPendingCall for the underlying D-Bus call.
 */
TpProxyPendingCall *
mc_account_set_connect_automatically (McAccount *account, gboolean connect,
				      tp_cli_dbus_properties_callback_for_set callback,
				      gpointer user_data,
				      GDestroyNotify destroy,
				      GObject *weak_object)
{
    GValue value = { 0 };

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    g_value_init (&value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&value, connect);
    return tp_cli_dbus_properties_call_set (account, -1,
	MC_IFACE_ACCOUNT, "ConnectAutomatically", &value,
	callback, user_data, destroy, weak_object);
}

/**
 * mc_account_set_nickname:
 * @account: the #McAccount.
 * @nickname: nickname to be set.
 * @callback: callback to be invoked when the operation completes, or %NULL.
 * @user_data: user data for @callback.
 * @destroy: #GDestroyNotify function for @user_data.
 * @weak_object: if not NULL, a GObject which will be weakly referenced; if it
 * is destroyed, this call will automatically be cancelled. Must be NULL if
 * callback is NULL.
 *
 * Set the nickname (alias) of @account.
 *
 * Returns: a #TpProxyPendingCall for the underlying D-Bus call.
 */
TpProxyPendingCall *
mc_account_set_nickname (McAccount *account, const gchar *nickname,
			 tp_cli_dbus_properties_callback_for_set callback,
			 gpointer user_data,
			 GDestroyNotify destroy,
			 GObject *weak_object)
{
    GValue value = { 0 };

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, nickname);
    return tp_cli_dbus_properties_call_set (account, -1,
	MC_IFACE_ACCOUNT, "Nickname", &value,
	callback, user_data, destroy, weak_object);
}

/**
 * mc_account_set_automatic_presence:
 * @account: the #McAccount.
 * @type: the presence type.
 * @status: the presence status string.
 * @message: the presence status message.
 * @callback: callback to be invoked when the operation completes, or %NULL.
 * @user_data: user data for @callback.
 * @destroy: #GDestroyNotify function for @user_data.
 * @weak_object: if not NULL, a GObject which will be weakly referenced; if it
 * is destroyed, this call will automatically be cancelled. Must be NULL if
 * callback is NULL.
 *
 * Set the automatic presence of @account.
 *
 * Returns: a #TpProxyPendingCall for the underlying D-Bus call.
 */
TpProxyPendingCall *
mc_account_set_automatic_presence (McAccount *account,
				   TpConnectionPresenceType type,
				   const gchar *status,
				   const gchar *message,
				   tp_cli_dbus_properties_callback_for_set callback,
				   gpointer user_data,
				   GDestroyNotify destroy,
				   GObject *weak_object)
{
    TpProxyPendingCall *call;
    GValue value = { 0 };

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    set_presence_gvalue (&value, type, status, message);
    call = tp_cli_dbus_properties_call_set (account, -1,
	MC_IFACE_ACCOUNT, "AutomaticPresence", &value,
	callback, user_data, destroy, weak_object);
    g_value_unset (&value);
    return call;
}

/**
 * mc_account_set_requested_presence:
 * @account: the #McAccount.
 * @type: the presence type.
 * @status: the presence status string.
 * @message: the presence status message.
 * @callback: callback to be invoked when the operation completes, or %NULL.
 * @user_data: user data for @callback.
 * @destroy: #GDestroyNotify function for @user_data.
 * @weak_object: if not NULL, a GObject which will be weakly referenced; if it
 * is destroyed, this call will automatically be cancelled. Must be NULL if
 * callback is NULL.
 *
 * Set the requested presence of @account.
 *
 * Returns: a #TpProxyPendingCall for the underlying D-Bus call.
 */
TpProxyPendingCall *
mc_account_set_requested_presence (McAccount *account,
				   TpConnectionPresenceType type,
				   const gchar *status,
				   const gchar *message,
				   tp_cli_dbus_properties_callback_for_set callback,
				   gpointer user_data,
				   GDestroyNotify destroy,
				   GObject *weak_object)
{
    TpProxyPendingCall *call;
    GValue value = { 0 };

    g_return_val_if_fail (MC_IS_ACCOUNT (account), NULL);
    set_presence_gvalue (&value, type, status, message);
    call = tp_cli_dbus_properties_call_set (account, -1,
	MC_IFACE_ACCOUNT, "RequestedPresence", &value,
	callback, user_data, destroy, weak_object);
    g_value_unset (&value);
    return call;
}

