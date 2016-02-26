/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright © 2007-2011 Nokia Corporation.
 * Copyright © 2009-2012 Collabora Ltd.
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

#include "mcd-account-manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "mcd-account-manager-priv.h"
#include "mcd-storage.h"

#include "mcd-account.h"
#include "mcd-account-config.h"
#include "mcd-account-priv.h"
#include "mcd-connection-priv.h"
#include "mcd-dbusprop.h"
#include "mcd-master-priv.h"
#include "mcd-misc.h"
#include "mcd-storage.h"
#include "mission-control-plugins/mission-control-plugins.h"
#include "mission-control-plugins/implementation.h"
#include "plugin-loader.h"

#include "_gen/interfaces.h"

#define PARAM_PREFIX "param-"
#define WRITE_CONF_DELAY    500

#define MCD_ACCOUNT_MANAGER_PRIV(account_manager) \
    (MCD_ACCOUNT_MANAGER (account_manager)->priv)

static void account_manager_iface_init (TpSvcAccountManagerClass *iface,
					gpointer iface_data);
static void account_manager_hidden_iface_init (
    McSvcAccountManagerInterfaceHiddenClass *iface,
    gpointer iface_data);
static void properties_iface_init (TpSvcDBusPropertiesClass *iface,
				   gpointer iface_data);

static void _mcd_account_manager_constructed (GObject *obj);

static const McdDBusProp account_manager_properties[];
static const McdDBusProp account_manager_hidden_properties[];

static const McdInterfaceData account_manager_interfaces[] = {
    MCD_IMPLEMENT_IFACE (tp_svc_account_manager_get_type,
			 account_manager,
			 TP_IFACE_ACCOUNT_MANAGER),
    MCD_IMPLEMENT_IFACE (mc_svc_account_manager_interface_hidden_get_type,
			 account_manager_hidden,
			 MC_IFACE_ACCOUNT_MANAGER_INTERFACE_HIDDEN),
    { NULL, }
};

G_DEFINE_TYPE_WITH_CODE (McdAccountManager, mcd_account_manager, G_TYPE_OBJECT,
			 MCD_DBUS_INIT_INTERFACES (account_manager_interfaces);
			 G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
						properties_iface_init);
			)

struct _McdAccountManagerPrivate
{
    TpDBusDaemon *dbus_daemon;
    TpSimpleClientFactory *client_factory;
    McdConnectivityMonitor *minotaur;

    McdStorage *storage;
    GHashTable *accounts;

    gchar *account_connections_dir;  /* directory for temporary file */
    gchar *account_connections_file; /* in account_connections_dir */

    gboolean dbus_registered;
};

typedef struct
{
    McdAccountManager *account_manager;
    McpAccountStorage *storage_plugin;
    McdAccount *account;
    gint account_lock;
} McdLoadAccountsData;

typedef struct
{
    McdAccountManager *account_manager;
    GHashTable *parameters;
    GHashTable *properties;
    McdGetAccountCb callback;
    gpointer user_data;
    GDestroyNotify destroy;

    gboolean ok;
    GError *error;
} McdCreateAccountData;

typedef struct
{
    McdAccount *account;
    gchar *key;
} McdAlterAccountData;

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
    PROP_CLIENT_FACTORY
};

static guint write_conf_id = 0;

static void register_dbus_service (McdAccountManager *account_manager);

static void release_load_accounts_lock (McdLoadAccountsData *lad);
static void add_account (McdAccountManager *manager, McdAccount *account,
    const gchar *source);
static void account_loaded (McdAccount *account,
                            const GError *error,
                            gpointer user_data);

/* calback chain for asynchronously updates from backends: */
static void
async_altered_validity_cb (McdAccount *account, const GError *invalid_reason, gpointer data)
{
    DEBUG ("asynchronously altered account %s is %svalid",
           mcd_account_get_unique_name (account), (invalid_reason == NULL) ? "" : "in");

    g_object_unref (account);
}

static void
async_altered_manager_cb (McdManager *cm, const GError *error, gpointer data)
{
    McdAccount *account = data;
    const gchar *name = NULL;

    if (cm != NULL)
        name = mcd_manager_get_name (cm);

    if (error != NULL)
        DEBUG ("manager %s not ready: %s", name, error->message);
    else
        DEBUG ("manager %s is ready", name);

    /* this triggers the final parameter check which results in dbus signals *
     * being fired and (potentially) the account going online automatically  */
    mcd_account_check_validity (account, async_altered_validity_cb, NULL);

    g_object_unref (cm);
}

/* account has been updated by a third party, and the McpAccountStorage *
 * plugin has just informed us of this fact                             */
static void
altered_cb (GObject *storage, const gchar *name, gpointer data)
{
    McdAccountManager *am = MCD_ACCOUNT_MANAGER (data);
    McdMaster *master = mcd_master_get_default ();
    McdAccount *account = NULL;
    McdManager *cm = NULL;
    const gchar *cm_name = NULL;

    account = mcd_account_manager_lookup_account (am, name);

    if (G_UNLIKELY (!account))
    {
        g_warning ("%s: account %s does not exist", G_STRFUNC, name);
        return;
    }

    /* in theory, the CM is already ready by this point, but make sure: */
    cm_name = mcd_account_get_manager_name (account);

    if (cm_name != NULL)
        cm = _mcd_master_lookup_manager (master, cm_name);

    if (cm != NULL)
    {
        g_object_ref (cm);
        g_object_ref (account);
        mcd_manager_call_when_ready (cm, async_altered_manager_cb, account);
    }
}

static void
async_altered_one_manager_cb (McdManager *cm,
                              const GError *error,
                              gpointer data)
{
    McdAlterAccountData *altered = data;
    const gchar *name = NULL;

    if (cm != NULL)
        name = mcd_manager_get_name (cm);

    if (error != NULL)
        DEBUG ("manager %s not ready: %s", name, error->message);
    else
        DEBUG ("manager %s is ready", name);

    /* this triggers the final parameter check which results in dbus signals *
     * being fired and (potentially) the account going online automatically  */
    mcd_account_altered_by_plugin (altered->account, altered->key);

    g_object_unref (cm);
    g_object_unref (altered->account);
    g_free (altered->key);
    g_slice_free (McdAlterAccountData, altered);
}


static void
altered_one_cb (GObject *storage,
                const gchar *account_name,
                const gchar *key,
                gpointer data)
{
    McdAccountManager *am = MCD_ACCOUNT_MANAGER (data);
    McdMaster *master = mcd_master_get_default ();
    McdAccount *account = NULL;
    McdManager *cm = NULL;
    const gchar *cm_name = NULL;

    account = mcd_account_manager_lookup_account (am, account_name);

    if (G_UNLIKELY (!account))
    {
        g_warning ("%s: account %s does not exist", G_STRFUNC, account_name);
        return;
    }

    /* in theory, the CM is already ready by this point, but make sure: */
    cm_name = mcd_account_get_manager_name (account);

    if (cm_name != NULL)
        cm = _mcd_master_lookup_manager (master, cm_name);

    if (cm != NULL)
    {
        McdAlterAccountData *altered = g_slice_new0 (McdAlterAccountData);

        g_object_ref (cm);
        altered->account = g_object_ref (account);
        altered->key = g_strdup (key);

        mcd_manager_call_when_ready (cm, async_altered_one_manager_cb, altered);
    }
}

/* callbacks for the various stages in an backend-driven account creation */
static void
async_created_validity_cb (McdAccount *account, const GError *invalid_reason, gpointer data)
{
    DEBUG ("asynchronously created account %s is %svalid",
           mcd_account_get_unique_name (account), (invalid_reason == NULL) ? "" : "in");

    /* safely cached in the accounts hash by now */
    g_object_unref (account);
}

static void
async_created_manager_cb (McdManager *cm, const GError *error, gpointer data)
{
    McdLoadAccountsData *lad = data;
    McdAccount *account = lad->account;
    McdAccountManager *am = lad->account_manager;
    McpAccountStorage *plugin = lad->storage_plugin;
    const gchar *name = NULL;

    if (cm != NULL)
        name = mcd_manager_get_name (cm);

    if (error != NULL)
        DEBUG ("manager %s not ready: %s", name, error->message);
    else
        DEBUG ("manager %s is ready", name);

    /* this takes a ref to the account and stores it in the accounts hash */
    add_account (am, account, mcp_account_storage_name (plugin));

    /* this will free the McdLoadAccountsData, don't use it after this */
    _mcd_account_load (account, account_loaded, lad);

    /* this triggers the final parameter check which results in dbus signals *
     * being fired and (potentially) the account going online automatically  */
    mcd_account_check_validity (account, async_created_validity_cb, NULL);

    g_object_unref (cm);
}

/* account created by an McpAccountStorage plugin after the initial setup   *
 * since the plugin does not have our cache, we need to poke the plugin     *
 * to fetch the named account explicitly at this point (ie it's a read, not *
 * not a write, from the plugin's POV:                                      */
static void
created_cb (GObject *storage_plugin_obj,
    const gchar *name,
    gpointer data)
{
    McpAccountStorage *plugin = MCP_ACCOUNT_STORAGE (storage_plugin_obj);
    McdAccountManager *am = MCD_ACCOUNT_MANAGER (data);
    McdAccountManagerPrivate *priv = MCD_ACCOUNT_MANAGER_PRIV (am);
    McdLoadAccountsData *lad = g_slice_new (McdLoadAccountsData);
    McdAccount *account = NULL;
    McdStorage *storage = priv->storage;
    McdMaster *master = mcd_master_get_default ();
    McdManager *cm = NULL;
    const gchar *cm_name = NULL;

    lad->account_manager = am;
    lad->storage_plugin = plugin;
    lad->account_lock = 1; /* will be released at the end of this function */

    /* actually fetch the data into our cache from the plugin: */
    if (mcd_storage_add_account_from_plugin (storage, plugin, name))
    {
        account = mcd_account_new (am, name, priv->minotaur);
        lad->account = account;
    }
    else
    {
        /* that function already warned about it */
        goto finish;
    }

    if (G_UNLIKELY (!account))
    {
        g_warning ("%s: account %s failed to instantiate", G_STRFUNC, name);
        goto finish;
    }

    cm_name = mcd_account_get_manager_name (account);

    if (cm_name != NULL)
        cm = _mcd_master_lookup_manager (master, cm_name);

    if (cm != NULL)
    {
        lad->account_lock++;
        g_object_ref (cm);
        mcd_manager_call_when_ready (cm, async_created_manager_cb, lad);
    }
    else
    {
        /* account is well and truly broken. forget it even existed: */
        g_warning ("%s: account %s has no manager, ignoring it",
                   G_STRFUNC, name);
        g_object_unref (account);
    }

finish:
    release_load_accounts_lock (lad);
}

static void
toggled_cb (GObject *plugin, const gchar *name, gboolean on, gpointer data)
{
  McpAccountStorage *storage_plugin = MCP_ACCOUNT_STORAGE (plugin);
  McdAccountManager *manager = MCD_ACCOUNT_MANAGER (data);
  McdAccount *account = NULL;
  GError *error = NULL;

  account = mcd_account_manager_lookup_account (manager, name);

  DEBUG ("%s plugin reports %s became %sabled",
      mcp_account_storage_name (storage_plugin), name, on ? "en" : "dis");

  if (account == NULL)
    {
      g_warning ("%s: Unknown account %s from %s plugin",
          G_STRFUNC, name, mcp_account_storage_name (storage_plugin));
      return;
    }

  _mcd_account_set_enabled (account, on, FALSE,
                            MCD_DBUS_PROP_SET_FLAG_NONE, &error);

  if (error != NULL)
    {
      g_warning ("Error setting Enabled for %s: %s", name, error->message);
      g_clear_error (&error);
    }
}

static void
reconnect_cb (GObject *plugin, const gchar *name, gpointer data)
{
  McpAccountStorage *storage_plugin = MCP_ACCOUNT_STORAGE (plugin);
  McdAccountManager *manager = MCD_ACCOUNT_MANAGER (data);
  McdAccount *account = NULL;

  account = mcd_account_manager_lookup_account (manager, name);

  DEBUG ("%s plugin request %s reconnection",
      mcp_account_storage_name (storage_plugin), name);

  if (account == NULL)
    {
      g_warning ("%s: Unknown account %s from %s plugin",
          G_STRFUNC, name, mcp_account_storage_name (storage_plugin));
      return;
    }

  /* Storage ask to reconnect when important parameters changed, which is an
   * user action. */
  _mcd_account_reconnect (account, TRUE);
}

static void
_mcd_account_delete_cb (McdAccount *account, const GError *error, gpointer data)
{
    /* no need to do anything other than release the account ref, which *
     * should be the last ref we hold by the time this rolls arouns:    */
    g_object_unref (account);
}

/* a backend plugin notified us that an account was vaporised: remove it */
static void
deleted_cb (GObject *plugin, const gchar *name, gpointer data)
{
    McpAccountStorage *storage_plugin = MCP_ACCOUNT_STORAGE (plugin);
    McdAccountManager *manager = MCD_ACCOUNT_MANAGER (data);
    McdAccount *account = NULL;

    account = g_hash_table_lookup (manager->priv->accounts, name);

    DEBUG ("%s reported deletion of %s (%p)",
           mcp_account_storage_name (storage_plugin), name, account);

    if (account != NULL)
    {
        const gchar * object_path = mcd_account_get_object_path (account);

        g_object_ref (account);
        /* this unhooks the account's signal handlers */
        g_hash_table_remove (manager->priv->accounts, name);
        tp_svc_account_manager_emit_account_removed (manager, object_path);
        mcd_account_delete (account, _mcd_account_delete_cb, NULL);
    }
}

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

        if (line + len == tab1 &&
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

    master = mcd_master_get_default ();
    g_return_val_if_fail (MCD_IS_MASTER (master), FALSE);

    object_path = g_strdelimit (g_strdup_printf ("/%s", name), ".", '/');
    if (!get_account_connection (file_contents, object_path,
                                 &bus_name, &account_name))
        goto err_match;

    account = g_hash_table_lookup (account_manager->priv->accounts,
                                   account_name);
    if (!account || !mcd_account_is_enabled (account))
        goto err_account;

    DEBUG ("account is %s", mcd_account_get_unique_name (account));
    manager_name = mcd_account_get_manager_name (account);

    manager = _mcd_master_lookup_manager (master, manager_name);
    if (G_UNLIKELY (!manager))
    {
        DEBUG ("Manager %s not found", manager_name);
        goto err_manager;
    }

    connection = mcd_manager_create_connection (manager, account);
    if (G_UNLIKELY (!connection)) goto err_connection;

    _mcd_connection_set_tp_connection (connection, bus_name, object_path,
                                       &error);
    if (G_UNLIKELY (error))
    {
        DEBUG ("got error: %s", error->message);
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

    DEBUG ("%" G_GSIZE_FORMAT " connections", n);

    /* if the file has no contents, we don't really care why */
    if (!g_file_get_contents (priv->account_connections_file, &contents,
                              NULL, NULL))
    {
        contents = NULL;
    }

    for (i = 0; i < n; i++)
    {
        g_return_if_fail (names[i] != NULL);
        DEBUG ("Connection %s", names[i]);
        if (!recover_connection (account_manager, contents, names[i]))
        {
            /* Close the connection */
            TpConnection *proxy;
            gchar *path;

            path = g_strdup_printf ("/%s", names[i]);
            g_strdelimit (path, ".", '/');

            DEBUG ("Killing connection");
            proxy = tp_simple_client_factory_ensure_connection (
                priv->client_factory, path, NULL, NULL);

            if (proxy)
            {
                tp_cli_connection_call_disconnect (proxy, -1, NULL, NULL,
                                                   NULL, NULL);
                g_object_unref (proxy);
            }

            g_free (path);
        }
    }
    g_free (contents);
}

static void
on_account_validity_changed (McdAccount *account, gboolean valid,
			     McdAccountManager *account_manager)
{
    const gchar *object_path;

    object_path = mcd_account_get_object_path (account);

    if (_mcd_account_is_hidden (account))
    {
        mc_svc_account_manager_interface_hidden_emit_hidden_account_validity_changed (
            account_manager, object_path, valid);
    }
    else
    {
        tp_svc_account_manager_emit_account_validity_changed (account_manager,
                                                              object_path,
                                                              valid);
    }
}

static void
on_account_removed (McdAccount *account, McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    McdStorage *storage = priv->storage;
    const gchar *name, *object_path;

    object_path = mcd_account_get_object_path (account);

    if (_mcd_account_is_hidden (account))
    {
        mc_svc_account_manager_interface_hidden_emit_hidden_account_removed (
            account_manager, object_path);
    }
    else
    {
        tp_svc_account_manager_emit_account_removed (account_manager,
                                                     object_path);
    }

    name = mcd_account_get_unique_name (account);
    g_hash_table_remove (priv->accounts, name);

    mcd_storage_delete_account (storage, name);
    mcd_account_manager_write_conf_async (account_manager, account, NULL,
                                          NULL);
}

static inline void
disconnect_signal (gpointer instance, gpointer func)
{
    g_signal_handlers_disconnect_matched (instance,
                                          G_SIGNAL_MATCH_FUNC,
                                          0, 0, NULL, func, NULL);
}

static void
unref_account (gpointer data)
{
    McdAccount *account = MCD_ACCOUNT (data);

    DEBUG ("called for %s", mcd_account_get_unique_name (account));

    disconnect_signal (account, on_account_validity_changed);
    disconnect_signal (account, on_account_removed);

    g_object_unref (account);
}

static void _mcd_account_manager_store_account_connections (
    McdAccountManager *);

static void
add_account (McdAccountManager *account_manager, McdAccount *account,
    const gchar *source)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    McdAccount *existing;
    const gchar *name;

    name = mcd_account_get_unique_name (account);
    DEBUG ("adding account %s (%p) from %s", name, account, source);

    existing = mcd_account_manager_lookup_account (account_manager, name);
    if (existing != NULL)
    {
        g_warning ("...but we already have an account %p with that name!", existing);
    }

    g_hash_table_insert (priv->accounts, (gchar *)name,
                         g_object_ref (account));

    /* if we have to connect to any signals from the account object, this is
     * the place to do it */
    g_signal_connect (account, "validity-changed",
		      G_CALLBACK (on_account_validity_changed),
		      account_manager);
    g_signal_connect (account, "removed", G_CALLBACK (on_account_removed),
		      account_manager);
    tp_g_signal_connect_object (account, "connection-path-changed",
        G_CALLBACK (_mcd_account_manager_store_account_connections),
        account_manager, G_CONNECT_SWAPPED);

    /* some reports indicate this doesn't always fire for async backend  *
     * accounts: testing here hasn't shown this, but at least we will be *
     * able to tell if this happens from MC debug logs now:              */
    DEBUG ("account %s validity: %d", name, mcd_account_is_valid (account));
    /* if the account is already valid, synthesize a signal indicating that
     * it's been added */
    if (mcd_account_is_valid (account))
        on_account_validity_changed (account, TRUE, account_manager);
}

static void
mcd_create_account_data_free (McdCreateAccountData *cad)
{
    g_hash_table_unref (cad->parameters);
    tp_clear_pointer (&cad->properties, g_hash_table_unref);

    if (G_UNLIKELY (cad->error))
        g_error_free (cad->error);

    g_slice_free (McdCreateAccountData, cad);
}

static gboolean
set_new_account_properties (McdAccount *account,
                            GHashTable *properties,
                            GError **error)
{
    GHashTableIter iter;
    gpointer key, value;
    gboolean ok = TRUE;

    g_hash_table_iter_init (&iter, properties);

    while (g_hash_table_iter_next (&iter, &key, &value) && ok)
    {
        gchar *name = key;
        gchar *dot, *iface, *pname;

        if ((dot = strrchr (name, '.')) != NULL)
        {
            iface = g_strndup (name, dot - name);
            pname = dot + 1;
            ok = mcd_dbusprop_set_property (TP_SVC_DBUS_PROPERTIES (account),
                                            iface, pname, value, error);
            g_free (iface);
        }
        else
        {
            g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                         "Malformed property name: %s", name);
            ok = FALSE;
        }
    }

    return ok;
}

static void
complete_account_creation_finish (McdAccount *account,
                                  McdCreateAccountData *cad)
{
    McdAccountManager *account_manager = cad->account_manager;

    if (!cad->ok)
    {
        mcd_account_delete (account, NULL, NULL);
        tp_clear_object (&account);
    }

    mcd_account_manager_write_conf_async (account_manager, account, NULL,
                                          NULL);

    if (cad->callback != NULL)
        cad->callback (account_manager, account, cad->error, cad->user_data);
    mcd_create_account_data_free (cad);

    tp_clear_object (&account);
}

static void
complete_account_creation_check_validity_cb (McdAccount *account,
                                             const GError *invalid_reason,
                                             gpointer user_data)
{
    McdCreateAccountData *cad = user_data;

    if (invalid_reason != NULL)
    {
        cad->ok = FALSE;
        g_set_error_literal (&cad->error, invalid_reason->domain,
            invalid_reason->code, invalid_reason->message);
    }

    complete_account_creation_finish (account, cad);
}

static void
complete_account_creation_set_cb (McdAccount *account, GPtrArray *not_yet,
                                  const GError *set_error, gpointer user_data)
{
    McdCreateAccountData *cad = user_data;
    McdAccountManager *account_manager;
    cad->ok = TRUE;

    account_manager = cad->account_manager;

    if (set_error != NULL)
    {
        cad->ok = FALSE;
        g_set_error_literal (&cad->error, set_error->domain, set_error->code,
                             set_error->message);
    }

    if (cad->ok && cad->properties != NULL)
    {
        cad->ok = set_new_account_properties (account, cad->properties, &cad->error);
    }

    if (cad->ok)
    {
        add_account (account_manager, account, G_STRFUNC);
        mcd_account_check_validity (account, complete_account_creation_check_validity_cb, cad);
    }
    else
    {
        complete_account_creation_finish (account, cad);
    }
}

static void
complete_account_creation (McdAccount *account,
                           const GError *cb_error,
                           gpointer user_data)
{
    McdCreateAccountData *cad = user_data;
    McdAccountManager *account_manager;

    account_manager = cad->account_manager;
    if (G_UNLIKELY (cb_error))
    {
        cad->callback (account_manager, account, cb_error, cad->user_data);
        mcd_create_account_data_free (cad);
        return;
    }

    _mcd_account_set_parameters (account, cad->parameters, NULL,
                                 complete_account_creation_set_cb,
                                 cad);
}

void
_mcd_account_manager_create_account (McdAccountManager *account_manager,
                                     const gchar *manager,
                                     const gchar *protocol,
                                     const gchar *display_name,
                                     GHashTable *params,
                                     GHashTable *properties,
                                     McdGetAccountCb callback,
                                     gpointer user_data,
                                     GDestroyNotify destroy)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    McdStorage *storage = priv->storage;
    McdCreateAccountData *cad;
    McdAccount *account;
    gchar *unique_name = NULL;
    const gchar *provider;
    GError *e = NULL;

    DEBUG ("called");
    if (G_UNLIKELY (manager == NULL || manager[0] == 0 ||
                    protocol == NULL || protocol[0] == 0))
    {
        GError error = { TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
            "Invalid parameters"};
        callback (account_manager, NULL, &error, user_data);
        if (destroy)
            destroy (user_data);
        return;
    }

    provider = tp_asv_get_string (properties,
                                  TP_PROP_ACCOUNT_INTERFACE_STORAGE_STORAGE_PROVIDER);

    unique_name = mcd_storage_create_account (storage, provider,
                                              manager, protocol, params,
                                              &e);

    if (unique_name == NULL)
    {
        callback (account_manager, NULL, e, user_data);
        g_clear_error (&e);
        if (destroy)
            destroy (user_data);
        return;
    }

    /* create the basic account keys */
    mcd_storage_set_string (storage, unique_name,
                            MC_ACCOUNTS_KEY_MANAGER, manager);
    mcd_storage_set_string (storage, unique_name,
                            MC_ACCOUNTS_KEY_PROTOCOL, protocol);

    if (display_name != NULL)
        mcd_storage_set_string (storage, unique_name,
                                MC_ACCOUNTS_KEY_DISPLAY_NAME, display_name);

    account = mcd_account_new (account_manager, unique_name, priv->minotaur);
    g_free (unique_name);

    if (G_LIKELY (account))
    {
        cad = g_slice_new (McdCreateAccountData);
        cad->account_manager = account_manager;
        cad->parameters = g_hash_table_ref (params);
        cad->properties = (properties ? g_hash_table_ref (properties) : NULL);
        cad->callback = callback;
        cad->user_data = user_data;
        cad->destroy = destroy;
        cad->error = NULL;
        _mcd_account_load (account, complete_account_creation, cad);
    }
    else
    {
        GError error = { TP_ERROR, TP_ERROR_NOT_AVAILABLE, "" };
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
    tp_svc_account_manager_return_from_create_account (context, object_path);
}

static void
account_manager_create_account (TpSvcAccountManager *self,
                                const gchar *manager,
                                const gchar *protocol,
                                const gchar *display_name,
                                GHashTable *parameters,
                                GHashTable *properties,
                                DBusGMethodInvocation *context)
{
    _mcd_account_manager_create_account (MCD_ACCOUNT_MANAGER (self),
                                         manager, protocol, display_name,
                                         parameters, properties,
                                         create_account_cb, context, NULL);
}

static void
account_manager_iface_init (TpSvcAccountManagerClass *iface,
			    gpointer iface_data)
{
#define IMPLEMENT(x) tp_svc_account_manager_implement_##x (\
    iface, account_manager_##x)
    IMPLEMENT(create_account);
#undef IMPLEMENT
}

static void
account_manager_hidden_iface_init (
    McSvcAccountManagerInterfaceHiddenClass *iface,
    gpointer iface_data)
{
}

static void
accounts_to_gvalue (GHashTable *accounts, gboolean valid, gboolean hidden,
                    GValue *value)
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
        if (mcd_account_is_valid (account) == valid &&
            _mcd_account_is_hidden (account) == hidden)
        {
            g_ptr_array_add (account_array,
                             g_strdup (mcd_account_get_object_path (account)));
        }
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

    DEBUG ("called");
    accounts_to_gvalue (priv->accounts, TRUE, FALSE, value);
}

static void
get_invalid_accounts (TpSvcDBusProperties *self, const gchar *name,
		      GValue *value)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (self);
    McdAccountManagerPrivate *priv = account_manager->priv;

    DEBUG ("called");
    accounts_to_gvalue (priv->accounts, FALSE, FALSE, value);
}

static void
get_supported_account_properties (TpSvcDBusProperties *svc,
                                  const gchar *name,
                                  GValue *value)
{
    static const gchar * const supported[] = {
        TP_IFACE_ACCOUNT ".AutomaticPresence",
        TP_IFACE_ACCOUNT ".Enabled",
        TP_IFACE_ACCOUNT ".Icon",
        TP_IFACE_ACCOUNT ".Nickname",
        TP_IFACE_ACCOUNT ".ConnectAutomatically",
        TP_IFACE_ACCOUNT ".RequestedPresence",
        TP_IFACE_ACCOUNT ".Supersedes",
        TP_PROP_ACCOUNT_SERVICE,
        TP_IFACE_ACCOUNT_INTERFACE_AVATAR ".Avatar",
        MC_IFACE_ACCOUNT_INTERFACE_CONDITIONS ".Condition",
        TP_PROP_ACCOUNT_INTERFACE_STORAGE_STORAGE_PROVIDER,
        MC_IFACE_ACCOUNT_INTERFACE_HIDDEN ".Hidden",
        NULL
    };

    g_value_init (value, G_TYPE_STRV);
    g_value_set_static_boxed (value, supported);
}

static const McdDBusProp account_manager_properties[] = {
    { "ValidAccounts", NULL, get_valid_accounts },
    { "InvalidAccounts", NULL, get_invalid_accounts },
    { "Interfaces", NULL, mcd_dbus_get_interfaces },
    { "SupportedAccountProperties", NULL, get_supported_account_properties },
    { 0 },
};

static void
get_valid_hidden_accounts (TpSvcDBusProperties *self, const gchar *name,
                           GValue *value)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (self);
    McdAccountManagerPrivate *priv = account_manager->priv;

    accounts_to_gvalue (priv->accounts, TRUE, TRUE, value);
}

static void
get_invalid_hidden_accounts (TpSvcDBusProperties *self, const gchar *name,
                             GValue *value)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (self);
    McdAccountManagerPrivate *priv = account_manager->priv;

    accounts_to_gvalue (priv->accounts, FALSE, TRUE, value);
}

static const McdDBusProp account_manager_hidden_properties[] = {
    { "ValidHiddenAccounts", NULL, get_valid_hidden_accounts },
    { "InvalidHiddenAccounts", NULL, get_invalid_hidden_accounts },
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

static gboolean
write_conf (gpointer userdata)
{
    McdStorage *storage = MCD_STORAGE (userdata);

    DEBUG ("called");
    g_source_remove (write_conf_id);
    write_conf_id = 0;

    mcd_storage_commit (storage, NULL);

    return TRUE;
}

static void
release_load_accounts_lock (McdLoadAccountsData *lad)
{
    g_return_if_fail (lad->account_lock > 0);
    lad->account_lock--;
    DEBUG ("called, count is now %d", lad->account_lock);

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

static void
uncork_storage_plugins (McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv = MCD_ACCOUNT_MANAGER_PRIV (account_manager);

    mcd_account_manager_write_conf_async (account_manager, NULL, NULL, NULL);
    mcd_storage_ready (priv->storage);
}

typedef struct
{
    McdAccountManager *self;
    McdAccount *account;
    McdLoadAccountsData *lad;
} MigrateCtx;

static MigrateCtx *
migrate_ctx_new (McdAccountManager *self,
                 McdAccount *account,
                 McdLoadAccountsData *lad)
{
    MigrateCtx *ctx = g_slice_new (MigrateCtx);

    ctx->self = g_object_ref (self);
    ctx->account = g_object_ref (account);
    ctx->lad = lad;

    /* Lock attempting to migrate the account */
    lad->account_lock++;
    return ctx;
}

static void
migrate_ctx_free (MigrateCtx *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->account);
    release_load_accounts_lock (ctx->lad);
    g_slice_free (MigrateCtx, ctx);
}


static void
migrate_delete_account_cb (McdAccount *account,
                           const GError *error,
                           gpointer user_data)
{
    MigrateCtx *ctx = user_data;

    migrate_ctx_free (ctx);
}

static void
migrate_create_account_cb (McdAccountManager *account_manager,
                           McdAccount *account,
                           const GError *error,
                           gpointer user_data)
{
    MigrateCtx *ctx = user_data;

    if (error != NULL)
    {
        DEBUG ("Failed to create account: %s", error->message);
        _mcd_account_set_enabled (ctx->account, FALSE, TRUE,
                                  MCD_DBUS_PROP_SET_FLAG_NONE, NULL);
        migrate_ctx_free (ctx);
        return;
    }

    DEBUG ("Account %s migrated, removing it",
           mcd_account_get_unique_name (ctx->account));

    mcd_account_delete (ctx->account, migrate_delete_account_cb, ctx);
}

static void
migrate_butterfly_haze_ready (McdManager *manager,
                              const GError *error,
                              gpointer user_data)
{
    MigrateCtx *ctx = user_data;
    gchar *display_name;
    GValue v = G_VALUE_INIT;
    GValue password_v = G_VALUE_INIT;
    GHashTable *parameters, *properties;
    gchar *str;
    GPtrArray *supersedes;
    GPtrArray *old_supersedes;

    if (error != NULL)
    {
        DEBUG ("Can't find Haze: %s", error->message);
        _mcd_account_set_enabled (ctx->account, FALSE, TRUE,
                                  MCD_DBUS_PROP_SET_FLAG_NONE, NULL);
        goto error;
    }

    /* Parameters; the only mandatory one is 'account' */
    if (!mcd_account_get_parameter_of_known_type (ctx->account,
                                                  "account", G_TYPE_STRING,
                                                  &v, NULL))
    {
        _mcd_account_set_enabled (ctx->account, FALSE, TRUE,
                                  MCD_DBUS_PROP_SET_FLAG_NONE, NULL);
        goto error;
    }

    parameters = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_insert (parameters, "account", &v);

    /* If MC is storing the password, let's copy that too, so Empathy
     * can migrate it somewhere better. */
    if (mcd_account_get_parameter_of_known_type (ctx->account,
                                                 "password", G_TYPE_STRING,
                                                 &password_v, NULL))
    {
        g_hash_table_insert (parameters, "password", &password_v);
    }

    display_name = mcd_account_dup_display_name (ctx->account);

    /* Properties */
    properties = tp_asv_new (NULL, NULL);

    str = mcd_account_dup_icon (ctx->account);
    if (str != NULL)
        tp_asv_take_string (properties, TP_PROP_ACCOUNT_ICON, str);

    tp_asv_set_boolean (properties, TP_PROP_ACCOUNT_ENABLED,
                        mcd_account_is_enabled (ctx->account));

    str = mcd_account_dup_nickname (ctx->account);
    if (str != NULL)
        tp_asv_take_string (properties, TP_PROP_ACCOUNT_NICKNAME, str);

    supersedes = g_ptr_array_new ();
    old_supersedes = _mcd_account_get_supersedes (ctx->account);

    if (old_supersedes != NULL)
    {
        guint i;

        for (i = 0; i < old_supersedes->len; i++)
            g_ptr_array_add (supersedes,
                             g_strdup (g_ptr_array_index (old_supersedes, i)));
    }

    g_ptr_array_add (supersedes,
                     g_strdup (mcd_account_get_object_path (ctx->account)));
    tp_asv_take_boxed (properties, TP_PROP_ACCOUNT_SUPERSEDES,
                       TP_ARRAY_TYPE_OBJECT_PATH_LIST, supersedes);

    /* Set the service while we're on it */
    tp_asv_set_string (properties, TP_PROP_ACCOUNT_SERVICE, "windows-live");

    _mcd_account_manager_create_account (ctx->self,
                                       "haze", "msn", display_name,
                                       parameters, properties,
                                       migrate_create_account_cb, ctx, NULL);

    g_value_unset (&v);
    g_free (display_name);
    g_hash_table_unref (parameters);
    g_hash_table_unref (properties);
    return;

error:
    migrate_ctx_free (ctx);
}

static void
butterfly_account_loaded (McdAccount *account,
                          const GError *error,
                          gpointer user_data)
{
    MigrateCtx *ctx = user_data;
    McdMaster *master = mcd_master_get_default ();
    McdManager *manager;

    if (error != NULL)
        goto error;

    DEBUG ("Try migrating butterfly account %s",
           mcd_account_get_unique_name (account));

    /* Check if Haze is installed */
    manager = _mcd_master_lookup_manager (master, "haze");
    if (manager == NULL)
    {
        DEBUG ("Can't find Haze");
        _mcd_account_set_enabled (account, FALSE, TRUE,
                                  MCD_DBUS_PROP_SET_FLAG_NONE, NULL);
        goto error;
    }

    mcd_manager_call_when_ready (manager, migrate_butterfly_haze_ready, ctx);
    return;

error:
    migrate_ctx_free (ctx);
}

static void
migrate_butterfly_account (McdAccountManager *self,
                           McdAccount *account,
                           McdLoadAccountsData *lad)
{
    MigrateCtx *ctx;

    ctx = migrate_ctx_new (self, account, lad);

    _mcd_account_load (account, butterfly_account_loaded, ctx);
}

/* Migrate some specific type of account. If something went wrong during the
 * migration we disable it. */
static void
migrate_accounts (McdAccountManager *self,
                  McdLoadAccountsData *lad)
{
    McdAccountManagerPrivate *priv = self->priv;
    GHashTableIter iter;
    gpointer v;

    g_hash_table_iter_init (&iter, priv->accounts);
    while (g_hash_table_iter_next (&iter, NULL, &v))
    {
        McdAccount *account = v;
        TpConnectionManager *cm;

        cm = mcd_account_get_cm (account);

        if (cm == NULL)
            continue;

        if (!tp_strdiff (tp_connection_manager_get_name (cm), "butterfly"))
            migrate_butterfly_account (self, account, lad);
    }
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
    McdStorage *storage = priv->storage;
    McdLoadAccountsData *lad;
    gchar **accounts, **name;
    GHashTableIter iter;
    gpointer v;

    tp_list_connection_names (priv->dbus_daemon,
                              list_connection_names_cb, NULL, NULL,
                              (GObject *)account_manager);

    lad = g_slice_new (McdLoadAccountsData);
    lad->account_manager = account_manager;
    lad->account_lock = 1; /* will be released at the end of this function */

    accounts = mcd_storage_dup_accounts (storage, NULL);

    for (name = accounts; *name != NULL; name++)
    {
        gboolean plausible = FALSE;
        const gchar *manager = NULL;
        const gchar *protocol = NULL;
        McdAccount *account = mcd_account_manager_lookup_account (
            account_manager, *name);

        if (account != NULL)
        {
            /* FIXME: this shouldn't really happen */
            DEBUG ("already have account %p called '%s'; skipping", account, *name);
            continue;
        }

        account = mcd_account_new (account_manager, *name, priv->minotaur);

        if (G_UNLIKELY (!account))
        {
            g_warning ("%s: account %s failed to instantiate", G_STRFUNC,
                       *name);
            continue;
        }

        manager = mcd_account_get_manager_name (account);
        protocol = mcd_account_get_protocol_name (account);

        plausible = !tp_str_empty (manager) && !tp_str_empty (protocol);

        if (G_UNLIKELY (!plausible))
        {
            const gchar *dbg_manager = (manager == NULL) ? "(nil)" : manager;
            const gchar *dbg_protocol = (protocol == NULL) ? "(nil)" : protocol;

            g_warning ("%s: account %s has implausible manager/protocol: %s/%s",
                       G_STRFUNC, *name, dbg_manager, dbg_protocol);
            g_object_unref (account);
            continue;
        }

        lad->account_lock++;
        add_account (lad->account_manager, account, "keyfile");
        _mcd_account_load (account, account_loaded, lad);
        g_object_unref (account);
    }
    g_strfreev (accounts);

    uncork_storage_plugins (account_manager);

    migrate_accounts (account_manager, lad);

    release_load_accounts_lock (lad);

    g_hash_table_iter_init (&iter, account_manager->priv->accounts);

    while (g_hash_table_iter_next (&iter, NULL, &v))
      _mcd_account_maybe_autoconnect (v);
}

static void
register_dbus_service (McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    GError *error = NULL;

    if (priv->dbus_registered)
        return;

    if (!tp_dbus_daemon_request_name (priv->dbus_daemon,
                                      TP_ACCOUNT_MANAGER_BUS_NAME,
                                      TRUE /* idempotent */, &error))
    {
        /* FIXME: put in proper error handling when MC gains the ability to
         * be the AM or the CD but not both */
        g_warning("Failed registering '%s' service: %s",
                  TP_ACCOUNT_MANAGER_BUS_NAME, error->message);
        g_error_free (error);
        exit (1);
    }

    priv->dbus_registered = TRUE;

    tp_dbus_daemon_register_object (priv->dbus_daemon,
                                    TP_ACCOUNT_MANAGER_OBJECT_PATH,
                                    account_manager);
}

static void
set_property (GObject *obj, guint prop_id,
	      const GValue *val, GParamSpec *pspec)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (obj);
    McdAccountManagerPrivate *priv = account_manager->priv;

    switch (prop_id)
    {
    case PROP_CLIENT_FACTORY:
        g_assert (priv->client_factory == NULL);  /* construct-only */
        priv->client_factory =
            TP_SIMPLE_CLIENT_FACTORY (g_value_dup_object (val));
        priv->dbus_daemon =
            tp_simple_client_factory_get_dbus_daemon (priv->client_factory);
        g_object_ref (priv->dbus_daemon);
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
    {
        write_conf (priv->storage);
        g_assert (write_conf_id == 0);
    }

    tp_clear_object (&priv->storage);
    g_free (priv->account_connections_dir);
    remove (priv->account_connections_file);
    g_free (priv->account_connections_file);

    g_hash_table_unref (priv->accounts);

    G_OBJECT_CLASS (mcd_account_manager_parent_class)->finalize (object);
}

static void
_mcd_account_manager_dispose (GObject *object)
{
    McdAccountManagerPrivate *priv = MCD_ACCOUNT_MANAGER_PRIV (object);

    tp_clear_object (&priv->dbus_daemon);
    tp_clear_object (&priv->client_factory);
    tp_clear_object (&priv->minotaur);

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
    object_class->constructed = _mcd_account_manager_constructed;

    g_object_class_install_property
        (object_class, PROP_DBUS_DAEMON,
         g_param_spec_object ("dbus-daemon", "DBus daemon", "DBus daemon",
                              TP_TYPE_DBUS_DAEMON,
                              G_PARAM_READABLE));

    g_object_class_install_property
        (object_class, PROP_CLIENT_FACTORY,
         g_param_spec_object ("client-factory",
                              "Client factory",
                              "Client factory",
                              TP_TYPE_SIMPLE_CLIENT_FACTORY,
                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
}

static const gchar *
get_connections_cache_dir (void)
{
    const gchar *from_env = g_getenv ("MC_ACCOUNT_DIR");

    if (from_env != NULL)
    {
        return from_env;
    }

    if ((ACCOUNTS_CACHE_DIR)[0] != '\0')
    {
        return ACCOUNTS_CACHE_DIR;
    }

    return g_get_user_cache_dir ();
}

static void
mcd_account_manager_init (McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE ((account_manager),
					MCD_TYPE_ACCOUNT_MANAGER,
					McdAccountManagerPrivate);
    account_manager->priv = priv;
}

static void
_mcd_account_manager_constructed (GObject *obj)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (obj);
    McdAccountManagerPrivate *priv = account_manager->priv;
    guint i = 0;
    static struct { const gchar *name; GCallback handler; } sig[] =
      { { "created", G_CALLBACK (created_cb) },
        { "altered", G_CALLBACK (altered_cb) },
        { "toggled", G_CALLBACK (toggled_cb) },
        { "deleted", G_CALLBACK (deleted_cb) },
        { "altered-one", G_CALLBACK (altered_one_cb) },
        { "reconnect", G_CALLBACK (reconnect_cb) },
        { NULL, NULL } };

    DEBUG ("");

    priv->minotaur = mcd_connectivity_monitor_new ();

    priv->storage = mcd_storage_new (priv->dbus_daemon);
    priv->accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            NULL, unref_account);

    priv->account_connections_dir = g_strdup (get_connections_cache_dir ());
    priv->account_connections_file =
        g_build_filename (priv->account_connections_dir, ".mc_connections",
                          NULL);

    DEBUG ("loading plugins");
    mcd_storage_load (priv->storage);

    /* hook up all the storage plugin signals to their handlers: */
    for (i = 0; sig[i].name != NULL; i++)
    {
        mcd_storage_connect_signal (sig[i].name, sig[i].handler,
                                    account_manager);
    }

    /* initializes the interfaces */
    mcd_dbus_init_interfaces_instances (account_manager);
}

McdAccountManager *
mcd_account_manager_new (TpSimpleClientFactory *client_factory)
{
    gpointer *obj;

    obj = g_object_new (MCD_TYPE_ACCOUNT_MANAGER,
                        "client-factory", client_factory,
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

McdConnectivityMonitor *
mcd_account_manager_get_connectivity_monitor (McdAccountManager *self)
{
  g_return_val_if_fail (MCD_IS_ACCOUNT_MANAGER (self), NULL);
  return self->priv->minotaur;
}

/**
 * McdAccountManagerWriteConfCb:
 * @account_manager: the #McdAccountManager
 * @error: a set #GError on failure or %NULL if there was no error
 * @user_data: user data
 *
 * The callback from mcd_account_manager_write_conf_async(). If the config
 * writing was successful, @error will be %NULL, otherwise it will be set
 * with the appropriate error.
 */

/**
 * mcd_account_manager_write_conf_async:
 * @account_manager: the #McdAccountManager
 * @account: the account to be written, or %NULL to flush all accounts
 * @callback: a callback to be called on write success or failure
 * @user_data: data to be passed to @callback
 *
 * Write the account manager configuration to disk.
 */
void
mcd_account_manager_write_conf_async (McdAccountManager *account_manager,
                                      McdAccount *account,
                                      McdAccountManagerWriteConfCb callback,
                                      gpointer user_data)
{
    McdStorage *storage = NULL;
    const gchar *account_name = NULL;

    g_return_if_fail (MCD_IS_ACCOUNT_MANAGER (account_manager));

    storage = account_manager->priv->storage;

    if (account != NULL)
    {
        account_name = mcd_account_get_unique_name (account);

        DEBUG ("updating %s", account_name);
        mcd_storage_commit (storage, account_name);
    }
    else
    {
        GStrv groups;
        gsize n_accounts = 0;

        groups = mcd_storage_dup_accounts (storage, &n_accounts);
        DEBUG ("updating all %" G_GSIZE_FORMAT " accounts", n_accounts);

        mcd_storage_commit (storage, NULL);

        g_strfreev (groups);
    }

    if (callback != NULL)
        callback (account_manager, NULL, user_data);
}

GHashTable *
_mcd_account_manager_get_accounts (McdAccountManager *account_manager)
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

McdAccount *
mcd_account_manager_lookup_account_by_path (McdAccountManager *account_manager,
					    const gchar *object_path)
{
    McdAccountManagerPrivate *priv = account_manager->priv;

    if (!g_str_has_prefix (object_path, TP_ACCOUNT_OBJECT_PATH_BASE))
    {
        /* can't possibly be right */
        return NULL;
    }

    return g_hash_table_lookup (priv->accounts,
        object_path + strlen (TP_ACCOUNT_OBJECT_PATH_BASE));
}

/*
 * _mcd_account_manager_store_account_connections:
 * @account_manager: the #McdAccountManager.
 *
 * This function is used to remember what connection an account was bound to.
 * The data is stored in a temporary file, and can be read when MC restarts
 * after a crash.
 */
static void
_mcd_account_manager_store_account_connections (McdAccountManager *manager)
{
    McdAccountManagerPrivate *priv;
    GHashTableIter iter;
    const gchar *account_name, *connection_path, *connection_name;
    McdAccount *account;
    FILE *file;

    g_return_if_fail (MCD_IS_ACCOUNT_MANAGER (manager));
    priv = manager->priv;

    /* make $XDG_CACHE_DIR (or whatever) if it doesn't exist */
    g_mkdir_with_parents (priv->account_connections_dir, 0700);
    _mcd_chmod_private (priv->account_connections_dir);

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

McdStorage *
mcd_account_manager_get_storage (McdAccountManager *account_manager)
{
    return account_manager->priv->storage;
}

