/* Representation of the account manager as presented to plugins. This is
 * deliberately a "smaller" API than McdAccountManager.
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010-2012 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "mcd-storage.h"

#include "mcd-account.h"
#include "mcd-account-config.h"
#include "mcd-debug.h"
#include "plugin-loader.h"

#include <string.h>

#include <telepathy-glib/telepathy-glib.h>

#include "mission-control-plugins/implementation.h"

/* these pseudo-plugins take care of the actual account storage/retrieval */
#include "mcd-account-manager-default.h"

#if ENABLE_LIBACCOUNTS_SSO
#include "mcd-account-manager-sso.h"
# ifdef ACCOUNTS_GLIB_HIDDEN_SERVICE_TYPE
# include "mcd-storage-ag-hidden.h"
# endif
#endif

static GList *stores = NULL;
static void sort_and_cache_plugins (void);

enum {
  PROP_DBUS_DAEMON = 1,
};

struct _McdStorageClass {
    GObjectClass parent;
};

static void plugin_iface_init (McpAccountManagerIface *iface,
    gpointer unused G_GNUC_UNUSED);

G_DEFINE_TYPE_WITH_CODE (McdStorage, mcd_storage,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_MANAGER, plugin_iface_init))

static void
mcd_storage_init (McdStorage *self)
{
  self->keyfile = g_key_file_new ();
  self->secrets = g_key_file_new ();
}

static void
storage_finalize (GObject *object)
{
  McdStorage *self = MCD_STORAGE (object);
  GObjectFinalizeFunc finalize =
    G_OBJECT_CLASS (mcd_storage_parent_class)->finalize;

  g_key_file_free (self->keyfile);
  g_key_file_free (self->secrets);
  self->keyfile = NULL;
  self->secrets = NULL;

  if (finalize != NULL)
    finalize (object);
}

static void
storage_dispose (GObject *object)
{
  McdStorage *self = MCD_STORAGE (object);
  GObjectFinalizeFunc dispose =
    G_OBJECT_CLASS (mcd_storage_parent_class)->dispose;

  tp_clear_object (&self->dbusd);

  if (dispose != NULL)
    dispose (object);
}

static void
storage_set_property (GObject *obj, guint prop_id,
	      const GValue *val, GParamSpec *pspec)
{
    McdStorage *self = MCD_STORAGE (obj);

    switch (prop_id)
    {
      case PROP_DBUS_DAEMON:
        tp_clear_object (&self->dbusd);
        self->dbusd = TP_DBUS_DAEMON (g_value_dup_object (val));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
        break;
    }
}

static void
storage_get_property (GObject *obj, guint prop_id,
	      GValue *val, GParamSpec *pspec)
{
    McdStorage *self = MCD_STORAGE (obj);

    switch (prop_id)
    {
      case PROP_DBUS_DAEMON:
        g_value_set_object (val, self->dbusd);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
        break;
    }
}

static void
mcd_storage_class_init (McdStorageClass *cls)
{
  GObjectClass *object_class = (GObjectClass *) cls;
  GParamSpec *spec = g_param_spec_object ("dbus-daemon",
      "DBus daemon",
      "DBus daemon",
      TP_TYPE_DBUS_DAEMON,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  object_class->set_property = storage_set_property;
  object_class->get_property = storage_get_property;
  object_class->dispose = storage_dispose;
  object_class->finalize = storage_finalize;

  g_object_class_install_property (object_class, PROP_DBUS_DAEMON, spec);
}

McdStorage *
mcd_storage_new (TpDBusDaemon *dbus_daemon)
{
  return g_object_new (MCD_TYPE_STORAGE,
      "dbus-daemon", dbus_daemon,
      NULL);
}

static gchar *
get_value (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key)
{
  McdStorage *self = MCD_STORAGE (ma);
  return g_key_file_get_value (self->keyfile, account, key, NULL);
}

static void
set_value (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key,
    const gchar *value)
{
  McdStorage *self = MCD_STORAGE (ma);

  if (value != NULL)
    g_key_file_set_value (self->keyfile, account, key, value);
  else
    g_key_file_remove_key (self->keyfile, account, key, NULL);
}

static GStrv
list_keys (const McpAccountManager *ma,
           const gchar * account)
{
  McdStorage *self = MCD_STORAGE (ma);

  return g_key_file_get_keys (self->keyfile, account, NULL, NULL);
}

static gboolean
is_secret (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key)
{
  McdStorage *self = MCD_STORAGE (ma);

  return g_key_file_get_boolean (self->secrets, account, key, NULL);
}

static void
make_secret (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key)
{
  McdStorage *self = MCD_STORAGE (ma);
  DEBUG ("flagging %s.%s as secret", account, key);
  g_key_file_set_boolean (self->secrets, account, key, TRUE);
}

static gchar *
unique_name (const McpAccountManager *ma,
    const gchar *manager,
    const gchar *protocol,
    const GHashTable *params)
{
  McdStorage *self = MCD_STORAGE (ma);
  const gchar *base = NULL;
  gchar *esc_manager, *esc_protocol, *esc_base;
  guint i;
  gsize base_len = strlen (TP_ACCOUNT_OBJECT_PATH_BASE);
  DBusGConnection *connection = tp_proxy_get_dbus_connection (self->dbusd);

  base = tp_asv_get_string (params, "account");

  if (base == NULL)
    base = "account";

  esc_manager = tp_escape_as_identifier (manager);
  esc_protocol = g_strdelimit (g_strdup (protocol), "-", '_');
  esc_base = tp_escape_as_identifier (base);

  for (i = 0; i < G_MAXUINT; i++)
    {
      gchar *path = g_strdup_printf (
          TP_ACCOUNT_OBJECT_PATH_BASE "%s/%s/%s%u",
          esc_manager, esc_protocol, esc_base, i);

      if (!g_key_file_has_group (self->keyfile, path + base_len) &&
          dbus_g_connection_lookup_g_object (connection, path) == NULL)
        {
          gchar *ret = g_strdup (path + base_len);

          g_free (path);
          return ret;
        }

      g_free (path);
    }

  return NULL;
}

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

static void
add_storage_plugin (McpAccountStorage *plugin)
{
  stores = g_list_insert_sorted (stores, plugin, account_storage_cmp);
}

static void
add_libaccounts_plugins_if_enabled (void)
{
#if ENABLE_LIBACCOUNTS_SSO
  add_storage_plugin (MCP_ACCOUNT_STORAGE (mcd_account_manager_sso_new ()));
# ifdef ACCOUNTS_GLIB_HIDDEN_SERVICE_TYPE
  add_storage_plugin (MCP_ACCOUNT_STORAGE (mcd_storage_ag_hidden_new ()));
# endif
#endif
}

static void
sort_and_cache_plugins ()
{
  const GList *p;
  static gboolean plugins_cached = FALSE;

  if (plugins_cached)
    return;

  /* not guaranteed to have been called, but idempotent: */
  _mcd_plugin_loader_init ();

  /* Add compiled-in plugins */
  add_storage_plugin (MCP_ACCOUNT_STORAGE (mcd_account_manager_default_new ()));
  add_libaccounts_plugins_if_enabled ();

  for (p = mcp_list_objects(); p != NULL; p = g_list_next (p))
    {
      if (MCP_IS_ACCOUNT_STORAGE (p->data))
        {
          McpAccountStorage *plugin = g_object_ref (p->data);

          add_storage_plugin (plugin);
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
    }

    plugins_cached = TRUE;
}

void
mcd_storage_connect_signal (const gchar *signame,
    GCallback func,
    gpointer user_data)
{
  GList *p;

  for (p = stores; p != NULL; p = g_list_next (p))
    {
      McpAccountStorage *plugin = p->data;

      DEBUG ("connecting handler to %s plugin signal %s ",
          mcp_account_storage_name (plugin), signame);
      g_signal_connect (plugin, signame, func, user_data);
    }
}

/*
 * mcd_storage_load:
 * @storage: An object implementing the #McdStorage interface
 *
 * Load the long term account settings storage into our internal cache.
 * Should only really be called during startup, ie before our DBus names
 * have been claimed and other people might be relying on responses from us.
 */
void
mcd_storage_load (McdStorage *self)
{
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  GList *store = NULL;

  g_return_if_fail (MCD_IS_STORAGE (self));

  sort_and_cache_plugins ();

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
}

/*
 * mcd_storage_dup_accounts:
 * @storage: An object implementing the #McdStorage interface
 * @n: place for the number of accounts to be written to (or %NULL)
 *
 * Returns: a newly allocated GStrv containing the unique account names,
 * which must be freed by the caller with g_strfreev().
 */
GStrv
mcd_storage_dup_accounts (McdStorage *self,
    gsize *n)
{
  g_return_val_if_fail (MCD_IS_STORAGE (self), NULL);

  return g_key_file_get_groups (self->keyfile, n);
}

/*
 * mcd_storage_dup_settings:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @n: place for the number of settings to be written to (or %NULL)
 *
 * Returns: a newly allocated GStrv containing the names of all the
 * settings or parameters currently stored for @account. Must be
 * freed by the caller with g_strfreev().
 */
GStrv
mcd_storage_dup_settings (McdStorage *self,
    const gchar *account,
    gsize *n)
{
  g_return_val_if_fail (MCD_IS_STORAGE (self), NULL);

  return g_key_file_get_keys (self->keyfile, account, n, NULL);
}

/*
 * mcd_storage_get_plugin:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 *
 * Returns: the #McpAccountStorage object which is handling the account,
 * if any (if a new account has not yet been flushed to storage this can
 * be %NULL).
 *
 * Plugins are kept in permanent storage and can never be unloaded, so
 * the returned pointer need not be reffed or unreffed. (Indeed, it's
 * probably safer not to)
 */
McpAccountStorage *
mcd_storage_get_plugin (McdStorage *self,
    const gchar *account)
{
  GList *store = stores;
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  McpAccountStorage *owner = NULL;

  g_return_val_if_fail (MCD_IS_STORAGE (self), NULL);
  g_return_val_if_fail (account != NULL, NULL);

  for (; store != NULL && owner == NULL; store = g_list_next (store))
    {
      McpAccountStorage *plugin = store->data;

      if (mcp_account_storage_get (plugin, ma, account, "manager"))
        owner = plugin;
    }

  return owner;
}

/*
 * mcd_storage_dup_string:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @key: name of the setting to be retrieved
 *
 * Returns: a newly allocated gchar * which must be freed with g_free().
 */
gchar *
mcd_storage_dup_string (McdStorage *self,
    const gchar *account,
    const gchar *key)
{
  gchar *value = NULL;

  g_return_val_if_fail (MCD_IS_STORAGE (self), NULL);
  g_return_val_if_fail (account != NULL, NULL);

  value = g_key_file_get_string (self->keyfile, account, key, NULL);

  return value;
}

/*
 * mcd_storage_has_value:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @key: name of the setting to be retrieved
 *
 * Returns: a #gboolean: %TRUE if the setting is present in the store,
 * %FALSE otherwise.
 */
gboolean
mcd_storage_has_value (McdStorage *self,
    const gchar *account,
    const gchar *key)
{
  g_return_val_if_fail (MCD_IS_STORAGE (self), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  return g_key_file_has_key (self->keyfile, account, key, NULL);
}

/*
 * mcd_storage_dup_value:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @key: name of the setting to be retrieved
 * @type: the type of #GValue to retrieve
 * @error: a place to store any #GError<!-- -->s that occur
 *
 * Returns: a newly allocated #GValue of type @type, whihc should be freed
 * with tp_g_value_slice_free() or g_slice_free() depending on whether the
 * the value itself should be freed (the former frees everything, the latter
 * only the #GValue container.
 *
 * If @error is set, but a non-%NULL value was returned, this indicates
 * that no value for the @key was found for @account, and the default
 * value for @type has been returned.
 */
GValue *
mcd_storage_dup_value (McdStorage *self,
    const gchar *account,
    const gchar *key,
    GType type,
    GError **error)
{
  GValue *value = NULL;
  gchar *v_string = NULL;
  gint64 v_int = 0;
  guint64 v_uint = 0;
  gboolean v_bool = FALSE;
  double v_double = 0.0;

  g_return_val_if_fail (MCD_IS_STORAGE (self), NULL);
  g_return_val_if_fail (account != NULL, NULL);

  switch (type)
    {
      case G_TYPE_STRING:
        v_string = g_key_file_get_string (self->keyfile, account, key, error);
        value = tp_g_value_slice_new_take_string (v_string);
        break;

      case G_TYPE_INT:
        v_int = g_key_file_get_integer (self->keyfile, account, key, error);
        value = tp_g_value_slice_new_int (v_int);
        break;

      case G_TYPE_INT64:
        v_int = tp_g_key_file_get_int64 (self->keyfile, account, key, error);
        value = tp_g_value_slice_new_int64 (v_int);
        break;

      case G_TYPE_UINT:
        v_uint = tp_g_key_file_get_uint64 (self->keyfile, account, key, error);

        if (v_uint > 0xFFFFFFFFU)
          g_set_error (error, MCD_ACCOUNT_ERROR,
              MCD_ACCOUNT_ERROR_GET_PARAMETER,
              "Integer is out of range");
        else
          value = tp_g_value_slice_new_uint (v_uint);
        break;

    case G_TYPE_UCHAR:
        v_int = g_key_file_get_integer (self->keyfile, account, key, error);

        if (v_int < 0 || v_int > 0xFF)
          {
            g_set_error (error, MCD_ACCOUNT_ERROR,
                MCD_ACCOUNT_ERROR_GET_PARAMETER,
                "Integer is out of range");
          }
        else
          {
            value = tp_g_value_slice_new (G_TYPE_UCHAR);
            g_value_set_uchar (value, v_int);
          }
        break;

      case G_TYPE_UINT64:
        v_uint = tp_g_key_file_get_uint64 (self->keyfile, account, key, error);
        value = tp_g_value_slice_new_uint64 (v_uint);
        break;

      case G_TYPE_BOOLEAN:
        v_bool = g_key_file_get_boolean (self->keyfile, account, key, error);
        value = tp_g_value_slice_new_boolean (v_bool);
        break;

      case G_TYPE_DOUBLE:
        v_double = g_key_file_get_double (self->keyfile, account, key, error);
        value = tp_g_value_slice_new_double (v_double);
        break;

      default:
        if (type == G_TYPE_STRV)
          {
            gchar **v = g_key_file_get_string_list (self->keyfile, account,
                key, NULL, error);

            value = tp_g_value_slice_new_take_boxed (G_TYPE_STRV, v);
          }
        else if (type == DBUS_TYPE_G_OBJECT_PATH)
          {
            v_string = g_key_file_get_string (self->keyfile, account, key,
                NULL);

            if (v_string == NULL)
              {
                g_set_error (error, MCD_ACCOUNT_ERROR,
                    MCD_ACCOUNT_ERROR_GET_PARAMETER,
                    "Invalid object path NULL");
              }
            else if (!tp_dbus_check_valid_object_path (v_string, NULL))
              {
                g_set_error (error, MCD_ACCOUNT_ERROR,
                    MCD_ACCOUNT_ERROR_GET_PARAMETER,
                    "Invalid object path %s", v_string);
                g_free (v_string);
              }
            else
              {
                value = tp_g_value_slice_new_take_object_path (v_string);
              }
          }
        else if (type == TP_ARRAY_TYPE_OBJECT_PATH_LIST)
          {
            gchar **v = g_key_file_get_string_list (self->keyfile, account,
                key, NULL, error);
            gchar **iter;
            GPtrArray *arr = g_ptr_array_new ();

            for (iter = v; iter != NULL && *iter != NULL; iter++)
              {
                if (!g_variant_is_object_path (*iter))
                  {
                    g_set_error (error, MCD_ACCOUNT_ERROR,
                        MCD_ACCOUNT_ERROR_GET_PARAMETER,
                        "Invalid object path %s stored in account", *iter);
                    g_strfreev (v);
                    v = NULL;
                    break;
                  }
              }

            for (iter = v; iter != NULL && *iter != NULL; iter++)
              {
                /* transfer ownership from v to arr */
                g_ptr_array_add (arr, *iter);
              }

            /* not g_strfreev - the strings' ownership has been transferred */
            g_free (v);

            value = tp_g_value_slice_new_take_boxed (
              TP_ARRAY_TYPE_OBJECT_PATH_LIST, arr);
          }
        else
          {
            gchar *message =
              g_strdup_printf ("cannot get property %s on account %s, "
                  "unknown type %s",
                  key, account, g_type_name (type));

            g_warning ("%s: %s", G_STRFUNC, message);
            g_set_error (error, MCD_ACCOUNT_ERROR,
                MCD_ACCOUNT_ERROR_GET_PARAMETER,
                "%s", message);
            g_free (message);
          }
    }

  /* This can return a non-NULL GValue * _and_ a non-NULL GError *,      *
   * indicating a value was not found and the default for that type      *
   * (eg 0 for integers) has been returned - this matches the behaviour  *
   * of the old code that this function replaces. If changing this, make *
   * sure all our callers are suitable updated                           */

  return value;
}

/*
 * mcd_storage_get_boolean:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @key: name of the setting to be retrieved
 *
 * Returns: a #gboolean. Unset/unparseable values are returned as %FALSE
 */
gboolean
mcd_storage_get_boolean (McdStorage *self,
    const gchar *account,
    const gchar *key)
{
  g_return_val_if_fail (MCD_IS_STORAGE (self), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);

  return g_key_file_get_boolean (self->keyfile, account, key, NULL);
}

/*
 * mcd_storage_get_integer:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @key: name of the setting to be retrieved
 *
 * Returns: a #gint. Unset or non-numeric values are returned as 0
 */
gint
mcd_storage_get_integer (McdStorage *self,
    const gchar *account,
    const gchar *key)
{
  g_return_val_if_fail (MCD_IS_STORAGE (self), 0);
  g_return_val_if_fail (account != NULL, 0);

  return g_key_file_get_integer (self->keyfile, account, key, NULL);
}

static void
update_storage (McdStorage *self,
    const gchar *account,
    const gchar *key)
{
  GList *store;
  gboolean done = FALSE;
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  gchar *val = NULL;

  /* don't unescape the value here, we're flushing it to storage         *
   * everywhere else should handle escaping on the way in and unescaping *
   * on the way out of the keyfile, but not here:                        */
  val = g_key_file_get_value (self->keyfile, account, key, NULL);

  /* we're deleting, which is unconditional, no need to check if anyone *
   * claims this setting for themselves                                 */
  if (val == NULL)
    done = TRUE;

  for (store = stores; store != NULL; store = g_list_next (store))
    {
      McpAccountStorage *plugin = store->data;
      const gchar *pn = mcp_account_storage_name (plugin);

      if (done)
        {
          DEBUG ("MCP:%s -> delete %s.%s", pn, account, key);
          mcp_account_storage_delete (plugin, ma, account, key);
        }
      else
        {
          done = mcp_account_storage_set (plugin, ma, account, key, val);
          DEBUG ("MCP:%s -> %s %s.%s",
              pn, done ? "store" : "ignore", account, key);
        }
    }

  g_free (val);
}

/*
 * mcd_storage_set_string:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 * @key: the key (name) of the parameter or setting
 * @value: the value to be stored (or %NULL to erase it)
 * @secret: whether the value is confidential (might get stored in the
 * keyring, for example)
 *
 * Copies and stores the supplied @value (or removes it if %NULL) to the
 * internal cache.
 *
 * Returns: a #gboolean indicating whether the cache actually required an
 * update (so that the caller can decide whether to request a commit to
 * long term storage or not). %TRUE indicates the cache was updated and
 * may not be in sync with the store any longer, %FALSE indicates we already
 * held the value supplied.
 */
gboolean
mcd_storage_set_string (McdStorage *self,
    const gchar *account,
    const gchar *key,
    const gchar *val,
    gboolean secret)
{
  gboolean updated = FALSE;
  gchar *old = g_key_file_get_string (self->keyfile, account, key, NULL);

  g_return_val_if_fail (MCD_IS_STORAGE (self), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  if (val == NULL)
    g_key_file_remove_key (self->keyfile, account, key, NULL);
  else
    g_key_file_set_string (self->keyfile, account, key, val);

  if (tp_strdiff (old, val))
    {
      if (secret)
        {
          McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);

          mcp_account_manager_parameter_make_secret (ma, account, key);
        }

      update_storage (self, account, key);
      updated = TRUE;
    }

  g_free (old);

  return updated;
}

/*
 * mcd_storage_set_value:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 * @key: the key (name) of the parameter or setting
 * @value: the value to be stored (or %NULL to erase it)
 * @secret: whether the value is confidential (might get stored in the
 * keyring, for example)
 *
 * Copies and stores the supplied @value (or removes it if %NULL) to the
 * internal cache.
 *
 * Returns: a #gboolean indicating whether the cache actually required an
 * update (so that the caller can decide whether to request a commit to
 * long term storage or not). %TRUE indicates the cache was updated and
 * may not be in sync with the store any longer, %FALSE indicates we already
 * held the value supplied.
 */
gboolean
mcd_storage_set_value (McdStorage *self,
    const gchar *name,
    const gchar *key,
    const GValue *value,
    gboolean secret)
{
  g_return_val_if_fail (MCD_IS_STORAGE (self), FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  if (value == NULL)
    {
      return mcd_storage_set_string (self, name, key, NULL, secret);
    }
  else
    {
      gboolean updated = FALSE;
      gchar *old = g_key_file_get_value (self->keyfile, name, key, NULL);
      gchar *new = NULL;
      gchar *buf = NULL;

      switch (G_VALUE_TYPE (value))
        {
          case G_TYPE_STRING:
            g_key_file_set_string (self->keyfile, name, key,
                g_value_get_string (value));
            break;

          case G_TYPE_UINT:
            buf = g_strdup_printf ("%u", g_value_get_uint (value));
            break;

          case G_TYPE_INT:
            g_key_file_set_integer (self->keyfile, name, key,
                g_value_get_int (value));
            break;

          case G_TYPE_BOOLEAN:
            g_key_file_set_boolean (self->keyfile, name, key,
                g_value_get_boolean (value));
            break;

          case G_TYPE_UCHAR:
            buf = g_strdup_printf ("%u", g_value_get_uchar (value));
            break;

          case G_TYPE_UINT64:
            buf = g_strdup_printf ("%" G_GUINT64_FORMAT,
                                   g_value_get_uint64 (value));
            break;

          case G_TYPE_INT64:
            buf = g_strdup_printf ("%" G_GINT64_FORMAT,
                                   g_value_get_int64 (value));
            break;

          case G_TYPE_DOUBLE:
            g_key_file_set_double (self->keyfile, name, key,
                g_value_get_double (value));
            break;

          default:
            if (G_VALUE_HOLDS (value, G_TYPE_STRV))
              {
                gchar **strings = g_value_get_boxed (value);

                g_key_file_set_string_list (self->keyfile, name, key,
                    (const gchar **)strings,
                    g_strv_length (strings));
              }
            else if (G_VALUE_HOLDS (value, DBUS_TYPE_G_OBJECT_PATH))
              {
                g_key_file_set_string (self->keyfile, name, key,
                    g_value_get_boxed (value));
              }
            else if (G_VALUE_HOLDS (value, TP_ARRAY_TYPE_OBJECT_PATH_LIST))
              {
                GPtrArray *arr = g_value_get_boxed (value);

                g_key_file_set_string_list (self->keyfile, name, key,
                    (const gchar * const *) arr->pdata, arr->len);
              }
            else
              {
                g_warning ("Unexpected param type %s",
                    G_VALUE_TYPE_NAME (value));
                return FALSE;
              }
        }

      if (buf != NULL)
        g_key_file_set_string (self->keyfile, name, key, buf);

      new = g_key_file_get_value (self->keyfile, name, key, NULL);

      if (tp_strdiff (old, new))
        {
          if (secret)
            {
              McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);

              mcp_account_manager_parameter_make_secret (ma, name, key);
            }

          update_storage (self, name, key);
          updated = TRUE;
        }

      g_free (new);
      g_free (buf);
      g_free (old);

      return updated;
    }
}

/*
 * mcd_storage_create_account:
 * @storage: An object implementing the #McdStorage interface
 * @provider: the desired storage provider, or %NULL
 * @manager: the name of the manager
 * @protocol: the name of the protocol
 * @params: A gchar * / GValue * hash table of account parameters
 * @error: a #GError to fill when returning %NULL
 *
 * Create a new account in storage. This should not store any
 * information on the long term storage until mcd_storage_commit() is called.
 *
 * See mcp_account_storage_create().
 *
 * Returns: the unique name to use for the new account, or %NULL on error.
 */
gchar *
mcd_storage_create_account (McdStorage *self,
    const gchar *provider,
    const gchar *manager,
    const gchar *protocol,
    GHashTable *params,
    GError **error)
{
  GList *store;
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);

  g_return_val_if_fail (MCD_IS_STORAGE (self), NULL);
  g_return_val_if_fail (!tp_str_empty (manager), NULL);
  g_return_val_if_fail (!tp_str_empty (protocol), NULL);

  /* If a storage provider is specified, use only it or fail */
  if (provider != NULL)
    {
      for (store = stores; store != NULL; store = g_list_next (store))
        {
          McpAccountStorage *plugin = store->data;

          if (!tp_strdiff (mcp_account_storage_provider (plugin), provider))
            {
              return mcp_account_storage_create (plugin, ma, manager,
                  protocol, params, error);
            }
        }

      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Storage provider '%s' does not exist", provider);

      return NULL;
    }

  /* No provider specified, let's pick the first plugin able to create this
   * account in priority order.
   *
   * FIXME: This is rather subtle, and relies on the fact that accounts
   * aren't always strongly tied to a single plugin.
   *
   * For plugins that only store their accounts set up specifically
   * through them (like the libaccounts/SSO pseudo-plugin,
   * McdAccountManagerSSO), create() will fail as unimplemented,
   * and we'll fall through to the next plugin. Eventually we'll
   * reach the default keyfile+gnome-keyring plugin, or another
   * plugin that accepts arbitrary accounts. When set() is called,
   * the libaccounts/SSO plugin will reject that too, and again,
   * we'll fall through to a plugin that accepts arbitrary
   * accounts.
   *
   * Plugins that will accept arbitrary accounts being created
   * via D-Bus (like the default keyfile+gnome-keyring plugin,
   * and the account-diversion plugin in tests/twisted)
   * should, in principle, implement create() to be successful.
   * If they do, their create() will succeed, and later, so will
   * their set().
   *
   * We can't necessarily rely on all such plugins implementing
   * create(), because it isn't a mandatory part of the plugin
   * API (it was added later). However, as it happens, the
   * default plugin returns successfully from create() without
   * really doing anything. When we iterate through the accounts again
   * to call set(), higher-priority plugins are given a second
   * chance to intercept that; so we end up with create() in
   * the default plugin being followed by set() from the
   * higher-priority plugin. In theory that's bad because it
   * splits the account across two plugins, but in practice
   * it isn't a problem because the default plugin's create()
   * doesn't really do anything anyway.
   */
  for (store = stores; store != NULL; store = g_list_next (store))
    {
      McpAccountStorage *plugin = store->data;
      gchar *ret;

      ret = mcp_account_storage_create (plugin, ma, manager, protocol, params,
          error);

      if (ret != NULL)
        return ret;

      g_clear_error (error);
    }

  /* This should never happen since the default storage is always able to create
   * an account */
  g_warn_if_reached ();
  g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
      "None of the storage provider are able to create the account");

  return NULL;
}


/*
 * mcd_storage_delete_account:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 *
 * Removes an account's settings from long term storage.
 * This does not handle any of the other logic to do with removing
 * accounts, it merely ensures that no trace of the account remains
 * in long term storage once mcd_storage_commit() has been called.
 */
void
mcd_storage_delete_account (McdStorage *self,
    const gchar *account)
{
  GList *store;
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);

  g_return_if_fail (MCD_IS_STORAGE (self));
  g_return_if_fail (account != NULL);

  g_key_file_remove_group (self->keyfile, account, NULL);

  for (store = stores; store != NULL; store = g_list_next (store))
    {
      McpAccountStorage *plugin = store->data;

      mcp_account_storage_delete (plugin, ma, account, NULL);
    }
}

/*
 * mcd_storage_commit:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 *
 * Sync the long term storage (whatever it might be) with the current
 * state of our internal cache.
 */
void
mcd_storage_commit (McdStorage *self, const gchar *account)
{
  GList *store;
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);

  g_return_if_fail (MCD_IS_STORAGE (self));

  for (store = stores; store != NULL; store = g_list_next (store))
    {
      McpAccountStorage *plugin = store->data;
      const gchar *pname = mcp_account_storage_name (plugin);

      if (account != NULL)
        {
          DEBUG ("flushing plugin %s %s to long term storage", pname, account);
          mcp_account_storage_commit_one (plugin, ma, account);
        }
      else
        {
          DEBUG ("flushing plugin %s to long term storage", pname);
          mcp_account_storage_commit (plugin, ma);
        }
    }
}

/*
 * mcd_storage_set_strv:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 * @key: the key (name) of the parameter or setting
 * @strv: the string vector to be stored (where %NULL is treated as equivalent
 * to an empty vector)
 * @secret: whether the value is confidential (might get stored in the
 * keyring, for example)
 *
 * Copies and stores the supplied string vector to the internal cache.
 *
 * Returns: a #gboolean indicating whether the cache actually required an
 * update (so that the caller can decide whether to request a commit to
 * long term storage or not). %TRUE indicates the cache was updated and
 * may not be in sync with the store any longer, %FALSE indicates we already
 * held the value supplied.
 */
gboolean
mcd_storage_set_strv (McdStorage *storage,
    const gchar *account,
    const gchar *key,
    const gchar * const *strv,
    gboolean secret)
{
  GValue v = G_VALUE_INIT;
  static const gchar * const *empty = { NULL };
  gboolean ret;

  g_return_val_if_fail (MCD_IS_STORAGE (storage), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  g_value_init (&v, G_TYPE_STRV);
  g_value_set_static_boxed (&v, strv == NULL ? empty : strv);
  ret = mcd_storage_set_value (storage, account, key, &v, secret);
  g_value_unset (&v);
  return ret;
}

void
mcd_storage_ready (McdStorage *self)
{
  GList *store;
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);

  for (store = stores; store != NULL; store = g_list_next (store))
    {
      McpAccountStorage *plugin = store->data;
      const gchar *plugin_name = mcp_account_storage_name (plugin);

      DEBUG ("Unblocking async account ops by %s", plugin_name);
      mcp_account_storage_ready (plugin, ma);
    }
}

static void
plugin_iface_init (McpAccountManagerIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  DEBUG ();

  iface->get_value = get_value;
  iface->set_value = set_value;
  iface->is_secret = is_secret;
  iface->make_secret = make_secret;
  iface->unique_name = unique_name;
  iface->list_keys = list_keys;
}
