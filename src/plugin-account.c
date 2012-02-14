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

#include "plugin-loader.h"
#include "plugin-account.h"
#include "config.h"

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

struct _McdPluginAccountManagerClass {
    GObjectClass parent;
};

static void storage_iface_init (McdStorageIface *iface,
    gpointer unused G_GNUC_UNUSED);

static void plugin_iface_init (McpAccountManagerIface *iface,
    gpointer unused G_GNUC_UNUSED);

G_DEFINE_TYPE_WITH_CODE (McdPluginAccountManager, mcd_plugin_account_manager, \
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCD_TYPE_STORAGE, storage_iface_init);
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_MANAGER, plugin_iface_init))

static void
mcd_plugin_account_manager_init (McdPluginAccountManager *self)
{
  self->keyfile = g_key_file_new ();
  self->secrets = g_key_file_new ();
}

static void
plugin_account_manager_finalize (GObject *object)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (object);
  GObjectFinalizeFunc finalize =
    G_OBJECT_CLASS (mcd_plugin_account_manager_parent_class)->finalize;

  g_key_file_free (self->keyfile);
  g_key_file_free (self->secrets);
  self->keyfile = NULL;
  self->secrets = NULL;

  if (finalize != NULL)
    finalize (object);
}

static void
plugin_account_manager_dispose (GObject *object)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (object);
  GObjectFinalizeFunc dispose =
    G_OBJECT_CLASS (mcd_plugin_account_manager_parent_class)->dispose;

  tp_clear_object (&self->dbusd);

  if (dispose != NULL)
    dispose (object);
}

static void
plugin_account_manager_set_property (GObject *obj, guint prop_id,
	      const GValue *val, GParamSpec *pspec)
{
    McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (obj);

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
plugin_account_manager_get_property (GObject *obj, guint prop_id,
	      GValue *val, GParamSpec *pspec)
{
    McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (obj);

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
mcd_plugin_account_manager_class_init (McdPluginAccountManagerClass *cls)
{
  GObjectClass *object_class = (GObjectClass *) cls;
  GParamSpec *spec = g_param_spec_object ("dbus-daemon",
      "DBus daemon",
      "DBus daemon",
      TP_TYPE_DBUS_DAEMON,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  object_class->set_property = plugin_account_manager_set_property;
  object_class->get_property = plugin_account_manager_get_property;
  object_class->dispose = plugin_account_manager_dispose;
  object_class->finalize = plugin_account_manager_finalize;

  g_object_class_install_property (object_class, PROP_DBUS_DAEMON, spec);
}

McdPluginAccountManager *
mcd_plugin_account_manager_new ()
{
  return g_object_new (MCD_TYPE_PLUGIN_ACCOUNT_MANAGER,
      NULL);
}

void
_mcd_plugin_account_manager_set_dbus_daemon (McdPluginAccountManager *self,
    TpDBusDaemon *dbusd)
{
  GValue value = { 0 };

  g_value_init (&value, G_TYPE_OBJECT);
  g_value_take_object (&value, dbusd);

  g_object_set_property (G_OBJECT (self), "dbus-daemon", &value);
}

static gchar *
get_value (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);
  return g_key_file_get_value (self->keyfile, account, key, NULL);
}

static void
set_value (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key,
    const gchar *value)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);

  if (value != NULL)
    g_key_file_set_value (self->keyfile, account, key, value);
  else
    g_key_file_remove_key (self->keyfile, account, key, NULL);
}

static GStrv
list_keys (const McpAccountManager *ma,
           const gchar * account)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);

  return g_key_file_get_keys (self->keyfile, account, NULL, NULL);
}

static gboolean
is_secret (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);

  return g_key_file_get_boolean (self->secrets, account, key, NULL);
}

static void
make_secret (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);
  DEBUG ("flagging %s.%s as secret", account, key);
  g_key_file_set_boolean (self->secrets, account, key, TRUE);
}

static gchar *
unique_name (const McpAccountManager *ma,
    const gchar *manager,
    const gchar *protocol,
    const GHashTable *params)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);
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
_mcd_plugin_account_manager_connect_signal (const gchar *signame,
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

/* implement the McdStorage interface */
static void
_storage_load (McdStorage *self)
{
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  GList *store = NULL;

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

static GStrv
_storage_dup_accounts (McdStorage *storage, gsize *n)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (storage);

  return g_key_file_get_groups (self->keyfile, n);
}

static GStrv
_storage_dup_settings (McdStorage *storage, const gchar *account, gsize *n)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (storage);

  return g_key_file_get_keys (self->keyfile, account, n, NULL);
}

static McpAccountStorage *
_storage_get_plugin (McdStorage *storage, const gchar *account)
{
  GList *store = stores;
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (storage);
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  McpAccountStorage *owner = NULL;

  for (; store != NULL && owner == NULL; store = g_list_next (store))
    {
      McpAccountStorage *plugin = store->data;

      if (mcp_account_storage_get (plugin, ma, account, "manager"))
        owner = plugin;
    }

  return owner;
}

static gchar *
_storage_dup_string (McdStorage *storage,
    const gchar *account,
    const gchar *key)
{
  gchar *value = NULL;
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (storage);

  value = g_key_file_get_string (self->keyfile, account, key, NULL);

  return value;
}

static gboolean
_storage_has_value (McdStorage *storage,
    const gchar *account,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (storage);

  return g_key_file_has_key (self->keyfile, account, key, NULL);
}

static GValue *
_storage_dup_value (McdStorage *storage,
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
  GKeyFile *keyfile = MCD_PLUGIN_ACCOUNT_MANAGER (storage)->keyfile;

  switch (type)
    {
      case G_TYPE_STRING:
        v_string = g_key_file_get_string (keyfile, account, key, error);
        value = tp_g_value_slice_new_take_string (v_string);
        break;

      case G_TYPE_INT:
        v_int = g_key_file_get_integer (keyfile, account, key, error);
        value = tp_g_value_slice_new_int (v_int);
        break;

      case G_TYPE_INT64:
        v_int = tp_g_key_file_get_int64 (keyfile, account, key, error);
        value = tp_g_value_slice_new_int64 (v_int);
        break;

      case G_TYPE_UINT:
        v_uint = tp_g_key_file_get_uint64 (keyfile, account, key, error);

        if (v_uint > 0xFFFFFFFFU)
          g_set_error (error, MCD_ACCOUNT_ERROR,
              MCD_ACCOUNT_ERROR_GET_PARAMETER,
              "Integer is out of range");
        else
          value = tp_g_value_slice_new_uint (v_uint);
        break;

    case G_TYPE_UCHAR:
        v_int = g_key_file_get_integer (keyfile, account, key, error);

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
        v_uint = tp_g_key_file_get_uint64 (keyfile, account, key, error);
        value = tp_g_value_slice_new_uint64 (v_uint);
        break;

      case G_TYPE_BOOLEAN:
        v_bool = g_key_file_get_boolean (keyfile, account, key, error);
        value = tp_g_value_slice_new_boolean (v_bool);
        break;

      case G_TYPE_DOUBLE:
        v_double = g_key_file_get_double (keyfile, account, key, error);
        value = tp_g_value_slice_new_double (v_double);
        break;

      default:
        if (type == G_TYPE_STRV)
          {
            gchar **v =
              g_key_file_get_string_list (keyfile, account, key, NULL, error);

            value = tp_g_value_slice_new_take_boxed (G_TYPE_STRV, v);
          }
        else if (type == DBUS_TYPE_G_OBJECT_PATH)
          {
            v_string = g_key_file_get_string (keyfile, account, key, NULL);

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
            gchar **v =
              g_key_file_get_string_list (keyfile, account, key, NULL, error);
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
              g_strdup_printf ("cannot get property %s, unknown type %s",
                  key, g_type_name (type));

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

static gboolean
_storage_get_boolean (McdStorage *storage,
    const gchar *account,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (storage);

  return g_key_file_get_boolean (self->keyfile, account, key, NULL);
}

static gint
_storage_get_integer (McdStorage *storage,
    const gchar *account,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (storage);

  return g_key_file_get_integer (self->keyfile, account, key, NULL);
}

static void
update_storage (McdPluginAccountManager *self,
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

static gboolean
_storage_set_string (McdStorage *storage,
    const gchar *account,
    const gchar *key,
    const gchar *val,
    gboolean secret)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (storage);
  gboolean updated = FALSE;
  gchar *old = g_key_file_get_string (self->keyfile, account, key, NULL);

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

static gboolean
_storage_set_value (McdStorage *storage,
    const gchar *name,
    const gchar *key,
    const GValue *value,
    gboolean secret)
{
  if (value == NULL)
    {
      return _storage_set_string (storage, name, key, NULL, secret);
    }
  else
    {
      McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (storage);
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

static void
_storage_delete_account (McdStorage *storage, const gchar *account)
{
  GList *store;
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (storage);
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);

  g_key_file_remove_group (self->keyfile, account, NULL);

  for (store = stores; store != NULL; store = g_list_next (store))
    {
      McpAccountStorage *plugin = store->data;

      mcp_account_storage_delete (plugin, ma, account, NULL);
    }
}

static void
_storage_commit (McdStorage *self, const gchar *account)
{
  GList *store;
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);

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

void
_mcd_plugin_account_manager_ready (McdPluginAccountManager *self)
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
storage_iface_init (McdStorageIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  iface->load = _storage_load;
  iface->dup_accounts = _storage_dup_accounts;
  iface->dup_settings = _storage_dup_settings;

  iface->delete_account = _storage_delete_account;
  iface->set_string = _storage_set_string;
  iface->set_value = _storage_set_value;
  iface->commit = _storage_commit;

  iface->has_value = _storage_has_value;
  iface->get_storage_plugin = _storage_get_plugin;
  iface->dup_value = _storage_dup_value;
  iface->dup_string = _storage_dup_string;
  iface->get_integer = _storage_get_integer;
  iface->get_boolean = _storage_get_boolean;
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
