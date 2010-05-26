/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
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

#include "mcd-account-manager.h"

#include <glib/gi18n.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <config.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>

#include <libmcclient/mc-interfaces.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-account-manager.h>
#include "mcd-account-manager-priv.h"

/* these pseudo-plugins take care of the actual account storage/retrieval */
#include "mcd-account-manager-default.h"
#if ENABLE_LIBACCOUNTS_SSO
#include "mcd-account-manager-sso.h"
#endif

#include "mcd-account.h"
#include "mcd-account-config.h"
#include "mcd-account-priv.h"
#include "mcd-connection-priv.h"
#include "mcd-dbusprop.h"
#include "mcd-master-priv.h"
#include "mcd-misc.h"
#include "mission-control-plugins/mission-control-plugins.h"
#include "mission-control-plugins/implementation.h"
#include "plugin-account.h"
#include "plugin-loader.h"

#define PARAM_PREFIX "param-"
#define WRITE_CONF_DELAY    500

#define MCD_ACCOUNT_MANAGER_PRIV(account_manager) \
    (MCD_ACCOUNT_MANAGER (account_manager)->priv)

static void account_manager_iface_init (TpSvcAccountManagerClass *iface,
					gpointer iface_data);
static void properties_iface_init (TpSvcDBusPropertiesClass *iface,
				   gpointer iface_data);
static void sso_iface_init (McSvcAccountManagerInterfaceSSOClass *iface,
                            gpointer data);

static void _mcd_account_manager_constructed (GObject *obj);

static const McdDBusProp account_manager_properties[];
static const McdDBusProp sso_properties[];

static gboolean plugins_cached = FALSE;
static GList *stores = NULL;

static const McdInterfaceData account_manager_interfaces[] = {
    MCD_IMPLEMENT_IFACE (tp_svc_account_manager_get_type,
			 account_manager,
			 TP_IFACE_ACCOUNT_MANAGER),
    MCD_IMPLEMENT_IFACE (mc_svc_account_manager_interface_query_get_type,
			 account_manager_query,
			 MC_IFACE_ACCOUNT_MANAGER_INTERFACE_QUERY),
    MCD_IMPLEMENT_IFACE (mc_svc_account_manager_interface_sso_get_type,
             sso,
             MC_IFACE_ACCOUNT_MANAGER_INTERFACE_SSO),
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

    McdPluginAccountManager *plugin_manager;
    GHashTable *accounts;

    gchar *account_connections_dir;  /* directory for temporary file */
    gchar *account_connections_file; /* in account_connections_dir */

    gboolean dbus_registered;
};

typedef struct
{
    McdAccountManager *account_manager;
    McpAccountStorage *storage;
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

enum
{
    PROP_0,
    PROP_DBUS_DAEMON,
};

static guint write_conf_id = 0;

static void register_dbus_service (McdAccountManager *account_manager);

static void release_load_accounts_lock (McdLoadAccountsData *lad);
static void add_account (McdAccountManager *manager, McdAccount *account,
    const gchar *source);
static void account_loaded (McdAccount *account,
                            const GError *error,
                            gpointer user_data);

/* sort in descending order of priority (ie higher prio => earlier in list) */
static gint
account_storage_cmp (gconstpointer a, gconstpointer b)
{
    gint pa = mcp_account_storage_priority (a);
    gint pb = mcp_account_storage_priority (b);

    if (pa > pb) return -1;
    if (pa < pb) return 1;

    return 0;
}

/* callbacks for the various stages in an backend-driven account creation */
static void
async_created_validity_cb (McdAccount *account, gboolean valid, gpointer data)
{
    DEBUG ("asynchronously created account %s is %svalid",
           mcd_account_get_unique_name (account), valid ? "" : "in");

    /* safely cached in the accounts hash by now */
    g_object_unref (account);
}

static void
async_created_manager_cb (McdManager *cm, const GError *error, gpointer data)
{
    McdLoadAccountsData *lad = data;
    McdAccount *account = lad->account;
    McdAccountManager *am = lad->account_manager;
    McpAccountStorage *plugin = lad->storage;
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
 * since the plugin does not have our GKeyFile, we need to poke the plugin  *
 * to fetch the named account explicitly at this point (ie it's a read, not *
 * not a write, from the plugin's POV:                                      */
static void
created_cb (GObject *storage, const gchar *name, gpointer data)
{
    McpAccountStorage *plugin = MCP_ACCOUNT_STORAGE (storage);
    McdAccountManager *am = MCD_ACCOUNT_MANAGER (data);
    McdAccountManagerPrivate *priv = MCD_ACCOUNT_MANAGER_PRIV (am);
    McdAccountManagerClass *mclass = MCD_ACCOUNT_MANAGER_GET_CLASS (am);
    McdLoadAccountsData *lad = g_slice_new (McdLoadAccountsData);
    McdAccount *account = NULL;
    McdPluginAccountManager *pa = priv->plugin_manager;
    McdMaster *master = mcd_master_get_default ();
    McdManager *cm = NULL;
    const gchar *cm_name = NULL;

    lad->account_manager = am;
    lad->storage = plugin;
    lad->account_lock = 1; /* will be released at the end of this function */

    /* actually fetch the data into our GKeyFile from the plugin: */
    DEBUG ("-> mcp_account_storage_get");
    if (mcp_account_storage_get (plugin, MCP_ACCOUNT_MANAGER (pa), name, NULL))
    {
        account = mclass->account_new (am, name);
        lad->account = account;
    }
    else
    {
        g_warning ("plugin %s disowned its own new account %s",
                   mcp_account_storage_name (plugin), name);
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

finish:
    release_load_accounts_lock (lad);
}

static void
altered_cb (GObject *plugin, const gchar *account)
{
  McpAccountStorage *storage = MCP_ACCOUNT_STORAGE (plugin);

  DEBUG ("%s plugin reports %s changed, async changes not supported yet",
      mcp_account_storage_name (storage), account);
}

static void
toggled_cb (GObject *plugin, const gchar *account, gboolean on)
{
  McpAccountStorage *storage = MCP_ACCOUNT_STORAGE (plugin);

  DEBUG ("%s plugin reports %s became %svalid, async changes supported yet",
      mcp_account_storage_name (storage), account, on ? "" : "in");
}

static void
_mcd_account_delete_cb (McdAccount *account, const GError *error, gpointer data)
{
  /* we don't do anything with this right now */
}

/* a backend plugin notified us that an account was vaporised: remove it */
static void
deleted_cb (GObject *plugin, const gchar *name, gpointer data)
{
    GList *store = NULL;
    McpAccountStorage *storage = MCP_ACCOUNT_STORAGE (plugin);
    McdAccountManager *manager = MCD_ACCOUNT_MANAGER (data);
    McdAccount *account = NULL;
    McdPluginAccountManager *pa = manager->priv->plugin_manager;

    account = g_hash_table_lookup (manager->priv->accounts, name);

    if (account != NULL)
    {
        mcd_account_delete (account, _mcd_account_delete_cb, NULL);
        g_hash_table_remove (manager->priv->accounts, name);
    }

    /* NOTE: we have to do this here, the mcd_account deletion just *
     * steals a copy of your internal storage and erases the entry  *
     * from underneath us, so we don't even know we had the account *
     * after mcd_account_delete: a rumsfeldian unknown unknown      */
    /* PS: which will be fixed, but this will do for now            */
    for (store = stores; store != NULL; store = g_list_next (store))
    {
        McpAccountStorage *p = store->data;

        DEBUG ("%s -> mcp_account_storage_delete",
               mcp_account_storage_name (p));

        /* don't call the plugin who informed us of deletion, it should  *
         * already have purged its own store and we don't want to risk   *
         * some sort of crazy keep-on-deleting infinite loop shenanigans */
        if (p != storage)
            mcp_account_storage_delete (p, MCP_ACCOUNT_MANAGER (pa), name, NULL);
    }
}

static void
add_libaccount_plugin_if_enabled (void)
{
#if ENABLE_LIBACCOUNTS_SSO
    McdAccountManagerSso *sso_plugin = mcd_account_manager_sso_new ();

    stores = g_list_insert_sorted (stores, sso_plugin, account_storage_cmp);
#endif
}

static void
sort_and_cache_plugins (McdAccountManager *self)
{
    const GList *p;
    McdAccountManagerDefault *default_plugin = NULL;

    if (plugins_cached)
        return;

    /* insert the default storage plugin into the sorted plugin list */
    default_plugin = mcd_account_manager_default_new ();
    stores = g_list_insert_sorted (stores, default_plugin, account_storage_cmp);

    /* now poke the pseudo-plugins into the sorted GList of storage plugins */
    add_libaccount_plugin_if_enabled ();

    for (p = mcp_list_objects(); p != NULL; p = g_list_next (p))
    {
        if (MCP_IS_ACCOUNT_STORAGE (p->data))
        {
            McpAccountStorage *plugin = g_object_ref (p->data);

            stores = g_list_insert_sorted (stores, plugin, account_storage_cmp);
        }
    }

    for (p = stores; p != NULL; p = g_list_next (p))
    {
        McpAccountStorage *plugin = p->data;

        DEBUG ("found plugin %s [%s; priority %d]\n%s",
               mcp_account_storage_name (plugin),
               g_type_name (G_TYPE_FROM_INSTANCE (plugin)),
               mcp_account_storage_priority (plugin),
               mcp_account_storage_description (plugin));
        g_signal_connect (plugin, "created", G_CALLBACK (created_cb), self);
        g_signal_connect (plugin, "altered", G_CALLBACK (altered_cb), self);
        g_signal_connect (plugin, "toggled", G_CALLBACK (toggled_cb), self);
        g_signal_connect (plugin, "deleted", G_CALLBACK (deleted_cb), self);
    }

    plugins_cached = TRUE;
}


GQuark
mcd_account_manager_error_quark (void)
{
    static GQuark quark = 0;

    if (quark == 0)
        quark = g_quark_from_static_string ("mcd-account-manager-error");

    return quark;
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

    master = mcd_master_get_default ();
    g_return_val_if_fail (MCD_IS_MASTER (master), FALSE);

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

            DEBUG ("Killing connection");
            proxy = tp_connection_new (priv->dbus_daemon, names[i], NULL, NULL);
            if (proxy)
            {
                tp_cli_connection_call_disconnect (proxy, -1, NULL, NULL,
                                                   NULL, NULL);
                g_object_unref (proxy);
            }
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
    tp_svc_account_manager_emit_account_validity_changed (account_manager,
							  object_path,
							  valid);
}

static void
on_account_removed (McdAccount *account, McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    McdPluginAccountManager *pa = priv->plugin_manager;
    const gchar *name, *object_path;
    GList *store;

    object_path = mcd_account_get_object_path (account);
    tp_svc_account_manager_emit_account_removed (account_manager, object_path);

    name = mcd_account_get_unique_name (account);
    g_hash_table_remove (priv->accounts, name);

    for (store = stores; store != NULL; store = g_list_next (store))
    {
        McpAccountStorage *plugin = store->data;

        DEBUG ("plugin %s; removing %s",
               mcp_account_storage_name (plugin), name);
        mcp_account_storage_delete (plugin, MCP_ACCOUNT_MANAGER (pa), name, NULL);
    }

    mcd_account_manager_write_conf_async (account_manager, NULL, NULL);
}

static void
unref_account (gpointer data)
{
    McdAccount *account = MCD_ACCOUNT (data);
    McdAccountManager *account_manager;

    DEBUG ("called for %s", mcd_account_get_unique_name (account));
    account_manager = mcd_account_get_account_manager (account);
    g_signal_handlers_disconnect_by_func (account, on_account_validity_changed,
                                          account_manager);
    g_signal_handlers_disconnect_by_func (account, on_account_removed,
                                          account_manager);
    g_object_unref (account);
}

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

    if (cad->properties != NULL)
    {
        g_hash_table_unref (cad->properties);
    }

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
            g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                         "Malformed property name: %s", name);
            ok = FALSE;
        }
    }

    return ok;
}

static void
complete_account_creation_finish (McdAccount *account, gboolean valid,
                                  gpointer user_data)
{
    McdCreateAccountData *cad = (McdCreateAccountData *) user_data;
    McdAccountManager *account_manager;

    account_manager = cad->account_manager;

    if (!valid)
    {
        cad->ok = FALSE;
        g_set_error (&cad->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                         "The supplied CM parameters were not valid");
    }

    if (!cad->ok)
    {
        mcd_account_delete (account, NULL, NULL);
        g_object_unref (account);
        account = NULL;
    }

    mcd_account_manager_write_conf_async (account_manager, NULL, NULL);

    if (cad->callback != NULL)
        cad->callback (account_manager, account, cad->error, cad->user_data);
    mcd_create_account_data_free (cad);

    if (account != NULL)
    {
        g_object_unref (account);
    }

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
        g_set_error (&cad->error, MCD_ACCOUNT_MANAGER_ERROR,
                     MCD_ACCOUNT_MANAGER_ERROR_SET_PARAMETER,
                     "Failed to set parameter: %s", set_error->message);
    }

    if (cad->ok && cad->properties != NULL)
    {
        cad->ok = set_new_account_properties (account, cad->properties, &cad->error);
    }

    if (cad->ok)
    {
        add_account (account_manager, account, G_STRFUNC);
        mcd_account_check_validity (account, complete_account_creation_finish, cad);
    }
    else
    {
        complete_account_creation_finish (account, TRUE, cad);
    }

    if (not_yet != NULL)
    {
        g_ptr_array_foreach (not_yet, (GFunc) g_free, NULL);
        g_ptr_array_free (not_yet, TRUE);
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
    McpAccountManager *ma = MCP_ACCOUNT_MANAGER (priv->plugin_manager);
    McdCreateAccountData *cad;
    McdAccount *account;
    gchar *unique_name;

    DEBUG ("called");
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

    unique_name =
      mcp_account_manager_get_unique_name (ma, manager, protocol, params);
    g_return_if_fail (unique_name != NULL);

    /* create the basic account keys */
    g_key_file_set_string (priv->plugin_manager->keyfile, unique_name,
			   MC_ACCOUNTS_KEY_MANAGER, manager);
    g_key_file_set_string (priv->plugin_manager->keyfile, unique_name,
			   MC_ACCOUNTS_KEY_PROTOCOL, protocol);
    if (display_name)
	g_key_file_set_string (priv->plugin_manager->keyfile, unique_name,
			       MC_ACCOUNTS_KEY_DISPLAY_NAME, display_name);

    account = MCD_ACCOUNT_MANAGER_GET_CLASS (account_manager)->account_new
        (account_manager, unique_name);
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
sso_get_service_accounts (McSvcAccountManagerInterfaceSSO *iface,
                          const gchar *service,
                          DBusGMethodInvocation *context)
{
    gsize len;
    McdAccountManager *manager = MCD_ACCOUNT_MANAGER (iface);
    McdAccountManagerPrivate *priv = manager->priv;
    GKeyFile *cache = priv->plugin_manager->keyfile;
    GStrv accounts = g_key_file_get_groups (cache, &len);
    GList *srv_accounts = NULL;
    GPtrArray *paths = g_ptr_array_new ();

    if (!plugins_cached)
        sort_and_cache_plugins (manager);

    if (len > 0 && accounts != NULL)
    {
        guint i = 0;
        gchar *name;
        McpAccountManager *ma = MCP_ACCOUNT_MANAGER (priv->plugin_manager);

        for (name = accounts[i]; name != NULL; name = accounts[++i])
        {
            gchar *id =
              g_key_file_get_string (cache, name, "libacct-uid", NULL);

            if (id != NULL)
            {
                gchar *supported =
                  g_key_file_get_string (cache, name, "sso-services", NULL);

                if (supported == NULL)
                {

                    GList *store = g_list_last (stores);
                    while (store != NULL)
                    {
                        McpAccountStorage *as = store->data;
                        mcp_account_storage_get (as, ma, name, "sso-services");
                        store = g_list_previous (store);
                    }
                    supported = g_key_file_get_string (cache, name,
                                                       "sso-services", NULL);
                }

                if (supported != NULL)
                {
                    guint i;
                    GStrv services = g_strsplit (supported, ";", 0);

                    for (i = 0; services[i] != NULL; i++)
                    {
                        if (g_str_equal (service, services[i]))
                        {
                            McdAccount *a =
                              g_hash_table_lookup (priv->accounts, name);

                            if (a != NULL)
                                srv_accounts = g_list_prepend (srv_accounts, a);
                        }
                    }

                    g_free (supported);
                    g_strfreev (services);
                }

                g_free (id);
            }
        }
    }

    if (srv_accounts != NULL)
    {
        GList *account = srv_accounts;

        for (; account != NULL; account = g_list_next (account))
        {
            const gchar *path = mcd_account_get_object_path (account->data);
            g_ptr_array_add (paths, (gpointer) path);
        }

        g_list_free (srv_accounts);
    }

    mc_svc_account_manager_interface_sso_return_from_get_service_accounts (
      context,
      paths);

    g_ptr_array_unref (paths);
}

static void
sso_get_account (McSvcAccountManagerInterfaceSSO *iface,
                 const guint id,
                 DBusGMethodInvocation *context)
{
    gsize len;
    McdAccountManager *manager = MCD_ACCOUNT_MANAGER (iface);
    McdAccountManagerPrivate *priv = manager->priv;
    GKeyFile *cache = priv->plugin_manager->keyfile;
    GStrv accounts = g_key_file_get_groups (cache, &len);
    const gchar *path = NULL;

    if (len > 0 && accounts != NULL)
    {
        guint i = 0;
        gchar *name;
        McdAccount *account = NULL;

        for (name = accounts[i]; name != NULL; name = accounts[++i])
        {
            gchar *str_id =
              g_key_file_get_string (cache, name, "libacct-uid", NULL);
            guint64 sso_id = g_ascii_strtoull (str_id, NULL, 10);

            if (sso_id != 0 && id != 0)
            {
                if (id == sso_id)
                {
                    account = g_hash_table_lookup (priv->accounts, name);
                    break;
                }
            }

            g_free (str_id);
        }

        if (account != NULL)
            path = mcd_account_get_object_path (account);
    }

    if (path != NULL)
    {
        mc_svc_account_manager_interface_sso_return_from_get_account (context,
                                                                      path);
    }
    else
    {
        GError *error = g_error_new (TP_TYPE_ERROR,
                                     TP_ERROR_DOES_NOT_EXIST,
                                     "SSO ID %u Not Found", id);

        dbus_g_method_return_error (context, error);

        g_error_free (error);
    }

    g_strfreev (accounts);
}

static void
sso_iface_init (McSvcAccountManagerInterfaceSSOClass *iface, gpointer data)
{
#define IMPLEMENT(x) \
mc_svc_account_manager_interface_sso_implement_##x (iface, sso_##x)
    IMPLEMENT (get_account);
    IMPLEMENT (get_service_accounts);
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

    DEBUG ("called");
    accounts_to_gvalue (priv->accounts, TRUE, value);
}

static void
get_invalid_accounts (TpSvcDBusProperties *self, const gchar *name,
		      GValue *value)
{
    McdAccountManager *account_manager = MCD_ACCOUNT_MANAGER (self);
    McdAccountManagerPrivate *priv = account_manager->priv;

    DEBUG ("called");
    accounts_to_gvalue (priv->accounts, FALSE, value);
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
        TP_IFACE_ACCOUNT_INTERFACE_AVATAR ".Avatar",
        MC_IFACE_ACCOUNT_INTERFACE_COMPAT ".Profile",
        MC_IFACE_ACCOUNT_INTERFACE_COMPAT ".SecondaryVCardFields",
        MC_IFACE_ACCOUNT_INTERFACE_CONDITIONS ".Condition",
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

static const McdDBusProp sso_properties[] = {
    { NULL, NULL, NULL },
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
    McdPluginAccountManager *pa = userdata;
    GKeyFile *keyfile = pa->keyfile;
    GStrv groups;
    gchar *group;
    gsize i = 0;
    GList *store;

    DEBUG ("called");
    g_source_remove (write_conf_id);
    write_conf_id = 0;

    groups = g_key_file_get_groups (keyfile, NULL);

    if (groups == NULL)
        return TRUE;

    /* poke the account settings into the local cache of the relevant  *
     * storage plugins, highest priority plugins get first dibs:       *
     * Note that the MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_DEFAULT priority  *
     * plugin is the default keyfile plugin and accepts all settings,  *
     * so no plugin of a lower priority will be asked to save anything */
    for (group = groups[i]; group != NULL; group = groups[++i])
    {
        gsize n_keys;
        gsize j = 0;
        GStrv keys = g_key_file_get_keys (keyfile, group, &n_keys, NULL);

        if (keys == NULL)
            n_keys = 0;

        for (j = 0; j < n_keys; j++)
        {
            gboolean done = FALSE;
            gchar *set = keys[j];
            gchar *val = g_key_file_get_string (keyfile, group, set, NULL);

            for (store = stores; store != NULL; store = g_list_next (store))
            {
                McpAccountStorage *plugin = store->data;
                McpAccountManager *ma = MCP_ACCOUNT_MANAGER (pa);
                const gchar *pn = mcp_account_storage_name (plugin);

                if (done)
                {
                    DEBUG ("%s -> mcp_account_storage_delete(%s)", pn, group);
                    mcp_account_storage_delete (plugin, ma, group, set);
                }
                else
                {
                    DEBUG ("%s -> mcp_account_storage_set(%s)", pn, group);
                    done = mcp_account_storage_set (plugin, ma, group, set, val);
                }
            }
        }

        g_strfreev (keys);
    }

    g_strfreev (groups);

    for (store = stores; store != NULL; store = g_list_next (store))
    {
        McpAccountManager *ma = MCP_ACCOUNT_MANAGER (pa);
        McpAccountStorage *plugin = store->data;
        const gchar *pname = mcp_account_storage_name (plugin);

        DEBUG ("flushing plugin %s to long term storage", pname);
        mcp_account_storage_commit (plugin, ma);
    }

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
    McpAccountManager *mcp_am = MCP_ACCOUNT_MANAGER (priv->plugin_manager);
    GList *store;

    /* Allow plugins to register new accounts, highest prio first */
    for (store = stores; store != NULL; store = g_list_next (store))
    {
        McpAccountStorage *plugin = store->data;

        DEBUG ("Unblocking async account ops by %s",
               mcp_account_storage_name (plugin));
        mcp_account_storage_ready (plugin, mcp_am);
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
    McdLoadAccountsData *lad;
    gchar **accounts, **name;

    tp_list_connection_names (priv->dbus_daemon,
                              list_connection_names_cb, NULL, NULL,
                              (GObject *)account_manager);

    lad = g_slice_new (McdLoadAccountsData);
    lad->account_manager = account_manager;
    lad->account_lock = 1; /* will be released at the end of this function */

    accounts = g_key_file_get_groups (priv->plugin_manager->keyfile, NULL);
    for (name = accounts; *name != NULL; name++)
    {
        McdAccount *account = mcd_account_manager_lookup_account (
            account_manager, *name);

        if (account != NULL)
        {
            /* FIXME: this shouldn't really happen */
            DEBUG ("already have account %p called '%s'; skipping", account, *name);
            continue;
        }

        account = MCD_ACCOUNT_MANAGER_GET_CLASS (account_manager)->account_new
            (account_manager, *name);
        if (G_UNLIKELY (!account))
        {
            g_warning ("%s: account %s failed to instantiate", G_STRFUNC,
                       *name);
            continue;
        }
        lad->account_lock++;
        add_account (lad->account_manager, account, "keyfile");
        _mcd_account_load (account, account_loaded, lad);
        g_object_unref (account);
    }
    g_strfreev (accounts);

    uncork_storage_plugins (account_manager);

    release_load_accounts_lock (lad);
}

static void
register_dbus_service (McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv = account_manager->priv;
    DBusGConnection *dbus_connection;
    GError *error = NULL;

    if (priv->dbus_registered)
        return;

    dbus_connection = TP_PROXY (priv->dbus_daemon)->dbus_connection;

    if (!tp_dbus_daemon_request_name (priv->dbus_daemon,
                                      MC_ACCOUNT_MANAGER_DBUS_SERVICE,
                                      TRUE /* idempotent */, &error))
    {
        /* FIXME: put in proper error handling when MC gains the ability to
         * be the AM or the CD but not both */
        g_error ("Failed registering '%s' service: %s",
                 MC_ACCOUNT_MANAGER_DBUS_SERVICE, error->message);
        g_error_free (error);
        exit (1);
    }

    priv->dbus_registered = TRUE;

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
    {
        write_conf (priv->plugin_manager);
        g_assert (write_conf_id == 0);
    }

    g_object_unref (priv->plugin_manager);
    priv->plugin_manager = NULL;

    g_free (priv->account_connections_dir);
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
    object_class->constructed = _mcd_account_manager_constructed;

    klass->account_new = account_new;

    g_object_class_install_property
        (object_class, PROP_DBUS_DAEMON,
         g_param_spec_object ("dbus-daemon", "DBus daemon", "DBus daemon",
                              TP_TYPE_DBUS_DAEMON,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
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
    GList *store = NULL;
    McpAccountManager *ma;

    DEBUG ("");

    priv = G_TYPE_INSTANCE_GET_PRIVATE ((account_manager),
					MCD_TYPE_ACCOUNT_MANAGER,
					McdAccountManagerPrivate);
    account_manager->priv = priv;

    priv->plugin_manager = mcd_plugin_account_manager_new ();
    ma = MCP_ACCOUNT_MANAGER (priv->plugin_manager);
    priv->accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            NULL, unref_account);

    priv->account_connections_dir = g_strdup (get_connections_cache_dir ());
    priv->account_connections_file =
        g_build_filename (priv->account_connections_dir, ".mc_connections",
                          NULL);

    DEBUG ("loading plugins");

    /* not guaranteed to have been called, but idempotent: */
    _mcd_plugin_loader_init ();

    if (!plugins_cached)
        sort_and_cache_plugins (account_manager);

    store = g_list_last (stores);

    /* fetch accounts stored in plugins, in reverse priority so higher prio *
     * plugins can overwrite lower prio ones' account data                  */
    while (store != NULL)
    {
        GList *account;
        McpAccountStorage *plugin = store->data;
        GList *stored = mcp_account_storage_list (plugin, ma);
        const gchar *pname = mcp_account_storage_name (plugin);
        const gint prio = mcp_account_storage_priority (plugin);

        DEBUG ("listing from plugin %s [prio: %d]", pname, prio);
        for (account = stored; account != NULL; account = g_list_next (account))
        {
            gchar *name = account->data;

            DEBUG ("fetching %s from plugin %s [prio: %d]", name, pname, prio);
            mcp_account_storage_get (plugin, ma, name, NULL);

            g_free (name);
        }

        /* already freed the contents, just need to free the list itself */
        g_list_free (stored);
        store = g_list_previous (store);
    }

    /* initializes the interfaces */
    mcd_dbus_init_interfaces_instances (account_manager);
}

static void
_mcd_account_manager_constructed (GObject *obj)
{
    McdAccountManager *manager = MCD_ACCOUNT_MANAGER (obj);
    McdAccountManagerPrivate *priv = MCD_ACCOUNT_MANAGER_PRIV (manager);
    McdPluginAccountManager *pa = priv->plugin_manager;

    /* FIXME: I'm pretty sure we should just move most of the above code out of
     * _init() to here and then mcd_plugin_account_manager_new() could take the
     * TpDBusDaemon * as it should and everyone wins.
     */
    mcd_plugin_account_manager_set_dbus_daemon (pa, priv->dbus_daemon);
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

/**
 * mcd_account_manager_write_conf:
 * @account_manager: the #McdAccountManager
 *
 * Write the account manager configuration to disk.
 *
 * Deprecated: Use mcd_account_manager_write_conf_async() in all code.
 */
void
mcd_account_manager_write_conf (McdAccountManager *account_manager)
{
    /* FIXME: this (reasonably) assumes that there is only one
     * McdAccountManager object running, since the write_conf_id is a static
     * variable */
    McdPluginAccountManager *data = account_manager->priv->plugin_manager;

    if (write_conf_id == 0) 
        write_conf_id =
            g_timeout_add_full (G_PRIORITY_HIGH, WRITE_CONF_DELAY, write_conf,
                                g_object_ref (data), g_object_unref);
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
 * @callback: a callback to be called on write success or failure
 * @user_data: data to be passed to @callback
 *
 * Write the account manager configuration to disk. This is an asynchronous
 * version of mcd_account_manager_write_conf() and should always used in favour
 * of its synchronous version.
 */
void
mcd_account_manager_write_conf_async (McdAccountManager *account_manager,
                                      McdAccountManagerWriteConfCb callback,
                                      gpointer user_data)
{
    GKeyFile *keyfile;
    GStrv groups;
    GList *store;
    gsize i = 0;
    gsize n_accounts = 0;
    gchar *group;
    McpAccountManager *ma;

    g_return_if_fail (MCD_IS_ACCOUNT_MANAGER (account_manager));

    keyfile = account_manager->priv->plugin_manager->keyfile;
    groups = g_key_file_get_groups (keyfile, &n_accounts);
    ma = MCP_ACCOUNT_MANAGER (account_manager->priv->plugin_manager);

    DEBUG ("called (writing %" G_GSIZE_FORMAT " accounts)", n_accounts);

    for (group = groups[i]; group != NULL; group = groups[++i])
    {
        gsize j = 0;
        gsize n_keys = 0;
        GStrv keys = g_key_file_get_keys (keyfile, group, &n_keys, NULL);
        McdAccount *acct =
          mcd_account_manager_lookup_account (account_manager, group);

        if (keys == NULL)
            n_keys = 0;

        for (j = 0; j < n_keys; j++)
        {
            gboolean done = FALSE;
            gchar *set = keys[j];
            gchar *val = g_key_file_get_value (keyfile, group, set, NULL);

            /* the param- prefix gets whacked on in the layer above us:     *
             * mcd-account et al don't know it exists so don't pass it back */
            if (acct != NULL && g_str_has_prefix (set, PARAM_PREFIX))
            {
                const gchar *p = set + strlen (PARAM_PREFIX);

                if (mcd_account_parameter_is_secret (acct, p))
                    mcp_account_manager_parameter_make_secret (ma, group, set);
            }

            for (store = stores; store != NULL; store = g_list_next (store))
            {
                McpAccountStorage *plugin = store->data;
                const gchar *pname = mcp_account_storage_name (plugin);

                DEBUG ("writing %s.%s to %s [prio: %d] %s",
                       group, set, pname, mcp_account_storage_priority (plugin),
                       done ? "DELETE" : "STORE");

                if (done)
                    mcp_account_storage_delete (plugin, ma, group, set);
                else
                    done = mcp_account_storage_set (plugin, ma, group, set, val);
            }
        }

        g_strfreev (keys);
    }

    g_strfreev (groups);

    for (store = stores; store != NULL; store = g_list_next (store))
    {
        McpAccountStorage *plugin = store->data;
        const gchar *pname = mcp_account_storage_name (plugin);

        DEBUG ("flushing plugin %s to long term storage", pname);
        mcp_account_storage_commit (plugin, ma);
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

    if (!g_str_has_prefix (object_path, MC_ACCOUNT_DBUS_OBJECT_BASE))
    {
        /* can't possibly be right */
        return NULL;
    }

    return g_hash_table_lookup (priv->accounts,
        object_path + (sizeof (MC_ACCOUNT_DBUS_OBJECT_BASE) - 1));
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

    return account_manager->priv->plugin_manager->keyfile;
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

