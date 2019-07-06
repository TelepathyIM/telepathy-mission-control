/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright © 2008–2010 Nokia Corporation.
 * Copyright © 2009–2013 Collabora Ltd.
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
 */

#include "config.h"
#include "mcd-account.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <dbus/dbus.h>
#include <glib/gstdio.h>
#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "mcd-account-priv.h"
#include "mcd-account-conditions.h"
#include "mcd-account-manager-priv.h"
#include "mcd-account-addressing.h"
#include "mcd-connection-priv.h"
#include "mcd-misc.h"
#include "mcd-manager.h"
#include "mcd-manager-priv.h"
#include "mcd-master.h"
#include "mcd-master-priv.h"
#include "mcd-dbusprop.h"

#include "_gen/interfaces.h"
#include "_gen/enums.h"
#include "_gen/gtypes.h"
#include "_gen/cli-Connection_Manager_Interface_Account_Storage-body.h"

#define MC_OLD_AVATAR_FILENAME	"avatar.bin"

#define MCD_ACCOUNT_PRIV(account) (MCD_ACCOUNT (account)->priv)

static void account_iface_init (TpSvcAccountClass *iface,
			       	gpointer iface_data);
static void properties_iface_init (TpSvcDBusPropertiesClass *iface,
				   gpointer iface_data);
static void account_avatar_iface_init (TpSvcAccountInterfaceAvatarClass *iface,
				       gpointer iface_data);
static void account_storage_iface_init (
    TpSvcAccountInterfaceStorageClass *iface,
    gpointer iface_data);
static void account_hidden_iface_init (
    McSvcAccountInterfaceHiddenClass *iface,
    gpointer iface_data);
static void account_external_password_storage_iface_init (
    McSvcAccountInterfaceExternalPasswordStorageClass *iface,
    gpointer iface_data);

static const McdDBusProp account_properties[];
static const McdDBusProp account_avatar_properties[];
static const McdDBusProp account_storage_properties[];
static const McdDBusProp account_hidden_properties[];
static const McdDBusProp account_external_password_storage_properties[];

static const McdInterfaceData account_interfaces[] = {
    MCD_IMPLEMENT_IFACE (tp_svc_account_get_type, account, TP_IFACE_ACCOUNT),
    MCD_IMPLEMENT_IFACE (tp_svc_account_interface_avatar_get_type,
			 account_avatar,
			 TP_IFACE_ACCOUNT_INTERFACE_AVATAR),
    MCD_IMPLEMENT_IFACE (mc_svc_account_interface_conditions_get_type,
			 account_conditions,
			 MC_IFACE_ACCOUNT_INTERFACE_CONDITIONS),
    MCD_IMPLEMENT_IFACE (tp_svc_account_interface_storage_get_type,
                         account_storage,
                         TP_IFACE_ACCOUNT_INTERFACE_STORAGE),
    MCD_IMPLEMENT_IFACE (tp_svc_account_interface_addressing_get_type,
        account_addressing,
        TP_IFACE_ACCOUNT_INTERFACE_ADDRESSING),
    MCD_IMPLEMENT_IFACE (mc_svc_account_interface_hidden_get_type,
                         account_hidden,
                         MC_IFACE_ACCOUNT_INTERFACE_HIDDEN),
    MCD_IMPLEMENT_OPTIONAL_IFACE (
        mc_svc_account_interface_external_password_storage_get_type,
        account_external_password_storage,
        MC_IFACE_ACCOUNT_INTERFACE_EXTERNAL_PASSWORD_STORAGE),

    { NULL, }
};

G_DEFINE_TYPE_WITH_CODE (McdAccount, mcd_account, G_TYPE_OBJECT,
			 MCD_DBUS_INIT_INTERFACES (account_interfaces);
			 G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
						properties_iface_init);
			)

typedef struct {
    McdOnlineRequestCb callback;
    gpointer user_data;
} McdOnlineRequestData;

struct _McdAccountPrivate
{
    gchar *unique_name;
    gchar *object_path;
    gchar *manager_name;
    gchar *protocol_name;

    TpConnection *tp_connection;
    TpContact *self_contact;
    McdConnection *connection;
    McdManager *manager;

    McdStorage *storage;
    TpDBusDaemon *dbus_daemon;
    gboolean registered;
    McdConnectivityMonitor *connectivity;

    McdAccountConnectionContext *connection_context;
    GKeyFile *keyfile;		/* configuration file */
    McpAccountStorage *storage_plugin;
    GPtrArray *supersedes;

    /* connection status */
    TpConnectionStatus conn_status;
    TpConnectionStatusReason conn_reason;
    gchar *conn_dbus_error;
    GHashTable *conn_error_details;

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

    /* %NULL if the account is valid; a valid error for reporting over the
     * D-Bus if the account is invalid.
     */
    GError *invalid_reason;

    gboolean connect_automatically;
    gboolean enabled;
    gboolean loaded;
    gboolean has_been_online;
    gboolean removed;
    gboolean always_on;
    gboolean changing_presence;
    gboolean setting_avatar;
    gboolean waiting_for_initial_avatar;
    gboolean waiting_for_connectivity;

    gboolean hidden;
    /* In addition to affecting dispatching, this flag also makes this
     * account bypass connectivity checks. */
    gboolean always_dispatch;

    /* These fields are used to cache the changed properties */
    gboolean properties_frozen;
    GHashTable *changed_properties;
    guint properties_source;

    gboolean password_saved;
};

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
    PROP_CONNECTIVITY_MONITOR,
    PROP_STORAGE,
    PROP_NAME,
    PROP_ALWAYS_ON,
    PROP_HIDDEN,
};

enum
{
    VALIDITY_CHANGED,
    CONNECTION_PATH_CHANGED,
    LAST_SIGNAL
};

static guint _mcd_account_signals[LAST_SIGNAL] = { 0 };
static GQuark account_ready_quark = 0;

GQuark
mcd_account_error_quark (void)
{
    static GQuark quark = 0;

    if (quark == 0)
        quark = g_quark_from_static_string ("mcd-account-error");

    return quark;
}

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

    g_return_if_fail (MCD_IS_ACCOUNT (account));
    priv = account->priv;

    if (!mcd_account_would_like_to_connect (account))
    {
        return;
    }

    if (_mcd_account_needs_dispatch (account))
    {
        DEBUG ("Always-dispatchable account %s needs no transport",
            priv->unique_name);
    }
    else if (mcd_connectivity_monitor_is_online (priv->connectivity))
    {
        DEBUG ("Account %s has connectivity, connecting",
            priv->unique_name);
    }
    else
    {
        DEBUG ("Account %s needs connectivity, not connecting",
            priv->unique_name);
    }

    DEBUG ("connecting account %s", priv->unique_name);
    _mcd_account_connect_with_auto_presence (account, FALSE);
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
        if (!mcd_account_is_valid (account) || !account->priv->enabled)
        {
            /* FIXME: pick better errors and put them in telepathy-spec? */
            GError e = { TP_ERROR, TP_ERROR_NOT_AVAILABLE,
                "account isn't Valid (not enough information to put it "
                    "online)" };
            GList *list;

            if (mcd_account_is_valid (account))
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
            _mcd_account_connect_with_auto_presence (account, TRUE);
        }
    }

    _mcd_account_maybe_autoconnect (account);

    g_object_unref (account);
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
    McdAccountPrivate *priv = account->priv;
    McdStorage *storage = priv->storage;
    const gchar *account_name = mcd_account_get_unique_name (account);
    gboolean secret = mcd_account_parameter_is_secret (account, name);

    mcd_storage_set_parameter (storage, account_name, name, value, secret);
}

static GType mc_param_type (const TpConnectionManagerParam *param);

/**
 * mcd_account_get_parameter:
 * @account: the #McdAccount.
 * @name: the parameter name.
 * @parameter: location at which to store the parameter's current value, or
 *  %NULL if you don't actually care about the parameter's value.
 * @error: location at which to store an error if the parameter cannot be
 *  retrieved.
 *
 * Get the @name parameter for @account.
 *
 * Returns: %TRUE if the parameter could be retrieved; %FALSE otherwise
 */
gboolean
mcd_account_get_parameter (McdAccount *account, const gchar *name,
                           GValue *parameter,
                           GError **error)
{
    McdAccountPrivate *priv = account->priv;
    const TpConnectionManagerParam *param;
    GType type;

    param = mcd_manager_get_protocol_param (priv->manager,
                                            priv->protocol_name, name);
    type = mc_param_type (param);

    return mcd_account_get_parameter_of_known_type (account, name,
                                                    type, parameter, error);
}

gboolean
mcd_account_get_parameter_of_known_type (McdAccount *account,
                                         const gchar *name,
                                         GType type,
                                         GValue *parameter,
                                         GError **error)
{
    const gchar *account_name = mcd_account_get_unique_name (account);
    McdStorage *storage = account->priv->storage;
    GValue tmp = G_VALUE_INIT;

    g_value_init (&tmp, type);

    if (mcd_storage_get_parameter (storage, account_name, name, &tmp, error))
    {
        if (parameter != NULL)
        {
            memcpy (parameter, &tmp, sizeof (tmp));
        }
        else
        {
            g_value_unset (&tmp);
        }

        return TRUE;
    }

    g_value_unset (&tmp);
    return FALSE;
}

typedef void (*CheckParametersCb) (
    McdAccount *account,
    const GError *invalid_reason,
    gpointer user_data);
static void mcd_account_check_parameters (McdAccount *account,
    CheckParametersCb callback, gpointer user_data);

static void
manager_ready_check_params_cb (McdAccount *account,
    const GError *invalid_reason,
    gpointer user_data)
{
    McdAccountPrivate *priv = account->priv;

    g_clear_error (&priv->invalid_reason);
    if (invalid_reason != NULL)
    {
        priv->invalid_reason = g_error_copy (invalid_reason);
    }

    mcd_account_loaded (account);
}

static void
account_external_password_storage_get_accounts_cb (TpProxy *cm,
    const GValue *value,
    const GError *in_error,
    gpointer user_data,
    GObject *self)
{
  McdAccount *account = MCD_ACCOUNT (self);
  const char *account_id = user_data;
  GHashTable *map, *props;

  if (in_error != NULL)
    {
      DEBUG ("Failed to get Account property: %s", in_error->message);
      return;
    }

  g_return_if_fail (G_VALUE_HOLDS (value, MC_HASH_TYPE_ACCOUNT_FLAGS_MAP));

  map = g_value_get_boxed (value);

  account->priv->password_saved =
    GPOINTER_TO_UINT (g_hash_table_lookup (map, account_id)) &
      MC_ACCOUNT_FLAG_CREDENTIALS_STORED;

  DEBUG ("PasswordSaved = %u", account->priv->password_saved);

  /* emit the changed signal */
  props = tp_asv_new (
      "PasswordSaved", G_TYPE_BOOLEAN, account->priv->password_saved,
      NULL);

  tp_svc_dbus_properties_emit_properties_changed (account,
      MC_IFACE_ACCOUNT_INTERFACE_EXTERNAL_PASSWORD_STORAGE,
      props,
      NULL);

  g_hash_table_unref (props);
}

static void
account_setup_identify_account_cb (TpProxy *protocol,
    const char *account_id,
    const GError *in_error,
    gpointer user_data,
    GObject *self)
{
  McdAccount *account = MCD_ACCOUNT (self);
  TpConnectionManager *cm = mcd_account_get_cm (account);

  if (in_error != NULL)
    {
      DEBUG ("Error identifying account: %s", in_error->message);
      return;
    }

  DEBUG ("Identified account as %s", account_id);

  /* look up the current value of the CM.I.AS.Accounts property
   * and monitor future changes */
  tp_cli_dbus_properties_call_get (cm, -1,
      MC_IFACE_CONNECTION_MANAGER_INTERFACE_ACCOUNT_STORAGE,
      "Accounts",
      account_external_password_storage_get_accounts_cb,
      g_strdup (account_id), g_free, G_OBJECT (account));
}

static void
account_external_password_storage_properties_changed_cb (TpProxy *cm,
    const char *iface,
    GHashTable *changed_properties,
    const char **invalidated_properties,
    gpointer user_data,
    GObject *self)
{
  McdAccount *account = MCD_ACCOUNT (self);
  TpProtocol *protocol = tp_connection_manager_get_protocol_object (
      TP_CONNECTION_MANAGER (cm), account->priv->protocol_name);
  GHashTable *params;

  if (tp_strdiff (iface,
        MC_IFACE_CONNECTION_MANAGER_INTERFACE_ACCOUNT_STORAGE))
    return;

  /* look up account identity so we can look up our value in
   * the Accounts map */
  params = _mcd_account_dup_parameters (account);
  tp_cli_protocol_call_identify_account (protocol, -1, params,
      account_setup_identify_account_cb,
      NULL, NULL, G_OBJECT (account));

  g_hash_table_unref (params);
}

static void on_manager_ready (McdManager *manager, const GError *error,
                              gpointer user_data)
{
    McdAccount *account = MCD_ACCOUNT (user_data);

    if (error)
    {
        DEBUG ("got error: %s", error->message);
        mcd_account_loaded (account);
    }
    else
    {
        TpConnectionManager *cm = mcd_manager_get_tp_proxy (manager);

        mcd_account_check_parameters (account, manager_ready_check_params_cb,
                                      NULL);

        /* determine if we support Acct.I.ExternalPasswordStorage */
        if (tp_proxy_has_interface_by_id (cm,
                MC_IFACE_QUARK_CONNECTION_MANAGER_INTERFACE_ACCOUNT_STORAGE))
        {
            TpProtocol *protocol = tp_connection_manager_get_protocol_object (
                cm, account->priv->protocol_name);
            GHashTable *params;

            DEBUG ("CM %s has CM.I.AccountStorage iface",
                   mcd_manager_get_name (manager));

            mcd_dbus_activate_optional_interface (
                TP_SVC_DBUS_PROPERTIES (account),
                MC_TYPE_SVC_ACCOUNT_INTERFACE_EXTERNAL_PASSWORD_STORAGE);

            /* look up account identity so we can look up our value in
             * the Accounts map */
            params = _mcd_account_dup_parameters (account);
            tp_cli_protocol_call_identify_account (protocol, -1, params,
                account_setup_identify_account_cb,
                NULL, NULL, G_OBJECT (account));

            tp_cli_dbus_properties_connect_to_properties_changed (cm,
                account_external_password_storage_properties_changed_cb,
                NULL, NULL, G_OBJECT (account), NULL);

            g_hash_table_unref (params);
        }
    }
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
get_old_account_data_path (McdAccountPrivate *priv)
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
account_delete_identify_account_cb (TpProxy *protocol,
    const char *account_id,
    const GError *in_error,
    gpointer user_data,
    GObject *self)
{
  McdAccount *account = MCD_ACCOUNT (self);
  TpConnectionManager *cm = mcd_account_get_cm (account);

  if (in_error != NULL)
    {
      DEBUG ("Error identifying account: %s", in_error->message);
    }
  else
    {
      DEBUG ("Identified account as %s", account_id);

      mc_cli_connection_manager_interface_account_storage_call_remove_account (
          cm, -1, account_id,
          NULL, NULL, NULL, NULL);
    }

  g_object_unref (account);
}

static void
unregister_dbus_service (McdAccount *self)
{
    DBusGConnection *dbus_connection;

    g_return_if_fail (MCD_IS_ACCOUNT (self));

    if (!self->priv->registered)
        return;

    dbus_connection = tp_proxy_get_dbus_connection (self->priv->dbus_daemon);
    dbus_g_connection_unregister_g_object (dbus_connection, (GObject *) self);

    self->priv->registered = FALSE;
}

void
mcd_account_delete (McdAccount *account,
                     McdAccountDeleteCb callback,
                     gpointer user_data)
{
    McdAccountPrivate *priv = account->priv;
    gchar *data_dir_str;
    GError *error = NULL;
    const gchar *name = mcd_account_get_unique_name (account);
    TpConnectionManager *cm = mcd_account_get_cm (account);

    /* if the CM implements CM.I.AccountStorage, we need to tell the CM
     * to forget any account credentials it knows */
    if (tp_proxy_has_interface_by_id (cm,
            MC_IFACE_QUARK_CONNECTION_MANAGER_INTERFACE_ACCOUNT_STORAGE))
    {
        TpProtocol *protocol;
        GHashTable *params;

        /* identify the account */
        protocol = tp_connection_manager_get_protocol_object (cm,
            account->priv->protocol_name);
        params = _mcd_account_dup_parameters (account);

        tp_cli_protocol_call_identify_account (protocol, -1, params,
            account_delete_identify_account_cb,
            NULL, NULL, g_object_ref (account));

        g_hash_table_unref (params);
    }

    /* got to turn the account off before removing it, otherwise we can *
     * end up with an orphaned CM holding the account online            */
    if (!_mcd_account_set_enabled (account, FALSE, FALSE,
                                   MCD_DBUS_PROP_SET_FLAG_NONE, &error))
    {
        g_warning ("could not disable account %s (%s)", name, error->message);
        callback (account, error, user_data);
        g_error_free (error);
        return;
    }

    mcd_storage_delete_account (priv->storage, name);

    data_dir_str = get_old_account_data_path (priv);

    if (data_dir_str != NULL)
    {
        GDir *data_dir = g_dir_open (data_dir_str, 0, NULL);

        if (data_dir)
        {
            const gchar *filename;

            while ((filename = g_dir_read_name (data_dir)) != NULL)
            {
                gchar *path = g_build_filename (data_dir_str, filename, NULL);

                g_remove (path);
                g_free (path);
            }

            g_dir_close (data_dir);
            g_rmdir (data_dir_str);
        }

        g_free (data_dir_str);
    }

    mcd_storage_commit (priv->storage, name);

    /* The callback may drop the latest ref on @account so make sure it stays
     * alive while we still need it. */
    g_object_ref (account);

    if (callback != NULL)
        callback (account, NULL, user_data);

    /* If the account was not removed via the DBus Account interface code     *
     * path and something is holding a ref to it so it does not get disposed, *
     * then this signal may not get fired, so we make sure it _does_ here     */
    if (!priv->removed)
    {
        DEBUG ("Forcing Account.Removed for %s", name);
        priv->removed = TRUE;
        tp_svc_account_emit_removed (account);
    }

    unregister_dbus_service (account);
    g_object_unref (account);
}

void
_mcd_account_load (McdAccount *account, McdAccountLoadCb callback,
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

static void mcd_account_changed_property (McdAccount *account,
    const gchar *key, const GValue *value);

static void
mcd_account_request_presence_int (McdAccount *account,
                                  TpConnectionPresenceType type,
                                  const gchar *status,
                                  const gchar *message,
                                  gboolean user_initiated)
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

    if (changed)
    {
        GValue value = G_VALUE_INIT;

        g_value_init (&value, TP_STRUCT_TYPE_SIMPLE_PRESENCE);
        g_value_take_boxed (&value,
                            tp_value_array_build (3,
                                G_TYPE_UINT, type,
                                G_TYPE_STRING, status,
                                G_TYPE_STRING, message,
                                G_TYPE_INVALID));
        mcd_account_changed_property (account, "RequestedPresence", &value);
        g_value_unset (&value);
    }

    DEBUG ("Requested presence: %u %s %s",
        priv->req_presence_type,
        priv->req_presence_status,
        priv->req_presence_message);

    if (type >= TP_CONNECTION_PRESENCE_TYPE_AVAILABLE)
    {
        if (!priv->enabled)
        {
            DEBUG ("%s not Enabled", priv->unique_name);
            return;
        }

        if (!mcd_account_is_valid (account))
        {
            DEBUG ("%s not Valid", priv->unique_name);
            return;
        }
    }

    if (priv->connection == NULL)
    {
        if (type >= TP_CONNECTION_PRESENCE_TYPE_AVAILABLE)
        {
            if (changed)
            {
                _mcd_account_set_changing_presence (account, TRUE);
            }

            _mcd_account_connection_begin (account, user_initiated);
        }
    }
    else
    {
        if (changed)
        {
            _mcd_account_set_changing_presence (account, TRUE);
        }

        _mcd_connection_request_presence (priv->connection,
                                          priv->req_presence_type,
					  priv->req_presence_status,
					  priv->req_presence_message);
    }
}

/*
 * mcd_account_rerequest_presence:
 *
 * Re-requests the account's current RequestedPresence, possibly triggering a
 * new connection attempt.
 */
static void
mcd_account_rerequest_presence (McdAccount *account,
                                gboolean user_initiated)
{
    McdAccountPrivate *priv = account->priv;

    mcd_account_request_presence_int (account,
                                      priv->req_presence_type,
                                      priv->req_presence_status,
                                      priv->req_presence_message,
                                      user_initiated);
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

static gboolean
emit_property_changed (gpointer userdata)
{
    McdAccount *account = MCD_ACCOUNT (userdata);
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called");

    if (g_hash_table_size (priv->changed_properties) > 0)
    {
        tp_svc_account_emit_account_property_changed (account,
            priv->changed_properties);
        g_hash_table_remove_all (priv->changed_properties);
    }

    if (priv->properties_source != 0)
    {
      g_source_remove (priv->properties_source);
      priv->properties_source = 0;
    }
    return FALSE;
}

static void
mcd_account_freeze_properties (McdAccount *self)
{
    g_return_if_fail (!self->priv->properties_frozen);
    DEBUG ("%s", self->priv->unique_name);
    self->priv->properties_frozen = TRUE;
}

static void
mcd_account_thaw_properties (McdAccount *self)
{
    g_return_if_fail (self->priv->properties_frozen);
    DEBUG ("%s", self->priv->unique_name);
    self->priv->properties_frozen = FALSE;

    if (g_hash_table_size (self->priv->changed_properties) != 0)
    {
        emit_property_changed (self);
    }
}

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
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called: %s", key);
    if (priv->changed_properties &&
	g_hash_table_lookup (priv->changed_properties, key))
    {
	/* the changed property was also changed before; then let's force the
	 * emission of the signal now, so that the property will appear in two
	 * separate signals */
        DEBUG ("Forcibly emit PropertiesChanged now");
	emit_property_changed (account);
    }

    if (priv->properties_source == 0)
    {
        DEBUG ("First changed property");
        priv->properties_source = g_timeout_add_full (G_PRIORITY_DEFAULT, 10,
                                                      emit_property_changed,
                                                      g_object_ref (account),
                                                      g_object_unref);
    }
    g_hash_table_insert (priv->changed_properties, (gpointer) key,
                         tp_g_value_slice_dup (value));
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
mcd_account_set_string_val (McdAccount *account,
                            const gchar *key,
                            const GValue *value,
                            McdDBusPropSetFlags flags,
                            GError **error)
{
    McdAccountPrivate *priv = account->priv;
    McdStorage *storage = priv->storage;
    const gchar *name = mcd_account_get_unique_name (account);
    const gchar *new_string;

    if (!G_VALUE_HOLDS_STRING (value))
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "Expected string for %s, but got %s", key,
                     G_VALUE_TYPE_NAME (value));
        return SET_RESULT_ERROR;
    }

    new_string = g_value_get_string (value);

    if (tp_str_empty (new_string)) {
        new_string = NULL;
    }

    if (flags & MCD_DBUS_PROP_SET_FLAG_ALREADY_IN_STORAGE)
    {
        mcd_account_changed_property (account, key, value);
        return SET_RESULT_CHANGED;
    }
    else if (mcd_storage_set_string (storage, name, key, new_string))
    {
        mcd_storage_commit (storage, name);
        mcd_account_changed_property (account, key, value);
        return SET_RESULT_CHANGED;
    }
    else
    {
        return SET_RESULT_UNCHANGED;
    }
}

static void
mcd_account_get_string_val (McdAccount *account, const gchar *key,
			    GValue *value)
{
    McdAccountPrivate *priv = account->priv;
    const gchar *name = mcd_account_get_unique_name (account);

    g_value_init (value, G_TYPE_STRING);

    if (!mcd_storage_get_attribute (priv->storage, name, key, value, NULL))
    {
        g_value_set_static_string (value, NULL);
    }
}

static gboolean
set_display_name (TpSvcDBusProperties *self,
                  const gchar *name,
                  const GValue *value,
                  McdDBusPropSetFlags flags,
                  GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called for %s", priv->unique_name);
    return (mcd_account_set_string_val (account,
        MC_ACCOUNTS_KEY_DISPLAY_NAME, value, flags,
        error) != SET_RESULT_ERROR);
}

static void
get_display_name (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    mcd_account_get_string_val (account, MC_ACCOUNTS_KEY_DISPLAY_NAME, value);
}

static gboolean
set_icon (TpSvcDBusProperties *self,
          const gchar *name,
          const GValue *value,
          McdDBusPropSetFlags flags,
          GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called for %s", priv->unique_name);
    return (mcd_account_set_string_val (account,
        MC_ACCOUNTS_KEY_ICON, value, flags, error) != SET_RESULT_ERROR);
}

static void
get_icon (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    mcd_account_get_string_val (account, MC_ACCOUNTS_KEY_ICON, value);
}

static void
get_valid (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, mcd_account_is_valid (account));
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

/**
 * mcd_account_set_enabled:
 * @account: the #McdAccount
 * @enabled: %TRUE if the account is to be enabled
 * @write_out: %TRUE if this should be written to the keyfile
 * @error: return location for an error condition
 *
 * Returns: %TRUE on success
 */
gboolean
_mcd_account_set_enabled (McdAccount *account,
                          gboolean enabled,
                          gboolean write_out,
                          McdDBusPropSetFlags flags,
                          GError **error)
{
    McdAccountPrivate *priv = account->priv;

    if (priv->always_on && !enabled)
    {
        g_set_error (error, TP_ERROR, TP_ERROR_PERMISSION_DENIED,
                     "Account %s cannot be disabled",
                     priv->unique_name);
        return FALSE;
    }

    if (priv->enabled != enabled)
    {
        GValue value = G_VALUE_INIT;
        const gchar *name = mcd_account_get_unique_name (account);

        if (!enabled && priv->connection != NULL)
            _mcd_connection_request_presence (priv->connection,
                                              TP_CONNECTION_PRESENCE_TYPE_OFFLINE,
                                              "offline",
                                              NULL);

        priv->enabled = enabled;

        g_value_init (&value, G_TYPE_BOOLEAN);
        g_value_set_boolean (&value, enabled);

        if (!(flags & MCD_DBUS_PROP_SET_FLAG_ALREADY_IN_STORAGE))
        {
            mcd_storage_set_attribute (priv->storage, name,
                                       MC_ACCOUNTS_KEY_ENABLED, &value);

            if (write_out)
                mcd_storage_commit (priv->storage, name);
        }

        mcd_account_changed_property (account, "Enabled", &value);

        g_value_unset (&value);

        if (enabled)
        {
            mcd_account_rerequest_presence (account, TRUE);
            _mcd_account_maybe_autoconnect (account);
        }
    }

    return TRUE;
}

static gboolean
set_enabled (TpSvcDBusProperties *self,
             const gchar *name,
             const GValue *value,
             McdDBusPropSetFlags flags,
             GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    gboolean enabled;

    DEBUG ("called for %s", priv->unique_name);

    if (!G_VALUE_HOLDS_BOOLEAN (value))
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "Expected boolean for Enabled, but got %s",
                     G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    enabled = g_value_get_boolean (value);

    return _mcd_account_set_enabled (account, enabled, TRUE, flags, error);
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
set_service (TpSvcDBusProperties *self, const gchar *name,
             const GValue *value,
             McdDBusPropSetFlags flags,
             GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    SetResult ret = SET_RESULT_ERROR;
    gboolean proceed = TRUE;
    static GRegex *rule = NULL;
    static gsize service_re_init = 0;

    if (g_once_init_enter (&service_re_init))
    {
        GError *regex_error = NULL;
        rule = g_regex_new ("^(?:[a-z][a-z0-9_-]*)?$",
                            G_REGEX_CASELESS|G_REGEX_DOLLAR_ENDONLY,
                            0, &regex_error);
        g_assert_no_error (regex_error);
        g_once_init_leave (&service_re_init, 1);
    }

    if (G_VALUE_HOLDS_STRING (value))
      proceed = g_regex_match (rule, g_value_get_string (value), 0, NULL);

    /* if value is not a string, mcd_account_set_string_val will set *
     * the appropriate error for us: don't duplicate that logic here */
    if (proceed)
    {
        ret = mcd_account_set_string_val (account, MC_ACCOUNTS_KEY_SERVICE,
                                          value, flags, error);
    }
    else
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "Invalid service '%s': Must consist of ASCII alphanumeric "
                     "characters, underscores (_) and hyphens (-) only, and "
                     "start with a letter",
                     g_value_get_string (value));
    }

    return (ret != SET_RESULT_ERROR);
}

static void
get_service (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    mcd_account_get_string_val (account, MC_ACCOUNTS_KEY_SERVICE, value);
}

static void
mcd_account_set_self_alias_cb (TpConnection *tp_connection,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  if (error)
    DEBUG ("%s", error->message);
}

static void
mcd_account_send_nickname_to_connection (McdAccount *self,
    const gchar *nickname)
{
  if (self->priv->tp_connection == NULL)
    return;

  if (self->priv->self_contact == NULL)
    return;

  DEBUG ("%s: '%s'", self->priv->unique_name, nickname);

  if (tp_proxy_has_interface_by_id (self->priv->tp_connection,
          TP_IFACE_QUARK_CONNECTION_INTERFACE_ALIASING))
    {
      GHashTable *aliases = g_hash_table_new (NULL, NULL);

      g_hash_table_insert (aliases,
          GUINT_TO_POINTER (tp_contact_get_handle (self->priv->self_contact)),
          (gchar *) nickname);
      tp_cli_connection_interface_aliasing_call_set_aliases (
          self->priv->tp_connection, -1, aliases,
          mcd_account_set_self_alias_cb, NULL, NULL, NULL);
      g_hash_table_unref (aliases);
    }
}


static gboolean
set_nickname (TpSvcDBusProperties *self, const gchar *name,
              const GValue *value,
              McdDBusPropSetFlags flags,
              GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    SetResult ret;
    GValue replacement = G_VALUE_INIT;

    DEBUG ("called for %s", priv->unique_name);

    /* If we're asked to set Nickname = "", set it to our identifier
     * (NormalizedName) instead, so that we always have some sort of nickname.
     * This matches what we do when connecting an account.
     *
     * Exception: if we're not fully connected yet (and hence have no
     * self-contact), rely on the corresponding special-case
     * when we do become connected.
     */
    if (G_VALUE_HOLDS_STRING (value) &&
        tp_str_empty (g_value_get_string (value)) &&
        priv->self_contact != NULL)
    {
        g_value_init (&replacement, G_TYPE_STRING);
        g_value_set_string (&replacement,
            tp_contact_get_identifier (priv->self_contact));
        value = &replacement;
    }

    ret = mcd_account_set_string_val (account, MC_ACCOUNTS_KEY_NICKNAME,
                                      value, flags, error);

    if (ret != SET_RESULT_ERROR)
    {
        mcd_account_send_nickname_to_connection (account,
            g_value_get_string (value));
    }

    if (value == &replacement)
        g_value_unset (&replacement);

    return (ret != SET_RESULT_ERROR);
}

static void
get_nickname (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    mcd_account_get_string_val (account, MC_ACCOUNTS_KEY_NICKNAME, value);
}

static void
mcd_account_self_contact_notify_avatar_file_cb (McdAccount *self,
    GParamSpec *unused_param_spec G_GNUC_UNUSED,
    TpContact *self_contact)
{
  const gchar *token;
  gchar *prev_token;
  GFile *file;
  GError *error = NULL;
  gboolean changed;

  if (self_contact != self->priv->self_contact)
    return;

  file = tp_contact_get_avatar_file (self_contact);
  token = tp_contact_get_avatar_token (self_contact);

  if (self->priv->setting_avatar)
    {
      DEBUG ("Ignoring avatar change notification: we are setting ours");
      return;
    }

  if (self->priv->waiting_for_initial_avatar)
    {
      DEBUG ("Ignoring avatar change notification: we are waiting for the "
          "initial value");
      return;
    }

  prev_token = _mcd_account_get_avatar_token (self);
  changed = tp_strdiff (prev_token, token);
  g_free (prev_token);

  if (!changed)
    {
      DEBUG ("Avatar unchanged: '%s'", token);
      return;
    }

  if (file == NULL)
    {
      if (!_mcd_account_set_avatar (self, NULL, "", "", &error))
        {
          DEBUG ("Attempt to clear avatar failed: %s", error->message);
          g_clear_error (&error);
        }
    }
  else
    {
      gchar *contents = NULL;
      gsize len = 0;
      GArray *arr;

      if (!g_file_load_contents (file, NULL, &contents, &len, NULL, &error))
        {
          gchar *uri = g_file_get_uri (file);

          WARNING ("Unable to read avatar file %s: %s", uri, error->message);
          g_clear_error (&error);
          g_free (uri);
          return;
        }

      if (G_UNLIKELY (len > G_MAXUINT))
        {
          gchar *uri = g_file_get_uri (file);

          WARNING ("Avatar file %s was ludicrously huge", uri);
          g_free (uri);
          g_free (contents);
          return;
        }

      arr = g_array_sized_new (TRUE, FALSE, 1, (guint) len);
      g_array_append_vals (arr, contents, (guint) len);
      g_free (contents);

      if (!_mcd_account_set_avatar (self, arr,
              tp_contact_get_avatar_mime_type (self_contact),
              tp_contact_get_avatar_token (self_contact), &error))
        {
          DEBUG ("Attempt to save avatar failed: %s", error->message);
          g_clear_error (&error);
        }

      g_array_unref (arr);
    }
}

static void
avatars_set_avatar_cb (TpConnection *tp_connection,
    const gchar *token,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  McdAccount *self = MCD_ACCOUNT (weak_object);

  self->priv->setting_avatar = FALSE;

  if (error != NULL)
    {
      DEBUG ("%s: %s", self->priv->unique_name, error->message);
    }
  else
    {
      DEBUG ("%s: new token %s", self->priv->unique_name, token);
      _mcd_account_set_avatar_token (self, token);
    }
}

static void
avatars_clear_avatar_cb (TpConnection *tp_connection,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  McdAccount *self = MCD_ACCOUNT (weak_object);

  self->priv->setting_avatar = FALSE;

  if (error != NULL)
    {
      DEBUG ("%s: %s", self->priv->unique_name, error->message);
    }
  else
    {
      DEBUG ("%s: success", self->priv->unique_name);
      _mcd_account_set_avatar_token (self, "");
    }
}

static void
mcd_account_send_avatar_to_connection (McdAccount *self,
    const GArray *avatar,
    const gchar *mime_type)
{
  if (self->priv->tp_connection == NULL)
    return;

  if (self->priv->self_contact == NULL)
    return;

  DEBUG ("%s: %u bytes", self->priv->unique_name, avatar->len);

  if (tp_proxy_has_interface_by_id (self->priv->tp_connection,
          TP_IFACE_QUARK_CONNECTION_INTERFACE_AVATARS))
    {
      self->priv->setting_avatar = TRUE;

      if (avatar->len > 0 && avatar->len < G_MAXUINT)
        {
          tp_cli_connection_interface_avatars_call_set_avatar (
            self->priv->tp_connection, -1, avatar, mime_type,
            avatars_set_avatar_cb, NULL, NULL, (GObject *) self);
        }
      else
        {
          tp_cli_connection_interface_avatars_call_clear_avatar (
              self->priv->tp_connection, -1, avatars_clear_avatar_cb,
              NULL, NULL, (GObject *) self);
        }
    }
  else
    {
      DEBUG ("unsupported, ignoring");
    }
}

static gboolean
set_avatar (TpSvcDBusProperties *self, const gchar *name, const GValue *value,
            McdDBusPropSetFlags flags,
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
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
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
get_parameters (TpSvcDBusProperties *self, const gchar *name,
                GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    GHashTable *params = _mcd_account_dup_parameters (account);

    if (params == NULL)
    {
        if (mcd_account_is_valid (account))
            g_warning ("%s is supposedly valid, but _dup_parameters() failed!",
                mcd_account_get_unique_name (account));

        params = tp_asv_new (NULL, NULL);
    }

    g_value_init (value, TP_HASH_TYPE_STRING_VARIANT_MAP);
    g_value_take_boxed (value, params);
}

gboolean
_mcd_account_presence_type_is_settable (TpConnectionPresenceType type)
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
                        const gchar *name,
                        const GValue *value,
                        McdDBusPropSetFlags flags,
                        GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    const gchar *status, *message;
    TpConnectionPresenceType type;
    gboolean changed = FALSE;
    GValueArray *va;
    const gchar *account_name = mcd_account_get_unique_name (account);

    DEBUG ("called for %s", account_name);

    if (!G_VALUE_HOLDS (value, TP_STRUCT_TYPE_SIMPLE_PRESENCE))
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
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
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "AutomaticPresence must be an online presence, not %d",
                     type);
        return FALSE;
    }

    DEBUG ("setting automatic presence: %d, %s, %s", type, status, message);

    if (priv->auto_presence_type != type)
    {
        priv->auto_presence_type = type;
        changed = TRUE;
    }

    if (tp_strdiff (priv->auto_presence_status, status))
    {
        g_free (priv->auto_presence_status);
        priv->auto_presence_status = g_strdup (status);
        changed = TRUE;
    }

    if (tp_strdiff (priv->auto_presence_message, message))
    {
        g_free (priv->auto_presence_message);
        priv->auto_presence_message = g_strdup (message);
        changed = TRUE;
    }

    if (changed)
    {
        mcd_storage_set_attribute (priv->storage, account_name,
                                   MC_ACCOUNTS_KEY_AUTOMATIC_PRESENCE,
                                   value);
        mcd_storage_commit (priv->storage, account_name);
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
                           const gchar *name,
                           const GValue *value,
                           McdDBusPropSetFlags flags,
                           GError **error)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;
    gboolean connect_automatically;

    DEBUG ("called for %s", priv->unique_name);

    if (!G_VALUE_HOLDS_BOOLEAN (value))
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "Expected boolean for ConnectAutomatically, but got %s",
                     G_VALUE_TYPE_NAME (value));
        return FALSE;
    }

    connect_automatically = g_value_get_boolean (value);

    if (priv->always_on && !connect_automatically)
    {
        g_set_error (error, TP_ERROR, TP_ERROR_PERMISSION_DENIED,
                     "Account %s always connects automatically",
                     priv->unique_name);
        return FALSE;
    }

    if (priv->connect_automatically != connect_automatically)
    {
        const gchar *account_name = mcd_account_get_unique_name (account);

        if (!(flags & MCD_DBUS_PROP_SET_FLAG_ALREADY_IN_STORAGE))
        {
            mcd_storage_set_attribute (priv->storage, account_name,
                                       MC_ACCOUNTS_KEY_CONNECT_AUTOMATICALLY,
                                       value);
            mcd_storage_commit (priv->storage, account_name);
        }

        priv->connect_automatically = connect_automatically;
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
get_connection_error (TpSvcDBusProperties *self,
                      const gchar *name,
                      GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    g_value_init (value, G_TYPE_STRING);
    g_value_set_string (value, account->priv->conn_dbus_error);
}

static void
get_connection_error_details (TpSvcDBusProperties *self,
                              const gchar *name,
                              GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    g_value_init (value, TP_HASH_TYPE_STRING_VARIANT_MAP);
    g_value_set_boxed (value, account->priv->conn_error_details);
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
                        const gchar *name,
                        const GValue *value,
                        McdDBusPropSetFlags flags,
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
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
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
        g_set_error (error, TP_ERROR, TP_ERROR_PERMISSION_DENIED,
                     "Account %s cannot be taken offline", priv->unique_name);
        return FALSE;
    }

    if (!_mcd_account_presence_type_is_settable (type))
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "RequestedPresence %d cannot be set on yourself", type);
        return FALSE;
    }

    DEBUG ("setting requested presence: %d, %s, %s", type, status, message);

    mcd_account_request_presence_int (account, type, status, message, TRUE);
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
get_changing_presence (TpSvcDBusProperties *self,
                       const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, priv->changing_presence);
}

static void
get_normalized_name (TpSvcDBusProperties *self,
		     const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);

    mcd_account_get_string_val (account, MC_ACCOUNTS_KEY_NORMALIZED_NAME,
                                value);
}

static gboolean
set_supersedes (TpSvcDBusProperties *svc,
    const gchar *name,
    const GValue *value,
    McdDBusPropSetFlags flags,
    GError **error)
{
  McdAccount *self = MCD_ACCOUNT (svc);

  if (!G_VALUE_HOLDS (value, TP_ARRAY_TYPE_OBJECT_PATH_LIST))
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Unexpected type for Supersedes: wanted 'ao', got %s",
          G_VALUE_TYPE_NAME (value));
      return FALSE;
    }

  if (self->priv->supersedes != NULL)
    g_ptr_array_unref (self->priv->supersedes);

  self->priv->supersedes = g_value_dup_boxed (value);
  mcd_account_changed_property (self, name, value);

  mcd_storage_set_attribute (self->priv->storage, self->priv->unique_name,
      MC_ACCOUNTS_KEY_SUPERSEDES, value);
  mcd_storage_commit (self->priv->storage, self->priv->unique_name);

  return TRUE;
}

static void
get_supersedes (TpSvcDBusProperties *svc,
    const gchar *name,
    GValue *value)
{
  McdAccount *self = MCD_ACCOUNT (svc);

  if (self->priv->supersedes == NULL)
    self->priv->supersedes = g_ptr_array_new ();

  g_value_init (value, TP_ARRAY_TYPE_OBJECT_PATH_LIST);
  g_value_set_boxed (value, self->priv->supersedes);
}

static McpAccountStorage *
get_storage_plugin (McdAccount *account)
{
  McdAccountPrivate *priv = account->priv;
  const gchar *account_name = mcd_account_get_unique_name (account);

  if (priv->storage_plugin != NULL)
    return priv->storage_plugin;

  priv->storage_plugin = mcd_storage_get_plugin (priv->storage, account_name);

  if (priv->storage_plugin != NULL)
      g_object_ref (priv->storage_plugin);

   return priv->storage_plugin;
}

static void
get_storage_provider (TpSvcDBusProperties *self,
    const gchar *name, GValue *value)
{
  McdAccount *account = MCD_ACCOUNT (self);
  McpAccountStorage *storage_plugin = get_storage_plugin (account);

  g_value_init (value, G_TYPE_STRING);

  if (storage_plugin != NULL)
    g_value_set_string (value, mcp_account_storage_provider (storage_plugin));
  else
    g_value_set_static_string (value, "");
}

static gboolean
set_storage_provider (TpSvcDBusProperties *self,
    const gchar *name,
    const GValue *value,
    McdDBusPropSetFlags flags,
    GError **error)
{
  McdAccount *account = MCD_ACCOUNT (self);
  McpAccountStorage *storage_plugin = get_storage_plugin (account);
  const gchar *current_provider = mcp_account_storage_provider (storage_plugin);

  if (!G_VALUE_HOLDS_STRING (value) ||
      tp_strdiff (g_value_get_string (value), current_provider))
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Cannot change provider, it is defined at account creation only");
      return FALSE;
    }

  return TRUE;
}

static void
get_storage_identifier (TpSvcDBusProperties *self,
    const gchar *name, GValue *value)
{

  McdAccount *account = MCD_ACCOUNT (self);
  McpAccountStorage *storage_plugin = get_storage_plugin (account);
  GValue identifier = G_VALUE_INIT;

  g_value_init (value, G_TYPE_VALUE);

  if (storage_plugin != NULL)
    {
      mcp_account_storage_get_identifier (
          storage_plugin, account->priv->unique_name, &identifier);
    }
  else
    {
      g_value_init (&identifier, G_TYPE_UINT);

      g_value_set_uint (&identifier, 0);
    }

  g_value_set_boxed (value, &identifier);

  g_value_unset (&identifier);
}

static void
get_storage_specific_info (TpSvcDBusProperties *self,
    const gchar *name, GValue *value)
{
  GHashTable *storage_specific_info;
  McdAccount *account = MCD_ACCOUNT (self);
  McpAccountStorage *storage_plugin = get_storage_plugin (account);

  g_value_init (value, TP_HASH_TYPE_STRING_VARIANT_MAP);

  if (storage_plugin != NULL)
    storage_specific_info = mcp_account_storage_get_additional_info (
        storage_plugin, account->priv->unique_name);
  else
    storage_specific_info = g_hash_table_new (g_str_hash, g_str_equal);

  g_value_take_boxed (value, storage_specific_info);
}

static void
get_storage_restrictions (TpSvcDBusProperties *self,
    const gchar *name, GValue *value)
{
  TpStorageRestrictionFlags flags;
  McdAccount *account = MCD_ACCOUNT (self);
  McpAccountStorage *storage_plugin = get_storage_plugin (account);

  g_value_init (value, G_TYPE_UINT);

  g_return_if_fail (storage_plugin != NULL);

  flags = mcp_account_storage_get_restrictions (storage_plugin,
      account->priv->unique_name);

  g_value_set_uint (value, flags);
}

static const McdDBusProp account_properties[] = {
    { "Interfaces", NULL, mcd_dbus_get_interfaces },
    { "DisplayName", set_display_name, get_display_name },
    { "Icon", set_icon, get_icon },
    { "Valid", NULL, get_valid },
    { "Enabled", set_enabled, get_enabled },
    { "Nickname", set_nickname, get_nickname },
    { "Service", set_service, get_service  },
    { "Parameters", NULL, get_parameters },
    { "AutomaticPresence", set_automatic_presence, get_automatic_presence },
    { "ConnectAutomatically", set_connect_automatically, get_connect_automatically },
    { "Connection", NULL, get_connection },
    { "ConnectionStatus", NULL, get_connection_status },
    { "ConnectionStatusReason", NULL, get_connection_status_reason },
    { "ConnectionError", NULL, get_connection_error },
    { "ConnectionErrorDetails", NULL, get_connection_error_details },
    { "CurrentPresence", NULL, get_current_presence },
    { "RequestedPresence", set_requested_presence, get_requested_presence },
    { "ChangingPresence", NULL, get_changing_presence },
    { "NormalizedName", NULL, get_normalized_name },
    { "HasBeenOnline", NULL, get_has_been_online },
    { "Supersedes", set_supersedes, get_supersedes },
    { 0 },
};

static const McdDBusProp account_avatar_properties[] = {
    { "Avatar", set_avatar, get_avatar },
    { 0 },
};

static const McdDBusProp account_storage_properties[] = {
    { "StorageProvider", set_storage_provider, get_storage_provider },
    { "StorageIdentifier", NULL, get_storage_identifier },
    { "StorageSpecificInformation", NULL, get_storage_specific_info },
    { "StorageRestrictions", NULL, get_storage_restrictions },
    { 0 },
};

static void
account_avatar_iface_init (TpSvcAccountInterfaceAvatarClass *iface,
			   gpointer iface_data)
{
}

static void
account_storage_iface_init (TpSvcAccountInterfaceStorageClass *iface,
                             gpointer iface_data)
{
}

static void
get_hidden (TpSvcDBusProperties *self,
    const gchar *name, GValue *value)
{
  g_value_init (value, G_TYPE_BOOLEAN);
  g_object_get_property (G_OBJECT (self), "hidden", value);
}

static gboolean
set_hidden (TpSvcDBusProperties *self,
    const gchar *name,
    const GValue *value,
    McdDBusPropSetFlags flags,
    GError **error)
{
  McdAccount *account = MCD_ACCOUNT (self);
  McdAccountPrivate *priv = account->priv;
  const gchar *account_name = mcd_account_get_unique_name (account);

  if (!G_VALUE_HOLDS_BOOLEAN (value))
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Hidden must be set to a boolean, not a %s",
          G_VALUE_TYPE_NAME (value));
      return FALSE;
    }

  /* Technically this property is immutable after the account's been created,
   * but currently it's not easy for this code to tell whether or not this is
   * a create-time property. It would probably be better if the create-time
   * properties were passed into us as a construct-time GObject property. But
   * that's a job for another month.
   *
   * So for now we check whether the value has changed, and violate the spec
   * by making this property mutable (at least with the keyfile backend).
   */
  if (mcd_storage_set_attribute (priv->storage, account_name,
          MC_ACCOUNTS_KEY_HIDDEN, value))
    {
      mcd_storage_commit (priv->storage, account_name);
      mcd_account_changed_property (account, MC_ACCOUNTS_KEY_HIDDEN, value);
      g_object_set_property (G_OBJECT (self), "hidden", value);
    }

  return TRUE;
}

static const McdDBusProp account_hidden_properties[] = {
    { "Hidden", set_hidden, get_hidden },
    { 0 },
};

static void
account_hidden_iface_init (
    McSvcAccountInterfaceHiddenClass *iface,
    gpointer iface_data)
{
  /* wow, it's pretty crap that I need this. */
}

static void
get_password_saved (TpSvcDBusProperties *self,
    const gchar *name,
    GValue *value)
{
  McdAccount *account = MCD_ACCOUNT (self);

  g_assert_cmpstr (name, ==, "PasswordSaved");

  g_value_init (value, G_TYPE_BOOLEAN);
  g_value_set_boolean (value, account->priv->password_saved);
}

static const McdDBusProp account_external_password_storage_properties[] = {
    { "PasswordSaved", NULL, get_password_saved },
    { 0 },
};

static void
account_external_password_storage_forget_credentials_cb (TpProxy *cm,
    const GError *in_error,
    gpointer user_data,
    GObject *self)
{
  DBusGMethodInvocation *context = user_data;

  if (in_error != NULL)
    {
      dbus_g_method_return_error (context, in_error);
      return;
    }

  mc_svc_account_interface_external_password_storage_return_from_forget_password (context);
}

static void
account_external_password_storage_identify_account_cb (TpProxy *protocol,
    const char *account_id,
    const GError *in_error,
    gpointer user_data,
    GObject *self)
{
  McdAccount *account = MCD_ACCOUNT (self);
  DBusGMethodInvocation *context = user_data;
  TpConnectionManager *cm = mcd_account_get_cm (account);

  if (in_error != NULL)
    {
      dbus_g_method_return_error (context, in_error);
      return;
    }

  DEBUG ("Identified account as %s", account_id);

  mc_cli_connection_manager_interface_account_storage_call_forget_credentials (
      cm, -1, account_id,
      account_external_password_storage_forget_credentials_cb,
      context, NULL, self);
}

static void
account_external_password_storage_forget_password (
    McSvcAccountInterfaceExternalPasswordStorage *self,
    DBusGMethodInvocation *context)
{
  McdAccount *account = MCD_ACCOUNT (self);
  TpConnectionManager *cm = mcd_account_get_cm (account);
  TpProtocol *protocol;
  GHashTable *params;

  /* do we support the interface */
  if (!tp_proxy_has_interface_by_id (cm,
          MC_IFACE_QUARK_CONNECTION_MANAGER_INTERFACE_ACCOUNT_STORAGE))
    {
      GError *error = g_error_new (TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
          "CM for this Account does not implement AccountStorage iface");

      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return;
    }

  /* identify the account */
  protocol = tp_connection_manager_get_protocol_object (cm,
      account->priv->protocol_name);
  params = _mcd_account_dup_parameters (account);

  tp_cli_protocol_call_identify_account (protocol, -1, params,
      account_external_password_storage_identify_account_cb,
      context, NULL, G_OBJECT (self));

  g_hash_table_unref (params);
}

static void
account_external_password_storage_iface_init (
    McSvcAccountInterfaceExternalPasswordStorageClass *iface,
    gpointer iface_data)
{
#define IMPLEMENT(x) \
  mc_svc_account_interface_external_password_storage_implement_##x (\
      iface, account_external_password_storage_##x)
  IMPLEMENT (forget_password);
#undef IMPLEMENT
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
    const gchar *dbus_signature;

    if (G_UNLIKELY (param == NULL))
        return G_TYPE_INVALID;

    dbus_signature = tp_connection_manager_param_get_dbus_signature (param);

    if (G_UNLIKELY (!dbus_signature))
        return G_TYPE_INVALID;

    switch (dbus_signature[0])
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
        if (dbus_signature[1] == DBUS_TYPE_STRING)
            return G_TYPE_STRV;
        /* other array types are not supported:
         * fall through the default case */
    default:
        g_warning ("skipping parameter %s, unknown type %s",
            tp_connection_manager_param_get_name (param), dbus_signature);
    }
    return G_TYPE_INVALID;
}

typedef struct
{
    McdAccount *self;
    DBusGMethodInvocation *context;
} RemoveMethodData;

static void
account_remove_delete_cb (McdAccount *account, const GError *error,
                          gpointer user_data)
{
    RemoveMethodData *data = (RemoveMethodData *) user_data;

    if (error != NULL)
    {
        dbus_g_method_return_error (data->context, (GError *) error);
        return;
    }

    if (!data->self->priv->removed)
    {
        data->self->priv->removed = TRUE;
        tp_svc_account_emit_removed (data->self);
    }

    tp_svc_account_return_from_remove (data->context);

    g_slice_free (RemoveMethodData, data);
}

static void
account_remove (TpSvcAccount *svc, DBusGMethodInvocation *context)
{
    McdAccount *self = MCD_ACCOUNT (svc);
    RemoveMethodData *data;

    data = g_slice_new0 (RemoveMethodData);
    data->self = self;
    data->context = context;

    DEBUG ("called");
    mcd_account_delete (self, account_remove_delete_cb, data);
}

/*
 * @account: the account
 * @name: an attribute name, or "param-" + a parameter name
 *
 * Tell the account that one of its attributes or parameters has changed
 * behind its back (as opposed to an external change triggered by DBus,
 * for example). This occurs when a storage plugin wishes to notify us
 * that something has changed. This will trigger an update when the
 * callback receives the new value. */
void
mcd_account_altered_by_plugin (McdAccount *account,
                               const gchar *name)
{
    /* parameters are handled en bloc, reinvoke self with bloc key: */
    if (g_str_has_prefix (name, "param-"))
    {
        mcd_account_altered_by_plugin (account, "Parameters");
    }
    else
    {
        guint i = 0;
        const McdDBusProp *prop = NULL;
        GValue value = G_VALUE_INIT;
        GError *error = NULL;

        DEBUG ("%s", name);

        if (tp_strdiff (name, "Parameters") &&
            !mcd_storage_init_value_for_attribute (&value, name))
        {
            WARNING ("plugin wants to alter %s but I don't know what "
                     "type that ought to be", name);
            return;
        }

        if (!tp_strdiff (name, "Parameters"))
        {
            get_parameters (TP_SVC_DBUS_PROPERTIES (account), name, &value);
        }
        else if (!mcd_storage_get_attribute (account->priv->storage,
                                             account->priv->unique_name,
                                             name, &value, &error))
        {
            WARNING ("cannot get new value of %s: %s", name, error->message);
            g_error_free (error);
            return;
        }

        /* find the property update handler */
        for (; prop == NULL && account_properties[i].name != NULL; i++)
        {
            if (g_str_equal (name, account_properties[i].name))
                prop = &account_properties[i];
        }

        /* is a known property: invoke the getter method for it (if any): *
         * then issue the change notification (DBus signals etc) for it   */
        if (prop != NULL)
        {
            /* poke the value back into itself with the setter: this      *
             * extra round-trip may trigger extra actions like notifying  *
             * the connection manager of the change, even though our own  *
             * internal storage already has this value and needn't change */
            if (prop->setprop != NULL)
            {
                DEBUG ("Calling property setter for %s", name);
                if (!prop->setprop (TP_SVC_DBUS_PROPERTIES (account),
                                    prop->name, &value,
                                    MCD_DBUS_PROP_SET_FLAG_ALREADY_IN_STORAGE,
                                    &error))
                {
                    WARNING ("Unable to set %s: %s", name, error->message);
                    g_error_free (error);
                }
            }
            else
            {
                DEBUG ("Emitting signal directly for %s", name);
                mcd_account_changed_property (account, prop->name, &value);
            }
        }
        else
        {
            DEBUG ("%s does not appear to be an Account property", name);
        }

        g_value_unset (&value);
    }
}


static void
mcd_account_check_parameters (McdAccount *account,
                              CheckParametersCb callback,
                              gpointer user_data)
{
    McdAccountPrivate *priv = account->priv;
    TpProtocol *protocol;
    GList *params = NULL;
    GList *iter;
    GError *error = NULL;

    g_return_if_fail (callback != NULL);

    DEBUG ("called for %s", priv->unique_name);
    protocol = _mcd_manager_dup_protocol (priv->manager, priv->protocol_name);

    if (protocol == NULL)
    {
        g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
            "CM '%s' doesn't implement protocol '%s'", priv->manager_name,
            priv->protocol_name);
        goto out;
    }

    params = tp_protocol_dup_params (protocol);

    for (iter = params; iter != NULL; iter = iter->next)
    {
        TpConnectionManagerParam *param = iter->data;
        const gchar *param_name = tp_connection_manager_param_get_name (param);

        if (!tp_connection_manager_param_is_required ((param)))
            continue;

        if (!mcd_account_get_parameter (account, param_name, NULL, NULL))
        {
            g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                "missing required parameter '%s'", param_name);
            goto out;
        }
    }

out:
    if (error != NULL)
    {
        DEBUG ("%s", error->message);
    }

    callback (account, error, user_data);
    g_clear_error (&error);
    g_list_free_full (params,
                      (GDestroyNotify) tp_connection_manager_param_free);
    g_clear_object (&protocol);
}

static void
set_parameters_maybe_autoconnect_cb (McdAccount *account,
                                     const GError *invalid_reason,
                                     gpointer user_data G_GNUC_UNUSED)
{
    /* Strictly speaking this doesn't need to be called unless invalid_reason
     * is NULL, but calling it in all cases gives us clearer debug output */
    _mcd_account_maybe_autoconnect (account);
}

static void
apply_parameter_updates (McdAccount *account,
                         GHashTable *params,
                         const gchar **unset,
                         GHashTable *dbus_properties)
{
    McdAccountPrivate *priv = account->priv;
    GHashTableIter iter;
    gpointer name, value;
    const gchar **unset_iter;

    g_hash_table_iter_init (&iter, params);
    while (g_hash_table_iter_next (&iter, &name, &value))
    {
        _mcd_account_set_parameter (account, name, value);
    }

    for (unset_iter = unset;
         unset_iter != NULL && *unset_iter != NULL;
         unset_iter++)
    {
        _mcd_account_set_parameter (account, *unset_iter, NULL);
    }

    if (mcd_account_get_connection_status (account) ==
        TP_CONNECTION_STATUS_CONNECTED)
    {
        g_hash_table_iter_init (&iter, dbus_properties);
        while (g_hash_table_iter_next (&iter, &name, &value))
        {
            DEBUG ("updating parameter %s", (const gchar *) name);
            _mcd_connection_update_property (priv->connection, name, value);
        }
    }

    mcd_account_check_validity (account,
                                set_parameters_maybe_autoconnect_cb, NULL);
}

static void
set_parameter_changed (GHashTable *dbus_properties,
                       GPtrArray *not_yet,
                       const TpConnectionManagerParam *param,
                       const GValue *new_value)
{
    const gchar *name = tp_connection_manager_param_get_name (param);

    DEBUG ("Parameter %s changed", name);

    /* can the param be updated on the fly? If yes, prepare to do so; and if
     * not, prepare to reset the connection */
    if (tp_connection_manager_param_is_dbus_property (param))
    {
        g_hash_table_insert (dbus_properties, g_strdup (name),
            tp_g_value_slice_dup (new_value));
    }
    else
    {
        g_ptr_array_add (not_yet, g_strdup (name));
    }
}

static gboolean
check_one_parameter_update (McdAccount *account,
                            TpProtocol *protocol,
                            GHashTable *dbus_properties,
                            GPtrArray *not_yet,
                            const gchar *name,
                            const GValue *new_value,
                            GError **error)
{
    const TpConnectionManagerParam *param =
        tp_protocol_get_param (protocol, name);
    GType type;

    if (param == NULL)
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "Protocol '%s' does not have parameter '%s'",
                     tp_protocol_get_name (protocol), name);
        return FALSE;
    }

    type = mc_param_type (param);

    if (G_VALUE_TYPE (new_value) != type)
    {
        /* FIXME: use D-Bus type names, not GType names. */
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "parameter '%s' must be of type %s, not %s",
                     tp_connection_manager_param_get_name (param),
                     g_type_name (type), G_VALUE_TYPE_NAME (new_value));
        return FALSE;
    }

    if (mcd_account_get_connection_status (account) ==
        TP_CONNECTION_STATUS_CONNECTED)
    {
        GValue current_value = G_VALUE_INIT;

        /* Check if the parameter's current value (or its default, if it has
         * one and it's not set to anything) matches the new value.
         */
        if (mcd_account_get_parameter (account, tp_connection_manager_param_get_name (param),
                &current_value, NULL) ||
            tp_connection_manager_param_get_default (param, &current_value))
        {
            if (!value_is_same (&current_value, new_value))
                set_parameter_changed (dbus_properties, not_yet, param,
                                       new_value);

            g_value_unset (&current_value);
        }
        else
        {
            /* The parameter wasn't previously set, and has no default value;
             * this update must be a change.
             */
            set_parameter_changed (dbus_properties, not_yet, param, new_value);
        }
    }

    return TRUE;
}

static gboolean
check_one_parameter_unset (McdAccount *account,
                           TpProtocol *protocol,
                           GHashTable *dbus_properties,
                           GPtrArray *not_yet,
                           const gchar *name,
                           GError **error)
{
    const TpConnectionManagerParam *param =
        tp_protocol_get_param (protocol, name);

    /* The spec decrees that “If the given parameters […] do not exist at all,
     * the account manager MUST accept this without error.”. Thus this function
     * is a no-op if @name doesn't actually exist.
     */
    if (param != NULL &&
        mcd_account_get_connection_status (account) ==
            TP_CONNECTION_STATUS_CONNECTED)
    {
        GValue current_value = G_VALUE_INIT;

        if (mcd_account_get_parameter (account, tp_connection_manager_param_get_name (param),
                                       &current_value, NULL))
        {
            /* There's an existing value; let's see if it's the same as the
             * default, if any.
             */
            GValue default_value = G_VALUE_INIT;

            if (tp_connection_manager_param_get_default (param, &default_value))
            {
                if (!value_is_same (&current_value, &default_value))
                    set_parameter_changed (dbus_properties, not_yet, param,
                                           &default_value);

                g_value_unset (&default_value);
            }
            else
            {
                /* It has no default; we're gonna have to reconnect to make
                 * this take effect.
                 */
                g_ptr_array_add (not_yet, g_strdup (tp_connection_manager_param_get_name (param)));
            }

            g_value_unset (&current_value);
        }
    }

    return TRUE;
}

static gboolean
check_parameters (McdAccount *account,
                  TpProtocol *protocol,
                  GHashTable *params,
                  const gchar **unset,
                  GHashTable *dbus_properties,
                  GPtrArray *not_yet,
                  GError **error)
{
    GHashTableIter iter;
    gpointer key, value;
    const gchar **unset_iter;

    g_hash_table_iter_init (&iter, params);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        if (!check_one_parameter_update (account, protocol, dbus_properties,
                                         not_yet, key, value, error))
            return FALSE;
    }

    for (unset_iter = unset;
         unset_iter != NULL && *unset_iter != NULL;
         unset_iter++)
    {
        if (!check_one_parameter_unset (account, protocol, dbus_properties,
                                        not_yet, *unset_iter, error))
            return FALSE;
    }

    return TRUE;
}

/*
 * _mcd_account_set_parameters:
 * @account: the #McdAccount.
 * @name: the parameter name.
 * @params: names and values of parameters to set
 * @unset: names of parameters to unset
 * @callback: function to be called when finished
 * @user_data: data to be passed to @callback
 *
 * Alter the account parameters.
 *
 */
void
_mcd_account_set_parameters (McdAccount *account, GHashTable *params,
                             const gchar **unset,
                             McdAccountSetParametersCb callback,
                             gpointer user_data)
{
    McdAccountPrivate *priv = account->priv;
    GHashTable *dbus_properties = NULL;
    GPtrArray *not_yet = NULL;
    GError *error = NULL;
    TpProtocol *protocol = NULL;

    DEBUG ("called");
    if (G_UNLIKELY (!priv->manager && !load_manager (account)))
    {
        /* FIXME: this branch is never reached, even if the specified CM
         * doesn't actually exist: load_manager essentially always succeeds,
         * but of course the TpCM hasn't prepared (or failed, as it will if we
         * would like to hit this path) yet. So in practice we hit the next
         * block for nonexistant CMs too.
         */
        g_set_error (&error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
                     "Manager '%s' not found", priv->manager_name);
        goto out;
    }

    protocol = _mcd_manager_dup_protocol (priv->manager, priv->protocol_name);

    if (G_UNLIKELY (protocol == NULL))
    {
        g_set_error (&error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
                     "Protocol '%s' not found on CM '%s'", priv->protocol_name,
                     priv->manager_name);
        goto out;
    }

    /* An a{sv} of DBus_Property parameters we should set on the connection. */
    dbus_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, (GDestroyNotify) tp_g_value_slice_free);
    not_yet = g_ptr_array_new_with_free_func (g_free);

    if (check_parameters (account, protocol, params, unset, dbus_properties,
                           not_yet, &error))
        apply_parameter_updates (account, params, unset, dbus_properties);

out:
    if (callback != NULL)
    {
        if (error == NULL)
            callback (account, not_yet, NULL, user_data);
        else
            callback (account, NULL, error, user_data);
    }

    g_clear_error (&error);
    tp_clear_pointer (&dbus_properties, g_hash_table_unref);
    tp_clear_pointer (&not_yet, g_ptr_array_unref);
    g_clear_object (&protocol);
}

static void
account_update_parameters_cb (McdAccount *account, GPtrArray *not_yet,
                              const GError *error, gpointer user_data)
{
    McdAccountPrivate *priv = account->priv;
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;
    const gchar *account_name = mcd_account_get_unique_name (account);
    GHashTable *params;
    GValue value = G_VALUE_INIT;

    if (error != NULL)
    {
        dbus_g_method_return_error (context, (GError *) error);
        return;
    }

    /* Emit the PropertiesChanged signal */
    params = _mcd_account_dup_parameters (account);
    g_return_if_fail (params != NULL);

    g_value_init (&value, TP_HASH_TYPE_STRING_VARIANT_MAP);
    g_value_take_boxed (&value, params);
    mcd_account_changed_property (account, "Parameters", &value);
    g_value_unset (&value);

    /* Commit the changes to disk */
    mcd_storage_commit (priv->storage, account_name);

    /* And finally, return from UpdateParameters() */
    g_ptr_array_add (not_yet, NULL);
    tp_svc_account_return_from_update_parameters (context,
        (const gchar **) not_yet->pdata);
}

static void
account_update_parameters (TpSvcAccount *self, GHashTable *set,
			   const gchar **unset, DBusGMethodInvocation *context)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPrivate *priv = account->priv;

    DEBUG ("called for %s", priv->unique_name);

    _mcd_account_set_parameters (account, set, unset,
                                 account_update_parameters_cb, context);
}

void
_mcd_account_reconnect (McdAccount *self,
    gboolean user_initiated)
{
    /* FIXME: this isn't quite right. If we've just called RequestConnection
     * (possibly with out of date parameters) but we haven't got a Connection
     * back from the CM yet, the old parameters will still be used, I think
     * (I can't quite make out what actually happens). */
    if (self->priv->connection)
        mcd_connection_close (self->priv->connection, NULL);

    _mcd_account_connection_begin (self, user_initiated);
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
        !mcd_account_is_valid (self) ||
        priv->req_presence_type == TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
    {
        DEBUG ("doing nothing (enabled=%c, valid=%c and "
               "combined presence=%i)",
               self->priv->enabled ? 'T' : 'F',
               mcd_account_is_valid (self) ? 'T' : 'F',
               self->priv->req_presence_type);
        tp_svc_account_return_from_reconnect (context);
        return;
    }

    /* Reconnect() counts as user-initiated */
    _mcd_account_reconnect (self, TRUE);

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
    /* these are invariants - the storage is set at construct-time
     * and the object path is set in mcd_account_setup, both of which are
     * run before this callback can possibly be invoked */
    g_assert (self->priv->storage != NULL);
    g_assert (self->priv->object_path != NULL);

    dbus_daemon = self->priv->dbus_daemon;
    g_return_if_fail (dbus_daemon != NULL);

    dbus_connection = tp_proxy_get_dbus_connection (TP_PROXY (dbus_daemon));

    if (G_LIKELY (dbus_connection)) {
        dbus_g_connection_register_g_object (dbus_connection,
                                             self->priv->object_path,
                                             (GObject *) self);

        self->priv->registered = TRUE;
    }
}

/*
 * @account: (allow-none):
 * @dir_out: (out): e.g. ~/.local/share/telepathy/mission-control
 * @basename_out: (out): e.g. gabble-jabber-fred_40example_2ecom.avatar
 * @file_out: (out): @dir_out + "/" + @basename_out
 */
static void
get_avatar_paths (McdAccount *account,
                  gchar **dir_out,
                  gchar **basename_out,
                  gchar **file_out)
{
    gchar *dir = NULL;

    dir = g_build_filename (g_get_user_data_dir (),
                            "telepathy", "mission-control", NULL);

    if (account == NULL)
    {
        if (file_out != NULL)
            *file_out = NULL;

        if (basename_out != NULL)
            *basename_out = NULL;
    }
    else if (basename_out != NULL || file_out != NULL)
    {
        gchar *basename = NULL;

        basename = g_strdup_printf ("%s.avatar", account->priv->unique_name);
        g_strdelimit (basename, "/", '-');

        if (file_out != NULL)
            *file_out = g_build_filename (dir, basename, NULL);

        if (basename_out != NULL)
            *basename_out = basename;
        else
            g_free (basename);
    }

    if (dir_out != NULL)
        *dir_out = dir;
    else
        g_free (dir);
}

static gboolean
save_avatar (McdAccount *self,
             gpointer data,
             gssize len,
             GError **error)
{
    gchar *dir = NULL;
    gchar *file = NULL;
    gboolean ret = FALSE;

    get_avatar_paths (self, &dir, NULL, &file);

    if (mcd_ensure_directory (dir, error) &&
        g_file_set_contents (file, data, len, error))
    {
        DEBUG ("Saved avatar to %s", file);
        ret = TRUE;
    }
    else if (len == 0)
    {
        GArray *avatar = NULL;

        /* It failed, but maybe that's OK, since we didn't really want
         * an avatar anyway. */
        _mcd_account_get_avatar (self, &avatar, NULL);

        if (avatar == NULL)
        {
            /* Creating the empty file failed, but it's fine, since what's
             * on disk correctly indicates that we have no avatar. */
            DEBUG ("Ignoring failure to write empty avatar");

            if (error != NULL)
                g_clear_error (error);
        }
        else
        {
            /* Continue to raise the error: we failed to write a 0-byte
             * file into the highest-priority avatar directory, and we do
             * need it, since there is a non-empty avatar in either that
             * directory or a lower-priority directory */
            g_array_free (avatar, TRUE);
        }
    }

    g_free (dir);
    g_free (file);
    return ret;
}

static gchar *_mcd_account_get_old_avatar_filename (McdAccount *account,
                                                    gchar **old_dir);

static void
mcd_account_migrate_avatar (McdAccount *account)
{
    GError *error = NULL;
    gchar *old_file;
    gchar *old_dir = NULL;
    gchar *new_dir = NULL;
    gchar *basename = NULL;
    gchar *new_file = NULL;
    gchar *contents = NULL;
    guint i;

    /* Try to migrate the avatar to a better location */
    old_file = _mcd_account_get_old_avatar_filename (account, &old_dir);

    if (!g_file_test (old_file, G_FILE_TEST_EXISTS))
    {
        /* nothing to do */
        goto finally;
    }

    DEBUG ("Migrating avatar from %s", old_file);

    get_avatar_paths (account, &new_dir, &basename, &new_file);

    if (g_file_test (new_file, G_FILE_TEST_IS_REGULAR))
    {
        DEBUG ("... already migrated to %s", new_file);

        if (g_unlink (old_file) != 0)
        {
            DEBUG ("Failed to unlink %s: %s", old_file,
                   g_strerror (errno));
        }

        goto finally;
    }

    if (!mcd_ensure_directory (new_dir, &error))
    {
        DEBUG ("%s", error->message);
        goto finally;
    }

    if (g_rename (old_file, new_file) == 0)
    {
        DEBUG ("Renamed %s to %s", old_file, new_file);
    }
    else
    {
        gsize len;

        DEBUG ("Unable to rename %s to %s, will try copy+delete: %s",
               old_file, new_file, g_strerror (errno));

        if (!g_file_get_contents (old_file, &contents, &len, &error))
        {
            DEBUG ("Unable to load old avatar %s: %s", old_file,
                   error->message);
            goto finally;
        }

        if (g_file_set_contents (new_file, contents, len, &error))
        {
            DEBUG ("Copied old avatar from %s to %s", old_file, new_file);
        }
        else
        {
            DEBUG ("Unable to save new avatar %s: %s", new_file,
                   error->message);
            goto finally;
        }

        if (g_unlink (old_file) != 0)
        {
            DEBUG ("Failed to unlink %s: %s", old_file, g_strerror (errno));
            goto finally;
        }
    }

    /* old_dir is typically ~/.mission-control/accounts/gabble/jabber/badger0.
     * We want to delete badger0, jabber, gabble, accounts if they are empty.
     * If they are not, we'll just get ENOTEMPTY and stop. */
    for (i = 0; i < 4; i++)
    {
        gchar *tmp;

        if (g_rmdir (old_dir) != 0)
        {
            DEBUG ("Failed to rmdir %s: %s", old_dir, g_strerror (errno));
            goto finally;
        }

        tmp = g_path_get_dirname (old_dir);
        g_free (old_dir);
        old_dir = tmp;
    }

finally:
    g_clear_error (&error);
    g_free (basename);
    g_free (new_file);
    g_free (new_dir);
    g_free (contents);
    g_free (old_file);
    g_free (old_dir);
}

static gboolean
mcd_account_setup (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;
    McdStorage *storage = priv->storage;
    const gchar *name = mcd_account_get_unique_name (account);
    GValue value = G_VALUE_INIT;

    priv->manager_name =
      mcd_storage_dup_string (storage, name, MC_ACCOUNTS_KEY_MANAGER);

    if (priv->manager_name == NULL)
    {
        g_warning ("Account '%s' has no manager", name);
        goto broken_account;
    }

    priv->protocol_name =
      mcd_storage_dup_string (storage, name, MC_ACCOUNTS_KEY_PROTOCOL);

    if (priv->protocol_name == NULL)
    {
        g_warning ("Account has no protocol");
        goto broken_account;
    }

    priv->object_path = g_strconcat (TP_ACCOUNT_OBJECT_PATH_BASE, name, NULL);

    if (!priv->always_on)
    {
        priv->enabled =
          mcd_storage_get_boolean (storage, name, MC_ACCOUNTS_KEY_ENABLED);

        priv->connect_automatically =
          mcd_storage_get_boolean (storage, name,
                                   MC_ACCOUNTS_KEY_CONNECT_AUTOMATICALLY);
    }

    priv->has_been_online =
      mcd_storage_get_boolean (storage, name, MC_ACCOUNTS_KEY_HAS_BEEN_ONLINE);
    priv->hidden =
      mcd_storage_get_boolean (storage, name, MC_ACCOUNTS_KEY_HIDDEN);

    /* special case flag (for ring accounts, so far) */
    priv->always_dispatch =
      mcd_storage_get_boolean (storage, name, MC_ACCOUNTS_KEY_ALWAYS_DISPATCH);

    g_value_init (&value, TP_STRUCT_TYPE_SIMPLE_PRESENCE);

    g_free (priv->auto_presence_status);
    g_free (priv->auto_presence_message);

    if (mcd_storage_get_attribute (storage, name,
                                   MC_ACCOUNTS_KEY_AUTOMATIC_PRESENCE, &value,
                                   NULL))
    {
        GValueArray *va = g_value_get_boxed (&value);

        priv->auto_presence_type = g_value_get_uint (va->values + 0);
        priv->auto_presence_status = g_value_dup_string (va->values + 1);
        priv->auto_presence_message = g_value_dup_string (va->values + 2);

        if (priv->auto_presence_status == NULL)
            priv->auto_presence_status = g_strdup ("");
        if (priv->auto_presence_message == NULL)
            priv->auto_presence_message = g_strdup ("");
    }
    else
    {
        /* try the old versions */
        priv->auto_presence_type =
          mcd_storage_get_integer (storage, name,
                                   MC_ACCOUNTS_KEY_AUTO_PRESENCE_TYPE);
        priv->auto_presence_status =
          mcd_storage_dup_string (storage, name,
                                  MC_ACCOUNTS_KEY_AUTO_PRESENCE_STATUS);
        priv->auto_presence_message =
          mcd_storage_dup_string (storage, name,
                                  MC_ACCOUNTS_KEY_AUTO_PRESENCE_MESSAGE);

        if (priv->auto_presence_status == NULL)
            priv->auto_presence_status = g_strdup ("");
        if (priv->auto_presence_message == NULL)
            priv->auto_presence_message = g_strdup ("");

        /* migrate to a more sensible storage format */
        g_value_take_boxed (&value, tp_value_array_build (3,
                G_TYPE_UINT, (guint) priv->auto_presence_type,
                G_TYPE_STRING, priv->auto_presence_status,
                G_TYPE_STRING, priv->auto_presence_message,
                G_TYPE_INVALID));

        if (mcd_storage_set_attribute (storage, name,
                                       MC_ACCOUNTS_KEY_AUTOMATIC_PRESENCE,
                                       &value))
        {
            mcd_storage_set_attribute (storage, name,
                                       MC_ACCOUNTS_KEY_AUTO_PRESENCE_TYPE,
                                       NULL);
            mcd_storage_set_attribute (storage, name,
                                       MC_ACCOUNTS_KEY_AUTO_PRESENCE_STATUS,
                                       NULL);
            mcd_storage_set_attribute (storage, name,
                                       MC_ACCOUNTS_KEY_AUTO_PRESENCE_MESSAGE,
                                       NULL);
            mcd_storage_commit (storage, name);
        }
    }

    /* If invalid or something, force it to AVAILABLE - we want the auto
     * presence type to be an online status */
    if (!_presence_type_is_online (priv->auto_presence_type))
    {
        priv->auto_presence_type = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;
        g_free (priv->auto_presence_status);
        priv->auto_presence_status = g_strdup ("available");
    }

    g_value_unset (&value);
    g_value_init (&value, TP_ARRAY_TYPE_OBJECT_PATH_LIST);

    if (priv->supersedes != NULL)
        g_ptr_array_unref (priv->supersedes);

    if (mcd_storage_get_attribute (storage, name,
                                   MC_ACCOUNTS_KEY_SUPERSEDES, &value, NULL))
    {
        priv->supersedes = g_value_dup_boxed (&value);
    }
    else
    {
        priv->supersedes = g_ptr_array_new ();
    }

    g_value_unset (&value);

    /* check the manager */
    if (!priv->manager && !load_manager (account))
    {
	g_warning ("Could not find manager `%s'", priv->manager_name);
        mcd_account_loaded (account);
    }

    /* even though the manager is absent or unusable, we still register *
     * the accounts dbus name as it is otherwise acceptably configured  */

    _mcd_account_load (account, register_dbus_service, NULL);
    return TRUE;

broken_account:
    /* normally, various callbacks would release locks when the manager      *
     * became ready: however, this cannot happen for an incomplete account   *
     * as it never gets a manager: We therefore invoke the account callbacks *
     * right now so the account manager doesn't hang around forever waiting  *
     * for an event that cannot happen (at least until the account is fixed) */
    mcd_account_loaded (account);
    return FALSE;
}

static void
set_property (GObject *obj, guint prop_id,
	      const GValue *val, GParamSpec *pspec)
{
    McdAccount *account = MCD_ACCOUNT (obj);
    McdAccountPrivate *priv = account->priv;

    switch (prop_id)
    {
    case PROP_STORAGE:
        g_assert (priv->storage == NULL);
        priv->storage = g_value_dup_object (val);
	break;

      case PROP_DBUS_DAEMON:
        g_assert (priv->dbus_daemon == NULL);
        priv->dbus_daemon = g_value_dup_object (val);
        break;

      case PROP_CONNECTIVITY_MONITOR:
        g_assert (priv->connectivity == NULL);
        priv->connectivity = g_value_dup_object (val);
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
    case PROP_HIDDEN:
        priv->hidden = g_value_get_boolean (val);
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

    case PROP_CONNECTIVITY_MONITOR:
        g_value_set_object (val, priv->connectivity);
        break;

    case PROP_NAME:
	g_value_set_string (val, priv->unique_name);
	break;
    case PROP_HIDDEN:
        g_value_set_boolean (val, priv->hidden);
        break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_account_finalize (GObject *object)
{
    McdAccount *account = MCD_ACCOUNT (object);
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);

    DEBUG ("%p (%s)", object, priv->unique_name);

    if (priv->changed_properties)
	g_hash_table_unref (priv->changed_properties);
    if (priv->properties_source != 0)
	g_source_remove (priv->properties_source);

    tp_clear_pointer (&priv->curr_presence_status, g_free);
    tp_clear_pointer (&priv->curr_presence_message, g_free);

    tp_clear_pointer (&priv->req_presence_status, g_free);
    tp_clear_pointer (&priv->req_presence_message, g_free);

    tp_clear_pointer (&priv->auto_presence_status, g_free);
    tp_clear_pointer (&priv->auto_presence_message, g_free);

    tp_clear_pointer (&priv->manager_name, g_free);
    tp_clear_pointer (&priv->protocol_name, g_free);
    tp_clear_pointer (&priv->unique_name, g_free);
    tp_clear_pointer (&priv->object_path, g_free);

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

        error = g_error_new (TP_ERROR, TP_ERROR_DISCONNECTED,
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

    tp_clear_object (&priv->manager);
    tp_clear_object (&priv->storage_plugin);
    tp_clear_object (&priv->storage);
    tp_clear_object (&priv->dbus_daemon);
    tp_clear_object (&priv->self_contact);
    tp_clear_object (&priv->connectivity);

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

    if (G_UNLIKELY (!priv->storage || !priv->unique_name))
    {
        g_object_unref (account);
        return NULL;
    }

    return (GObject *) account;
}

static void
monitor_state_changed_cb (
    McdConnectivityMonitor *monitor,
    gboolean connected,
    McdInhibit *inhibit,
    gpointer user_data)
{
  McdAccount *self = MCD_ACCOUNT (user_data);

  if (connected)
    {
      if (mcd_account_would_like_to_connect (self))
        {
          DEBUG ("account %s would like to connect",
              self->priv->unique_name);
          _mcd_account_connect_with_auto_presence (self, FALSE);
        }
    }
  else
    {
      if (_mcd_account_needs_dispatch (self))
        {
          /* special treatment for cellular accounts */
          DEBUG ("account %s is always dispatched and does not need a "
              "transport", self->priv->unique_name);
        }
      else
        {
          McdConnection *connection;

          DEBUG ("account %s must disconnect", self->priv->unique_name);
          connection = mcd_account_get_connection (self);

          if (connection != NULL)
            mcd_connection_close (connection, inhibit);
        }
    }

  if (!self->priv->waiting_for_connectivity)
    return;

  /* If we've gone online, allow the account to actually try to connect;
   * if we've fallen offline, say as much. (I don't actually think this
   * code will be reached if !connected, but.)
   */
  DEBUG ("telling %s to %s", self->priv->unique_name,
      connected ? "proceed" : "give up");
  mcd_account_connection_proceed_with_reason (self, connected,
      connected ? TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED
                : TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
  self->priv->waiting_for_connectivity = FALSE;
}

static void
_mcd_account_constructed (GObject *object)
{
    GObjectClass *object_class = (GObjectClass *)mcd_account_parent_class;
    McdAccount *account = MCD_ACCOUNT (object);

    if (object_class->constructed)
        object_class->constructed (object);

    DEBUG ("%p (%s)", object, account->priv->unique_name);

    mcd_account_migrate_avatar (account);
    mcd_account_setup (account);

    tp_g_signal_connect_object (account->priv->connectivity, "state-change",
        (GCallback) monitor_state_changed_cb, account, 0);
}

static void
mcd_account_add_signals (TpProxy *self,
    guint quark,
    DBusGProxy *proxy,
    gpointer data)
{
  mc_cli_Connection_Manager_Interface_Account_Storage_add_signals (self,
      quark, proxy, data);
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

    klass->check_request = _mcd_account_check_request_real;

    g_object_class_install_property
        (object_class, PROP_DBUS_DAEMON,
         g_param_spec_object ("dbus-daemon", "DBus daemon", "DBus daemon",
                              TP_TYPE_DBUS_DAEMON,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_CONNECTIVITY_MONITOR,
         g_param_spec_object ("connectivity monitor",
                              "Connectivity monitor",
                              "Connectivity monitor",
                              MCD_TYPE_CONNECTIVITY_MONITOR,
                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_STORAGE,
         g_param_spec_object ("storage", "storage",
                               "storage", MCD_TYPE_STORAGE,
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

    g_object_class_install_property
        (object_class, PROP_HIDDEN,
         g_param_spec_boolean ("hidden", "Hidden?", "Is this account hidden?",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    /* Signals */
    _mcd_account_signals[VALIDITY_CHANGED] =
	g_signal_new ("validity-changed",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
		      G_TYPE_NONE, 1,
		      G_TYPE_BOOLEAN);
    _mcd_account_signals[CONNECTION_PATH_CHANGED] =
        g_signal_new ("connection-path-changed",
                      G_OBJECT_CLASS_TYPE (klass),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                      0,
                      NULL, NULL, g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    account_ready_quark = g_quark_from_static_string ("mcd_account_load");

    tp_proxy_or_subclass_hook_on_interface_add (TP_TYPE_CONNECTION_MANAGER,
        mcd_account_add_signals);
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

    priv->curr_presence_type = TP_CONNECTION_PRESENCE_TYPE_OFFLINE;
    priv->curr_presence_status = g_strdup ("offline");
    priv->curr_presence_message = g_strdup ("");

    priv->always_on = FALSE;
    priv->always_dispatch = FALSE;
    priv->enabled = FALSE;
    priv->connect_automatically = FALSE;

    priv->changing_presence = FALSE;

    priv->auto_presence_type = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;
    priv->auto_presence_status = g_strdup ("available");
    priv->auto_presence_message = g_strdup ("");

    /* initializes the interfaces */
    mcd_dbus_init_interfaces_instances (account);

    priv->conn_status = TP_CONNECTION_STATUS_DISCONNECTED;
    priv->conn_reason = TP_CONNECTION_STATUS_REASON_REQUESTED;
    priv->conn_dbus_error = g_strdup ("");
    priv->conn_error_details = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, (GDestroyNotify) tp_g_value_slice_free);

    priv->changed_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
        NULL, (GDestroyNotify) tp_g_value_slice_free);

    g_set_error (&priv->invalid_reason, TP_ERROR, TP_ERROR_NOT_YET,
        "This account is not yet fully loaded");
}

McdAccount *
mcd_account_new (McdAccountManager *account_manager,
    const gchar *name,
    McdConnectivityMonitor *connectivity)
{
    gpointer *obj;
    McdStorage *storage = mcd_account_manager_get_storage (account_manager);
    TpDBusDaemon *dbus = mcd_account_manager_get_dbus_daemon (account_manager);

    obj = g_object_new (MCD_TYPE_ACCOUNT,
                        "storage", storage,
                        "dbus-daemon", dbus,
                        "connectivity-monitor", connectivity,
			"name", name,
			NULL);
    return MCD_ACCOUNT (obj);
}

McdStorage *
_mcd_account_get_storage (McdAccount *account)
{
    return account->priv->storage;
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
    return priv->invalid_reason == NULL;
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

gboolean
_mcd_account_is_hidden (McdAccount *account)
{
    g_return_val_if_fail (MCD_IS_ACCOUNT (account), FALSE);

    return account->priv->hidden;
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

/**
 * _mcd_account_dup_parameters:
 * @account: the #McdAccount.
 *
 * Get the parameters set for this account. The resulting #GHashTable will be
 * newly allocated and must be g_hash_table_unref()'d after use.
 *
 * Returns: @account's current parameters, or %NULL if they could not be
 *          retrieved.
 */
GHashTable *
_mcd_account_dup_parameters (McdAccount *account)
{
    McdAccountPrivate *priv;
    TpProtocol *protocol;
    GList *protocol_params;
    GList *iter;
    GHashTable *params;

    g_return_val_if_fail (MCD_IS_ACCOUNT (account), NULL);

    priv = account->priv;

    DEBUG ("called");

    /* FIXME: this is ridiculous. MC stores the parameters for the account, so
     * it should be able to expose them on D-Bus even if the CM is uninstalled.
     * It shouldn't need to iterate across the parameters supported by the CM.
     * But it does, because MC doesn't store the types of parameters. So it
     * needs the CM (or .manager file) to be around to tell it whether "true"
     * is a string or a boolean…
     */
    if (!priv->manager && !load_manager (account))
    {
        DEBUG ("unable to load manager for account %s", priv->unique_name);
        return NULL;
    }

    protocol = _mcd_manager_dup_protocol (priv->manager,
                                          priv->protocol_name);

    if (G_UNLIKELY (protocol == NULL))
    {
        DEBUG ("unable to get protocol for %s account %s", priv->protocol_name,
               priv->unique_name);
        return NULL;
    }

    params = g_hash_table_new_full (g_str_hash, g_str_equal,
                                    g_free,
                                    (GDestroyNotify) tp_g_value_slice_free);

    protocol_params = tp_protocol_dup_params (protocol);

    for (iter = protocol_params; iter != NULL; iter = iter->next)
    {
        TpConnectionManagerParam *param = iter->data;
        const gchar *name = tp_connection_manager_param_get_name (param);
        GValue v = G_VALUE_INIT;

        if (mcd_account_get_parameter (account, name, &v, NULL))
        {
            g_hash_table_insert (params, g_strdup (name),
                                 tp_g_value_slice_dup (&v));
            g_value_unset (&v);
        }
    }

    g_list_free_full (protocol_params,
                      (GDestroyNotify) tp_connection_manager_param_free);
    g_object_unref (protocol);
    return params;
}

/**
 * mcd_account_request_presence:
 * @account: the #McdAccount.
 * @presence: a #TpConnectionPresenceType.
 * @status: presence status.
 * @message: presence status message.
 *
 * Request a presence status on the account, initiated by some other part of
 * MC (i.e. not by user request).
 */
void
mcd_account_request_presence (McdAccount *account,
			      TpConnectionPresenceType presence,
			      const gchar *status, const gchar *message)
{
    mcd_account_request_presence_int (account, presence, status, message,
                                      FALSE);
}

static void
mcd_account_update_self_presence (McdAccount *account,
                                  guint presence,
                                  const gchar *status,
                                  const gchar *message,
                                  TpContact *self_contact)
{
    McdAccountPrivate *priv = account->priv;
    gboolean changed = FALSE;
    GValue value = G_VALUE_INIT;

    if (self_contact != account->priv->self_contact)
        return;

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

    if (_mcd_connection_presence_info_is_ready (priv->connection))
    {
        _mcd_account_set_changing_presence (account, FALSE);
    }

    if (!changed) return;

    g_value_init (&value, TP_STRUCT_TYPE_SIMPLE_PRESENCE);
    g_value_take_boxed (&value,
                        tp_value_array_build (3,
                                              G_TYPE_UINT, presence,
                                              G_TYPE_STRING, status,
                                              G_TYPE_STRING, message,
                                              G_TYPE_INVALID));
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

/*
 * mcd_account_would_like_to_connect:
 * @account: an account
 *
 * Returns: %TRUE if @account is not currently in the process of trying to
 *          connect, but would like to be, in a perfect world.
 */
gboolean
mcd_account_would_like_to_connect (McdAccount *account)
{
    McdAccountPrivate *priv;

    g_return_val_if_fail (MCD_IS_ACCOUNT (account), FALSE);
    priv = account->priv;

    if (!priv->enabled)
    {
        DEBUG ("%s not Enabled", priv->unique_name);
        return FALSE;
    }

    if (!mcd_account_is_valid (account))
    {
        DEBUG ("%s not Valid", priv->unique_name);
        return FALSE;
    }

    if (priv->conn_status != TP_CONNECTION_STATUS_DISCONNECTED)
    {
        DEBUG ("%s already connecting/connected", priv->unique_name);
        return FALSE;
    }

    if (!priv->connect_automatically &&
        !_presence_type_is_online (priv->req_presence_type))
    {
        DEBUG ("%s does not ConnectAutomatically, and its RequestedPresence "
            "(%u, '%s', '%s') doesn't indicate the user wants to be online",
            priv->unique_name, priv->req_presence_type,
            priv->req_presence_status, priv->req_presence_message);
        return FALSE;
    }

    return TRUE;
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

/**
 * mcd_account_get_cm:
 * @account: an account
 *
 * Fetches the connection manager through which @account connects. If @account
 * is not ready, or is invalid (perhaps because the connection manager is
 * missing), this may be %NULL.
 *
 * Returns: the connection manager through which @account connects, or %NULL.
 */
TpConnectionManager *
mcd_account_get_cm (McdAccount *account)
{
    g_return_val_if_fail (account != NULL, NULL);
    g_return_val_if_fail (MCD_IS_ACCOUNT (account), NULL);

    return mcd_manager_get_tp_proxy (account->priv->manager);
}

void
_mcd_account_set_normalized_name (McdAccount *account, const gchar *name)
{
    McdAccountPrivate *priv = account->priv;
    GValue value = G_VALUE_INIT;
    const gchar *account_name = mcd_account_get_unique_name (account);

    DEBUG ("called (%s)", name);

    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, name);

    mcd_storage_set_attribute (priv->storage, account_name,
                               MC_ACCOUNTS_KEY_NORMALIZED_NAME, &value);
    mcd_storage_commit (priv->storage, account_name);
    mcd_account_changed_property (account, MC_ACCOUNTS_KEY_NORMALIZED_NAME,
                                  &value);

    g_value_unset (&value);
}

void
_mcd_account_set_avatar_token (McdAccount *account, const gchar *token)
{
    McdAccountPrivate *priv = account->priv;
    const gchar *account_name = mcd_account_get_unique_name (account);

    DEBUG ("called (%s)", token);
    mcd_storage_set_string (priv->storage,
                            account_name,
                            MC_ACCOUNTS_KEY_AVATAR_TOKEN,
                            token);

    mcd_storage_commit (priv->storage, account_name);
}

gchar *
_mcd_account_get_avatar_token (McdAccount *account)
{
    McdAccountPrivate *priv = account->priv;
    const gchar *account_name = mcd_account_get_unique_name (account);

    return mcd_storage_dup_string (priv->storage,
                                   account_name,
                                   MC_ACCOUNTS_KEY_AVATAR_TOKEN);
}

gboolean
_mcd_account_set_avatar (McdAccount *account, const GArray *avatar,
			const gchar *mime_type, const gchar *token,
			GError **error)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    const gchar *account_name = mcd_account_get_unique_name (account);

    DEBUG ("called");

    if (G_LIKELY(avatar) && avatar->len > 0)
    {
        if (!save_avatar (account, avatar->data, avatar->len, error))
	{
	    g_warning ("%s: writing avatar failed", G_STRLOC);
	    return FALSE;
	}
    }
    else
    {
        /* We implement "deleting" an avatar by writing out a zero-length
         * file, so that it will override lower-priority directories. */
        if (!save_avatar (account, "", 0, error))
        {
            g_warning ("%s: writing empty avatar failed", G_STRLOC);
            return FALSE;
        }
    }

    if (mime_type != NULL)
        mcd_storage_set_string (priv->storage,
                                account_name,
                                MC_ACCOUNTS_KEY_AVATAR_MIME,
                                mime_type);

    if (token)
    {
        gchar *prev_token;

        prev_token = _mcd_account_get_avatar_token (account);

        mcd_storage_set_string (priv->storage,
                                account_name,
                                MC_ACCOUNTS_KEY_AVATAR_TOKEN,
                                token);

        if (!prev_token || strcmp (prev_token, token) != 0)
            tp_svc_account_interface_avatar_emit_avatar_changed (account);

        g_free (prev_token);
    }
    else
    {
        mcd_storage_set_attribute (priv->storage, account_name,
                                   MC_ACCOUNTS_KEY_AVATAR_TOKEN, NULL);

        mcd_account_send_avatar_to_connection (account, avatar, mime_type);
    }

    mcd_storage_commit (priv->storage, account_name);

    return TRUE;
}

static GArray *
load_avatar_or_warn (const gchar *filename)
{
    GError *error = NULL;
    gchar *data = NULL;
    gsize length;

    if (g_file_get_contents (filename, &data, &length, &error))
    {
        if (length > 0 && length < G_MAXUINT)
        {
            GArray *ret;

            ret = g_array_new (FALSE, FALSE, 1);
            g_array_append_vals (ret, data, (guint) length);
            return ret;
        }
        else
        {
            DEBUG ("avatar %s was empty or ridiculously large (%"
                   G_GSIZE_FORMAT " bytes)", filename, length);
            return NULL;
        }
    }
    else
    {
        DEBUG ("error reading %s: %s", filename, error->message);
        g_error_free (error);
        return NULL;
    }
}

void
_mcd_account_get_avatar (McdAccount *account, GArray **avatar,
                         gchar **mime_type)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    const gchar *account_name = mcd_account_get_unique_name (account);
    gchar *basename;
    gchar *filename;

    if (mime_type != NULL)
        *mime_type =  mcd_storage_dup_string (priv->storage, account_name,
                                              MC_ACCOUNTS_KEY_AVATAR_MIME);

    if (avatar == NULL)
        return;

    *avatar = NULL;

    get_avatar_paths (account, NULL, &basename, &filename);

    if (g_file_test (filename, G_FILE_TEST_EXISTS))
    {
        *avatar = load_avatar_or_warn (filename);
    }
    else
    {
        const gchar * const *iter;

        for (iter = g_get_system_data_dirs ();
             iter != NULL && *iter != NULL;
             iter++)
        {
            gchar *candidate = g_build_filename (*iter, "telepathy",
                                                 "mission-control",
                                                 basename, NULL);

            if (g_file_test (candidate, G_FILE_TEST_EXISTS))
            {
                *avatar = load_avatar_or_warn (candidate);
                g_free (candidate);
                break;
            }

            g_free (candidate);
        }
    }

    g_free (filename);
    g_free (basename);
}

GPtrArray *
_mcd_account_get_supersedes (McdAccount *self)
{
  return self->priv->supersedes;
}

static void
mcd_account_self_contact_notify_alias_cb (McdAccount *self,
    GParamSpec *unused_param_spec G_GNUC_UNUSED,
    TpContact *self_contact)
{
    GValue value = G_VALUE_INIT;

    if (self_contact != self->priv->self_contact)
        return;

    g_value_init (&value, G_TYPE_STRING);
    g_object_get_property (G_OBJECT (self_contact), "alias", &value);
    mcd_account_set_string_val (self, MC_ACCOUNTS_KEY_NICKNAME, &value,
                                MCD_DBUS_PROP_SET_FLAG_NONE, NULL);
    g_value_unset (&value);
}

static gchar *
mcd_account_get_alias (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    const gchar *account_name = mcd_account_get_unique_name (account);

    return mcd_storage_dup_string (priv->storage, account_name,
                                   MC_ACCOUNTS_KEY_NICKNAME);
}

static void
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
        error = g_error_new (TP_ERROR, TP_ERROR_DISCONNECTED,
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
                        TpConnection *tp_conn,
                        const gchar *dbus_error,
                        GHashTable *details,
                        McdAccount *account)
{
    _mcd_account_set_connection_status (account, status, reason, tp_conn,
                                        dbus_error, details);
}

/* clear the "register" flag, if necessary */
static void
clear_register (McdAccount *self)
{
    GHashTable *params = _mcd_account_dup_parameters (self);

    if (params == NULL)
    {
        DEBUG ("no params returned");
        return;
    }

    if (tp_asv_get_boolean (params, "register", NULL))
    {
        GValue value = G_VALUE_INIT;
        const gchar *account_name = mcd_account_get_unique_name (self);

        _mcd_account_set_parameter (self, "register", NULL);

        g_hash_table_remove (params, "register");

        g_value_init (&value, TP_HASH_TYPE_STRING_VARIANT_MAP);
        g_value_take_boxed (&value, params);
        mcd_account_changed_property (self, "Parameters", &value);
        g_value_unset (&value);

        mcd_storage_commit (self->priv->storage, account_name);
    }
    else
    {
      g_hash_table_unref (params);
    }
}

void
_mcd_account_set_connection_status (McdAccount *account,
                                    TpConnectionStatus status,
                                    TpConnectionStatusReason reason,
                                    TpConnection *tp_conn,
                                    const gchar *dbus_error,
                                    const GHashTable *details)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    gboolean changed = FALSE;

    DEBUG ("%s: %u because %u", priv->unique_name, status, reason);

    mcd_account_freeze_properties (account);

    if (status == TP_CONNECTION_STATUS_CONNECTED)
    {
        _mcd_account_set_has_been_online (account);
        clear_register (account);

        DEBUG ("clearing connection error details");
        g_free (priv->conn_dbus_error);
        priv->conn_dbus_error = g_strdup ("");
        g_hash_table_remove_all (priv->conn_error_details);

    }
    else if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
        /* we'll get this from the TpContact soon, but it makes sense
         * to bundle everything together into one signal */
        mcd_account_update_self_presence (account,
            TP_CONNECTION_PRESENCE_TYPE_OFFLINE,
            "offline",
            "",
            priv->self_contact);

        if (dbus_error == NULL)
            dbus_error = "";

        if (tp_strdiff (dbus_error, priv->conn_dbus_error))
        {
            DEBUG ("changing detailed D-Bus error from '%s' to '%s'",
                   priv->conn_dbus_error, dbus_error);
            g_free (priv->conn_dbus_error);
            priv->conn_dbus_error = g_strdup (dbus_error);
            changed = TRUE;
        }

        /* to avoid having to do deep comparisons, we assume that any change to
         * or from a non-empty hash table is interesting. */
        if ((details != NULL && tp_asv_size (details) > 0) ||
            tp_asv_size (priv->conn_error_details) > 0)
        {
            DEBUG ("changing error details");
            g_hash_table_remove_all (priv->conn_error_details);

            if (details != NULL)
                tp_g_hash_table_update (priv->conn_error_details,
                                        (GHashTable *) details,
                                        (GBoxedCopyFunc) g_strdup,
                                        (GBoxedCopyFunc) tp_g_value_slice_dup);

            changed = TRUE;
        }
    }

    if (priv->tp_connection != tp_conn
        || (tp_conn != NULL && status == TP_CONNECTION_STATUS_DISCONNECTED))
    {
        tp_clear_object (&priv->tp_connection);
        tp_clear_object (&priv->self_contact);

        if (tp_conn != NULL && status != TP_CONNECTION_STATUS_DISCONNECTED)
            priv->tp_connection = g_object_ref (tp_conn);
        else
            priv->tp_connection = NULL;

        changed = TRUE;
    }

    if (status != priv->conn_status)
    {
        DEBUG ("changing connection status from %u to %u", priv->conn_status,
               status);
	priv->conn_status = status;
	changed = TRUE;
    }

    if (reason != priv->conn_reason)
    {
        DEBUG ("changing connection status reason from %u to %u",
               priv->conn_reason, reason);
	priv->conn_reason = reason;
	changed = TRUE;
    }

    if (changed)
    {
        GValue value = G_VALUE_INIT;

        _mcd_account_tp_connection_changed (account, priv->tp_connection);

        g_value_init (&value, G_TYPE_UINT);
        g_value_set_uint (&value, priv->conn_status);
        mcd_account_changed_property (account, "ConnectionStatus", &value);
        g_value_set_uint (&value, priv->conn_reason);
        mcd_account_changed_property (account, "ConnectionStatusReason",
                                      &value);
        g_value_unset (&value);

        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, priv->conn_dbus_error);
        mcd_account_changed_property (account, "ConnectionError", &value);
        g_value_unset (&value);

        g_value_init (&value, TP_HASH_TYPE_STRING_VARIANT_MAP);
        g_value_set_boxed (&value, priv->conn_error_details);
        mcd_account_changed_property (account, "ConnectionErrorDetails",
                                      &value);
        g_value_unset (&value);
    }

    mcd_account_thaw_properties (account);

    process_online_requests (account, status, reason);
}

TpConnectionStatus
mcd_account_get_connection_status (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    return priv->conn_status;
}

void
_mcd_account_tp_connection_changed (McdAccount *account,
                                    TpConnection *tp_conn)
{
    GValue value = G_VALUE_INIT;

    g_value_init (&value, DBUS_TYPE_G_OBJECT_PATH);

    if (tp_conn == NULL)
    {
        g_value_set_static_boxed (&value, "/");
    }
    else
    {
        g_value_set_boxed (&value, tp_proxy_get_object_path (tp_conn));
    }

    mcd_account_changed_property (account, "Connection", &value);

    g_signal_emit (account, _mcd_account_signals[CONNECTION_PATH_CHANGED], 0,
        g_value_get_boxed (&value));
    g_value_unset (&value);
}

McdConnection *
mcd_account_get_connection (McdAccount *account)
{
    McdAccountPrivate *priv = MCD_ACCOUNT_PRIV (account);
    return priv->connection;
}

typedef struct
{
    McdAccountCheckValidityCb callback;
    gpointer user_data;
} CheckValidityData;

static void
check_validity_check_parameters_cb (McdAccount *account,
                                    const GError *invalid_reason,
                                    gpointer user_data)
{
    CheckValidityData *data = (CheckValidityData *) user_data;
    McdAccountPrivate *priv = account->priv;
    gboolean now_valid = (invalid_reason == NULL);
    gboolean was_valid = (priv->invalid_reason == NULL);

    g_clear_error (&priv->invalid_reason);
    if (invalid_reason != NULL)
    {
        priv->invalid_reason = g_error_copy (invalid_reason);
    }

    if (was_valid != now_valid)
    {
        GValue value = G_VALUE_INIT;
        DEBUG ("Account validity changed (old: %d, new: %d)",
               was_valid, now_valid);
        g_signal_emit (account, _mcd_account_signals[VALIDITY_CHANGED], 0,
                       now_valid);
        g_value_init (&value, G_TYPE_BOOLEAN);
        g_value_set_boolean (&value, now_valid);
        mcd_account_changed_property (account, "Valid", &value);

        if (now_valid)
        {
            /* Newly valid - try setting requested presence again.
             * This counts as user-initiated, because the user caused the
             * account to become valid somehow. */
            mcd_account_rerequest_presence (account, TRUE);
        }
    }

    if (data->callback != NULL)
        data->callback (account, invalid_reason, data->user_data);

    g_slice_free (CheckValidityData, data);
}

void
mcd_account_check_validity (McdAccount *account,
                            McdAccountCheckValidityCb callback,
                            gpointer user_data)
{
    CheckValidityData *data;

    g_return_if_fail (MCD_IS_ACCOUNT (account));

    data = g_slice_new0 (CheckValidityData);
    data->callback = callback;
    data->user_data = user_data;

    mcd_account_check_parameters (account, check_validity_check_parameters_cb,
                                  data);
}

/*
 * _mcd_account_connect_with_auto_presence:
 * @account: the #McdAccount.
 * @user_initiated: %TRUE if the connection attempt is in response to a user
 *                  request (like a request for a channel)
 *
 * Request the account to go back online with the current RequestedPresence, if
 * it is not Offline, or with the configured AutomaticPresence otherwise.
 *
 * This is appropriate in these situations:
 * - going online automatically because we've gained connectivity
 * - going online automatically in order to request a channel
 */
void
_mcd_account_connect_with_auto_presence (McdAccount *account,
                                         gboolean user_initiated)
{
    McdAccountPrivate *priv = account->priv;

    if (_presence_type_is_online (priv->req_presence_type))
        mcd_account_rerequest_presence (account, user_initiated);
    else
        mcd_account_request_presence_int (account,
                                          priv->auto_presence_type,
                                          priv->auto_presence_status,
                                          priv->auto_presence_message,
                                          user_initiated);
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

    if (priv->loaded && !mcd_account_is_valid (account))
    {
        /* FIXME: pick a better error and put it in telepathy-spec? */
        GError e = { TP_ERROR, TP_ERROR_NOT_AVAILABLE,
            "account isn't Valid (not enough information to put it online)" };

        DEBUG ("%s: %s", priv->unique_name, e.message);
        callback (account, userdata, &e);
        return;
    }

    if (priv->loaded && !priv->enabled)
    {
        /* FIXME: pick a better error and put it in telepathy-spec? */
        GError e = { TP_ERROR, TP_ERROR_NOT_AVAILABLE,
            "account isn't Enabled" };

        DEBUG ("%s: %s", priv->unique_name, e.message);
        callback (account, userdata, &e);
        return;
    }

    /* listen to the StatusChanged signal */
    if (priv->loaded && priv->conn_status == TP_CONNECTION_STATUS_DISCONNECTED)
        _mcd_account_connect_with_auto_presence (account, TRUE);

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

static gchar *
_mcd_account_get_old_avatar_filename (McdAccount *account,
                                      gchar **old_dir)
{
    McdAccountPrivate *priv = account->priv;
    gchar *filename;

    *old_dir = get_old_account_data_path (priv);
    filename = g_build_filename (*old_dir, MC_OLD_AVATAR_FILENAME, NULL);
    return filename;
}

static void
mcd_account_process_initial_avatar_token (McdAccount *self,
    const gchar *token)
{
  GArray *avatar = NULL;
  gchar *mime_type = NULL;
  gchar *prev_token;

  g_assert (self->priv->self_contact != NULL);

  prev_token = _mcd_account_get_avatar_token (self);

  DEBUG ("%s", self->priv->unique_name);

  if (prev_token == NULL)
    DEBUG ("no previous local avatar token");
  else
    DEBUG ("previous local avatar token: '%s'", prev_token);

  if (avatar == NULL)
    DEBUG ("no previous local avatar");
  else
    DEBUG ("previous local avatar: %u bytes, MIME type '%s'", avatar->len,
        (mime_type != NULL ? mime_type : "(null)"));

  if (token == NULL)
    DEBUG ("no remote avatar token");
  else
    DEBUG ("remote avatar token: '%s'", token);

  _mcd_account_get_avatar (self, &avatar, &mime_type);

  /* If we have a stored avatar but no avatar token, we must have
   * changed it locally; set it.
   *
   * Meanwhile, if the self-contact's avatar token is missing, this is
   * a protocol like link-local XMPP where avatars don't persist.
   * We can distinguish between this case (token is missing, so token = NULL)
   * and the case where there is no avatar on an XMPP server (token is
   * present and empty), although it's ridiculously subtle.
   *
   * Either way, upload our avatar, if any. */
  if (tp_str_empty (prev_token) || token == NULL)
    {

      if (avatar != NULL)
        {
          if (tp_str_empty (prev_token))
            DEBUG ("We have an avatar that has never been uploaded");
          if (tp_str_empty (token))
            DEBUG ("We have an avatar and the server doesn't");

          mcd_account_send_avatar_to_connection (self, avatar, mime_type);
          goto out;
        }
    }

  /* Otherwise, if the self-contact's avatar token
   * differs from ours, one of our "other selves" must have changed
   * it remotely. Behave the same as if it changes remotely
   * mid-session - i.e. download it and use it as our new avatar.
   *
   * In particular, this includes the case where we had
   * a non-empty avatar last time we signed in, but another client
   * has deleted it from the server since then (prev_token nonempty,
   * token = ""). */
  if (tp_strdiff (token, prev_token))
    {
      GFile *file = tp_contact_get_avatar_file (self->priv->self_contact);

      DEBUG ("The server's avatar does not match ours");

      if (file != NULL)
        {
          /* We have already downloaded it: copy it. */
          mcd_account_self_contact_notify_avatar_file_cb (self, NULL,
              self->priv->self_contact);
        }
      /* ... else we haven't downloaded it yet, but when we do,
       * notify::avatar-file will go off. */
    }

out:
  g_free (prev_token);
  tp_clear_pointer (&avatar, g_array_unref);
  g_free (mime_type);
}

static void
account_conn_get_known_avatar_tokens_cb (TpConnection *conn,
    GHashTable *tokens,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  McdAccount *self = g_object_ref (weak_object);

  self->priv->waiting_for_initial_avatar = FALSE;

  if (error != NULL)
    {
      DEBUG ("%s: GetKnownAvatarTokens raised %s #%d: %s",
          self->priv->unique_name, g_quark_to_string (error->domain),
          error->code, error->message);
    }
  else if (self->priv->self_contact == user_data)
    {
      TpHandle handle = tp_contact_get_handle (self->priv->self_contact);

      mcd_account_process_initial_avatar_token (self,
          g_hash_table_lookup (tokens, GUINT_TO_POINTER (handle)));
    }
  else
    {
      DEBUG ("%s: GetKnownAvatarTokens for outdated self-contact '%s', "
          "ignoring",
          self->priv->unique_name, tp_contact_get_identifier (user_data));
    }

  g_object_unref (self);
}

static void
mcd_account_self_contact_upgraded_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpConnection *conn = TP_CONNECTION (source_object);
  McdAccount *self = tp_weak_ref_dup_object (user_data);
  GPtrArray *contacts = NULL;
  GError *error = NULL;

  if (self == NULL)
    return;

  g_return_if_fail (MCD_IS_ACCOUNT (self));

  if (tp_connection_upgrade_contacts_finish (conn, res, &contacts, &error))
    {
      TpContact *self_contact;

      g_assert (contacts->len == 1);
      self_contact = g_ptr_array_index (contacts, 0);

      if (self_contact == self->priv->self_contact)
        {
          DEBUG ("%s", tp_contact_get_identifier (self_contact));

          tp_g_signal_connect_object (self_contact, "notify::alias",
              G_CALLBACK (mcd_account_self_contact_notify_alias_cb),
              self, G_CONNECT_SWAPPED);
          mcd_account_self_contact_notify_alias_cb (self, NULL, self_contact);

          tp_g_signal_connect_object (self_contact, "notify::avatar-file",
              G_CALLBACK (mcd_account_self_contact_notify_avatar_file_cb),
              self, G_CONNECT_SWAPPED);

          tp_g_signal_connect_object (self_contact, "presence-changed",
              G_CALLBACK (mcd_account_update_self_presence),
              self, G_CONNECT_SWAPPED);

          /* If the connection doesn't support SimplePresence then the
           * presence will be (UNSET, '', '') which is what we want anyway. */
          mcd_account_update_self_presence (self,
              tp_contact_get_presence_type (self_contact),
              tp_contact_get_presence_status (self_contact),
              tp_contact_get_presence_message (self_contact),
              self_contact);

          /* We have to use GetKnownAvatarTokens() because of its special
           * case for CMs that don't always download an up-to-date
           * avatar token before signalling CONNECTED. */
          if (tp_proxy_has_interface_by_id (conn,
              TP_IFACE_QUARK_CONNECTION_INTERFACE_AVATARS))
            {
              guint self_handle = tp_contact_get_handle (self_contact);
              GArray *arr = g_array_new (FALSE, FALSE, sizeof (guint));

              g_array_append_val (arr, self_handle);
              tp_cli_connection_interface_avatars_call_get_known_avatar_tokens (
                  conn, -1, arr, account_conn_get_known_avatar_tokens_cb,
                  g_object_ref (self_contact), g_object_unref,
                  (GObject *) self);
            }
        }
      else if (self->priv->self_contact == NULL)
        {
          DEBUG ("self-contact '%s' has disappeared since we asked to "
              "upgrade it", tp_contact_get_identifier (self_contact));
        }
      else
        {
          DEBUG ("self-contact '%s' has changed to '%s' since we asked to "
              "upgrade it", tp_contact_get_identifier (self_contact),
              tp_contact_get_identifier (self->priv->self_contact));
        }

      g_ptr_array_unref (contacts);
    }
  else
    {
      DEBUG ("failed to prepare self-contact: %s", error->message);
      g_clear_error (&error);
    }

  g_object_unref (self);
  tp_weak_ref_destroy (user_data);
}

static void
mcd_account_self_contact_changed_cb (McdAccount *self,
    GParamSpec *unused_param_spec G_GNUC_UNUSED,
    TpConnection *tp_connection)
{
  static const TpContactFeature contact_features[] = {
      TP_CONTACT_FEATURE_AVATAR_TOKEN,
      TP_CONTACT_FEATURE_AVATAR_DATA,
      TP_CONTACT_FEATURE_ALIAS,
      TP_CONTACT_FEATURE_PRESENCE
  };
  TpContact *self_contact;

  if (tp_connection != self->priv->tp_connection)
    return;

  self_contact = tp_connection_get_self_contact (tp_connection);
  g_assert (self_contact != NULL);

  DEBUG ("%s", tp_contact_get_identifier (self_contact));

  if (self_contact == self->priv->self_contact)
    return;

  g_clear_object (&self->priv->self_contact);
  self->priv->self_contact = g_object_ref (self_contact);

  _mcd_account_set_normalized_name (self,
      tp_contact_get_identifier (self_contact));

  tp_connection_upgrade_contacts_async (tp_connection,
      1, &self_contact,
      G_N_ELEMENTS (contact_features), contact_features,
      mcd_account_self_contact_upgraded_cb,
      tp_weak_ref_new (self, NULL, NULL));
}

static void
mcd_account_connection_ready_cb (McdAccount *account,
                                 McdConnection *connection)
{
    McdAccountPrivate *priv = account->priv;
    gchar *nickname;
    TpConnection *tp_connection;
    TpConnectionStatus status;
    TpConnectionStatusReason reason;
    const gchar *dbus_error = NULL;
    const GHashTable *details = NULL;

    g_return_if_fail (MCD_IS_ACCOUNT (account));
    g_return_if_fail (connection == priv->connection);

    tp_connection = mcd_connection_get_tp_connection (connection);
    g_return_if_fail (tp_connection != NULL);
    g_return_if_fail (priv->tp_connection == NULL ||
                      tp_connection == priv->tp_connection);
    g_assert (tp_proxy_is_prepared (tp_connection,
                                    TP_CONNECTION_FEATURE_CONNECTED));

    status = tp_connection_get_status (tp_connection, &reason);
    dbus_error = tp_connection_get_detailed_error (tp_connection, &details);
    _mcd_account_set_connection_status (account, status, reason,
                                        tp_connection, dbus_error, details);

    tp_g_signal_connect_object (tp_connection, "notify::self-contact",
        G_CALLBACK (mcd_account_self_contact_changed_cb), account,
        G_CONNECT_SWAPPED);
    mcd_account_self_contact_changed_cb (account, NULL, tp_connection);
    g_assert (priv->self_contact != NULL);

    /* FIXME: ideally, on protocols with server-stored nicknames, this should
     * only be done if the local Nickname has been changed since last time we
     * were online; Aliasing doesn't currently offer a way to tell whether
     * this is such a protocol, though.
     *
     * As a first step towards doing the right thing, we assume that if our
     * locally-stored nickname is just the protocol identifer, the
     * server-stored nickname (if any) takes precedence.
     */
    nickname = mcd_account_get_alias (account);

    if (tp_str_empty (nickname))
    {
        DEBUG ("no nickname yet");
    }
    else if (!tp_strdiff (nickname,
            tp_contact_get_identifier (priv->self_contact)))
    {
        DEBUG ("not setting nickname to '%s' since it matches the "
            "NormalizedName", nickname);
    }
    else
    {
        mcd_account_send_nickname_to_connection (account, nickname);
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
                                              on_conn_status_changed,
                                              account);
        g_signal_handlers_disconnect_by_func (priv->connection,
                                              mcd_account_connection_ready_cb,
                                              account);
        g_object_unref (priv->connection);
    }

    tp_clear_object (&priv->tp_connection);

    priv->connection = connection;
    priv->waiting_for_initial_avatar = TRUE;

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

        g_signal_connect (connection, "connection-status-changed",
                          G_CALLBACK (on_conn_status_changed), account);
        g_signal_connect (connection, "abort",
                          G_CALLBACK (on_connection_abort), account);
    }
    else
    {
        priv->conn_status = TP_CONNECTION_STATUS_DISCONNECTED;
    }
}

void
_mcd_account_set_has_been_online (McdAccount *account)
{
    if (!account->priv->has_been_online)
    {
        GValue value = G_VALUE_INIT;
        const gchar *account_name = mcd_account_get_unique_name (account);

        g_value_init (&value, G_TYPE_BOOLEAN);
        g_value_set_boolean (&value, TRUE);

        mcd_storage_set_attribute (account->priv->storage, account_name,
                                   MC_ACCOUNTS_KEY_HAS_BEEN_ONLINE, &value);
        account->priv->has_been_online = TRUE;
        mcd_storage_commit (account->priv->storage, account_name);
        mcd_account_changed_property (account, MC_ACCOUNTS_KEY_HAS_BEEN_ONLINE,
                                      &value);
        g_value_unset (&value);
    }
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

gboolean
_mcd_account_needs_dispatch (McdAccount *self)
{
    g_return_val_if_fail (MCD_IS_ACCOUNT (self), FALSE);

    return self->priv->always_dispatch;
}

gboolean
mcd_account_parameter_is_secret (McdAccount *self, const gchar *name)
{
    McdAccountPrivate *priv = self->priv;
    const TpConnectionManagerParam *param;

    param = mcd_manager_get_protocol_param (priv->manager,
                                            priv->protocol_name, name);

    return (param != NULL &&
        tp_connection_manager_param_is_secret (param));
}

void
_mcd_account_set_changing_presence (McdAccount *self, gboolean value)
{
    McdAccountPrivate *priv = self->priv;
    GValue changing_presence = G_VALUE_INIT;

    priv->changing_presence = value;

    g_value_init (&changing_presence, G_TYPE_BOOLEAN);
    g_value_set_boolean (&changing_presence, value);

    mcd_account_changed_property (self, "ChangingPresence",
                                  &changing_presence);

    g_value_unset (&changing_presence);
}

gchar *
mcd_account_dup_display_name (McdAccount *self)
{
    const gchar *name = mcd_account_get_unique_name (self);

    return mcd_storage_dup_string (self->priv->storage, name, "DisplayName");
}

gchar *
mcd_account_dup_icon (McdAccount *self)
{
    const gchar *name = mcd_account_get_unique_name (self);

    return mcd_storage_dup_string (self->priv->storage, name, "Icon");
}

gchar *
mcd_account_dup_nickname (McdAccount *self)
{
    const gchar *name = mcd_account_get_unique_name (self);

    return mcd_storage_dup_string (self->priv->storage, name, "Nickname");
}

McdConnectivityMonitor *
mcd_account_get_connectivity_monitor (McdAccount *self)
{
    return self->priv->connectivity;
}

gboolean
mcd_account_get_waiting_for_connectivity (McdAccount *self)
{
  return self->priv->waiting_for_connectivity;
}

void
mcd_account_set_waiting_for_connectivity (McdAccount *self,
    gboolean waiting)
{
  self->priv->waiting_for_connectivity = waiting;
}
