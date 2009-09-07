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

#include "config.h"
#include "mcd-account.h"

#include <stdio.h>
#include <string.h>

#include <dbus/dbus.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-account.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>

#include <libmcclient/mc-gtypes.h>
#include <libmcclient/mc-interfaces.h>

#include "mcd-account-priv.h"
#include "mcd-account-compat.h"
#include "mcd-account-conditions.h"
#include "mcd-account-manager-priv.h"
#include "mcd-connection-plugin.h"
#include "mcd-connection-priv.h"
#include "mcd-misc.h"
#include "mcd-signals-marshal.h"
#include "mcd-manager.h"
#include "mcd-master.h"
#include "mcd-master-priv.h"
#include "mcd-dbusprop.h"

#define DELAY_PROPERTY_CHANGED

#define MAX_KEY_LENGTH (DBUS_MAXIMUM_NAME_LENGTH + 6)
#define MC_AVATAR_FILENAME	"avatar.bin"

#define MCD_ACCOUNT_PRIV(account) (MCD_ACCOUNT (account)->priv)

static void account_iface_init (TpSvcAccountClass *iface,
			       	gpointer iface_data);
static void properties_iface_init (TpSvcDBusPropertiesClass *iface,
				   gpointer iface_data);
static void account_avatar_iface_init (TpSvcAccountInterfaceAvatarClass *iface,
				       gpointer iface_data);

static const McdDBusProp account_properties[];
static const McdDBusProp account_avatar_properties[];

static const McdInterfaceData account_interfaces[] = {
    MCD_IMPLEMENT_IFACE (tp_svc_account_get_type, account, TP_IFACE_ACCOUNT),
    MCD_IMPLEMENT_IFACE (tp_svc_account_interface_avatar_get_type,
			 account_avatar,
			 TP_IFACE_ACCOUNT_INTERFACE_AVATAR),
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
    McdTransport *transport;
    McdAccountConnectionContext *connection_context;
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
    guint removed : 1;
    guint always_on : 1;

    /* These fields are used to cache the changed properties */
    GHashTable *changed_properties;
    guint properties_source;
};

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
    PROP_ACCOUNT_MANAGER,
    PROP_NAME,
    PROP_ALWAYS_ON,
};

enum
{
    CONNECTION_STATUS_CHANGED,
    VALIDITY_CHANGED,
    LAST_SIGNAL
};

static guint _mcd_account_signals[LAST_SIGNAL] = { 0 };
static GQuark account_ready_quark = 0;

/*
 * _mcd_account_maybe_autoconnect:
 * @account: the #McdAccount.
 *
 * Check whether automatic connection should happen (and attempt it if needed).
 */
void
_mcd_account_maybe_autoconnect (McdAccount *account)
{
    McdAccountPrivate *priv;
    McdMaster *master;

    g_return_if_fail (MCD_IS_ACCOUNT (account));
    priv = account->priv;

    if (!priv->enabled)
    {
        DEBUG ("%s not Enabled", priv->unique_name);
        return;
    }

    if (!priv->valid)
    {
        DEBUG ("%s not Valid", priv->unique_name);
        return;
    }

    if (priv->conn_status != TP_CONNECTION_STATUS_DISCONNECTED)
    {
        DEBUG ("%s already connected", priv->unique_name);
        return;
    }

    if (!priv->connect_automatically)
    {
        DEBUG ("%s does not ConnectAutomatically", priv->unique_name);
        return;
    }

    master = mcd_master_get_default ();

    if (!_mcd_master_account_replace_transport (master, account))
    {
        DEBUG ("%s conditions not satisfied", priv->unique_name);
        return;
    }

    DEBUG ("connecting account %s", priv->unique_name);
    _mcd_account_connect_with_auto_presence (account);
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

    case G_TYPE_DOUBLE:
        return g_value_get_double (val1) == g_value_get_double (val2);

    default:
        if (G_VALUE_TYPE (val1) == DBUS_TYPE_G_OBJECT_PATH)
        {
            return !tp_strdiff (g_value_get_boxed (val1),
                                g_value_get_boxed (val2));
        }
        else if (G_VALUE_TYPE (val1) == G_TYPE_STRV)
        {
            gchar **left = g_value_get_boxed (val1);
            gchar **right = g_value_get_boxed (val2);

            if (left == NULL || right == NULL ||
                *left == NULL || *right == NULL)
            {
                return ((left == NULL || *left == NULL) &&
                        (right == NULL || *right == NULL));
            }

            while (*left != NULL || *right != NULL)
            {
                if (tp_strdiff (*left, *right))
                {
                    return FALSE;
                }

                left++;
                right++;
            }

            return TRUE;
        }
        else
        {
            g_warning ("%s: unexpected type %s",
                       G_STRFUNC, G_VALUE_TYPE_NAME (val1));
            return FALSE;
        }
    }
}

static void
mcd_account_loaded (McdAccount *account)
{
    g_return_if_fail (!account->priv->loaded);
    account->priv->loaded = TRUE;

    /* invoke all the callbacks */
    g_object_ref (account);

    _mcd_object_ready (account, account_ready_quark, NULL);

    if (account->priv->online_requests != NULL)
    {
        /* if we have established that the account is not valid or is
         * disabled, cancel all requests */
        if (!account->priv->valid || !account->priv->enabled)
        {
            /* FIXME: pick better errors and put them in telepathy-spec? */
            GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                "account isn't Valid (not enough information to put it "
                    "online)" };
            GList *list;

            if (account->priv->valid)
            {
                e.message = "account isn't Enabled";
            }

            list = account->priv->online_requests;
            account->priv->online_requests = NULL;

            for (/* already initialized */ ;
                 list != NULL;
                 list = g_list_delete_link (list, list))
            {
                McdOnlineRequestData *data = list->data;

                data->callback (account, data->user_data, &e);
                g_slice_free (McdOnlineRequestData, data);
            }
        }

        /* otherwise, we want to go online now */
        if (account->priv->conn_status == TP_CONNECTION_STATUS_DISCONNECTED)
        {
            _mcd_account_connect_with_auto_presence (account);
        }
    }

    _mcd_account_maybe_autoconnect (account);

    g_object_unref (account);
}

static void
set_parameter (McdAccount *account, const gchar *name, const GValue *value)
{
    McdAccountPrivate *priv = account->priv;
    gchar key[MAX_KEY_LENGTH];
    gchar buf[21];  /* enough for '-' + the 19 digits of 2**63 + '\0' */

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
        g_snprintf (buf, sizeof (buf), "%u", g_value_get_uint (value));
        g_key_file_set_string (priv->keyfile, priv->unique_name, key,
                               buf);
        break;

    case G_TYPE_INT:
	g_key_file_set_integer (priv->keyfile, priv->unique_name, key,
				g_value_get_int (value));
	break;

    case G_TYPE_BOOLEAN:
	g_key_file_set_boolean (priv->keyfile, priv->unique_name, key,
				g_value_get_boolean (value));
	break;

    case G_TYPE_UCHAR:
        g_key_file_set_integer (priv->keyfile, priv->unique_name, key,
                                g_value_get_uchar (value));
        break;

    case G_TYPE_UINT64:
        g_snprintf (buf, sizeof (buf), "%" G_GUINT64_FORMAT,
                    g_value_get_uint64 (value));
        g_key_file_set_string (priv->keyfile, priv->unique_name, key,
                               buf);
        break;

    case G_TYPE_INT64:
        g_snprintf (buf, sizeof (buf), "%" G_GINT64_FORMAT,
                    g_value_get_int64 (value));
        g_key_file_set_string (priv->keyfile, priv->unique_name, key,
                               buf);
        break;

    case G_TYPE_DOUBLE:
        g_key_file_set_double (priv->keyfile, priv->unique_name, key,
                               g_value_get_double (value));
        break;

    default:
        if (G_VALUE_HOLDS (value, G_TYPE_STRV))
        {
            gchar **strings = g_value_get_boxed (value);

            g_key_file_set_string_list (priv->keyfile, priv->unique_name, key,
                                        (const gchar **)strings,
                                        g_strv_length (strings));
        }
        else if (G_VALUE_HOLDS (value, DBUS_TYPE_G_OBJECT_PATH))
        {
            const gchar *path = g_value_get_boxed (value);

            g_key_file_set_string (priv->keyfile, priv->unique_name, key,
                                   path);
        }
        else
        {
            g_warning ("Unexpected param type %s", G_VALUE_TYPE_NAME (value));
        }
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
        gint64 v_int = 0;
        guint64 v_uint = 0;
        gboolean v_bool = FALSE;
        double v_double = 0.0;

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
            v_int = tp_g_key_file_get_int64 (priv->keyfile, priv->unique_name,
                                             key, NULL);
            g_value_set_int64 (value, v_int);
            break;

        case G_TYPE_UCHAR:
            v_int = g_key_file_get_integer (priv->keyfile, priv->unique_name,
                                            key, NULL);

            if (v_int < 0 || v_int > 0xFF)
            {
                return FALSE;
            }

            g_value_set_uchar (value, v_int);
            break;

        case G_TYPE_UINT:
            v_uint = tp_g_key_file_get_uint64 (priv->keyfile,
                                               priv->unique_name, key, NULL);

            if (v_uint > 0xFFFFFFFFU)
            {
                return FALSE;
            }

            g_value_set_uint (value, v_uint);
            break;

        case G_TYPE_UINT64:
            v_uint = tp_g_key_file_get_uint64 (priv->keyfile,
                                               priv->unique_name, key, NULL);
            g_value_set_uint64 (value, v_uint);
            break;

        case G_TYPE_BOOLEAN:
            v_bool = g_key_file_get_boolean (priv->keyfile, priv->unique_name,
                                             key, NULL);
            g_value_set_boolean (value, v_bool);
            break;

        case G_TYPE_DOUBLE:
            v_double = g_key_file_get_double (priv->keyfile, priv->unique_name,
                                             key, NULL);
            g_value_set_double (value, v_double);
            break;

        default:
            if (G_VALUE_HOLDS (value, G_TYPE_STRV))
            {
                gchar **v = g_key_file_get_string_list (priv->keyfile,
                                                        priv->unique_name, key,
                                                        NULL, NULL);

                g_value_take_boxed (value, v);
            }
            else if (G_VALUE_HOLDS (value, DBUS_TYPE_G_OBJECT_PATH))
            {
                v_string = g_key_file_get_string (priv->keyfile,
                                                  priv->unique_name, key,
                                                  NULL);

                if (!tp_dbus_check_valid_object_path (v_string, NULL))
                {
                    g_free (v_string);
                    return FALSE;
                }

                g_value_take_boxed (value, v_string);
            }
            else
            {
                g_warning ("%s: skipping parameter %s, unknown type %s",
                           G_STRFUNC, name, G_VALUE_TYPE_NAME (value));
                return FALSE;
            }
        }
    }

    return TRUE;
}

static gboolean mcd_account_check_parameters (McdAccount *account);

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
    priv->manager = _mcd_master_lookup_manager (master, priv->manager_name);
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
    GError *kf_error = NULL;

    if (!g_key_file_remove_group (priv->keyfile, priv->unique_name,
                                  &kf_error))
    {
        if (kf_error->domain == G_KEY_FILE_ERROR &&
            kf_error->code == G_KEY_FILE_ERROR_GROUP_NOT_FOUND)
        {
            DEBUG ("account not found in key file, doing nothing");
            g_clear_error (&kf_error);
        }
        else
        {
            g_warning ("Could not remove group (%s)", kf_error->message);
            g_propagate_error (error, kf_error);
            return FALSE;
        }
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
        _mcd_object_call_when_ready (account, account_ready_quark,
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

    if (type >= TP_CONNECTION_PRESENCE_TYPE_AVAILABLE)
    {
        if (!priv->enabled)
        {
            DEBUG ("%s not Enabled", priv->unique_name);
            return changed;
        }

        if (!priv->valid)
        {
            DEBUG ("%s not Valid", priv->unique_name);
            return changed;
        }
    }

    if (priv->connection == NULL)
    {
        if (type >= TP_CONNECTION_PRESENCE_TYPE_AVAILABLE)
        {
            _mcd_account_connection_begin (account);
        }
    }
    else
    {
        _mcd_connection_request_presence (priv->connection,
                                          type, status, message);
    }

    return changed;
}

void
_mcd_account_connect (McdAccount *account, GHashTable *params)
{
    McdAccountPrivate *priv = account->priv;

    g_assert (params != NULL);

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
    _mcd_connection_connect (priv->connection, params);
}

#ifdef DELAY_PROPERTY_CHANGED
static gboolean
emit_property_changed (gpointer userdata)
{
    McdAccount *account = MCD_ACCOUNT (userdata);
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called");
    tp_svc_account_emit_account_property_changed (account,
						  priv->changed_properties);

    g_hash_table_remove_all (priv->changed_properties);

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
				   NULL,
                                   (GDestroyNotify) tp_g_value_slice_free);
    }

    if (priv->properties_source == 0)
    {
        DEBUG ("First changed property");
	priv->properties_source = g_timeout_add (10, emit_property_changed,
						 account);
    }
    g_hash_table_insert (priv->changed_properties, (gpointer) key,
                         tp_g_value_slice_dup (value));
#else
    GHashTable *properties;

    DEBUG ("called: %s", key);
    properties = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_insert (properties, (gpointer)key, (gpointer)value);
    tp_svc_account_emit_account_property_changed (account,
						  properties);

    g_hash_table_destroy (properties);
#endif
}

typedef enum {
    SET_RESULT_ERROR,
    SET_RESULT_UNCHANGED,
    SET_RESULT_CHANGED
} SetResult;

/*
 * mcd_account_set_string_val:
 * @account: an account
 * @key: a D-Bus property name that is a string
 * @value: the new value for that property
 * @error: set to an error if %SET_RESULT_ERROR is returned
 *
 * Returns: %SET_RESULT_CHANGED or %SET_RESULT_UNCHANGED on success,
 *  %SET_RESULT_ERROR on error
 */
static SetResult
mcd_account_set_string_val (McdAccount *account, const gchar *key,
                            const GValue *value, GError **error)
{
    McdAccountPrivate *priv = account->priv;
    const gchar *string;
    gchar *old_string;

    if (!G_VALUE_HOLDS_STRING (value))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Expected string for %s, but got %s", key,
                     G_VALUE_TYPE_NAME (value));
        return SET_RESULT_ERROR;
    }

    string = g_value_get_string (value);
    old_string = g_key_file_get_string (priv->keyfile, priv->unique_name,
				       	key, NULL);
    if (!tp_strdiff (old_string, string))
    {
	g_free (old_string);
	return SET_RESULT_UNCHANGED;
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
    return SET_RESULT_CHANGED;
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

static gboolean
set_display_name (TpSvcDBusProperties *self, const gchar *name,
                  const GValue *value, GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called for %s", priv->unique_name);
    return (mcd_account_set_string_val (account, name, value, error)
            != SET_RESULT_ERROR);
}

static void
get_display_name (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    mcd_account_get_string_val (account, name, value);
}

static gboolean
set_icon (TpSvcDBusProperties *self, const gchar *name, const GValue *value,
          GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called for %s", priv->unique_name);
    return (mcd_account_set_string_val (account, name, value, error)
            != SET_RESULT_ERROR);
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

static gboolean
set_enabled (TpSvcDBusProperties *self, const gchar *name, const GValue *value,
             GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    gboolean enabled;

    DEBUG ("called for %s", priv->unique_name);

    if (!G_VALUE_HOLDS_BOOLEAN (value))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Expected boolean for Enabled, but got %s",
                     G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    enabled = g_value_get_boolean (value);

    if (priv->always_on && !enabled)
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                     "Account %s cannot be disabled",
                     priv->unique_name);
        return FALSE;
    }

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

        if (enabled)
        {
            mcd_account_request_presence_int (account,
                                              priv->req_presence_type,
                                              priv->req_presence_status,
                                              priv->req_presence_message);
            _mcd_account_maybe_autoconnect (account);
        }
    }

    return TRUE;
}

static void
get_enabled (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, priv->enabled);
}

static gboolean
set_nickname (TpSvcDBusProperties *self, const gchar *name,
              const GValue *value, GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    SetResult ret;

    DEBUG ("called for %s", priv->unique_name);
    ret = mcd_account_set_string_val (account, name, value, error);

    if (ret == SET_RESULT_CHANGED && priv->connection != NULL)
    {
        /* this is a no-op if the connection doesn't support it */
        _mcd_connection_set_nickname (priv->connection,
                                      g_value_get_string (value));
    }

    return (ret != SET_RESULT_ERROR);
}

static void
get_nickname (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    mcd_account_get_string_val (account, name, value);
}

static gboolean
set_avatar (TpSvcDBusProperties *self, const gchar *name, const GValue *value,
            GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    const gchar *mime_type;
    const GArray *avatar;
    GValueArray *va;

    DEBUG ("called for %s", priv->unique_name);

    if (!G_VALUE_HOLDS (value, TP_STRUCT_TYPE_AVATAR))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Unexpected type for Avatar: wanted (ay,s), got %s",
                     G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    va = g_value_get_boxed (value);
    avatar = g_value_get_boxed (va->values);
    mime_type = g_value_get_string (va->values + 1);

    if (!_mcd_account_set_avatar (account, avatar, mime_type, NULL, error))
    {
        return FALSE;
    }

    tp_svc_account_interface_avatar_emit_avatar_changed (account);
    return TRUE;
}

static void
get_avatar (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    gchar *mime_type;
    GArray *avatar = NULL;
    GType type = TP_STRUCT_TYPE_AVATAR;
    GValueArray *va;

    _mcd_account_get_avatar (account, &avatar, &mime_type);
    if (!avatar)
        avatar = g_array_new (FALSE, FALSE, 1);

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

    parameters = _mcd_account_dup_parameters (account);
    g_value_init (value, TP_HASH_TYPE_STRING_VARIANT_MAP);
    g_value_take_boxed (value, parameters);
}

static gboolean
_presence_type_is_settable (TpConnectionPresenceType type)
{
    switch (type)
    {
        case TP_CONNECTION_PRESENCE_TYPE_UNSET:
        case TP_CONNECTION_PRESENCE_TYPE_UNKNOWN:
        case TP_CONNECTION_PRESENCE_TYPE_ERROR:
            return FALSE;

        default:
            return TRUE;
    }
}

static gboolean
_presence_type_is_online (TpConnectionPresenceType type)
{
    switch (type)
    {
        case TP_CONNECTION_PRESENCE_TYPE_UNSET:
        case TP_CONNECTION_PRESENCE_TYPE_OFFLINE:
        case TP_CONNECTION_PRESENCE_TYPE_UNKNOWN:
        case TP_CONNECTION_PRESENCE_TYPE_ERROR:
            return FALSE;

        default:
            return TRUE;
    }
}

static gboolean
set_automatic_presence (TpSvcDBusProperties *self,
                        const gchar *name, const GValue *value, GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    const gchar *status, *message;
    TpConnectionPresenceType type;
    gboolean changed = FALSE;
    GValueArray *va;

    DEBUG ("called for %s", priv->unique_name);

    if (!G_VALUE_HOLDS (value, TP_STRUCT_TYPE_SIMPLE_PRESENCE))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Unexpected type for AutomaticPresence: wanted (u,s,s), "
                     "got %s", G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    va = g_value_get_boxed (value);
    type = g_value_get_uint (va->values);
    status = g_value_get_string (va->values + 1);
    message = g_value_get_string (va->values + 2);

    if (!_presence_type_is_online (type))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "AutomaticPresence must be an online presence, not %d",
                     type);
        return FALSE;
    }

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

    return TRUE;
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

static gboolean
set_connect_automatically (TpSvcDBusProperties *self,
                           const gchar *name, const GValue *value,
                           GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    gboolean connect_automatically;

    DEBUG ("called for %s", priv->unique_name);

    if (!G_VALUE_HOLDS_BOOLEAN (value))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Expected boolean for ConnectAutomatically, but got %s",
                     G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    connect_automatically = g_value_get_boolean (value);

    if (priv->always_on && !connect_automatically)
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                     "Account %s always connects automatically",
                     priv->unique_name);
        return FALSE;
    }

    if (priv->connect_automatically != connect_automatically)
    {
	g_key_file_set_boolean (priv->keyfile, priv->unique_name,
				MC_ACCOUNTS_KEY_CONNECT_AUTOMATICALLY,
			       	connect_automatically);
	priv->connect_automatically = connect_automatically;
	mcd_account_manager_write_conf (priv->account_manager);
	mcd_account_changed_property (account, name, value);

        if (connect_automatically)
        {
            _mcd_account_maybe_autoconnect (account);
        }
    }

    return TRUE;
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

    g_value_init (value, G_TYPE_UINT);
    g_value_set_uint (value, account->priv->conn_status);
}

static void
get_connection_status_reason (TpSvcDBusProperties *self,
			      const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    g_value_init (value, G_TYPE_UINT);
    g_value_set_uint (value, account->priv->conn_reason);
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

static gboolean
set_requested_presence (TpSvcDBusProperties *self,
                        const gchar *name, const GValue *value,
                        GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    const gchar *status, *message;
    gint type;
    GValueArray *va;

    DEBUG ("called for %s", priv->unique_name);

    if (!G_VALUE_HOLDS (value, TP_STRUCT_TYPE_SIMPLE_PRESENCE))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Unexpected type for RequestedPresence: wanted (u,s,s), "
                     "got %s", G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    va = g_value_get_boxed (value);
    type = (gint)g_value_get_uint (va->values);
    status = g_value_get_string (va->values + 1);
    message = g_value_get_string (va->values + 2);

    if (priv->always_on && !_presence_type_is_online (type))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                     "Account %s cannot be taken offline", priv->unique_name);
        return FALSE;
    }

    if (!_presence_type_is_settable (type))
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "RequestedPresence %d cannot be set on yourself", type);
        return FALSE;
    }

    DEBUG ("setting requested presence: %d, %s, %s", type, status, message);

    if (mcd_account_request_presence_int (account, type, status, message))
    {
	mcd_account_changed_property (account, name, value);
    }

    return TRUE;
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
account_avatar_iface_init (TpSvcAccountInterfaceAvatarClass *iface,
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

    case DBUS_TYPE_BYTE:
        return G_TYPE_UCHAR;

    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
	return G_TYPE_INT;

    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
	return G_TYPE_UINT;

    case DBUS_TYPE_BOOLEAN:
	return G_TYPE_BOOLEAN;

    case DBUS_TYPE_DOUBLE:
        return G_TYPE_DOUBLE;

    case DBUS_TYPE_OBJECT_PATH:
        return DBUS_TYPE_G_OBJECT_PATH;

    case DBUS_TYPE_INT64:
        return G_TYPE_INT64;

    case DBUS_TYPE_UINT64:
        return G_TYPE_UINT64;

    case DBUS_TYPE_ARRAY:
        if (param->dbus_signature[1] == DBUS_TYPE_STRING)
            return G_TYPE_STRV;
        /* other array types are not supported:
         * fall through the default case */
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
account_remove (TpSvcAccount *svc, DBusGMethodInvocation *context)
{
    McdAccount *self = MCD_ACCOUNT (svc);
    GError *error = NULL;

    DEBUG ("called");
    if (!mcd_account_delete (self, &error))
    {
	if (!error)
	    g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
			 "Internal error");
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }

    if (!self->priv->removed)
    {
        self->priv->removed = TRUE;
        tp_svc_account_emit_removed (self);
    }

    tp_svc_account_return_from_remove (context);
}

/*
 * mcd_account_get_parameter:
 * @account: the #McdAccount.
 * @name: the parameter name.
 * @value: a initialized #GValue to receive the parameter value, or %NULL.
 *
 * Get the @name parameter for @account.
 *
 * Returns: %TRUE if found, %FALSE otherwise.
 */
static gboolean
mcd_account_get_parameter (McdAccount *account, const gchar *name,
                           GValue *value)
{
    return MCD_ACCOUNT_GET_CLASS (account)->get_parameter (account, name,
                                                           value);
}

static gboolean
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

/*
 * _mcd_account_set_parameter:
 * @account: the #McdAccount.
 * @name: the parameter name.
 * @value: a #GValue with the value to set, or %NULL.
 *
 * Sets the parameter @name to the value in @value. If @value, is %NULL, the
 * parameter is unset.
 */
static void
_mcd_account_set_parameter (McdAccount *account, const gchar *name,
                            const GValue *value)
{
    MCD_ACCOUNT_GET_CLASS (account)->set_parameter (account, name, value);
}

/*
 * _mcd_account_set_parameters:
 * @account: the #McdAccount.
 * @name: the parameter name.
 * @params: names and values of parameters to set
 * @unset: names of parameters to unset
 * @not_yet: if not %NULL, borrowed names of parameters that cannot take
 *  effect until Reconnect() is called will be appended to this array
 *
 * Alter the account parameters.
 *
 * Returns: %TRUE (possibly appending borrowed strings to @not_yet) on success,
 *  %FALSE (setting @error) on failure
 */
gboolean
_mcd_account_set_parameters (McdAccount *account, GHashTable *params,
                             const gchar ** unset, GPtrArray *not_yet,
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
                    {
                        if (not_yet != NULL)
                        {
                            /* we assume that the TpConnectionManager won't get
                             * freed */
                            g_ptr_array_add (not_yet, param->name);
                        }

                        reset_connection = TRUE;
                    }
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
        _mcd_account_set_parameter (account, name, value);
    }

    if (unset != NULL)
    {
        const gchar **unset_iter;

        for (unset_iter = unset; *unset_iter != NULL; unset_iter++)
        {
            if (mcd_account_get_parameter (account, *unset_iter, NULL))
            {
                DEBUG ("unsetting %s", *unset_iter);
                /* pessimistically assume that removing any parameter merits
                 * reconnection (in a perfect implementation, if the
                 * Has_Default flag was set we'd check whether the current
                 * value is the default already) */
                if (not_yet != NULL)
                {
                    /* we assume that the TpConnectionManager won't get
                     * freed */
                    g_ptr_array_add (not_yet, (gchar *) *unset_iter);
                }

                reset_connection = TRUE;
            }

            _mcd_account_set_parameter (account, *unset_iter, NULL);
        }
    }

    if (mcd_account_get_connection_status (account) ==
        TP_CONNECTION_STATUS_CONNECTED)
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
    g_slist_free (dbus_properties);

    mcd_account_check_validity (account);
    _mcd_account_maybe_autoconnect (account);
    return TRUE;
}

static void
account_update_parameters (TpSvcAccount *self, GHashTable *set,
			   const gchar **unset, DBusGMethodInvocation *context)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    GHashTable *parameters;
    GValue value = { 0 };
    GError *error = NULL;
    GPtrArray *not_yet;

    DEBUG ("called for %s", priv->unique_name);

    /* pessimistically assume that every parameter mentioned will be deferred
     * until reconnection */
    not_yet = g_ptr_array_sized_new (g_hash_table_size (set) +
                                     g_strv_length ((gchar **) unset) + 1);

    if (!_mcd_account_set_parameters (account, set, unset, not_yet, &error))
    {
        g_ptr_array_free (not_yet, TRUE);
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    /* emit the PropertiesChanged signal */
    parameters = _mcd_account_dup_parameters (account);
    g_value_init (&value, TP_HASH_TYPE_STRING_VARIANT_MAP);
    g_value_take_boxed (&value, parameters);
    mcd_account_changed_property (account, "Parameters", &value);
    g_value_unset (&value);

    mcd_account_manager_write_conf (priv->account_manager);

    g_ptr_array_add (not_yet, NULL);

    tp_svc_account_return_from_update_parameters (context,
        (const gchar **) not_yet->pdata);
    g_ptr_array_free (not_yet, TRUE);
}

static void
account_reconnect (TpSvcAccount *service,
                   DBusGMethodInvocation *context)
{
    McdAccount *self = MCD_ACCOUNT (service);
    McdAccountPrivate *priv = self->priv;

    DEBUG ("%s", mcd_account_get_unique_name (self));

    /* if we can't, or don't want to, connect this method is a no-op */
    if (!priv->enabled ||
        !priv->valid ||
        priv->req_presence_type == TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
    {
        DEBUG ("doing nothing (enabled=%c, valid=%c and "
               "RequestedPresence=%i)",
               self->priv->enabled ? 'T' : 'F',
               self->priv->valid ? 'T' : 'F',
               self->priv->req_presence_type);
        tp_svc_account_return_from_reconnect (context);
        return;
    }

    /* FIXME: this isn't quite right. If we've just called RequestConnection
     * (possibly with out of date parameters) but we haven't got a Connection
     * back from the CM yet, the old parameters will still be used, I think
     * (I can't quite make out what actually happens). */
    if (priv->connection)
        mcd_connection_close (priv->connection);
    _mcd_account_connection_begin (self);

    /* FIXME: we shouldn't really return from this method until the
     * reconnection has actually happened, but that would require less tangled
     * integration between Account and Connection */
    tp_svc_account_return_from_reconnect (context);
}

static void
account_iface_init (TpSvcAccountClass *iface, gpointer iface_data)
{
#define IMPLEMENT(x) tp_svc_account_implement_##x (\
    iface, account_##x)
    IMPLEMENT(remove);
    IMPLEMENT(update_parameters);
    IMPLEMENT(reconnect);
#undef IMPLEMENT
}

static void
register_dbus_service (McdAccount *self,
                       const GError *error,
                       gpointer unused G_GNUC_UNUSED)
{
    DBusGConnection *dbus_connection;
    TpDBusDaemon *dbus_daemon;

    if (error != NULL)
    {
        /* due to some tangled error handling, the McdAccount might already
         * have been freed by the time we get here, so it's no longer safe to
         * dereference self here! */
        DEBUG ("%p failed to load: %s code %d: %s", self,
               g_quark_to_string (error->domain), error->code, error->message);
        return;
    }

    g_assert (MCD_IS_ACCOUNT (self));
    /* these are invariants - the account manager is set at construct-time
     * and the object path is set in mcd_account_setup, both of which are
     * run before this callback can possibly be invoked */
    g_assert (self->priv->account_manager != NULL);
    g_assert (self->priv->object_path != NULL);

    dbus_daemon = mcd_account_manager_get_dbus_daemon (
        self->priv->account_manager);
    g_return_if_fail (dbus_daemon != NULL);

    dbus_connection = TP_PROXY (dbus_daemon)->dbus_connection;

    if (G_LIKELY (dbus_connection))
	dbus_g_connection_register_g_object (dbus_connection,
					     self->priv->object_path,
					     (GObject *) self);
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

    if (!priv->always_on)
    {
        priv->enabled =
            g_key_file_get_boolean (priv->keyfile, priv->unique_name,
                                    MC_ACCOUNTS_KEY_ENABLED, NULL);

        priv->connect_automatically =
            g_key_file_get_boolean (priv->keyfile, priv->unique_name,
                                    MC_ACCOUNTS_KEY_CONNECT_AUTOMATICALLY,
                                    NULL);
    }

    priv->has_been_online =
	g_key_file_get_boolean (priv->keyfile, priv->unique_name,
				MC_ACCOUNTS_KEY_HAS_BEEN_ONLINE, NULL);

    /* load the automatic presence */
    priv->auto_presence_type =
	g_key_file_get_integer (priv->keyfile, priv->unique_name,
				MC_ACCOUNTS_KEY_AUTO_PRESENCE_TYPE, NULL);

    /* If invalid or something, force it to AVAILABLE - we want the auto
     * presence type to be an online status */
    if (!_presence_type_is_online (priv->auto_presence_type))
    {
        priv->auto_presence_type = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;
        priv->auto_presence_status = g_strdup ("available");
    }
    else
    {
        priv->auto_presence_status =
            g_key_file_get_string (priv->keyfile, priv->unique_name,
                                   MC_ACCOUNTS_KEY_AUTO_PRESENCE_STATUS,
                                   NULL);
    }

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

    _mcd_account_load (account, register_dbus_service, NULL);
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

    case PROP_ALWAYS_ON:
        priv->always_on = g_value_get_boolean (val);

        if (priv->always_on)
        {
            priv->enabled = TRUE;
            priv->connect_automatically = TRUE;
            priv->req_presence_type = priv->auto_presence_type;
            priv->req_presence_status = g_strdup (priv->auto_presence_status);
            priv->req_presence_message = g_strdup (priv->auto_presence_message);
        }

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

    DEBUG ("%p (%s)", object, priv->unique_name);
    if (priv->changed_properties)
	g_hash_table_destroy (priv->changed_properties);
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
    McdAccount *self = MCD_ACCOUNT (object);
    McdAccountPrivate *priv = self->priv;

    DEBUG ("%p (%s)", object, priv->unique_name);

    if (!self->priv->removed)
    {
        self->priv->removed = TRUE;
        tp_svc_account_emit_removed (self);
    }

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

    _mcd_account_set_connection_context (self, NULL);
    _mcd_account_set_connection (self, NULL);

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

    DEBUG ("%p (%s)", object, account->priv->unique_name);

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
    klass->check_request = _mcd_account_check_request_real;

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

    g_object_class_install_property
        (object_class, PROP_ALWAYS_ON,
         g_param_spec_boolean ("always-on", "Always on?", "Always on?",
                              FALSE,
                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                              G_PARAM_STATIC_STRINGS));

    /* Signals */
    _mcd_account_signals[CONNECTION_STATUS_CHANGED] =
	g_signal_new ("connection-status-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, _mcd_marshal_VOID__UINT_UINT,
		      G_TYPE_NONE,
		      2, G_TYPE_UINT, G_TYPE_UINT);
    _mcd_account_signals[VALIDITY_CHANGED] =
	g_signal_new ("validity-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
		      G_TYPE_NONE, 1,
		      G_TYPE_BOOLEAN);

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

    priv->req_presence_type = TP_CONNECTION_PRESENCE_TYPE_OFFLINE;
    priv->req_presence_status = g_strdup ("offline");
    priv->req_presence_message = g_strdup ("");

    priv->always_on = FALSE;
    priv->enabled = FALSE;
    priv->connect_automatically = FALSE;

    priv->auto_presence_type = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;
    priv->auto_presence_status = g_strdup ("available");
    priv->auto_presence_message = g_strdup ("");

    /* initializes the interfaces */
    mcd_dbus_init_interfaces_instances (account);

    priv->conn_status = TP_CONNECTION_STATUS_DISCONNECTED;
    priv->conn_reason = TP_CONNECTION_STATUS_REASON_REQUESTED;
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

/*
 * _mcd_account_dup_parameters:
 * @account: the #McdAccount.
 *
 * Get the parameters set for this account.
 *
 * Returns: a newly allocated #GHashTable containing the account parameters.
 */
GHashTable *
_mcd_account_dup_parameters (McdAccount *account)
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

static void
on_conn_self_presence_changed (McdConnection *connection,
                               TpConnectionPresenceType presence,
                               const gchar *status,
                               const gchar *message,
                               gpointer user_data)
{
    McdAccount *account = MCD_ACCOUNT (user_data);
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
}

/* TODO: remove when the relative members will become public */
void
mcd_account_get_requested_presence (McdAccount *account,
				    TpConnectionPresenceType *presence,
				    const gchar **status,
				    const gchar **message)
{
    McdAccountPrivate *priv = account->priv;

    if (presence != NULL)
        *presence = priv->req_presence_type;

    if (status != NULL)
        *status = priv->req_presence_status;

    if (message != NULL)
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

    if (presence != NULL)
        *presence = priv->curr_presence_type;

    if (status != NULL)
        *status = priv->curr_presence_status;

    if (message != NULL)
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

    if (presence != NULL)
        *presence = priv->auto_presence_type;

    if (status != NULL)
        *status = priv->auto_presence_status;

    if (message != NULL)
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
_mcd_account_set_normalized_name (McdAccount *account, const gchar *name)
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
_mcd_account_set_avatar_token (McdAccount *account, const gchar *token)
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
_mcd_account_get_avatar_token (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;

    return g_key_file_get_string (priv->keyfile, priv->unique_name,
				  MC_ACCOUNTS_KEY_AVATAR_TOKEN, NULL);
}

gboolean
_mcd_account_set_avatar (McdAccount *account, const GArray *avatar,
			const gchar *mime_type, const gchar *token,
			GError **error)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    gchar *data_dir, *filename;

    DEBUG ("called");
    data_dir = get_account_data_path (priv);
    filename = g_build_filename (data_dir, MC_AVATAR_FILENAME, NULL);
    if (!g_file_test (data_dir, G_FILE_TEST_EXISTS))
	g_mkdir_with_parents (data_dir, 0700);
    _mcd_chmod_private (data_dir);
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

        prev_token = _mcd_account_get_avatar_token (account);
        g_key_file_set_string (priv->keyfile, priv->unique_name,
                               MC_ACCOUNTS_KEY_AVATAR_TOKEN, token);
        if (!prev_token || strcmp (prev_token, token) != 0)
            tp_svc_account_interface_avatar_emit_avatar_changed (account);
        g_free (prev_token);
    }
    else
    {
        g_key_file_remove_key (priv->keyfile, priv->unique_name,
                               MC_ACCOUNTS_KEY_AVATAR_TOKEN, NULL);
        /* this is a no-op if the connection doesn't support avatars */
        if (priv->connection != NULL)
        {
            _mcd_connection_set_avatar (priv->connection, avatar, mime_type);
        }
    }

    mcd_account_manager_write_conf (priv->account_manager);
    return TRUE;
}

void
_mcd_account_get_avatar (McdAccount *account, GArray **avatar,
                         gchar **mime_type)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    gchar *filename;

    if (mime_type != NULL)
        *mime_type = g_key_file_get_string (priv->keyfile, priv->unique_name,
                                            MC_ACCOUNTS_KEY_AVATAR_MIME, NULL);

    if (avatar == NULL)
        return;

    *avatar = NULL;

    filename = _mcd_account_get_avatar_filename (account);

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

static void
mcd_account_connection_self_nickname_changed_cb (McdAccount *account,
                                                 const gchar *alias,
                                                 McdConnection *connection)
{
    GValue value = { 0 };

    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, alias);
    mcd_account_set_string_val (account, MC_ACCOUNTS_KEY_ALIAS, &value, NULL);
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

static void
on_conn_status_changed (McdConnection *connection,
                        TpConnectionStatus status,
                        TpConnectionStatusReason reason,
                        McdAccount *account)
{
    _mcd_account_set_connection_status (account, status, reason);
}

void
_mcd_account_set_connection_status (McdAccount *account,
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

    process_online_requests (account, status, reason);

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

    valid = (priv->loaded && mcd_account_check_parameters (account));

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

        if (valid)
        {
            /* newly valid - try setting requested presence again */
            mcd_account_request_presence_int (account,
                                              priv->req_presence_type,
                                              priv->req_presence_status,
                                              priv->req_presence_message);
        }
    }
    return valid;
}

/*
 * _mcd_account_connect_with_auto_presence:
 * @account: the #McdAccount.
 *
 * Request the account to go online with the configured AutomaticPresence.
 * This is appropriate in these situations:
 * - going online automatically because we've gained connectivity
 * - going online automatically in order to request a channel
 */
void
_mcd_account_connect_with_auto_presence (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;

    mcd_account_request_presence (account,
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
    McdOnlineRequestData *data;

    DEBUG ("connection status for %s is %d",
           priv->unique_name, priv->conn_status);
    if (priv->conn_status == TP_CONNECTION_STATUS_CONNECTED)
    {
        /* invoke the callback now */
        DEBUG ("%s is already connected", priv->unique_name);
        callback (account, userdata, NULL);
        return;
    }

    if (priv->loaded && !priv->valid)
    {
        /* FIXME: pick a better error and put it in telepathy-spec? */
        GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "account isn't Valid (not enough information to put it online)" };

        DEBUG ("%s: %s", priv->unique_name, e.message);
        callback (account, userdata, &e);
        return;
    }

    if (priv->loaded && !priv->enabled)
    {
        /* FIXME: pick a better error and put it in telepathy-spec? */
        GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "account isn't Enabled" };

        DEBUG ("%s: %s", priv->unique_name, e.message);
        callback (account, userdata, &e);
        return;
    }

    /* listen to the StatusChanged signal */
    if (priv->loaded && priv->conn_status == TP_CONNECTION_STATUS_DISCONNECTED)
        _mcd_account_connect_with_auto_presence (account);

    /* now the connection should be in connecting state; insert the
     * callback in the online_requests hash table, which will be processed
     * in the connection-status-changed callback */
    data = g_slice_new (McdOnlineRequestData);
    data->callback = callback;
    data->user_data = userdata;
    priv->online_requests = g_list_append (priv->online_requests, data);
}

GKeyFile *
_mcd_account_get_keyfile (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    return priv->keyfile;
}

/* this is public because of mcd-account-compat */
gchar *
_mcd_account_get_avatar_filename (McdAccount *account)
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

static void
mcd_account_self_handle_inspected_cb (TpConnection *connection,
                                      const gchar **names,
                                      const GError *error,
                                      gpointer user_data,
                                      GObject *weak_object)
{
    McdAccount *self = MCD_ACCOUNT (weak_object);

    if (error)
    {
        g_warning ("%s: InspectHandles failed: %s", G_STRFUNC, error->message);
        return;
    }

    if (names != NULL && names[0] != NULL)
    {
        _mcd_account_set_normalized_name (self, names[0]);
    }
}

static void
mcd_account_connection_ready_cb (McdAccount *account,
                                 McdConnection *connection)
{
    McdAccountPrivate *priv = account->priv;
    gchar *nickname;
    TpConnection *tp_connection;
    GArray *self_handle_array;
    guint self_handle;

    g_return_if_fail (MCD_IS_ACCOUNT (account));
    g_return_if_fail (connection == priv->connection);

    tp_connection = mcd_connection_get_tp_connection (connection);
    g_return_if_fail (tp_connection != NULL);

    self_handle_array = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
    self_handle = tp_connection_get_self_handle (tp_connection);
    g_array_append_val (self_handle_array, self_handle);
    tp_cli_connection_call_inspect_handles (tp_connection, -1,
                                            TP_HANDLE_TYPE_CONTACT,
                                            self_handle_array,
                                            mcd_account_self_handle_inspected_cb,
                                            NULL, NULL,
                                            (GObject *) account);
    g_array_free (self_handle_array, TRUE);

    /* FIXME: ideally, on protocols with server-stored nicknames, this should
     * only be done if the local Nickname has been changed since last time we
     * were online; Aliasing doesn't currently offer a way to tell whether
     * this is such a protocol, though. */

    nickname = mcd_account_get_alias (account);

    if (nickname != NULL)
    {
        /* this is a no-op if the connection doesn't support it */
        _mcd_connection_set_nickname (connection, nickname);
    }

    g_free (nickname);
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
        g_signal_handlers_disconnect_by_func (priv->connection,
                                              on_conn_self_presence_changed,
                                              account);
        g_signal_handlers_disconnect_by_func (priv->connection,
                                              on_conn_status_changed,
                                              account);
        g_signal_handlers_disconnect_by_func (priv->connection,
                                              mcd_account_connection_ready_cb,
                                              account);
        g_object_unref (priv->connection);
    }

    priv->connection = connection;

    if (connection)
    {
        g_return_if_fail (MCD_IS_CONNECTION (connection));
        g_object_ref (connection);

        if (_mcd_connection_is_ready (connection))
        {
            mcd_account_connection_ready_cb (account, connection);
        }
        else
        {
            g_signal_connect_swapped (connection, "ready",
                G_CALLBACK (mcd_account_connection_ready_cb), account);
        }

        g_signal_connect_swapped (connection, "self-nickname-changed",
                G_CALLBACK (mcd_account_connection_self_nickname_changed_cb),
                account);

        g_signal_connect (connection, "self-presence-changed",
                          G_CALLBACK (on_conn_self_presence_changed), account);
        g_signal_connect (connection, "connection-status-changed",
                          G_CALLBACK (on_conn_status_changed), account);
        g_signal_connect (connection, "abort",
                          G_CALLBACK (on_connection_abort), account);
    }
    else
    {
        priv->conn_status = TP_CONNECTION_STATUS_DISCONNECTED;
        priv->transport = NULL;
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

void
_mcd_account_request_temporary_presence (McdAccount *self,
                                         TpConnectionPresenceType type,
                                         const gchar *status)
{
    if (self->priv->connection != NULL)
    {
        _mcd_connection_request_presence (self->priv->connection,
                                          type, status, "");
    }
}

/**
 * mcd_account_connection_bind_transport:
 * @account: the #McdAccount.
 * @transport: the #McdTransport.
 *
 * Set @account as dependent on @transport; connectivity plugins should call
 * this function in the callback they registered with
 * mcd_plugin_register_account_connection(). This tells the account manager to
 * disconnect @account when @transport goes away.
 */
void
mcd_account_connection_bind_transport (McdAccount *account,
                                       McdTransport *transport)
{
    g_return_if_fail (MCD_IS_ACCOUNT (account));

    if (transport == account->priv->transport)
    {
        DEBUG ("account %s transport remains %p",
               account->priv->unique_name, transport);
    }
    else if (transport == NULL)
    {
        DEBUG ("unbinding account %s from transport %p",
               account->priv->unique_name, account->priv->transport);
        account->priv->transport = NULL;
    }
    else if (account->priv->transport == NULL)
    {
        DEBUG ("binding account %s to transport %p",
               account->priv->unique_name, transport);

        account->priv->transport = transport;
    }
    else
    {
        DEBUG ("disallowing migration of account %s from transport %p to %p",
               account->priv->unique_name, account->priv->transport,
               transport);
    }
}

McdTransport *
_mcd_account_connection_get_transport (McdAccount *account)
{
    g_return_val_if_fail (MCD_IS_ACCOUNT (account), NULL);

    return account->priv->transport;
}

McdAccountConnectionContext *
_mcd_account_get_connection_context (McdAccount *self)
{
    g_return_val_if_fail (MCD_IS_ACCOUNT (self), NULL);

    return self->priv->connection_context;
}

void
_mcd_account_set_connection_context (McdAccount *self,
                                     McdAccountConnectionContext *c)
{
    g_return_if_fail (MCD_IS_ACCOUNT (self));

    if (self->priv->connection_context != NULL)
    {
        _mcd_account_connection_context_free (self->priv->connection_context);
    }

    self->priv->connection_context = c;
}

gboolean
_mcd_account_get_always_on (McdAccount *self)
{
    g_return_val_if_fail (MCD_IS_ACCOUNT (self), FALSE);

    return self->priv->always_on;
}
