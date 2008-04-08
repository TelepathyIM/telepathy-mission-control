/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
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
#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-dbusprop.h"
#include "_gen/interfaces.h"

#define WRITE_CONF_DELAY    2000
#define INITIAL_CONFIG_FILE_CONTENTS "# Telepathy accounts\n"

#define MCD_ACCOUNT_MANAGER_PRIV(account_manager) \
    (MCD_ACCOUNT_MANAGER (account_manager)->priv)

static void account_manager_iface_init (McSvcAccountManagerClass *iface,
					gpointer iface_data);
static void properties_iface_init (TpSvcDBusPropertiesClass *iface,
				   gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (McdAccountManager, mcd_account_manager, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (MC_TYPE_SVC_ACCOUNT_MANAGER,
						account_manager_iface_init);
			 G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
						properties_iface_init);
			)

struct _McdAccountManagerPrivate
{
    /* DBUS connection */
    TpDBusDaemon *dbus_daemon;

    GKeyFile *keyfile;		/* configuration file */
    GHashTable *accounts;
    GHashTable *invalid_accounts;
};

typedef struct
{
    gchar **accounts;
    gint i;
} McdAccountArray;

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
};

static guint write_conf_id = 0;

static void
on_account_validity_changed (McdAccount *account, gboolean valid,
			     McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    const gchar *name, *object_path;
    GHashTable *ht_old, *ht_new;
    gboolean found_old, found_new;

    if (valid)
    {
	ht_old = priv->invalid_accounts;
	ht_new = priv->accounts;
    }
    else
    {
	ht_old = priv->accounts;
	ht_new = priv->invalid_accounts;
    }

    name = mcd_account_get_unique_name (account);
    found_old = g_hash_table_steal (ht_old, name);
    if (!found_old)
	g_warning ("%s (%d): account %s not found in list",
		   G_STRFUNC, valid, name);
    found_new = g_hash_table_lookup (ht_new, name) ? TRUE : FALSE;
    if (found_new)
	g_warning ("%s (%d): account %s is already in list",
		   G_STRFUNC, valid, name);
    else
	g_hash_table_insert (ht_new, (gchar *)name, account);
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
    if (mcd_account_is_valid (account))
	g_hash_table_remove (priv->accounts, name);
    else
	g_hash_table_remove (priv->invalid_accounts, name);
}

static gboolean
add_account (McdAccountManager *account_manager, McdAccount *account)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    const gchar *name;
    gboolean valid;

    name = mcd_account_get_unique_name (account);
    valid = mcd_account_is_valid (account);
    if (valid)
	g_hash_table_insert (priv->accounts, (gchar *)name, account);
    else
	g_hash_table_insert (priv->invalid_accounts, (gchar *)name, account);

    /* if we have to connect to any signals from the account object, this is
     * the place to do it */
    g_signal_connect (account, "validity-changed",
		      G_CALLBACK (on_account_validity_changed),
		      account_manager);
    g_signal_connect (account, "removed", G_CALLBACK (on_account_removed),
		      account_manager);
    return valid;
}

static gboolean
complete_account_creation (McdAccountManager *account_manager,
			   const gchar *unique_name,
			   GHashTable *params,
			   const gchar **object_path,
			   GError **error)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    McdAccount *account;
    gboolean ok;

    account = mcd_account_new (priv->dbus_daemon, priv->keyfile, unique_name);

    ok = mcd_account_set_parameters (account, params, error);
    if (ok)
    {
	*object_path = mcd_account_get_object_path (account);
	add_account (account_manager, account);
	mcd_account_check_validity (account);
    }
    else
    {
	mcd_account_delete (account, NULL);
	g_object_unref (account);
    }
    mcd_account_manager_write_conf (priv->keyfile);
    return ok;
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

static gboolean
mcd_account_manager_create_account (McdAccountManager *account_manager,
				    const gchar *manager,
				    const gchar *protocol,
				    const gchar *display_name,
				    GHashTable *params,
				    const gchar **account_obj,
				    GError **error)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    gchar *unique_name;
    gboolean ok;

    g_debug ("%s called", G_STRFUNC);
    if (G_UNLIKELY (manager == NULL || manager[0] == 0 ||
		    protocol == NULL || protocol[0] == 0))
    {
	g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
		     "Invalid parameters");
	return FALSE;
    }

    unique_name = create_unique_name (priv, manager, protocol, params);
    if (G_UNLIKELY (unique_name == NULL))
    {
	g_warning ("Couldn't create a unique name");
	return FALSE;
    }

    /* create the basic GConf keys */
    g_key_file_set_string (priv->keyfile, unique_name,
			   MC_ACCOUNTS_KEY_MANAGER, manager);
    g_key_file_set_string (priv->keyfile, unique_name,
			   MC_ACCOUNTS_KEY_PROTOCOL, protocol);
    if (display_name)
	g_key_file_set_string (priv->keyfile, unique_name,
			       MC_ACCOUNTS_KEY_DISPLAY_NAME, display_name);

    ok = complete_account_creation (account_manager, unique_name,
				    params, account_obj, error);
    g_free (unique_name);
    return ok;
}

static void
account_manager_create_account (McSvcAccountManager *self,
			       	const gchar *manager, const gchar *protocol,
				const gchar *display_name,
				GHashTable *parameters,
				DBusGMethodInvocation *context)
{
    GError *error = NULL;
    const gchar *object_path;

    if (!mcd_account_manager_create_account (MCD_ACCOUNT_MANAGER (self),
					     manager, protocol, display_name,
					     parameters, &object_path, &error))
    {
	if (!error)
	    g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
			 "Internal error");
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }

    mc_svc_account_manager_return_from_create_account (context, object_path);
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
append_account (gpointer key, gpointer val, gpointer userdata)
{
    McdAccountArray *aa = userdata;
    const gchar *object_path;

    object_path = mcd_account_get_object_path (val);
    aa->accounts[aa->i++] = g_strdup (object_path);
    aa->accounts[aa->i] = NULL;
}

static void
accounts_to_gvalue (GHashTable *accounts, GValue *value)
{
    McdAccountArray account_array;
    gint n_accounts;

    n_accounts = g_hash_table_size (accounts);
    account_array.accounts = g_new (gchar *, n_accounts + 1);
    account_array.accounts[0] = NULL;
    account_array.i = 0;

    g_hash_table_foreach (accounts, append_account, &account_array);
    g_value_init (value, G_TYPE_STRV);
    g_value_take_boxed (value, account_array.accounts);
}

static void
get_valid_accounts (TpSvcDBusProperties *self, const gchar *name,
		    GValue *value)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (self);
    McdAccountManagerPrivate *priv = account_manager->priv;

    g_debug ("%s called", G_STRFUNC);
    accounts_to_gvalue (priv->accounts, value);
}

static void
get_invalid_accounts (TpSvcDBusProperties *self, const gchar *name,
		      GValue *value)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (self);
    McdAccountManagerPrivate *priv = account_manager->priv;

    g_debug ("%s called", G_STRFUNC);
    accounts_to_gvalue (priv->invalid_accounts, value);
}

static McdDBusProp am_properties[] = {
    { "ValidAccounts", NULL, get_valid_accounts },
    { "InvalidAccounts", NULL, get_invalid_accounts },
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
get_account_conf_filename ()
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
mcd_account_manager_setup (McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    gchar **accounts, **name;

    accounts = g_key_file_get_groups (priv->keyfile, NULL);
    for (name = accounts; *name != NULL; name++)
    {
	McdAccount *account;

	account = mcd_account_new (priv->dbus_daemon, priv->keyfile, *name);
	if (account)
	    add_account (account_manager, account);
    }
    g_strfreev (accounts);
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
	if (priv->dbus_daemon)
	{
	    register_dbus_service (account_manager);
	    mcd_account_manager_setup (account_manager);
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

    g_hash_table_destroy (priv->accounts);
    g_hash_table_destroy (priv->invalid_accounts);

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

    g_object_class_install_property (object_class, PROP_DBUS_DAEMON,
				     g_param_spec_object ("dbus-daemon",
							  _("DBus daemon"),
							  _("DBus daemon"),
							  TP_TYPE_DBUS_DAEMON,
							  G_PARAM_READWRITE |
							  G_PARAM_CONSTRUCT_ONLY));
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
					    NULL, g_object_unref);
    priv->invalid_accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
						    NULL, g_object_unref);

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

    /* add the interface properties */
    dbusprop_add_interface (TP_SVC_DBUS_PROPERTIES (account_manager),
			    MC_IFACE_ACCOUNT_MANAGER,
			    am_properties);
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

void
mcd_account_manager_write_conf (GKeyFile *keyfile)
{
    /* FIXME: this (reasonably) assumes that there is only one
     * McdAccountManager object running, since the write_conf_id is a static
     * variable */
    if (write_conf_id == 0) 
	write_conf_id = g_timeout_add (WRITE_CONF_DELAY,
				       write_conf, keyfile);
}

/* temporary function useful for McdPresenceFrame only */
GHashTable *
mcd_account_manager_get_valid_accounts (McdAccountManager *account_manager)
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

