/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

#include "config.h"
#include "mcd-account.h"

#include <stdio.h>
#include <string.h>

#include <dbus/dbus.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>

#include "mcd-account-priv.h"
#include "mcd-account-compat.h"
#include "mcd-account-conditions.h"
#include "mcd-account-connection.h"
#include "mcd-account-requests.h"
#include "mcd-account-stats.h"
#include "mcd-misc.h"
#include "mcd-signals-marshal.h"
#include "mcd-manager.h"
#include "mcd-master.h"
#include "mcd-dbusprop.h"
#include "_gen/interfaces.h"
#include "_gen/gtypes.h"

#define DELAY_PROPERTY_CHANGED

#define MAX_KEY_LENGTH	64
#define MC_AVATAR_FILENAME	"avatar.bin"

#define MCD_ACCOUNT_PRIV(account) (MCD_ACCOUNT (account)->priv)

static void account_iface_init (McSvcAccountClass *iface,
			       	gpointer iface_data);
static void properties_iface_init (TpSvcDBusPropertiesClass *iface,
				   gpointer iface_data);
static void account_avatar_iface_init (McSvcAccountInterfaceAvatarClass *iface,
				       gpointer iface_data);

static const McdDBusProp account_properties[];
static const McdDBusProp account_avatar_properties[];

static const McdInterfaceData account_interfaces[] = {
    MCD_IMPLEMENT_IFACE (mc_svc_account_get_type, account, MC_IFACE_ACCOUNT),
    MCD_IMPLEMENT_IFACE (mc_svc_account_interface_avatar_get_type,
			 account_avatar,
			 MC_IFACE_ACCOUNT_INTERFACE_AVATAR),
    MCD_IMPLEMENT_IFACE (mc_svc_account_interface_channelrequests_get_type,
			 account_channelrequests,
			 MC_IFACE_ACCOUNT_INTERFACE_CHANNELREQUESTS),
    MCD_IMPLEMENT_IFACE (mc_svc_account_interface_compat_get_type,
			 account_compat,
			 MC_IFACE_ACCOUNT_INTERFACE_COMPAT),
    MCD_IMPLEMENT_IFACE (mc_svc_account_interface_conditions_get_type,
			 account_conditions,
			 MC_IFACE_ACCOUNT_INTERFACE_CONDITIONS),
    MCD_IMPLEMENT_IFACE_WITH_INIT (mc_svc_account_interface_stats_get_type,
                                   account_stats,
                                   MC_IFACE_ACCOUNT_INTERFACE_STATS),
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

    McdConnection *connection;
    McdManager *manager;
    McdAccountManager *account_manager;
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

    GList *online_requests; /* list of McdOnlineRequestData structures
                               (callback with user data) to be called when the
                               account will be online */

    guint connect_automatically : 1;
    guint enabled : 1;
    guint valid : 1;
    guint loaded : 1;
    guint has_been_online : 1;

    /* These fields are used to cache the changed properties */
    GHashTable *changed_properties;
    GArray *property_values;
    guint properties_source;
};

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
    PROP_ACCOUNT_MANAGER,
    PROP_NAME,
};

guint _mcd_account_signals[LAST_SIGNAL] = { 0 };
static GQuark account_ready_quark = 0;

/*
 * _mcd_account_maybe_autoconnect:
 * @account: the #McdAccount.
 *
 * Check whether automatic connection should happen (and attempt it if needed).
 */
static void
_mcd_account_maybe_autoconnect (McdAccount *account)
{
    McdAccountPrivate *priv;

    g_return_if_fail (MCD_IS_ACCOUNT (account));
    priv = account->priv;

    if (priv->enabled &&
        priv->conn_status == TP_CONNECTION_STATUS_DISCONNECTED &&
        priv->connect_automatically)
    {
        McdMaster *master = mcd_master_get_default ();
        if (_mcd_master_account_conditions_satisfied (master, account))
        {
            DEBUG ("connecting account %s", priv->unique_name);
            _mcd_account_request_connection (account);
        }
    }
}

static gboolean
value_is_same (const GValue *val1, const GValue *val2)
{
    g_return_val_if_fail (val1 != NULL && val2 != NULL, FALSE);
    switch (G_VALUE_TYPE (val1))
    {
    case G_TYPE_STRING:
        return g_strcmp0 (g_value_get_string (val1),
                          g_value_get_string (val2)) == 0;
    case G_TYPE_CHAR:
    case G_TYPE_UCHAR:
    case G_TYPE_INT:
    case G_TYPE_UINT:
    case G_TYPE_BOOLEAN:
        return val1->data[0].v_uint == val2->data[0].v_uint;
    case G_TYPE_INT64:
        return g_value_get_int64 (val1) == g_value_get_int64 (val2);
    case G_TYPE_UINT64:
        return g_value_get_uint64 (val1) == g_value_get_uint64 (val2);
    default:
        g_warning ("%s: unexpected type %s",
                   G_STRFUNC, G_VALUE_TYPE_NAME (val1));
        return FALSE;
    }
}

static void
mcd_account_loaded (McdAccount *account)
{
    g_return_if_fail (!account->priv->loaded);
    account->priv->loaded = TRUE;

    /* invoke all the callbacks */
    mcd_object_ready (account, account_ready_quark, NULL);
    _mcd_account_maybe_autoconnect (account);
}

static void
set_parameter (McdAccount *account, const gchar *name, const GValue *value)
{
    McdAccountPrivate *priv = account->priv;
    gchar key[MAX_KEY_LENGTH];

    g_snprintf (key, sizeof (key), "param-%s", name);

    if (!value)
    {
        g_key_file_remove_key (priv->keyfile, priv->unique_name, key, NULL);
        DEBUG ("unset param %s", name);
        return;
    }

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

static gboolean
get_parameter (McdAccount *account, const gchar *name, GValue *value)
{
    McdAccountPrivate *priv = account->priv;
    gchar key[MAX_KEY_LENGTH];

    g_snprintf (key, sizeof (key), "param-%s", name);
    if (!g_key_file_has_key (priv->keyfile, priv->unique_name, key, NULL))
        return FALSE;

    if (value)
    {
        gchar *v_string = NULL;
        gint v_int = 0;
        gboolean v_bool = FALSE;

        switch (G_VALUE_TYPE (value))
        {
        case G_TYPE_STRING:
            v_string = g_key_file_get_string (priv->keyfile, priv->unique_name,
                                              key, NULL);
            g_value_take_string (value, v_string);
            break;
        case G_TYPE_INT:
            v_int = g_key_file_get_integer (priv->keyfile, priv->unique_name,
                                            key, NULL);
            g_value_set_int (value, v_int);
            break;
        case G_TYPE_INT64:
            v_int = g_key_file_get_integer (priv->keyfile, priv->unique_name,
                                            key, NULL);
            g_value_set_int64 (value, v_int);
            break;
        case G_TYPE_UCHAR:
            v_int = g_key_file_get_integer (priv->keyfile, priv->unique_name,
                                            key, NULL);
            g_value_set_uchar (value, v_int);
            break;
        case G_TYPE_UINT:
            v_int = g_key_file_get_integer (priv->keyfile, priv->unique_name,
                                            key, NULL);
            g_value_set_uint (value, v_int);
            break;
        case G_TYPE_UINT64:
            v_int = g_key_file_get_integer (priv->keyfile, priv->unique_name,
                                            key, NULL);
            g_value_set_uint64 (value, v_int);
            break;
        case G_TYPE_BOOLEAN:
            v_bool = g_key_file_get_boolean (priv->keyfile, priv->unique_name,
                                             key, NULL);
            g_value_set_boolean (value, v_bool);
            break;
        default:
            g_warning ("%s: skipping parameter %s, unknown type %s", G_STRFUNC,
                       name, G_VALUE_TYPE_NAME (value));
            return FALSE;
        }
    }

    return TRUE;
}

static void on_manager_ready (McdManager *manager, const GError *error,
                              gpointer user_data)
{
    McdAccount *account = MCD_ACCOUNT (user_data);
    McdAccountPrivate *priv = account->priv;

    if (error)
    {
        DEBUG ("got error: %s", error->message);
    }
    else
    {
        priv->valid = mcd_account_check_parameters (account);
    }
    mcd_account_loaded (account);
}

static gboolean
load_manager (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;
    McdMaster *master;

    if (G_UNLIKELY (!priv->manager_name)) return FALSE;
    master = mcd_master_get_default ();
    priv->manager = mcd_master_lookup_manager (master, priv->manager_name);
    if (priv->manager)
    {
	g_object_ref (priv->manager);
        mcd_manager_call_when_ready (priv->manager, on_manager_ready, account);
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

static gboolean
_mcd_account_delete (McdAccount *account, GError **error)
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
    mcd_account_manager_write_conf (priv->account_manager);
    return TRUE;
}

static void
_mcd_account_load_real (McdAccount *account, McdAccountLoadCb callback,
                        gpointer user_data)
{
    if (account->priv->loaded)
        callback (account, NULL, user_data);
    else
        mcd_object_call_when_ready (account, account_ready_quark,
                                    (McdReadyCb)callback, user_data);
}

static void
on_connection_abort (McdConnection *connection, McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);

    DEBUG ("called (%p, account %s)", connection, priv->unique_name);
    _mcd_account_set_connection (account, NULL);
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
        McdConnection *connection;

	if (!priv->manager && !load_manager (account))
	{
	    g_warning ("%s: Could not find manager `%s'",
		       G_STRFUNC, priv->manager_name);
	    return;
	}

        connection = mcd_manager_create_connection (priv->manager, account);
        _mcd_account_set_connection (account, connection);
    }
    mcd_connection_connect (priv->connection, params);
}

#ifdef DELAY_PROPERTY_CHANGED
static gboolean
emit_property_changed (gpointer userdata)
{
    McdAccount *account = MCD_ACCOUNT (userdata);
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called");
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

    DEBUG ("called: %s", key);
    if (priv->changed_properties &&
	g_hash_table_lookup (priv->changed_properties, key))
    {
	/* the changed property was also changed before; then let's force the
	 * emission of the signal now, so that the property will appear in two
	 * separate signals */
        DEBUG ("Forcibly emit PropertiesChanged now");
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
        DEBUG ("First changed property");
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

    DEBUG ("called: %s", key);
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
    if (!tp_strdiff (old_string, string))
    {
	g_free (old_string);
	return FALSE;
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
    mcd_account_manager_write_conf (priv->account_manager);
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

    DEBUG ("called for %s", priv->unique_name);
    mcd_account_set_string_val (account, name, value);
}

static void
get_display_name (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    mcd_account_get_string_val (account, name, value);
}

static void
set_icon (TpSvcDBusProperties *self, const gchar *name, const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called for %s", priv->unique_name);
    mcd_account_set_string_val (account, name, value);
}

static void
get_icon (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    mcd_account_get_string_val (account, name, value);
}

static void
get_valid (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, priv->valid);
}

static void
get_has_been_online (TpSvcDBusProperties *self, const gchar *name,
                     GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, priv->has_been_online);
}

static void
set_enabled (TpSvcDBusProperties *self, const gchar *name, const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    gboolean enabled;

    DEBUG ("called for %s", priv->unique_name);
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
	mcd_account_manager_write_conf (priv->account_manager);
	mcd_account_changed_property (account, name, value);
    }
}

static void
get_enabled (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, priv->enabled);
}

static void
set_nickname (TpSvcDBusProperties *self, const gchar *name, const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called for %s", priv->unique_name);
    if (mcd_account_set_string_val (account, name, value))
	g_signal_emit (account, _mcd_account_signals[ALIAS_CHANGED], 0,
		       g_value_get_string (value));
}

static void
get_nickname (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

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

    DEBUG ("called for %s", priv->unique_name);
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
    gchar *mime_type;
    GArray *avatar = NULL;
    GType type;
    GValueArray *va;

    mcd_account_get_avatar (account, &avatar, &mime_type);
    if (!avatar)
        avatar = g_array_new (FALSE, FALSE, 1);

    type = dbus_g_type_get_struct ("GValueArray",
				   dbus_g_type_get_collection ("GArray",
							       G_TYPE_UCHAR),
				   G_TYPE_STRING,
				   G_TYPE_INVALID);
    g_value_init (value, type);
    g_value_take_boxed (value, dbus_g_type_specialized_construct (type));
    va = (GValueArray *) g_value_get_boxed (value);
    g_value_take_boxed (va->values, avatar);
    g_value_take_string (va->values + 1, mime_type);
}

static void
get_parameters (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    GHashTable *parameters;

    parameters = mcd_account_get_parameters (account);
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
    TpConnectionPresenceType type;
    gboolean changed = FALSE;
    GValueArray *va;

    DEBUG ("called for %s", priv->unique_name);
    va = g_value_get_boxed (value);
    type = g_value_get_uint (va->values);
    status = g_value_get_string (va->values + 1);
    message = g_value_get_string (va->values + 2);
    DEBUG ("setting automatic presence: %d, %s, %s", type, status, message);

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
	mcd_account_manager_write_conf (priv->account_manager);
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
    GValueArray *va;

    presence_type = priv->auto_presence_type;
    presence = priv->auto_presence_status;
    message = priv->auto_presence_message;

    type = TP_STRUCT_TYPE_SIMPLE_PRESENCE;
    g_value_init (value, type);
    g_value_take_boxed (value, dbus_g_type_specialized_construct (type));
    va = (GValueArray *) g_value_get_boxed (value);
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

    DEBUG ("called for %s", priv->unique_name);
    connect_automatically = g_value_get_boolean (value);
    if (priv->connect_automatically != connect_automatically)
    {
	g_key_file_set_boolean (priv->keyfile, priv->unique_name,
				MC_ACCOUNTS_KEY_CONNECT_AUTOMATICALLY,
			       	connect_automatically);
	priv->connect_automatically = connect_automatically;
	mcd_account_manager_write_conf (priv->account_manager);
	mcd_account_changed_property (account, name, value);
    }
}

static void
get_connect_automatically (TpSvcDBusProperties *self,
			   const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called for %s", priv->unique_name);
    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, priv->connect_automatically);
}

static void
get_connection (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    const gchar *object_path;

    g_value_init (value, DBUS_TYPE_G_OBJECT_PATH);
    if (priv->connection &&
	(object_path = mcd_connection_get_object_path (priv->connection)))
	g_value_set_boxed (value, object_path);
    else
	g_value_set_static_boxed (value, "/");
}

static void
get_connection_status (TpSvcDBusProperties *self,
		       const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    TpConnectionStatus status;

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
    GValueArray *va;

    presence_type = priv->curr_presence_type;
    status = priv->curr_presence_status;
    message = priv->curr_presence_message;

    type = TP_STRUCT_TYPE_SIMPLE_PRESENCE;
    g_value_init (value, type);
    g_value_take_boxed (value, dbus_g_type_specialized_construct (type));
    va = (GValueArray *) g_value_get_boxed (value);
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

    DEBUG ("called for %s", priv->unique_name);
    va = g_value_get_boxed (value);
    type = (gint)g_value_get_uint (va->values);
    status = g_value_get_string (va->values + 1);
    message = g_value_get_string (va->values + 2);
    DEBUG ("setting requested presence: %d, %s, %s", type, status, message);

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
    GValueArray *va;

    presence_type = priv->req_presence_type;
    presence = priv->req_presence_status;
    message = priv->req_presence_message;

    type = TP_STRUCT_TYPE_SIMPLE_PRESENCE;
    g_value_init (value, type);
    g_value_take_boxed (value, dbus_g_type_specialized_construct (type));
    va = (GValueArray *) g_value_get_boxed (value);
    g_value_set_uint (va->values, presence_type);
    g_value_set_static_string (va->values + 1, presence);
    g_value_set_static_string (va->values + 2, message);
}

static void
get_normalized_name (TpSvcDBusProperties *self,
		     const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

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
    { "AutomaticPresence", set_automatic_presence, get_automatic_presence },
    { "ConnectAutomatically", set_connect_automatically, get_connect_automatically },
    { "Connection", NULL, get_connection },
    { "ConnectionStatus", NULL, get_connection_status },
    { "ConnectionStatusReason", NULL, get_connection_status_reason },
    { "CurrentPresence", NULL, get_current_presence },
    { "RequestedPresence", set_requested_presence, get_requested_presence },
    { "NormalizedName", NULL, get_normalized_name },
    { "HasBeenOnline", NULL, get_has_been_online },
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
mc_param_type (const TpConnectionManagerParam *param)
{
    if (G_UNLIKELY (!param->dbus_signature)) return G_TYPE_INVALID;

    switch (param->dbus_signature[0])
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
        g_warning ("skipping parameter %s, unknown type %s",
                   param->name, param->dbus_signature);
    }
    return G_TYPE_INVALID;
}

gboolean
mcd_account_delete (McdAccount *account, GError **error)
{
    return MCD_ACCOUNT_GET_CLASS (account)->delete (account, error);
}

static void
account_remove (McSvcAccount *self, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    DEBUG ("called");
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
    const TpConnectionManagerParam *param;
    gboolean valid;

    DEBUG ("called for %s", priv->unique_name);
    param = mcd_manager_get_parameters (priv->manager, priv->protocol_name);
    if (!param) return FALSE;
    valid = TRUE;
    while (param->name != NULL)
    {
        if (param->flags & TP_CONN_MGR_PARAM_FLAG_REQUIRED)
	{
	    if (!mcd_account_get_parameter (account, param->name, NULL))
	    {
                DEBUG ("missing required parameter %s", param->name);
		valid = FALSE;
		break;
	    }
	}
        param++;
    }

    return valid;
}

/**
 * mcd_account_set_parameter:
 * @account: the #McdAccount.
 * @name: the parameter name.
 * @value: a #GValue with the value to set, or %NULL.
 *
 * Sets the parameter @name to the value in @value. If @value, is %NULL, the
 * parameter is unset.
 */
void
mcd_account_set_parameter (McdAccount *account, const gchar *name,
                           const GValue *value)
{
    MCD_ACCOUNT_GET_CLASS (account)->set_parameter (account, name, value);
}

gboolean
mcd_account_set_parameters (McdAccount *account, GHashTable *params,
			    GError **error)
{
    McdAccountPrivate *priv = account->priv;
    const TpConnectionManagerParam *param;
    guint n_params = 0;
    GHashTableIter iter;
    const gchar *name;
    const GValue *value;
    GSList *dbus_properties = NULL;
    gboolean reset_connection;

    DEBUG ("called");
    if (G_UNLIKELY (!priv->manager && !load_manager (account)))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Manager %s not found", priv->manager_name);
        return FALSE;
    }

    param = mcd_manager_get_parameters (priv->manager, priv->protocol_name);
    if (G_UNLIKELY (!param))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Protocol %s not found", priv->protocol_name);
        return FALSE;
    }

    reset_connection = FALSE;
    while (param->name != NULL)
    {
	GType type;

	type = mc_param_type (param);
	value = g_hash_table_lookup (params, param->name);
	if (value)
	{
            DEBUG ("Got param %s", param->name);
	    if (G_VALUE_TYPE (value) != type)
	    {
		/* FIXME: define proper error */
		g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
			     "parameter %s must be of type %s, not %s",
			     param->name,
			     g_type_name (type), G_VALUE_TYPE_NAME (value));
		return FALSE;
	    }

            if (mcd_account_get_connection_status (account) ==
                TP_CONNECTION_STATUS_CONNECTED)
            {
                GValue old = { 0 };

                g_value_init (&old, type);
                if (!mcd_account_get_parameter (account, param->name, &old) ||
                    !value_is_same (value, &old))
                {
                    DEBUG ("Parameter %s changed", param->name);
                    /* can the param be updated on the fly? If yes, prepare to
                     * do so; and if not, prepare to reset the connection */
                    if (param->flags & TP_CONN_MGR_PARAM_FLAG_DBUS_PROPERTY)
                    {
                        dbus_properties = g_slist_prepend (dbus_properties,
                                                           param->name);
                    }
                    else
                        reset_connection = TRUE;
                }
                g_value_unset (&old);
            }
	    n_params++;
	}
        param++;
    }

    if (n_params != g_hash_table_size (params))
    {
	g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "Not all parameters were recognized");
	return FALSE;
    }

    g_hash_table_iter_init (&iter, params);
    while (g_hash_table_iter_next (&iter, (gpointer)&name, (gpointer)&value))
    {
        mcd_account_set_parameter (account, name, value);
    }

    if (mcd_account_get_connection_status (account) ==
        TP_CONNECTION_STATUS_CONNECTED)
    {
        if (reset_connection)
        {
            DEBUG ("resetting connection");
            mcd_connection_close (priv->connection);
            mcd_account_connection_begin (account);
        }
        else
        {
            GSList *list;

            for (list = dbus_properties; list != NULL; list = list->next)
            {
                name = list->data;
                DEBUG ("updating parameter %s", name);
                value = g_hash_table_lookup (params, name);
                _mcd_connection_update_property (priv->connection, name, value);
            }
        }
    }
    g_slist_free (dbus_properties);

    _mcd_account_maybe_autoconnect (account);
    return TRUE;
}

static inline void
mcd_account_unset_parameters (McdAccount *account, const gchar **params)
{
    const gchar **param;

    for (param = params; *param != NULL; param++)
    {
        mcd_account_set_parameter (account, *param, NULL);
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

    DEBUG ("called for %s", priv->unique_name);

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
    mcd_account_manager_write_conf (priv->account_manager);
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
    TpDBusDaemon *dbus_daemon;

    if (!priv->account_manager || !priv->object_path) return;

    dbus_daemon = mcd_account_manager_get_dbus_daemon (priv->account_manager);
    g_return_if_fail (dbus_daemon != NULL);

    dbus_connection = TP_PROXY (dbus_daemon)->dbus_connection;

    if (G_LIKELY (dbus_connection))
	dbus_g_connection_register_g_object (dbus_connection,
					     priv->object_path,
					     (GObject *)account);
}

static gboolean
mcd_account_setup (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;

    priv->keyfile = mcd_account_manager_get_config (priv->account_manager);
    if (!priv->keyfile) return FALSE;

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

    priv->has_been_online =
	g_key_file_get_boolean (priv->keyfile, priv->unique_name,
				MC_ACCOUNTS_KEY_HAS_BEEN_ONLINE, NULL);

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

    /* check the manager */
    if (!priv->manager && !load_manager (account))
    {
	g_warning ("Could not find manager `%s'", priv->manager_name);
        mcd_account_loaded (account);
    }

    _mcd_account_load (account, (McdAccountLoadCb)register_dbus_service, NULL);
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
    case PROP_ACCOUNT_MANAGER:
        g_assert (priv->account_manager == NULL);
        /* don't keep a reference to the account_manager: we can safely assume
         * its lifetime is longer than the McdAccount's */
        priv->account_manager = g_value_get_object (val);
	break;
    case PROP_NAME:
	g_assert (priv->unique_name == NULL);
	priv->unique_name = g_value_dup_string (val);
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
        g_value_set_object
            (val, mcd_account_manager_get_dbus_daemon (priv->account_manager));
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

    DEBUG ("called for %s", priv->unique_name);
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

    DEBUG ("called for %s", priv->unique_name);
    if (priv->online_requests)
    {
        GError *error;
        GList *list = priv->online_requests;

        error = g_error_new (TP_ERRORS, TP_ERROR_DISCONNECTED,
                             "Disposing account %s", priv->unique_name);
        while (list)
        {
            McdOnlineRequestData *data = list->data;

            data->callback (MCD_ACCOUNT (object), data->user_data, error);
            g_slice_free (McdOnlineRequestData, data);
            list = g_list_delete_link (list, list);
        }
        g_error_free (error);
	priv->online_requests = NULL;
    }

    if (priv->manager)
    {
	g_object_unref (priv->manager);
	priv->manager = NULL;
    }

    _mcd_account_set_connection (MCD_ACCOUNT (object), NULL);

    G_OBJECT_CLASS (mcd_account_parent_class)->dispose (object);
}

static GObject *
_mcd_account_constructor (GType type, guint n_params,
                          GObjectConstructParam *params)
{
    GObjectClass *object_class = (GObjectClass *)mcd_account_parent_class;
    McdAccount *account;
    McdAccountPrivate *priv;

    account = MCD_ACCOUNT (object_class->constructor (type, n_params, params));
    priv = account->priv;

    g_return_val_if_fail (account != NULL, NULL);
    if (G_UNLIKELY (!priv->account_manager || !priv->unique_name))
    {
        g_object_unref (account);
        return NULL;
    }

    return (GObject *) account;
}

static void
_mcd_account_constructed (GObject *object)
{
    GObjectClass *object_class = (GObjectClass *)mcd_account_parent_class;
    McdAccount *account = MCD_ACCOUNT (object);

    mcd_account_setup (account);

    if (object_class->constructed)
        object_class->constructed (object);
}

static void
mcd_account_class_init (McdAccountClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdAccountPrivate));

    object_class->constructor = _mcd_account_constructor;
    object_class->constructed = _mcd_account_constructed;
    object_class->dispose = _mcd_account_dispose;
    object_class->finalize = _mcd_account_finalize;
    object_class->set_property = set_property;
    object_class->get_property = get_property;

    klass->get_parameter = get_parameter;
    klass->set_parameter = set_parameter;
    klass->delete = _mcd_account_delete;
    klass->load = _mcd_account_load_real;

    g_object_class_install_property
        (object_class, PROP_DBUS_DAEMON,
         g_param_spec_object ("dbus-daemon", "DBus daemon", "DBus daemon",
                              TP_TYPE_DBUS_DAEMON, G_PARAM_READABLE));

    g_object_class_install_property
        (object_class, PROP_ACCOUNT_MANAGER,
         g_param_spec_object ("account-manager", "account-manager",
                               "account-manager", MCD_TYPE_ACCOUNT_MANAGER,
                               G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_NAME,
         g_param_spec_string ("name", "Unique name", "Unique name",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    /* Signals */
    _mcd_account_signals[CONNECTION_STATUS_CHANGED] =
	g_signal_new ("connection-status-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, _mcd_marshal_VOID__UINT_UINT,
		      G_TYPE_NONE,
		      2, G_TYPE_UINT, G_TYPE_UINT);
    _mcd_account_signals[CURRENT_PRESENCE_CHANGED] =
	g_signal_new ("current-presence-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, _mcd_marshal_VOID__UINT_STRING_STRING,
		      G_TYPE_NONE,
		      3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
    _mcd_account_signals[REQUESTED_PRESENCE_CHANGED] =
	g_signal_new ("requested-presence-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, _mcd_marshal_VOID__UINT_STRING_STRING,
		      G_TYPE_NONE,
		      3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
    _mcd_account_signals[VALIDITY_CHANGED] =
	g_signal_new ("validity-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
		      G_TYPE_NONE, 1,
		      G_TYPE_BOOLEAN);
    _mcd_account_signals[AVATAR_CHANGED] =
	g_signal_new ("mcd-avatar-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, _mcd_marshal_VOID__BOXED_STRING,
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
    _mcd_account_compat_class_init (klass);
    _mcd_account_connection_class_init (klass);

    account_ready_quark = g_quark_from_static_string ("mcd_account_load");
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
mcd_account_new (McdAccountManager *account_manager, const gchar *name)
{
    gpointer *obj;
    obj = g_object_new (MCD_TYPE_ACCOUNT,
                        "account-manager", account_manager,
			"name", name,
			NULL);
    return MCD_ACCOUNT (obj);
}

/**
 * mcd_account_get_account_manager:
 * @account: the #McdAccount.
 *
 * Returns: the #McdAccountManager.
 */
McdAccountManager *
mcd_account_get_account_manager (McdAccount *account)
{
    g_return_val_if_fail (MCD_IS_ACCOUNT (account), NULL);
    return account->priv->account_manager;
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

static inline void
add_parameter (McdAccount *account, const TpConnectionManagerParam *param,
	       GHashTable *params)
{
    GValue *value;
    GType type;

    type = mc_param_type (param);
    if (G_UNLIKELY (type == G_TYPE_INVALID)) return;

    value = tp_g_value_slice_new (type);

    if (mcd_account_get_parameter (account, param->name, value))
        g_hash_table_insert (params, g_strdup (param->name), value);
    else
        tp_g_value_slice_free (value);
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
    const TpConnectionManagerParam *param;
    GHashTable *params;

    DEBUG ("called");
    if (!priv->manager && !load_manager (account)) return NULL;

    params = g_hash_table_new_full (g_str_hash, g_str_equal,
				    g_free,
                                    (GDestroyNotify)tp_g_value_slice_free);
    param = mcd_manager_get_parameters (priv->manager, priv->protocol_name);
    if (G_UNLIKELY (!param)) return params;

    while (param->name != NULL)
    {
	add_parameter (account, param, params);
        param++;
    }
    return params;
}

/**
 * mcd_account_get_parameter:
 * @account: the #McdAccount.
 * @name: the parameter name.
 * @value: a initialized #GValue to receive the parameter value, or %NULL.
 *
 * Get the @name parameter for @account.
 *
 * Returns: %TRUE if found, %FALSE otherwise.
 */
gboolean
mcd_account_get_parameter (McdAccount *account, const gchar *name,
                           GValue *value)
{
    return MCD_ACCOUNT_GET_CLASS (account)->get_parameter (account, name,
                                                           value);
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
        GValueArray *va;

	type = TP_STRUCT_TYPE_SIMPLE_PRESENCE;
	g_value_init (&value, type);
	g_value_take_boxed (&value, dbus_g_type_specialized_construct (type));
	va = (GValueArray *) g_value_get_boxed (&value);
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
    GValueArray *va;

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

    type = TP_STRUCT_TYPE_SIMPLE_PRESENCE;
    g_value_init (&value, type);
    g_value_take_boxed (&value, dbus_g_type_specialized_construct (type));
    va = (GValueArray *) g_value_get_boxed (&value);
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
    GValue value = { 0, };

    DEBUG ("called (%s)", name);
    if (name)
	g_key_file_set_string (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_NORMALIZED_NAME, name);
    else
	g_key_file_remove_key (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_NORMALIZED_NAME, NULL);
    mcd_account_manager_write_conf (priv->account_manager);

    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, name);
    mcd_account_changed_property (account, "NormalizedName", &value);
    g_value_unset (&value);
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

    DEBUG ("called (%s)", token);
    if (token)
	g_key_file_set_string (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_AVATAR_TOKEN, token);
    else
	g_key_file_remove_key (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_AVATAR_TOKEN, NULL);
    mcd_account_manager_write_conf (priv->account_manager);
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

    DEBUG ("called");
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

    if (mime_type)
	g_key_file_set_string (priv->keyfile, priv->unique_name,
			       MC_ACCOUNTS_KEY_AVATAR_MIME, mime_type);

    if (token)
    {
        gchar *prev_token;

        prev_token = mcd_account_get_avatar_token (account);
        g_key_file_set_string (priv->keyfile, priv->unique_name,
                               MC_ACCOUNTS_KEY_AVATAR_TOKEN, token);
        if (!prev_token || strcmp (prev_token, token) != 0)
            mc_svc_account_interface_avatar_emit_avatar_changed (account);
        g_free (prev_token);
    }
    else
    {
        g_key_file_remove_key (priv->keyfile, priv->unique_name,
                               MC_ACCOUNTS_KEY_AVATAR_TOKEN, NULL);
        g_signal_emit (account, _mcd_account_signals[AVATAR_CHANGED], 0,
                       avatar, mime_type);
    }

    mcd_account_manager_write_conf (priv->account_manager);
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
            DEBUG ("error reading %s: %s", filename, error->message);
	    g_error_free (error);
	}
    }
    g_free (filename);
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

void
_mcd_account_online_request_completed (McdAccount *account, GError *error)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    GList *list;

    list = priv->online_requests;

    while (list)
    {
        McdOnlineRequestData *data = list->data;

        data->callback (account, data->user_data, error);
        g_slice_free (McdOnlineRequestData, data);
        list = g_list_delete_link (list, list);
    }
    if (error)
        g_error_free (error);
    priv->online_requests = NULL;
}

GList *
_mcd_account_get_online_requests (McdAccount *account)
{
    g_return_val_if_fail (MCD_IS_ACCOUNT (account), NULL);

    return account->priv->online_requests;
}

static inline void
process_online_requests (McdAccount *account,
			 TpConnectionStatus status,
			 TpConnectionStatusReason reason)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    GError *error;

    switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTED:
        error = NULL;
	break;
    case TP_CONNECTION_STATUS_DISCONNECTED:
        error = g_error_new (TP_ERRORS, TP_ERROR_DISCONNECTED,
                             "Account %s disconnected with reason %d",
                             priv->unique_name, reason);
	break;
    default:
	return;
    }
    _mcd_account_online_request_completed (account, error);
}

void
mcd_account_set_connection_status (McdAccount *account,
				   TpConnectionStatus status,
				   TpConnectionStatusReason reason)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    gboolean changed = FALSE;

    if (status == TP_CONNECTION_STATUS_CONNECTED)
    {
        _mcd_account_set_has_been_online (account);
    }

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

void
_mcd_account_tp_connection_changed (McdAccount *account)
{
    GValue value = { 0 };

    get_connection ((TpSvcDBusProperties *)account, "Connection", &value);
    mcd_account_changed_property (account, "Connection", &value);
    g_value_unset (&value);

    _mcd_account_manager_store_account_connections
        (account->priv->account_manager);
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
        DEBUG ("Account validity changed (old: %d, new: %d)",
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

/*
 * _mcd_account_request_connection:
 * @account: the #McdAccount.
 *
 * Request the account to go online. If an automatic presence is specified, set
 * it.
 */
void
_mcd_account_request_connection (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;

    if (!priv->connection)
        mcd_account_connection_begin (account);

    if (priv->auto_presence_type >= TP_CONNECTION_PRESENCE_TYPE_AVAILABLE)
	mcd_account_request_presence_int (account,
					  priv->auto_presence_type,
					  priv->auto_presence_status,
					  priv->auto_presence_message);
}

/*
 * _mcd_account_online_request:
 * @account: the #McdAccount.
 * @callback: a #McdOnlineRequestCb.
 * @userdata: user data to be passed to @callback.
 *
 * If the account is online, call @callback immediately; else, try to put the
 * account online (set its presence to the automatic presence) and eventually
 * invoke @callback.
 *
 * @callback is always invoked exactly once.
 */
void
_mcd_account_online_request (McdAccount *account,
                             McdOnlineRequestCb callback,
                             gpointer userdata)
{
    McdAccountPrivate *priv = account->priv;

    DEBUG ("connection status for %s is %d",
           priv->unique_name, priv->conn_status);
    if (priv->conn_status == TP_CONNECTION_STATUS_CONNECTED)
    {
        /* invoke the callback now */
        callback (account, userdata, NULL);
    }
    else
    {
        McdOnlineRequestData *data;
	/* listen to the StatusChanged signal */
       	if (priv->conn_status == TP_CONNECTION_STATUS_DISCONNECTED)
            _mcd_account_request_connection (account);

	/* now the connection should be in connecting state; insert the
	 * callback in the online_requests hash table, which will be processed
	 * in the mcd_account_set_connection_status function */
        data = g_slice_new (McdOnlineRequestData);
        data->callback = callback;
        data->user_data = userdata;
        priv->online_requests = g_list_append (priv->online_requests, data);
    }
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
    DEBUG("data dir: %s", data_dir);
    filename = g_build_filename (data_dir, MC_AVATAR_FILENAME, NULL);
    g_free (data_dir);
    return filename;
}

void
_mcd_account_load (McdAccount *account, McdAccountLoadCb callback,
                   gpointer user_data)
{
    g_return_if_fail (MCD_IS_ACCOUNT (account));
    g_return_if_fail (callback != NULL);

    MCD_ACCOUNT_GET_CLASS (account)->load (account, callback, user_data);
}

void
_mcd_account_set_connection (McdAccount *account, McdConnection *connection)
{
    McdAccountPrivate *priv;

    g_return_if_fail (MCD_IS_ACCOUNT (account));
    priv = account->priv;
    if (connection == priv->connection) return;

    if (priv->connection)
    {
        g_signal_handlers_disconnect_by_func (priv->connection,
                                              on_connection_abort, account);
        g_object_unref (priv->connection);
    }
    priv->connection = connection;
    if (connection)
    {
        g_return_if_fail (MCD_IS_CONNECTION (connection));
        g_object_ref (connection);
        g_signal_connect (connection, "abort",
                          G_CALLBACK (on_connection_abort), account);
    }
}

void
_mcd_account_set_has_been_online (McdAccount *account)
{
    if (!account->priv->has_been_online)
    {
        GValue value = { 0 };

        g_key_file_set_boolean (account->priv->keyfile,
                                account->priv->unique_name,
                                MC_ACCOUNTS_KEY_HAS_BEEN_ONLINE, TRUE);
        account->priv->has_been_online = TRUE;
        mcd_account_manager_write_conf (account->priv->account_manager);

        g_value_init (&value, G_TYPE_BOOLEAN);
        g_value_set_boolean (&value, TRUE);
        mcd_account_changed_property (account, "HasBeenOnline", &value);
        g_value_unset (&value);
    }
}
