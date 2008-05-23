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
#include <telepathy-glib/util.h>
#include <telepathy-glib/gtypes.h>
#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-compat.h"
#include "mcd-account-conditions.h"
#include "mcd-account-connection.h"
#include "mcd-account-manager.h"
#include "mcd-signals-marshal.h"
#include "mcd-manager.h"
#include "mcd-master.h"
#include "mcd-dbusprop.h"
#include "_gen/interfaces.h"

#define MAX_KEY_LENGTH	64
#define MC_AVATAR_FILENAME	"avatar.bin"

#define MCD_ACCOUNT_PRIV(account) (MCD_ACCOUNT (account)->priv)

static void account_iface_init (McSvcAccountClass *iface,
			       	gpointer iface_data);
static void properties_iface_init (TpSvcDBusPropertiesClass *iface,
				   gpointer iface_data);
static void account_avatar_iface_init (McSvcAccountInterfaceAvatarClass *iface,
				       gpointer iface_data);

typedef void (*McdOnlineRequestCb) (McdAccount *account, gpointer userdata,
				    const GError *error);

static const McdDBusProp account_properties[];
static const McdDBusProp account_avatar_properties[];

static const McdInterfaceData account_interfaces[] = {
    MCD_IMPLEMENT_IFACE (mc_svc_account_get_type, account, MC_IFACE_ACCOUNT),
    MCD_IMPLEMENT_IFACE (mc_svc_account_interface_avatar_get_type,
			 account_avatar,
			 MC_IFACE_ACCOUNT_INTERFACE_AVATAR),
    MCD_IMPLEMENT_IFACE (mc_svc_account_interface_compat_get_type,
			 account_compat,
			 MC_IFACE_ACCOUNT_INTERFACE_COMPAT),
    MCD_IMPLEMENT_IFACE (mc_svc_account_interface_conditions_get_type,
			 account_conditions,
			 MC_IFACE_ACCOUNT_INTERFACE_CONDITIONS),
    { G_TYPE_INVALID, }
};

G_DEFINE_TYPE_WITH_CODE (McdAccount, mcd_account, G_TYPE_OBJECT,
			 MCD_DBUS_INIT_INTERFACES (account_interfaces);
			 G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
						properties_iface_init);
			)

struct _McdAccountPrivate
{
    gchar *unique_name;
    gchar *object_path;
    gchar *manager_name;
    gchar *protocol_name;

    /* DBUS connection */
    TpDBusDaemon *dbus_daemon;

    McdConnection *connection;
    McdManager *manager;
    GKeyFile *keyfile;		/* configuration file */

    /* connection status */
    TpConnectionStatus conn_status;
    TpConnectionStatusReason conn_reason;

    /* current presence fields */
    TpConnectionPresenceType curr_presence_type;
    gchar *curr_presence_status;
    gchar *curr_presence_message;

    /* requested presence fields */
    TpConnectionPresenceType req_presence_type;
    gchar *req_presence_status;
    gchar *req_presence_message;

    /* automatic presence fields */
    TpConnectionPresenceType auto_presence_type;
    gchar *auto_presence_status; /* TODO: consider loading these from the
				    configuration file as needed */
    gchar *auto_presence_message;

    GHashTable *online_requests; /* callbacks with userdata to be called when
				    the account will be online */

    guint connect_automatically : 1;
    guint enabled : 1;
    guint valid : 1;

    /* These fields are used to cache the changed properties */
    GHashTable *changed_properties;
    GArray *property_values;
    guint properties_source;
};

typedef struct {
    McdAccount *account;
    GError *error;
} McdOnlineRequestData;

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
    PROP_KEYFILE,
    PROP_NAME,
};

guint _mcd_account_signals[LAST_SIGNAL] = { 0 };

static void
process_online_request (gpointer key, gpointer cb_userdata, gpointer userdata)
{
    McdOnlineRequestCb callback = key;
    McdOnlineRequestData *data = userdata;

    callback (data->account, cb_userdata, data->error);
}

static gboolean
load_manager (McdAccountPrivate *priv)
{
    McdMaster *master;

    if (G_UNLIKELY (!priv->manager_name)) return FALSE;
    master = mcd_master_get_default ();
    priv->manager = mcd_master_lookup_manager (master, priv->manager_name);
    if (priv->manager)
    {
	g_object_ref (priv->manager);
	return TRUE;
    }
    else
	return FALSE;
}

/* Returns the data dir for the given account name.
 * Returned string must be freed by caller. */
static gchar *
get_account_data_path (McdAccountPrivate *priv)
{
    const gchar *base;

    base = g_getenv ("MC_ACCOUNT_DIR");
    if (!base)
	base = ACCOUNTS_DIR;
    if (!base)
	return NULL;

    if (base[0] == '~')
	return g_build_filename (g_get_home_dir(), base + 1,
				 priv->unique_name, NULL);
    else
	return g_build_filename (base, priv->unique_name, NULL);
}

static void
on_connection_abort (McdConnection *connection, McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);

    g_debug ("%s called (%p, account %s)", G_STRFUNC,
	     connection, priv->unique_name);
    g_object_unref (priv->connection);
    priv->connection = NULL;
}

static gboolean
mcd_account_request_presence_int (McdAccount *account,
				  TpConnectionPresenceType type,
				  const gchar *status, const gchar *message)
{
    McdAccountPrivate *priv = account->priv;
    gboolean changed = FALSE;

    if (type >= TP_CONNECTION_PRESENCE_TYPE_AVAILABLE && !priv->enabled)
	return FALSE;

    if (priv->req_presence_type != type)
    {
	priv->req_presence_type = type;
	changed = TRUE;
    }
    if (tp_strdiff (priv->req_presence_status, status))
    {
	g_free (priv->req_presence_status);
	priv->req_presence_status = g_strdup (status);
	changed = TRUE;
    }
    if (tp_strdiff (priv->req_presence_message, message))
    {
	g_free (priv->req_presence_message);
	priv->req_presence_message = g_strdup (message);
	changed = TRUE;
    }

    if (!changed) return changed;

    if (type >= TP_CONNECTION_PRESENCE_TYPE_AVAILABLE && !priv->connection)
    {
	mcd_account_connection_begin (account);
    }

    g_signal_emit (account,
		   _mcd_account_signals[REQUESTED_PRESENCE_CHANGED], 0,
		   type, status, message);
    return TRUE;
}

void
_mcd_account_connect (McdAccount *account, GHashTable *params)
{
    McdAccountPrivate *priv = account->priv;

    if (!priv->connection)
    {
	if (!priv->manager && !load_manager (priv))
	{
	    g_warning ("%s: Could not find manager `%s'",
		       G_STRFUNC, priv->manager_name);
	    return;
	}

	priv->connection = mcd_manager_create_connection (priv->manager,
							  account);
	g_return_if_fail (priv->connection != NULL);
	g_object_ref (priv->connection);
	g_signal_connect (priv->connection, "abort",
			  G_CALLBACK (on_connection_abort), account);
    }
    mcd_connection_connect (priv->connection, params);
}

#ifdef DELAY_PROPERTY_CHANGED
static gboolean
emit_property_changed (gpointer userdata)
{
    McdAccount *account = MCD_ACCOUNT (userdata);
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called", G_STRFUNC);
    mc_svc_account_emit_account_property_changed (account,
						  priv->changed_properties);

    g_hash_table_remove_all (priv->changed_properties);

    g_array_free (priv->property_values, TRUE);
    priv->property_values = NULL;

    priv->properties_source = 0;
    return FALSE;
}
#endif

/*
 * This function is responsible of emitting the AccountPropertyChanged signal.
 * One possible improvement would be to save the HashTable and have the signal
 * emitted in an idle function (or a timeout function with a very small delay)
 * to group together several property changes that occur at the same time.
 */
static void
mcd_account_changed_property (McdAccount *account, const gchar *key,
			      const GValue *value)
{
#ifdef DELAY_PROPERTY_CHANGED
    McdAccountPrivate *priv = account->priv;
    GValue *val;

    g_debug ("%s called: %s", G_STRFUNC, key);
    if (priv->changed_properties &&
	g_hash_table_lookup (priv->changed_properties, key))
    {
	/* the changed property was also changed before; then let's force the
	 * emission of the signal now, so that the property will appear in two
	 * separate signals */
	g_debug ("Forcibly emit PropertiesChanged now");
	g_source_remove (priv->properties_source);
	emit_property_changed (account);
    }

    if (G_UNLIKELY (!priv->changed_properties))
    {
	priv->changed_properties =
	    g_hash_table_new_full (g_str_hash, g_str_equal,
				   NULL, (GDestroyNotify)g_value_unset);
    }

    if (priv->properties_source == 0)
    {
	g_debug ("First changed property");
	priv->property_values = g_array_sized_new (FALSE, FALSE,
						   sizeof (GValue), 4);
	priv->properties_source = g_timeout_add (10, emit_property_changed,
						 account);
    }
    g_array_append_vals (priv->property_values, value, 1);
    val = &g_array_index (priv->property_values, GValue,
			  priv->property_values->len - 1);
    memset (val, 0, sizeof (GValue));
    g_value_init (val, G_VALUE_TYPE (value));
    g_value_copy (value, val);
    g_hash_table_insert (priv->changed_properties, (gpointer)key, val);
#else
    GHashTable *properties;

    g_debug ("%s called: %s", G_STRFUNC, key);
    properties = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_insert (properties, (gpointer)key, (gpointer)value);
    mc_svc_account_emit_account_property_changed (account,
						  properties);

    g_hash_table_destroy (properties);
#endif
}

static gboolean
mcd_account_set_string_val (McdAccount *account, const gchar *key,
			    const GValue *value)
{
    McdAccountPrivate *priv = account->priv;
    const gchar *string;
    gchar *old_string;

    string = g_value_get_string (value);
    old_string = g_key_file_get_string (priv->keyfile, priv->unique_name,
				       	key, NULL);
    if (old_string == string ||
	(old_string && string && strcmp (old_string, string) == 0))
    {
	g_free (old_string);
	return TRUE;
    }

    g_free (old_string);
    if (string && string[0] != 0)
	g_key_file_set_string (priv->keyfile, priv->unique_name,
			       key, string);
    else
    {
	g_key_file_remove_key (priv->keyfile, priv->unique_name,
			       key, NULL);
	string = NULL;
    }
    mcd_account_manager_write_conf (priv->keyfile);
    mcd_account_changed_property (account, key, value);
    return TRUE;
}

static void
mcd_account_get_string_val (McdAccount *account, const gchar *key,
			    GValue *value)
{
    McdAccountPrivate *priv = account->priv;
    gchar *string;

    string = g_key_file_get_string (priv->keyfile, priv->unique_name,
				    key, NULL);
    g_value_init (value, G_TYPE_STRING);
    g_value_take_string (value, string);
}

static void
set_display_name (TpSvcDBusProperties *self, const gchar *name,
		  const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    mcd_account_set_string_val (account, name, value);
}

static void
get_display_name (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    mcd_account_get_string_val (account, name, value);
}

static void
set_icon (TpSvcDBusProperties *self, const gchar *name, const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    mcd_account_set_string_val (account, name, value);
}

static void
get_icon (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    mcd_account_get_string_val (account, name, value);
}

static void
get_valid (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, priv->valid);
}

static void
set_enabled (TpSvcDBusProperties *self, const gchar *name, const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    gboolean enabled;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    enabled = g_value_get_boolean (value);
    if (priv->enabled != enabled)
    {
	if (!enabled)
	    mcd_account_request_presence (account,
					  TP_CONNECTION_PRESENCE_TYPE_OFFLINE,
					  "offline", NULL);

	g_key_file_set_boolean (priv->keyfile, priv->unique_name,
				MC_ACCOUNTS_KEY_ENABLED,
			       	enabled);
	priv->enabled = enabled;
	mcd_account_manager_write_conf (priv->keyfile);
	mcd_account_changed_property (account, name, value);
    }
}

static void
get_enabled (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, priv->enabled);
}

static void
set_nickname (TpSvcDBusProperties *self, const gchar *name, const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    mcd_account_set_string_val (account, name, value);
}

static void
get_nickname (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    mcd_account_get_string_val (account, name, value);
}

static void
set_avatar (TpSvcDBusProperties *self, const gchar *name, const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    const gchar *mime_type;
    const GArray *avatar;
    GValueArray *va;
    GError *error = NULL;
    gboolean changed;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    va = g_value_get_boxed (value);
    avatar = g_value_get_boxed (va->values);
    mime_type = g_value_get_string (va->values + 1);
    changed = mcd_account_set_avatar (account, avatar, mime_type, NULL,
				      &error);
    if (error)
    {
	g_warning ("%s: failed: %s", G_STRFUNC, error->message);
	g_error_free (error);
	return;
    }
    if (changed)
	mc_svc_account_interface_avatar_emit_avatar_changed (account);
}

static void
get_avatar (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    gchar *mime_type;
    GArray *avatar = NULL;
    GType type;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);

    mcd_account_get_avatar (account, &avatar, &mime_type);

    type = dbus_g_type_get_struct ("GValueArray",
				   dbus_g_type_get_collection ("GArray",
							       G_TYPE_UCHAR),
				   G_TYPE_STRING,
				   G_TYPE_INVALID);
    g_value_init (value, type);
    g_value_take_boxed (value, dbus_g_type_specialized_construct (type));
    GValueArray *va = (GValueArray *) g_value_get_boxed (value);
    g_value_take_boxed (va->values, avatar);
    g_value_take_string (va->values + 1, mime_type);
}

static void
get_parameters (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    GHashTable *parameters;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    parameters = mcd_account_get_parameters (account);
    g_value_init (value, TP_HASH_TYPE_STRING_VARIANT_MAP);
    g_value_take_boxed (value, parameters);
}

static void
get_preset_parameters (TpSvcDBusProperties *self, const gchar *name,
		       GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    GHashTable *parameters;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    parameters = g_hash_table_new (g_str_hash, g_str_equal);
    g_warning ("Preset parameters not used in current implementation");
    g_value_init (value, TP_HASH_TYPE_STRING_VARIANT_MAP);
    g_value_take_boxed (value, parameters);
}

static void
set_automatic_presence (TpSvcDBusProperties *self,
			const gchar *name, const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    const gchar *status, *message;
    gint type;
    gboolean changed = FALSE;
    GValueArray *va;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    va = g_value_get_boxed (value);
    type = (gint)g_value_get_uint (va->values);
    status = g_value_get_string (va->values + 1);
    message = g_value_get_string (va->values + 2);
    g_debug ("setting automatic presence: %d, %s, %s", type, status, message);

    if (priv->auto_presence_type != type)
    {
	g_key_file_set_integer (priv->keyfile, priv->unique_name,
				MC_ACCOUNTS_KEY_AUTO_PRESENCE_TYPE, type);
	priv->auto_presence_type = type;
	changed = TRUE;
    }
    if (tp_strdiff (priv->auto_presence_status, status))
    {
	if (status && status[0] != 0)
	    g_key_file_set_string (priv->keyfile, priv->unique_name,
				   MC_ACCOUNTS_KEY_AUTO_PRESENCE_STATUS,
				   status);
	else
	    g_key_file_remove_key (priv->keyfile, priv->unique_name,
				   MC_ACCOUNTS_KEY_AUTO_PRESENCE_STATUS,
				   NULL);
	g_free (priv->auto_presence_status);
	priv->auto_presence_status = g_strdup (status);
	changed = TRUE;
    }
    if (tp_strdiff (priv->auto_presence_message, message))
    {
	if (message && message[0] != 0)
	    g_key_file_set_string (priv->keyfile, priv->unique_name,
				   MC_ACCOUNTS_KEY_AUTO_PRESENCE_MESSAGE,
				   message);
	else
	    g_key_file_remove_key (priv->keyfile, priv->unique_name,
				   MC_ACCOUNTS_KEY_AUTO_PRESENCE_MESSAGE,
				   NULL);
	g_free (priv->auto_presence_message);
	priv->auto_presence_message = g_strdup (message);
	changed = TRUE;
    }

    if (changed)
    {
	mcd_account_manager_write_conf (priv->keyfile);
	mcd_account_changed_property (account, name, value);
    }
}

static void
get_automatic_presence (TpSvcDBusProperties *self,
			const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    gchar *presence, *message;
    gint presence_type;
    GType type;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);

    presence_type = priv->auto_presence_type;
    presence = priv->auto_presence_status;
    message = priv->auto_presence_message;

    type = MC_STRUCT_TYPE_ACCOUNT_PRESENCE;
    g_value_init (value, type);
    g_value_take_boxed (value, dbus_g_type_specialized_construct (type));
    GValueArray *va = (GValueArray *) g_value_get_boxed (value);
    g_value_set_uint (va->values, presence_type);
    g_value_set_static_string (va->values + 1, presence);
    g_value_set_static_string (va->values + 2, message);
}

static void
set_connect_automatically (TpSvcDBusProperties *self,
			   const gchar *name, const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    gboolean connect_automatically;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    connect_automatically = g_value_get_boolean (value);
    if (priv->connect_automatically != connect_automatically)
    {
	g_key_file_set_boolean (priv->keyfile, priv->unique_name,
				MC_ACCOUNTS_KEY_CONNECT_AUTOMATICALLY,
			       	connect_automatically);
	priv->connect_automatically = connect_automatically;
	mcd_account_manager_write_conf (priv->keyfile);
	mcd_account_changed_property (account, name, value);
    }
}

static void
get_connect_automatically (TpSvcDBusProperties *self,
			   const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, priv->connect_automatically);
}

static void
get_connection (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    const gchar *object_path;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    g_value_init (value, G_TYPE_STRING);
    if (priv->connection &&
	(object_path = mcd_connection_get_object_path (priv->connection)))
	g_value_set_string (value, object_path);
    else
	g_value_set_static_string (value, "");
}

static void
get_connection_status (TpSvcDBusProperties *self,
		       const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    TpConnectionStatus status;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    if (priv->connection)
	status = mcd_connection_get_connection_status (priv->connection);
    else
	status = TP_CONNECTION_STATUS_DISCONNECTED;

    g_value_init (value, G_TYPE_UINT);
    g_value_set_uint (value, status);
}

static void
get_connection_status_reason (TpSvcDBusProperties *self,
			      const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    TpConnectionStatusReason reason;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    if (priv->connection)
	reason = mcd_connection_get_connection_status_reason (priv->connection);
    else
	reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;

    g_value_init (value, G_TYPE_UINT);
    g_value_set_uint (value, reason);
}

static void
get_current_presence (TpSvcDBusProperties *self, const gchar *name,
		      GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    gchar *status, *message;
    gint presence_type;
    GType type;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);

    presence_type = priv->curr_presence_type;
    status = priv->curr_presence_status;
    message = priv->curr_presence_message;

    type = MC_STRUCT_TYPE_ACCOUNT_PRESENCE;
    g_value_init (value, type);
    g_value_take_boxed (value, dbus_g_type_specialized_construct (type));
    GValueArray *va = (GValueArray *) g_value_get_boxed (value);
    g_value_set_uint (va->values, presence_type);
    g_value_set_static_string (va->values + 1, status);
    g_value_set_static_string (va->values + 2, message);
}

static void
set_requested_presence (TpSvcDBusProperties *self,
			const gchar *name, const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    const gchar *status, *message;
    gint type;
    GValueArray *va;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    va = g_value_get_boxed (value);
    type = (gint)g_value_get_uint (va->values);
    status = g_value_get_string (va->values + 1);
    message = g_value_get_string (va->values + 2);
    g_debug ("setting requested presence: %d, %s, %s", type, status, message);

    if (mcd_account_request_presence_int (account, type, status, message))
    {
	mcd_account_changed_property (account, name, value);
    }
}

static void
get_requested_presence (TpSvcDBusProperties *self,
			const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    gchar *presence, *message;
    gint presence_type;
    GType type;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);

    presence_type = priv->req_presence_type;
    presence = priv->req_presence_status;
    message = priv->req_presence_message;

    type = MC_STRUCT_TYPE_ACCOUNT_PRESENCE;
    g_value_init (value, type);
    g_value_take_boxed (value, dbus_g_type_specialized_construct (type));
    GValueArray *va = (GValueArray *) g_value_get_boxed (value);
    g_value_set_uint (va->values, presence_type);
    g_value_set_static_string (va->values + 1, presence);
    g_value_set_static_string (va->values + 2, message);
}

static void
get_normalized_name (TpSvcDBusProperties *self,
		     const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    mcd_account_get_string_val (account, name, value);
}

static const McdDBusProp account_properties[] = {
    { "Interfaces", NULL, mcd_dbus_get_interfaces },
    { "DisplayName", set_display_name, get_display_name },
    { "Icon", set_icon, get_icon },
    { "Valid", NULL, get_valid },
    { "Enabled", set_enabled, get_enabled },
    { "Nickname", set_nickname, get_nickname },
    { "Parameters", NULL, get_parameters },
    { "PresetParameters", NULL, get_preset_parameters },
    { "AutomaticPresence", set_automatic_presence, get_automatic_presence },
    { "ConnectAutomatically", set_connect_automatically, get_connect_automatically },
    { "Connection", NULL, get_connection },
    { "ConnectionStatus", NULL, get_connection_status },
    { "ConnectionStatusReason", NULL, get_connection_status_reason },
    { "CurrentPresence", NULL, get_current_presence },
    { "RequestedPresence", set_requested_presence, get_requested_presence },
    { "NormalizedName", NULL, get_normalized_name },
    { 0 },
};

static const McdDBusProp account_avatar_properties[] = {
    { "Avatar", set_avatar, get_avatar },
    { 0 },
};

static void
account_avatar_iface_init (McSvcAccountInterfaceAvatarClass *iface,
			   gpointer iface_data)
{
}

static void
properties_iface_init (TpSvcDBusPropertiesClass *iface, gpointer iface_data)
{
#define IMPLEMENT(x) tp_svc_dbus_properties_implement_##x (\
    iface, dbusprop_##x)
    IMPLEMENT(set);
    IMPLEMENT(get);
    IMPLEMENT(get_all);
#undef IMPLEMENT
}

static GType
mc_param_type (McdProtocolParam *param)
{
    switch (param->signature[0])
    {
    case DBUS_TYPE_STRING:
	return G_TYPE_STRING;
    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
	return G_TYPE_INT;
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
	return G_TYPE_UINT;
    case DBUS_TYPE_BOOLEAN:
	return G_TYPE_BOOLEAN;
    default:
	g_warning ("%s: skipping parameter %s, unknown type %s", G_STRFUNC, param->name, param->signature);
    }
    return G_TYPE_INVALID;
}

static gboolean
has_param (McdAccountPrivate *priv, const gchar *name)
{
    gchar key[MAX_KEY_LENGTH];

    snprintf (key, sizeof (key), "param-%s", name);
    return g_key_file_has_key (priv->keyfile, priv->unique_name, key, NULL);
}

gboolean
mcd_account_delete (McdAccount *account, GError **error)
{
    McdAccountPrivate *priv = account->priv;
    gchar *data_dir_str;
    GDir *data_dir;

    g_key_file_remove_group (priv->keyfile, priv->unique_name, error);
    if (error && *error)
    {
	g_warning ("Could not remove GConf dir (%s)",
		   error ? (*error)->message : "");
	return FALSE;
    }

    data_dir_str = get_account_data_path (priv);
    data_dir = g_dir_open (data_dir_str, 0, NULL);
    if (data_dir)
    {
	const gchar *filename;
	while ((filename = g_dir_read_name (data_dir)) != NULL)
	{
	    gchar *path;
	    path = g_build_filename (data_dir_str, filename, NULL);
	    g_remove (path);
	    g_free (path);
	}
	g_dir_close (data_dir);
	g_rmdir (data_dir_str);
    }
    g_free (data_dir_str);
    mcd_account_manager_write_conf (priv->keyfile);
    return TRUE;
}

static void
account_remove (McSvcAccount *self, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    g_debug ("%s called", G_STRFUNC);
    if (!mcd_account_delete (MCD_ACCOUNT (self), &error))
    {
	if (!error)
	    g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
			 "Internal error");
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }
    mc_svc_account_emit_removed (self);
    mc_svc_account_return_from_remove (context);
}

gboolean
mcd_account_check_parameters (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;
    const GArray *parameters;
    gboolean valid;
    gint i;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);
    parameters = mcd_manager_get_parameters (priv->manager,
					     priv->protocol_name);
    if (!parameters) return FALSE;
    valid = TRUE;
    for (i = 0; i < parameters->len; i++)
    {
	McdProtocolParam *param;
	GType type;

	param = &g_array_index (parameters, McdProtocolParam, i);
	type = mc_param_type (param);
	if (param->flags & MCD_PROTOCOL_PARAM_REQUIRED)
	{
	    if (!has_param (priv, param->name))
	    {
		g_debug ("missing required parameter %s", param->name);
		valid = FALSE;
		break;
	    }
	}
    }

    return valid;
}

static void
set_parameter (gpointer ht_key, gpointer ht_value, gpointer userdata)
{
    McdAccountPrivate *priv = userdata;
    const gchar *name = ht_key;
    const GValue *value = ht_value;
    gchar key[MAX_KEY_LENGTH];

    snprintf (key, sizeof (key), "param-%s", name);
    switch (G_VALUE_TYPE (value))
    {
    case G_TYPE_STRING:
	g_key_file_set_string (priv->keyfile, priv->unique_name, key,
			       g_value_get_string (value));
	break;
    case G_TYPE_UINT:
	g_key_file_set_integer (priv->keyfile, priv->unique_name, key,
				g_value_get_uint (value));
	break;
    case G_TYPE_INT:
	g_key_file_set_integer (priv->keyfile, priv->unique_name, key,
				g_value_get_int (value));
	break;
    case G_TYPE_BOOLEAN:
	g_key_file_set_boolean (priv->keyfile, priv->unique_name, key,
				g_value_get_boolean (value));
	break;
    default:
	g_warning ("Unexpected param type %s", G_VALUE_TYPE_NAME (value));
    }
}

gboolean
mcd_account_set_parameters (McdAccount *account, GHashTable *params,
			    GError **error)
{
    McdAccountPrivate *priv = account->priv;
    const GArray *parameters;
    GValue *value;
    guint i, n_params = 0;

    g_debug ("%s called", G_STRFUNC);
    if (!priv->manager && !load_manager (priv)) return FALSE;

    parameters = mcd_manager_get_parameters (priv->manager,
					     priv->protocol_name);
    for (i = 0; i < parameters->len; i++)
    {
	McdProtocolParam *param;
	GType type;

	param = &g_array_index (parameters, McdProtocolParam, i);
	type = mc_param_type (param);
	value = g_hash_table_lookup (params, param->name);
	if (value)
	{
	    g_debug ("Got param %s", param->name);
	    if (G_VALUE_TYPE (value) != type)
	    {
		/* FIXME: define proper error */
		g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
			     "parameter %s must be of type %s, not %s",
			     param->name,
			     g_type_name (type), G_VALUE_TYPE_NAME (value));
		return FALSE;
	    }
	    n_params++;
	}
    }

    if (n_params != g_hash_table_size (params))
    {
	g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "Not all parameters were recognized");
	return FALSE;
    }

    g_hash_table_foreach (params, set_parameter, priv);
    return TRUE;
}

static inline void
mcd_account_unset_parameters (McdAccount *account, const gchar **params)
{
    McdAccountPrivate *priv = account->priv;
    const gchar **param;
    gchar key[MAX_KEY_LENGTH];

    for (param = params; *param != NULL; param++)
    {
	snprintf (key, sizeof (key), "param-%s", *param);
	g_key_file_remove_key (priv->keyfile, priv->unique_name,
			       key, NULL);
	g_debug ("unset param %s", *param);
    }
}

static void
account_update_parameters (McSvcAccount *self, GHashTable *set,
			   const gchar **unset, DBusGMethodInvocation *context)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    GHashTable *parameters;
    GValue value = { 0 };
    GError *error = NULL;

    g_debug ("%s called for %s", G_STRFUNC, priv->unique_name);

    if (!mcd_account_set_parameters (account, set, &error))
    {
	if (!error)
	    g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
			 "Internal error");
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }

    mcd_account_unset_parameters (account, unset);

    /* emit the PropertiesChanged signal */
    parameters = mcd_account_get_parameters (account);
    g_value_init (&value, TP_HASH_TYPE_STRING_VARIANT_MAP);
    g_value_take_boxed (&value, parameters);
    mcd_account_changed_property (account, "Parameters", &value);
    g_value_unset (&value);

    mcd_account_check_validity (account);
    mcd_account_manager_write_conf (priv->keyfile);
    mc_svc_account_return_from_update_parameters (context);
}

static void
account_iface_init (McSvcAccountClass *iface, gpointer iface_data)
{
#define IMPLEMENT(x) mc_svc_account_implement_##x (\
    iface, account_##x)
    IMPLEMENT(remove);
    IMPLEMENT(update_parameters);
#undef IMPLEMENT
}

static void
register_dbus_service (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;
    DBusGConnection *dbus_connection;

    if (!priv->dbus_daemon || !priv->object_path) return;

    dbus_connection = TP_PROXY (priv->dbus_daemon)->dbus_connection;

    if (G_LIKELY (dbus_connection))
	dbus_g_connection_register_g_object (dbus_connection,
					     priv->object_path,
					     (GObject *)account);
}

static gboolean
mcd_account_setup (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;
    gboolean valid;

    if (!priv->keyfile || !priv->unique_name) return FALSE;

    priv->manager_name =
	g_key_file_get_string (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_MANAGER, NULL);
    if (!priv->manager_name) return FALSE;

    priv->protocol_name =
	g_key_file_get_string (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_PROTOCOL, NULL);
    if (!priv->protocol_name) return FALSE;

    priv->object_path = g_strconcat (MC_ACCOUNT_DBUS_OBJECT_BASE,
				     priv->unique_name, NULL);

    priv->enabled =
	g_key_file_get_boolean (priv->keyfile, priv->unique_name,
				MC_ACCOUNTS_KEY_ENABLED, NULL);

    priv->connect_automatically =
	g_key_file_get_boolean (priv->keyfile, priv->unique_name,
				MC_ACCOUNTS_KEY_CONNECT_AUTOMATICALLY, NULL);

    /* check the manager */
    if (!priv->manager && !load_manager (priv))
    {
	g_warning ("Could not find manager `%s'", priv->manager_name);
	return FALSE;
    }

    valid = mcd_account_check_parameters (account);
    priv->valid = valid;

    /* load the automatic presence */
    priv->auto_presence_type =
	g_key_file_get_integer (priv->keyfile, priv->unique_name,
				MC_ACCOUNTS_KEY_AUTO_PRESENCE_TYPE, NULL);
    priv->auto_presence_status =
	g_key_file_get_string (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_AUTO_PRESENCE_STATUS,
			       NULL);
    priv->auto_presence_message =
	g_key_file_get_string (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_AUTO_PRESENCE_MESSAGE,
			       NULL);

    return TRUE;
}

static void
set_property (GObject *obj, guint prop_id,
	      const GValue *val, GParamSpec *pspec)
{
    McdAccount *account = MCD_ACCOUNT (obj);
    McdAccountPrivate *priv = account->priv;

    switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
	if (priv->dbus_daemon)
	    g_object_unref (priv->dbus_daemon);
	priv->dbus_daemon = TP_DBUS_DAEMON (g_value_dup_object (val));
	register_dbus_service (MCD_ACCOUNT (obj));
	break;
    case PROP_KEYFILE:
	g_assert (priv->keyfile == NULL);
	priv->keyfile = g_value_get_pointer (val);
	if (mcd_account_setup (account))
	    register_dbus_service (account);
	break;
    case PROP_NAME:
	g_assert (priv->unique_name == NULL);
	priv->unique_name = g_value_dup_string (val);
	if (G_UNLIKELY (!priv->unique_name))
	{
	    g_warning ("unique name cannot be NULL");
	    return;
	}
	if (mcd_account_setup (account))
	    register_dbus_service (account);
	break;

    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
get_property (GObject *obj, guint prop_id,
	      GValue *val, GParamSpec *pspec)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (obj);

    switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
	g_value_set_object (val, priv->dbus_daemon);
	break;
    case PROP_NAME:
	g_value_set_string (val, priv->unique_name);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_account_finalize (GObject *object)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (object);

    if (priv->changed_properties)
	g_hash_table_destroy (priv->changed_properties);
    if (priv->property_values)
	g_array_free (priv->property_values, TRUE);
    if (priv->properties_source != 0)
	g_source_remove (priv->properties_source);

    g_free (priv->curr_presence_status);
    g_free (priv->curr_presence_message);

    g_free (priv->req_presence_status);
    g_free (priv->req_presence_message);

    g_free (priv->auto_presence_status);
    g_free (priv->auto_presence_message);

    g_free (priv->manager_name);
    g_free (priv->protocol_name);
    g_free (priv->unique_name);
    g_free (priv->object_path);

    G_OBJECT_CLASS (mcd_account_parent_class)->finalize (object);
}

static void
_mcd_account_dispose (GObject *object)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (object);

    if (priv->online_requests)
    {
	McdOnlineRequestData data;

	data.account = MCD_ACCOUNT (object);
	data.error = NULL;
	g_set_error (&data.error, TP_ERRORS, TP_ERROR_DISCONNECTED,
		     "Disposing account %s", priv->unique_name);
	g_hash_table_foreach (priv->online_requests,
			      process_online_request,
			      &data);
	g_error_free (data.error);
	g_hash_table_destroy (priv->online_requests);
	priv->online_requests = NULL;
    }

    if (priv->manager)
    {
	g_object_unref (priv->manager);
	priv->manager = NULL;
    }

    if (priv->connection)
    {
	g_object_unref (priv->connection);
	priv->connection = NULL;
    }

    if (priv->dbus_daemon)
    {
	g_object_unref (priv->dbus_daemon);
	priv->dbus_daemon = NULL;
    }
    G_OBJECT_CLASS (mcd_account_parent_class)->dispose (object);
}

static void
mcd_account_class_init (McdAccountClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdAccountPrivate));

    object_class->dispose = _mcd_account_dispose;
    object_class->finalize = _mcd_account_finalize;
    object_class->set_property = set_property;
    object_class->get_property = get_property;

    g_object_class_install_property (object_class, PROP_DBUS_DAEMON,
				     g_param_spec_object ("dbus-daemon",
							  _("DBus daemon"),
							  _("DBus daemon"),
							  TP_TYPE_DBUS_DAEMON,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (object_class, PROP_KEYFILE,
				     g_param_spec_pointer ("keyfile",
							   _("Conf file"),
							   _("Conf file"),
							   G_PARAM_WRITABLE |
							   G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (object_class, PROP_NAME,
				     g_param_spec_string ("name",
							  _("Unique name"),
							  _("Unique name"),
							  NULL,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));

    /* Signals */
    _mcd_account_signals[CONNECTION_STATUS_CHANGED] =
	g_signal_new ("connection-status-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, mcd_marshal_VOID__UINT_UINT,
		      G_TYPE_NONE,
		      2, G_TYPE_UINT, G_TYPE_UINT);
    _mcd_account_signals[CURRENT_PRESENCE_CHANGED] =
	g_signal_new ("current-presence-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, mcd_marshal_VOID__UINT_STRING_STRING,
		      G_TYPE_NONE,
		      3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
    _mcd_account_signals[REQUESTED_PRESENCE_CHANGED] =
	g_signal_new ("requested-presence-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, mcd_marshal_VOID__UINT_STRING_STRING,
		      G_TYPE_NONE,
		      3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
    _mcd_account_signals[VALIDITY_CHANGED] =
	g_signal_new ("validity-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, mcd_marshal_VOID__BOOLEAN,
		      G_TYPE_NONE, 1,
		      G_TYPE_BOOLEAN);
    _mcd_account_signals[AVATAR_CHANGED] =
	g_signal_new ("mcd-avatar-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, mcd_marshal_VOID__BOXED_STRING,
		      G_TYPE_NONE, 2,
		      dbus_g_type_get_collection ("GArray", G_TYPE_UCHAR),
		      G_TYPE_STRING);
    _mcd_account_signals[ALIAS_CHANGED] =
	g_signal_new ("alias-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, g_cclosure_marshal_VOID__STRING,
		      G_TYPE_NONE, 1, G_TYPE_STRING);
    _mcd_account_connection_class_init (klass);
}

static void
mcd_account_init (McdAccount *account)
{
    McdAccountPrivate *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE ((account),
					MCD_TYPE_ACCOUNT,
					McdAccountPrivate);
    account->priv = priv;

    /* initializes the interfaces */
    mcd_dbus_init_interfaces_instances (account);

    priv->conn_status = TP_CONNECTION_STATUS_DISCONNECTED;
}

McdAccount *
mcd_account_new (TpDBusDaemon *dbus_daemon, GKeyFile *keyfile,
		 const gchar *name)
{
    gpointer *obj;
    obj = g_object_new (MCD_TYPE_ACCOUNT,
			"dbus-daemon", dbus_daemon,
			"keyfile", keyfile,
			"name", name,
			NULL);
    return MCD_ACCOUNT (obj);
}

/*
 * mcd_account_is_valid:
 * @account: the #McdAccount.
 *
 * Checks that the account is usable:
 * - Manager, protocol and TODO presets (if specified) must exist
 * - All required parameters for the protocol must be set
 *
 * Returns: %TRUE if the account is valid, false otherwise.
 */
gboolean
mcd_account_is_valid (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    return priv->valid;
}

/**
 * mcd_account_is_enabled:
 * @account: the #McdAccount.
 *
 * Checks if the account is enabled:
 *
 * Returns: %TRUE if the account is enabled, false otherwise.
 */
gboolean
mcd_account_is_enabled (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    return priv->enabled;
}

const gchar *
mcd_account_get_unique_name (McdAccount *account)
{
    return account->priv->unique_name;
}

const gchar *
mcd_account_get_object_path (McdAccount *account)
{
    return account->priv->object_path;
}

static void
_g_value_free (gpointer data)
{
  GValue *value = (GValue *) data;
  g_value_unset (value);
  g_free (value);
}

static inline void
add_parameter (McdAccountPrivate *priv, McdProtocolParam *param,
	       GHashTable *params)
{
    GValue *value = NULL;
    GError *error = NULL;
    gchar key[MAX_KEY_LENGTH];
    gchar *v_string = NULL;
    gint v_int = 0;
    gboolean v_bool = FALSE;

    g_return_if_fail (param != NULL);
    g_return_if_fail (param->name != NULL);
    g_return_if_fail (param->signature != NULL);

    snprintf (key, sizeof (key), "param-%s", param->name);

    switch (param->signature[0])
    {
    case DBUS_TYPE_STRING:
	v_string = g_key_file_get_string (priv->keyfile, priv->unique_name,
					  key, &error);
	break;
    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
	v_int = g_key_file_get_integer (priv->keyfile, priv->unique_name,
					key, &error);
	break;
    case DBUS_TYPE_BOOLEAN:
	v_bool = g_key_file_get_boolean (priv->keyfile, priv->unique_name,
					 key, &error);
	break;
    default:
	g_warning ("%s: skipping parameter %s, unknown type %s", G_STRFUNC, param->name, param->signature);
	return;
    }

    if (error)
    {
	g_error_free (error);
	return;
    }
    value = g_new0(GValue, 1);

    switch (param->signature[0])
    {
    case DBUS_TYPE_STRING:
	g_value_init (value, G_TYPE_STRING);
	g_value_take_string (value, v_string);
	break;
    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
	g_value_init (value, G_TYPE_INT);
	g_value_set_int (value, v_int);
	break;
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
	g_value_init (value, G_TYPE_UINT);
	g_value_set_uint (value, (guint)v_int);
	break;
    case DBUS_TYPE_BOOLEAN:
	g_value_init (value, G_TYPE_BOOLEAN);
	g_value_set_boolean (value, v_bool);
	break;
    }

    g_hash_table_insert (params, g_strdup (param->name), value);
}

/**
 * mcd_account_get_parameters:
 * @account: the #McdAccount.
 *
 * Get the parameters set for this account.
 *
 * Returns: a #GHashTable containing the account parameters.
 */
GHashTable *
mcd_account_get_parameters (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    GHashTable *params;
    const GArray *parameters;
    gint i;

    g_debug ("%s called", G_STRFUNC);
    if (!priv->manager && !load_manager (priv)) return NULL;

    params = g_hash_table_new_full (g_str_hash, g_str_equal,
				    g_free, _g_value_free);
    parameters = mcd_manager_get_parameters (priv->manager,
					     priv->protocol_name);
    if (!parameters) return params;
    for (i = 0; i < parameters->len; i++)
    {
	McdProtocolParam *param;

	param = &g_array_index (parameters, McdProtocolParam, i);
	add_parameter (priv, param, params);
    }
    return params;
}

/**
 * mcd_account_request_presence:
 * @account: the #McdAccount.
 * @presence: a #TpConnectionPresenceType.
 * @status: presence status.
 * @message: presence status message.
 *
 * Request a presence status on the account.
 */
void
mcd_account_request_presence (McdAccount *account,
			      TpConnectionPresenceType presence,
			      const gchar *status, const gchar *message)
{
    if (mcd_account_request_presence_int (account, presence, status, message))
    {
	GValue value = { 0 };
	GType type;

	type = MC_STRUCT_TYPE_ACCOUNT_PRESENCE;
	g_value_init (&value, type);
	g_value_take_boxed (&value, dbus_g_type_specialized_construct (type));
	GValueArray *va = (GValueArray *) g_value_get_boxed (&value);
	g_value_set_uint (va->values, presence);
	g_value_set_static_string (va->values + 1, status);
	g_value_set_static_string (va->values + 2, message);
	mcd_account_changed_property (account, "RequestedPresence", &value);
	g_value_unset (&value);
    }
}

/**
 * mcd_account_set_current_presence:
 * @account: the #McdAccount.
 * @presence: a #TpConnectionPresenceType.
 * @status: presence status.
 * @message: presence status message.
 *
 * Set a presence status on the account.
 */
void
mcd_account_set_current_presence (McdAccount *account,
				  TpConnectionPresenceType presence,
				  const gchar *status, const gchar *message)
{
    McdAccountPrivate *priv = account->priv;
    gboolean changed = FALSE;
    GValue value = { 0 };
    GType type;


    if (priv->curr_presence_type != presence)
    {
	priv->curr_presence_type = presence;
	changed = TRUE;
    }
    if (tp_strdiff (priv->curr_presence_status, status))
    {
	g_free (priv->curr_presence_status);
	priv->curr_presence_status = g_strdup (status);
	changed = TRUE;
    }
    if (tp_strdiff (priv->curr_presence_message, message))
    {
	g_free (priv->curr_presence_message);
	priv->curr_presence_message = g_strdup (message);
	changed = TRUE;
    }

    if (!changed) return;

    type = MC_STRUCT_TYPE_ACCOUNT_PRESENCE;
    g_value_init (&value, type);
    g_value_take_boxed (&value, dbus_g_type_specialized_construct (type));
    GValueArray *va = (GValueArray *) g_value_get_boxed (&value);
    g_value_set_uint (va->values, presence);
    g_value_set_static_string (va->values + 1, status);
    g_value_set_static_string (va->values + 2, message);
    mcd_account_changed_property (account, "CurrentPresence", &value);
    g_value_unset (&value);

    /* TODO: when the McdPresenceFrame is removed, check if this signal is
     * still used by someone else, or remove it */
    g_signal_emit (account, _mcd_account_signals[CURRENT_PRESENCE_CHANGED], 0,
		   presence, status, message);
}

/* TODO: remove when the relative members will become public */
void
mcd_account_get_requested_presence (McdAccount *account,
				    TpConnectionPresenceType *presence,
				    const gchar **status,
				    const gchar **message)
{
    McdAccountPrivate *priv = account->priv;

    *presence = priv->req_presence_type;
    *status = priv->req_presence_status;
    *message = priv->req_presence_message;
}

/* TODO: remove when the relative members will become public */
void
mcd_account_get_current_presence (McdAccount *account,
				  TpConnectionPresenceType *presence,
				  const gchar **status,
				  const gchar **message)
{
    McdAccountPrivate *priv = account->priv;

    *presence = priv->curr_presence_type;
    *status = priv->curr_presence_status;
    *message = priv->curr_presence_message;
}

gboolean
mcd_account_get_connect_automatically (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    return priv->connect_automatically;
}

/* TODO: remove when the relative members will become public */
void
mcd_account_get_automatic_presence (McdAccount *account,
				    TpConnectionPresenceType *presence,
				    const gchar **status,
				    const gchar **message)
{
    McdAccountPrivate *priv = account->priv;

    *presence = priv->auto_presence_type;
    *status = priv->auto_presence_status;
    *message = priv->auto_presence_message;
}

/* TODO: remove when the relative members will become public */
const gchar *
mcd_account_get_manager_name (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;

    return priv->manager_name;
}

/* TODO: remove when the relative members will become public */
const gchar *
mcd_account_get_protocol_name (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;

    return priv->protocol_name;
}

void
mcd_account_set_normalized_name (McdAccount *account, const gchar *name)
{
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called (%s)", G_STRFUNC, name);
    if (name)
	g_key_file_set_string (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_NORMALIZED_NAME, name);
    else
	g_key_file_remove_key (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_NORMALIZED_NAME, NULL);
    mcd_account_manager_write_conf (priv->keyfile);
}

gchar *
mcd_account_get_normalized_name (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;

    return g_key_file_get_string (priv->keyfile, priv->unique_name,
				  MC_ACCOUNTS_KEY_NORMALIZED_NAME, NULL);
}

void
mcd_account_set_avatar_token (McdAccount *account, const gchar *token)
{
    McdAccountPrivate *priv = account->priv;

    g_debug ("%s called (%s)", G_STRFUNC, token);
    if (token)
	g_key_file_set_string (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_AVATAR_TOKEN, token);
    else
	g_key_file_remove_key (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_AVATAR_TOKEN, NULL);
    mcd_account_manager_write_conf (priv->keyfile);
}

gchar *
mcd_account_get_avatar_token (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;

    return g_key_file_get_string (priv->keyfile, priv->unique_name,
				  MC_ACCOUNTS_KEY_AVATAR_TOKEN, NULL);
}

gboolean
mcd_account_set_avatar (McdAccount *account, const GArray *avatar,
			const gchar *mime_type, const gchar *token,
			GError **error)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    gchar *data_dir, *filename;

    g_debug ("%s called", G_STRFUNC);
    data_dir = get_account_data_path (priv);
    filename = g_build_filename (data_dir, MC_AVATAR_FILENAME, NULL);
    if (!g_file_test (data_dir, G_FILE_TEST_EXISTS))
	g_mkdir_with_parents (data_dir, 0777);
    g_free (data_dir);

    if (G_LIKELY(avatar) && avatar->len > 0)
    {
	if (!g_file_set_contents (filename, avatar->data,
				  (gssize)avatar->len, error))
	{
	    g_warning ("%s: writing to file %s failed", G_STRLOC,
		       filename);
	    g_free (filename);
	    return FALSE;
	}
    }
    else
    {
	g_remove (filename);
    }
    g_free (filename);

    if (token)
	g_key_file_set_string (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_AVATAR_TOKEN, token);
    else
	g_key_file_remove_key (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_AVATAR_TOKEN, NULL);

    if (mime_type)
	g_key_file_set_string (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_AVATAR_MIME, mime_type);

    g_signal_emit (account, _mcd_account_signals[AVATAR_CHANGED], 0,
		   avatar, mime_type);

    mcd_account_manager_write_conf (priv->keyfile);
    return TRUE;
}

void
mcd_account_get_avatar (McdAccount *account, GArray **avatar,
		       	gchar **mime_type)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    gchar *filename;

    *mime_type = g_key_file_get_string (priv->keyfile, priv->unique_name,
					MC_ACCOUNTS_KEY_AVATAR_MIME, NULL);

    if (avatar) *avatar = NULL;
    filename = mcd_account_get_avatar_filename (account);

    if (filename && g_file_test (filename, G_FILE_TEST_EXISTS))
    {
	GError *error = NULL;
	gchar *data = NULL;
	gsize length;
	if (g_file_get_contents (filename, &data, &length, &error))
	{
	    if (length > 0 && length < G_MAXUINT) 
	    {
		*avatar = g_array_new (FALSE, FALSE, 1);
		(*avatar)->data = data;
		(*avatar)->len = (guint)length;
	    }
	}
	else
	{
	    g_debug ("%s: error reading %s: %s", G_STRFUNC,
		     filename, error->message);
	    g_error_free (error);
	}
    }
    g_free (filename);

    if (!*avatar)
	*avatar = g_array_new (FALSE, FALSE, 1);
}

void
mcd_account_set_alias (McdAccount *account, const gchar *alias)
{
    GValue value = { 0 };

    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, alias);
    mcd_account_set_string_val (account, MC_ACCOUNTS_KEY_ALIAS, &value);
    g_value_unset (&value);
}

gchar *
mcd_account_get_alias (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);

    return g_key_file_get_string (priv->keyfile, priv->unique_name,
				  MC_ACCOUNTS_KEY_ALIAS, NULL);
}

static inline void
process_online_requests (McdAccount *account,
			 TpConnectionStatus status,
			 TpConnectionStatusReason reason)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    McdOnlineRequestData data;

    if (!priv->online_requests) return;

    switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTED:
	data.error = NULL;
	break;
    case TP_CONNECTION_STATUS_DISCONNECTED:
	data.error = NULL;
	g_set_error (&data.error, TP_ERRORS, TP_ERROR_DISCONNECTED,
		     "Account %s disconnected with reason %d",
		     priv->unique_name, reason);
	break;
    default:
	return;
    }
    data.account = account;
    g_hash_table_foreach (priv->online_requests,
			  process_online_request,
			  &data);
    if (data.error)
	g_error_free (data.error);
    g_hash_table_destroy (priv->online_requests);
    priv->online_requests = NULL;
}

void
mcd_account_set_connection_status (McdAccount *account,
				   TpConnectionStatus status,
				   TpConnectionStatusReason reason)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    gboolean changed = FALSE;

    if (status != priv->conn_status)
    {
	GValue value = { 0 };
	priv->conn_status = status;
	g_value_init (&value, G_TYPE_UINT);
	g_value_set_uint (&value, status);
	mcd_account_changed_property (account, "ConnectionStatus",
				      &value);
	g_value_unset (&value);
	changed = TRUE;

	process_online_requests (account, status, reason);
    }
    if (reason != priv->conn_reason)
    {
	GValue value = { 0 };
	priv->conn_reason = reason;
	g_value_init (&value, G_TYPE_UINT);
	g_value_set_uint (&value, reason);
	mcd_account_changed_property (account, "ConnectionStatusReason",
				      &value);
	g_value_unset (&value);
	changed = TRUE;
    }

    if (changed)
	g_signal_emit (account,
		       _mcd_account_signals[CONNECTION_STATUS_CHANGED], 0,
		       status, reason);
}

TpConnectionStatus
mcd_account_get_connection_status (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    return priv->conn_status;
}

TpConnectionStatusReason
mcd_account_get_connection_status_reason (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    return priv->conn_reason;
}

McdConnection *
mcd_account_get_connection (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    return priv->connection;
}

gboolean
mcd_account_check_validity (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;
    gboolean valid;

    valid = mcd_account_check_parameters (account);
    if (valid != priv->valid)
    {
	GValue value = { 0 };
	g_debug ("Account validity changed (old: %d, new: %d)",
		 priv->valid, valid);
	priv->valid = valid;
	g_signal_emit (account, _mcd_account_signals[VALIDITY_CHANGED], 0,
		       valid);
	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, valid);
	mcd_account_changed_property (account, "Valid", &value);
    }
    return valid;
}

static void
mcd_account_request_automatic_presence (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;
    if (priv->auto_presence_type >= TP_CONNECTION_PRESENCE_TYPE_AVAILABLE)
	mcd_account_request_presence_int (account,
					  priv->auto_presence_type,
					  priv->auto_presence_status,
					  priv->auto_presence_message);
}

static gboolean
mcd_account_online_request (McdAccount *account,
			    McdOnlineRequestCb callback,
			    gpointer userdata,
			    GError **imm_error)
{
    McdAccountPrivate *priv = account->priv;
    GError *error = NULL;

    g_debug ("%s: connection status for %s is %d",
	     G_STRFUNC, priv->unique_name, priv->conn_status);
    if (priv->conn_status == TP_CONNECTION_STATUS_CONNECTED)
    {
	/* invoke the callback now */
	callback (account, userdata, error);
    }
    else
    {
	/* listen to the StatusChanged signal */
       	if (priv->conn_status == TP_CONNECTION_STATUS_DISCONNECTED)
	    mcd_account_request_automatic_presence (account);
	if (!priv->connection)
	{
	    g_set_error (imm_error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
			 "Could not create a connection for account %s",
			 priv->unique_name);
	    return FALSE;
	}

	/* now the connection should be in connecting state; insert the
	 * callback in the online_requests hash table, which will be processed
	 * in the mcd_account_set_connection_status function */
	if (!priv->online_requests)
	{
	    priv->online_requests = g_hash_table_new (g_direct_hash,
						      g_direct_equal);
	    g_return_val_if_fail (priv->online_requests, FALSE);
	}
	g_hash_table_insert (priv->online_requests, callback, userdata);
    }
    return TRUE;
}

static void
requested_channel_free (struct mcd_channel_request *req)
{
    g_free ((gchar *)req->account_name);
    g_free ((gchar *)req->channel_type);
    g_free ((gchar *)req->channel_handle_string);
    g_free ((gchar *)req->requestor_client_id);
    g_free (req);
}

static void
process_channel_request (McdAccount *account, gpointer userdata,
			 const GError *error)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    struct mcd_channel_request *req = userdata;
    GError *err = NULL;

    if (error)
    {
	g_warning ("%s: got error: %s", G_STRFUNC, error->message);
	/* TODO: report the error to the requestor process */
	requested_channel_free (req);
	return;
    }
    g_debug ("%s called", G_STRFUNC);
    g_return_if_fail (priv->connection != NULL);
    g_return_if_fail (priv->conn_status == TP_CONNECTION_STATUS_CONNECTED);

    mcd_connection_request_channel (priv->connection, req, &err);
    requested_channel_free (req);
}

gboolean
mcd_account_request_channel_nmc4 (McdAccount *account,
				  const struct mcd_channel_request *req,
				  GError **error)
{
    struct mcd_channel_request *req_cp;

    req_cp = g_malloc (sizeof (struct mcd_channel_request));
    memcpy(req_cp, req, sizeof (struct mcd_channel_request));
    req_cp->account_name = g_strdup (req->account_name);
    req_cp->channel_type = g_strdup (req->channel_type);
    req_cp->channel_handle_string = g_strdup (req->channel_handle_string);
    req_cp->requestor_client_id = g_strdup (req->requestor_client_id);

    return mcd_account_online_request (account,
				       process_channel_request,
				       req_cp,
				       error);
}

GKeyFile *
mcd_account_get_keyfile (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    return priv->keyfile;
}

/* this is public because of mcd-account-compat */
gchar *
mcd_account_get_avatar_filename (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;
    gchar *data_dir, *filename;

    data_dir = get_account_data_path (priv);
    g_debug("data dir: %s", data_dir);
    filename = g_build_filename (data_dir, MC_AVATAR_FILENAME, NULL);
    g_free (data_dir);
    return filename;
}

