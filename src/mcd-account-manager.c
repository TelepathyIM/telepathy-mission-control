/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
 *
 * Contact: Naba Kumar  <naba.kumar@nokia.com>
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

#include <glib/gi18n.h>
#include <string.h>
#include <stdio.h>
#include <config.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>

#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/errors.h>
#include "mcd-account-manager.h"
#include "mcd-account-manager-query.h"
#include "mcd-account-manager-creation.h"
#include "mcd-account.h"
#include "mcd-account-config.h"
#include "mcd-dbusprop.h"
#include "_gen/interfaces.h"

#define WRITE_CONF_DELAY    500
#define INITIAL_CONFIG_FILE_CONTENTS "# Telepathy accounts\n"

#define MCD_ACCOUNT_MANAGER_PRIV(account_manager) \
    (MCD_ACCOUNT_MANAGER (account_manager)->priv)

static void account_manager_iface_init (McSvcAccountManagerClass *iface,
					gpointer iface_data);
static void properties_iface_init (TpSvcDBusPropertiesClass *iface,
				   gpointer iface_data);

static const McdDBusProp account_manager_properties[];

static const McdInterfaceData account_manager_interfaces[] = {
    MCD_IMPLEMENT_IFACE (mc_svc_account_manager_get_type,
			 account_manager,
			 MC_IFACE_ACCOUNT_MANAGER),
    MCD_IMPLEMENT_IFACE (mc_svc_account_manager_interface_query_get_type,
			 account_manager_query,
			 MC_IFACE_ACCOUNT_MANAGER_INTERFACE_QUERY),
    MCD_IMPLEMENT_IFACE (mc_svc_account_manager_interface_creation_get_type,
			 account_manager_creation,
			 MC_IFACE_ACCOUNT_MANAGER_INTERFACE_CREATION),
    { G_TYPE_INVALID, }
};

G_DEFINE_TYPE_WITH_CODE (McdAccountManager, mcd_account_manager, G_TYPE_OBJECT,
			 MCD_DBUS_INIT_INTERFACES (account_manager_interfaces);
			 G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
						properties_iface_init);
			)

struct _McdAccountManagerPrivate
{
    /* DBUS connection */
    TpDBusDaemon *dbus_daemon;

    GKeyFile *keyfile;		/* configuration file */
    GHashTable *accounts;

    gchar *account_connections_file; /* temporary file */
};

typedef struct
{
    McdAccountManager *account_manager;
    gint account_lock;
} McdLoadAccountsData;

typedef struct
{
    McdAccountManager *account_manager;
    GHashTable *parameters;
    McdGetAccountCb callback;
    gpointer user_data;
    GDestroyNotify destroy;
} McdCreateAccountData;

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
};

static guint write_conf_id = 0;

static void register_dbus_service (McdAccountManager *account_manager);

static gboolean
get_account_connection (const gchar *file_contents, const gchar *path,
                        gchar **p_bus_name, gchar **p_account_name)
{
    const gchar *line, *tab1, *tab2, *endline;
    const gchar *connection_path, *bus_name, *account_name;
    size_t len;

    g_return_val_if_fail (path != NULL, FALSE);
    if (!file_contents) return FALSE;

    len = strlen (path);
    line = file_contents;
    while ((tab1 = strchr (line, '\t')) != NULL)
    {
        connection_path = line;

        bus_name = tab1 + 1;
        tab2 = strchr (bus_name, '\t');
        if (!tab2) break;

        account_name = tab2 + 1;
        endline = strchr (account_name, '\n');
        if (!endline) break;

        if (len == tab1 - line &&
            strncmp (path, connection_path, len) == 0)
        {
            *p_bus_name = g_strndup (bus_name, tab2 - bus_name);
            *p_account_name = g_strndup (account_name, endline - account_name);
            return TRUE;
        }
        line = endline + 1;
    }
    return FALSE;
}

static gboolean
recover_connection (McdAccountManager *account_manager, gchar *file_contents,
                    const gchar *name)
{
    McdAccount *account;
    McdConnection *connection;
    McdManager *manager;
    McdMaster *master;
    const gchar *manager_name;
    gchar *object_path, *bus_name, *account_name;
    GError *error = NULL;
    gboolean ret = FALSE;

    object_path = g_strdelimit (g_strdup_printf ("/%s", name), ".", '/');
    if (!get_account_connection (file_contents, object_path,
                                 &bus_name, &account_name))
        goto err_match;

    account = g_hash_table_lookup (account_manager->priv->accounts,
                                   account_name);
    if (!account || !mcd_account_is_enabled (account))
        goto err_account;

    g_debug ("%s: account is %s", G_STRFUNC,
             mcd_account_get_unique_name (account));
    manager_name = mcd_account_get_manager_name (account);

    master = mcd_master_get_default ();
    g_return_val_if_fail (MCD_IS_MASTER (master), FALSE);

    manager = mcd_master_lookup_manager (master, manager_name);
    if (G_UNLIKELY (!manager))
    {
        g_debug ("Manager %s not found", manager_name);
        goto err_manager;
    }

    connection = mcd_manager_create_connection (manager, account);
    if (G_UNLIKELY (!connection)) goto err_connection;

    _mcd_connection_set_tp_connection (connection, bus_name, object_path,
                                       &error);
    if (G_UNLIKELY (error))
    {
        g_debug ("%s: got error: %s", G_STRFUNC, error->message);
        g_error_free (error);
        goto err_connection;
    }
    ret = TRUE;

err_connection:
err_manager:
err_account:
    g_free (account_name);
    g_free (bus_name);
err_match:
    g_free (object_path);
    return ret;
}

static void
list_connection_names_cb (const gchar * const *names, gsize n,
                          const gchar * const *cms,
                          const gchar * const *protocols,
                          const GError *error, gpointer user_data,
                          GObject *weak_object)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (weak_object);
    McdAccountManagerPrivate *priv = account_manager->priv;
    gchar *contents = NULL;
    guint i;

    g_debug ("%s called, %u connections", G_STRFUNC, n);
    g_file_get_contents (priv->account_connections_file, &contents, NULL, NULL);

    for (i = 0; i < n; i++)
    {
        g_return_if_fail (names[i] != NULL);
        g_debug ("Connection %s", names[i]);
        if (!recover_connection (account_manager, contents, names[i]))
        {
            /* Close the connection */
            TpConnection *proxy;

            g_debug ("Killing connection");
            proxy = tp_connection_new (priv->dbus_daemon, names[i], NULL, NULL);
            if (proxy)
                tp_cli_connection_call_disconnect (proxy, -1, NULL, NULL,
                                                   NULL, NULL);
        }
    }
    g_free (contents);
}

static McdAccount *
account_new (McdAccountManager *account_manager, const gchar *name)
{
    return mcd_account_new (account_manager, name);
}

static void
on_account_validity_changed (McdAccount *account, gboolean valid,
			     McdAccountManager *account_manager)
{
    const gchar *object_path;

    object_path = mcd_account_get_object_path (account);
    mc_svc_account_manager_emit_account_validity_changed (account_manager,
							  object_path,
							  valid);
}

static void
on_account_removed (McdAccount *account, McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    const gchar *name, *object_path;

    object_path = mcd_account_get_object_path (account);
    mc_svc_account_manager_emit_account_removed (account_manager, object_path);

    name = mcd_account_get_unique_name (account);
    g_hash_table_remove (priv->accounts, name);
}

static void
unref_account (gpointer data)
{
    McdAccount *account = MCD_ACCOUNT (data);
    McdAccountManager *account_manager;

    g_debug ("%s called for %s", G_STRFUNC,
             mcd_account_get_unique_name (account));
    account_manager = mcd_account_get_account_manager (account);
    g_signal_handlers_disconnect_by_func (account, on_account_validity_changed,
                                          account_manager);
    g_signal_handlers_disconnect_by_func (account, on_account_removed,
                                          account_manager);
    g_object_unref (account);
}

static void
add_account (McdAccountManager *account_manager, McdAccount *account)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    const gchar *name;

    name = mcd_account_get_unique_name (account);
    g_hash_table_insert (priv->accounts, (gchar *)name, account);

    /* if we have to connect to any signals from the account object, this is
     * the place to do it */
    g_signal_connect (account, "validity-changed",
		      G_CALLBACK (on_account_validity_changed),
		      account_manager);
    g_signal_connect (account, "removed", G_CALLBACK (on_account_removed),
		      account_manager);
}

static void
mcd_create_account_data_free (McdCreateAccountData *cad)
{
    g_hash_table_unref (cad->parameters);
    g_slice_free (McdCreateAccountData, cad);
}

static void
complete_account_creation (McdAccount *account,
                           const GError *cb_error,
                           gpointer user_data)
{
    McdCreateAccountData *cad = user_data;
    McdAccountManager *account_manager;
    GError *error = NULL;
    gboolean ok;

    account_manager = cad->account_manager;
    if (G_UNLIKELY (cb_error))
    {
        cad->callback (account_manager, account, cb_error, cad->user_data);
        mcd_create_account_data_free (cad);
        return;
    }

    ok = mcd_account_set_parameters (account, cad->parameters, &error);
    if (ok)
    {
	add_account (account_manager, account);
	mcd_account_check_validity (account);
    }
    else
    {
	mcd_account_delete (account, NULL);
	g_object_unref (account);
	account = NULL;
    }
    mcd_account_manager_write_conf (account_manager);

    cad->callback (account_manager, account, error, cad->user_data);
    if (G_UNLIKELY (error))
        g_error_free (error);
    mcd_create_account_data_free (cad);
}

static gchar *
create_unique_name (McdAccountManagerPrivate *priv, const gchar *manager,
		    const gchar *protocol, GHashTable *params)
{
    gchar *path, *seq, *unique_name = NULL;
    const gchar *base = NULL;
    gchar *esc_manager, *esc_protocol, *esc_base;
    GValue *value;
    gint i, len;

    value = g_hash_table_lookup (params, "account");
    if (value)
	base = g_value_get_string (value);

    if (!base)
	base = "account";

    esc_manager = tp_escape_as_identifier (manager);
    esc_protocol = tp_escape_as_identifier (protocol);
    esc_base = tp_escape_as_identifier (base);
    /* add two chars for the "/" */
    len = strlen (esc_manager) + strlen (esc_protocol) + strlen (esc_base) + 2;
    path = g_malloc (len + 5);
    sprintf (path, "%s/%s/%s", esc_manager, esc_protocol, esc_base);
    g_free (esc_manager);
    g_free (esc_protocol);
    g_free (esc_base);
    seq = path + len;
    for (i = 0; i < 1024; i++)
    {
	sprintf (seq, "%u", i);
	if (!g_key_file_has_group (priv->keyfile, path))
	{
	    unique_name = path;
	    break;
	}
    }
    return unique_name;
}

void
mcd_account_manager_create_account (McdAccountManager *account_manager,
				    const gchar *manager,
				    const gchar *protocol,
				    const gchar *display_name,
				    GHashTable *params,
                                    McdGetAccountCb callback,
                                    gpointer user_data, GDestroyNotify destroy)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    McdCreateAccountData *cad;
    McdAccount *account;
    gchar *unique_name;

    g_debug ("%s called", G_STRFUNC);
    if (G_UNLIKELY (manager == NULL || manager[0] == 0 ||
		    protocol == NULL || protocol[0] == 0))
    {
        GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
            "Invalid parameters"};
        callback (account_manager, NULL, &error, user_data);
        if (destroy)
            destroy (user_data);
        return;
    }

    unique_name = create_unique_name (priv, manager, protocol, params);
    g_return_if_fail (unique_name != NULL);

    /* create the basic GConf keys */
    g_key_file_set_string (priv->keyfile, unique_name,
			   MC_ACCOUNTS_KEY_MANAGER, manager);
    g_key_file_set_string (priv->keyfile, unique_name,
			   MC_ACCOUNTS_KEY_PROTOCOL, protocol);
    if (display_name)
	g_key_file_set_string (priv->keyfile, unique_name,
			       MC_ACCOUNTS_KEY_DISPLAY_NAME, display_name);

    account = MCD_ACCOUNT_MANAGER_GET_CLASS (account_manager)->account_new
        (account_manager, unique_name);
    g_free (unique_name);

    if (G_LIKELY (account))
    {
        cad = g_slice_new (McdCreateAccountData);
        cad->account_manager = account_manager;
        cad->parameters = g_hash_table_ref (params);
        cad->callback = callback;
        cad->user_data = user_data;
        cad->destroy = destroy;
        _mcd_account_load (account, complete_account_creation, cad);
    }
    else
    {
        GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "" };
        callback (account_manager, NULL, &error, user_data);
        if (destroy)
            destroy (user_data);
    }
}

static void
create_account_cb (McdAccountManager *account_manager, McdAccount *account,
                   const GError *error, gpointer user_data)
{
    DBusGMethodInvocation *context = user_data;
    const gchar *object_path;

    if (G_UNLIKELY (error))
    {
        dbus_g_method_return_error (context, (GError *)error);
	return;
    }

    g_return_if_fail (MCD_IS_ACCOUNT (account));
    object_path = mcd_account_get_object_path (account);
    mc_svc_account_manager_return_from_create_account (context, object_path);
}

static void
account_manager_create_account (McSvcAccountManager *self,
			       	const gchar *manager, const gchar *protocol,
				const gchar *display_name,
				GHashTable *parameters,
				DBusGMethodInvocation *context)
{
    mcd_account_manager_create_account (MCD_ACCOUNT_MANAGER (self),
                                        manager, protocol, display_name,
                                        parameters,
                                        create_account_cb, context, NULL);
}

static void
account_manager_iface_init (McSvcAccountManagerClass *iface,
			    gpointer iface_data)
{
#define IMPLEMENT(x) mc_svc_account_manager_implement_##x (\
    iface, account_manager_##x)
    IMPLEMENT(create_account);
#undef IMPLEMENT
}

static void
accounts_to_gvalue (GHashTable *accounts, gboolean valid, GValue *value)
{
    static GType ao_type = G_TYPE_INVALID;
    GPtrArray *account_array;
    GHashTableIter iter;
    McdAccount *account;
    gpointer k;

    if (G_UNLIKELY (ao_type == G_TYPE_INVALID))
        ao_type = dbus_g_type_get_collection ("GPtrArray",
                                              DBUS_TYPE_G_OBJECT_PATH);

    account_array = g_ptr_array_sized_new (g_hash_table_size (accounts));

    g_hash_table_iter_init (&iter, accounts);

    while (g_hash_table_iter_next (&iter, &k, (gpointer)&account))
    {
        if (mcd_account_is_valid (account) == valid)
            g_ptr_array_add (account_array,
                             g_strdup (mcd_account_get_object_path (account)));
    }

    g_value_init (value, ao_type);
    g_value_take_boxed (value, account_array);
}

static void
get_valid_accounts (TpSvcDBusProperties *self, const gchar *name,
		    GValue *value)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (self);
    McdAccountManagerPrivate *priv = account_manager->priv;

    g_debug ("%s called", G_STRFUNC);
    accounts_to_gvalue (priv->accounts, TRUE, value);
}

static void
get_invalid_accounts (TpSvcDBusProperties *self, const gchar *name,
		      GValue *value)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (self);
    McdAccountManagerPrivate *priv = account_manager->priv;

    g_debug ("%s called", G_STRFUNC);
    accounts_to_gvalue (priv->accounts, FALSE, value);
}

static const McdDBusProp account_manager_properties[] = {
    { "ValidAccounts", NULL, get_valid_accounts },
    { "InvalidAccounts", NULL, get_invalid_accounts },
    { "Interfaces", NULL, mcd_dbus_get_interfaces },
    { 0 },
};

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

/* Returns the location of the account configuration file.
 * Returned string must be freed by caller. */
static gchar *
get_account_conf_filename (void)
{
    const gchar *base;

    base = g_getenv ("MC_ACCOUNT_DIR");
    if (!base)
	base = ACCOUNTS_DIR;
    if (!base)
	return NULL;

    if (base[0] == '~')
	return g_build_filename (g_get_home_dir(), base + 1,
				 "accounts.cfg", NULL);
    else
	return g_build_filename (base, "accounts.cfg", NULL);
}

static gboolean
write_conf (gpointer userdata)
{
    GKeyFile *keyfile = userdata;
    GError *error = NULL;
    gboolean ok;
    gchar *filename, *data;
    gsize len;

    g_debug ("%s called", G_STRFUNC);
    write_conf_id = 0;

    data = g_key_file_to_data (keyfile, &len, &error);
    if (error)
    {
	g_warning ("Could not save account data: %s", error->message);
	g_error_free (error);
	return FALSE;
    }
    filename = get_account_conf_filename ();
    ok = g_file_set_contents (filename, data, len, &error);
    g_free (filename);
    g_free (data);
    if (G_UNLIKELY (!ok))
    {
	g_warning ("Could not save account data: %s", error->message);
	g_error_free (error);
	return FALSE;
    }
    return FALSE;
}

static void
release_load_accounts_lock (McdLoadAccountsData *lad)
{
    g_return_if_fail (lad->account_lock > 0);
    lad->account_lock--;
    g_debug ("%s called, count is now %d", G_STRFUNC, lad->account_lock);
    if (lad->account_lock == 0)
    {
        register_dbus_service (lad->account_manager);
        g_slice_free (McdLoadAccountsData, lad);
    }
}

static void
account_loaded (McdAccount *account, const GError *error, gpointer user_data)
{
    McdLoadAccountsData *lad = user_data;

    if (error)
    {
        g_warning ("%s: got error: %s", G_STRFUNC, error->message);
        g_hash_table_remove (lad->account_manager->priv->accounts,
                             mcd_account_get_unique_name (account));
    }

    release_load_accounts_lock (lad);
}

/**
 * _mcd_account_manager_setup:
 * @account_manager: the #McdAccountManager.
 *
 * This function must be called by the McdMaster; it reads the accounts from
 * the config file, and it needs a McdMaster instance to be active.
 */
void
_mcd_account_manager_setup (McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    McdLoadAccountsData *lad;
    gchar **accounts, **name;

    tp_list_connection_names (priv->dbus_daemon,
                              list_connection_names_cb, NULL, NULL,
                              (GObject *)account_manager);

    lad = g_slice_new (McdLoadAccountsData);
    lad->account_manager = account_manager;
    lad->account_lock = 1; /* will be released at the end of this function */

    accounts = g_key_file_get_groups (priv->keyfile, NULL);
    for (name = accounts; *name != NULL; name++)
    {
        McdAccount *account;

        account = MCD_ACCOUNT_MANAGER_GET_CLASS (account_manager)->account_new
            (account_manager, *name);
        if (G_UNLIKELY (!account))
        {
            g_warning ("%s: account %s failed to instantiate", G_STRFUNC,
                       *name);
            continue;
        }
        lad->account_lock++;
        add_account (lad->account_manager, account);
        _mcd_account_load (account, account_loaded, lad);
    }
    g_strfreev (accounts);

    release_load_accounts_lock (lad);
}

static void
register_dbus_service (McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    DBusGConnection *dbus_connection;
    DBusConnection *connection;
    DBusError error = { 0 };

    dbus_connection = TP_PROXY (priv->dbus_daemon)->dbus_connection;
    connection = dbus_g_connection_get_connection (dbus_connection);

    dbus_bus_request_name (connection, MC_ACCOUNT_MANAGER_DBUS_SERVICE,
			   0, &error);
    if (dbus_error_is_set (&error))
    {
	g_error ("Failed registering '%s' service: %s",
		 MC_ACCOUNT_MANAGER_DBUS_SERVICE, error.message);
	dbus_error_free (&error);
    }

    if (G_LIKELY (dbus_connection))
	dbus_g_connection_register_g_object (dbus_connection,
					     MC_ACCOUNT_MANAGER_DBUS_OBJECT,
					     (GObject *)account_manager);
}

static void
set_property (GObject *obj, guint prop_id,
	      const GValue *val, GParamSpec *pspec)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (obj);
    McdAccountManagerPrivate *priv = account_manager->priv;

    switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
	if (priv->dbus_daemon)
	    g_object_unref (priv->dbus_daemon);
	priv->dbus_daemon = TP_DBUS_DAEMON (g_value_dup_object (val));
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
    McdAccountManagerPrivate *priv = MCD_ACCOUNT_MANAGER_PRIV (obj);

    switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
	g_value_set_object (val, priv->dbus_daemon);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_account_manager_finalize (GObject *object)
{
    McdAccountManagerPrivate *priv = MCD_ACCOUNT_MANAGER_PRIV (object);

    if (write_conf_id)
	write_conf (priv->keyfile);
    g_key_file_free (priv->keyfile);

    remove (priv->account_connections_file);
    g_free (priv->account_connections_file);

    g_hash_table_destroy (priv->accounts);

    G_OBJECT_CLASS (mcd_account_manager_parent_class)->finalize (object);
}

static void
_mcd_account_manager_dispose (GObject *object)
{
    McdAccountManagerPrivate *priv = MCD_ACCOUNT_MANAGER_PRIV (object);

    if (priv->dbus_daemon)
    {
	g_object_unref (priv->dbus_daemon);
	priv->dbus_daemon = NULL;
    }
    G_OBJECT_CLASS (mcd_account_manager_parent_class)->dispose (object);
}

static void
mcd_account_manager_class_init (McdAccountManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdAccountManagerPrivate));

    object_class->dispose = _mcd_account_manager_dispose;
    object_class->finalize = _mcd_account_manager_finalize;
    object_class->set_property = set_property;
    object_class->get_property = get_property;

    klass->account_new = account_new;

    g_object_class_install_property
        (object_class, PROP_DBUS_DAEMON,
         g_param_spec_object ("dbus-daemon", "DBus daemon", "DBus daemon",
                              TP_TYPE_DBUS_DAEMON,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
mcd_account_manager_init (McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv;
    gchar *conf_filename;
    GError *error = NULL;

    priv = G_TYPE_INSTANCE_GET_PRIVATE ((account_manager),
					MCD_TYPE_ACCOUNT_MANAGER,
					McdAccountManagerPrivate);
    account_manager->priv = priv;

    priv->accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
					    NULL, unref_account);

    priv->account_connections_file =
        g_build_filename (g_get_tmp_dir (), ".mc_connections", NULL);

    priv->keyfile = g_key_file_new ();
    conf_filename = get_account_conf_filename ();
    g_debug ("Loading accounts from %s", conf_filename);
    if (!g_file_test (conf_filename, G_FILE_TEST_EXISTS))
    {
	gchar *dirname = g_path_get_dirname (conf_filename);
	g_mkdir_with_parents (dirname, 0777);
	g_free (dirname);

	g_debug ("Creating file");
	g_file_set_contents (conf_filename, INITIAL_CONFIG_FILE_CONTENTS,
			     sizeof (INITIAL_CONFIG_FILE_CONTENTS) - 1, NULL);
    }
    g_key_file_load_from_file (priv->keyfile, conf_filename,
			       G_KEY_FILE_KEEP_COMMENTS, &error);
    g_free (conf_filename);
    if (error)
    {
	g_warning ("Error: %s", error->message);
	g_error_free (error);
	return;
    }

    /* initializes the interfaces */
    mcd_dbus_init_interfaces_instances (account_manager);
}

McdAccountManager *
mcd_account_manager_new (TpDBusDaemon *dbus_daemon)
{
    gpointer *obj;

    obj = g_object_new (MCD_TYPE_ACCOUNT_MANAGER,
		       	"dbus-daemon", dbus_daemon,
			NULL);
    return MCD_ACCOUNT_MANAGER (obj);
}

/**
 * mcd_account_manager_get_dbus_daemon:
 * @account_manager: the #McdAccountManager.
 *
 * Returns: the #TpDBusDaemon.
 */
TpDBusDaemon *
mcd_account_manager_get_dbus_daemon (McdAccountManager *account_manager)
{
    g_return_val_if_fail (MCD_IS_ACCOUNT_MANAGER (account_manager), NULL);

    return account_manager->priv->dbus_daemon;
}

void
mcd_account_manager_write_conf (McdAccountManager *account_manager)
{
    /* FIXME: this (reasonably) assumes that there is only one
     * McdAccountManager object running, since the write_conf_id is a static
     * variable */
    if (write_conf_id == 0) 
        write_conf_id =
            g_timeout_add_full (G_PRIORITY_HIGH, WRITE_CONF_DELAY, write_conf,
                                account_manager->priv->keyfile, NULL);
}

GHashTable *
mcd_account_manager_get_accounts (McdAccountManager *account_manager)
{
    return account_manager->priv->accounts;
}

McdAccount *
mcd_account_manager_lookup_account (McdAccountManager *account_manager,
				    const gchar *name)
{
    McdAccountManagerPrivate *priv = account_manager->priv;

    return g_hash_table_lookup (priv->accounts, name);
}

static gboolean
find_by_path (gpointer key, gpointer value, gpointer user_data)
{
    McdAccount *account = value;
    const gchar *object_path = user_data;

    if (strcmp (object_path,
		mcd_account_get_object_path (account)) == 0)
	return TRUE;
    return FALSE;
}

/* NOTE: this might become unused when the presence-frame gets removed */
McdAccount *
mcd_account_manager_lookup_account_by_path (McdAccountManager *account_manager,
					    const gchar *object_path)
{
    McdAccountManagerPrivate *priv = account_manager->priv;

    return g_hash_table_find (priv->accounts, find_by_path,
			      (gpointer)object_path);
}

/**
 * mcd_account_manager_get_config:
 * @account_manager: the #McdAccountManager.
 *
 * Returns: the #GKeyFile holding the configuration.
 */
GKeyFile *
mcd_account_manager_get_config (McdAccountManager *account_manager)
{
    g_return_val_if_fail (MCD_IS_ACCOUNT_MANAGER (account_manager), NULL);

    return account_manager->priv->keyfile;
}

/*
 * _mcd_account_manager_store_account_connections:
 * @account_manager: the #McdAccountManager.
 *
 * This function is used to remember what connection an account was bound to.
 * The data is stored in a temporary file, and can be read when MC restarts
 * after a crash.
 */
void
_mcd_account_manager_store_account_connections (McdAccountManager *manager)
{
    McdAccountManagerPrivate *priv;
    GHashTableIter iter;
    const gchar *account_name, *connection_path, *connection_name;
    McdAccount *account;
    FILE *file;

    g_return_if_fail (MCD_IS_ACCOUNT_MANAGER (manager));
    g_return_if_fail (account != NULL);
    priv = manager->priv;

    file = fopen (priv->account_connections_file, "w");
    if (G_UNLIKELY (!file)) return;

    g_hash_table_iter_init (&iter, priv->accounts);
    while (g_hash_table_iter_next (&iter, (gpointer)&account_name,
                                   (gpointer)&account))
    {
        McdConnection *connection;

        connection = mcd_account_get_connection (account);
        if (connection)
        {
            connection_path = mcd_connection_get_object_path (connection);
            connection_name = mcd_connection_get_name (connection);
            if (connection_path && connection_name)
                fprintf (file, "%s\t%s\t%s\n",
                         connection_path, connection_name, account_name);
        }
    }
    fclose (file);
}

