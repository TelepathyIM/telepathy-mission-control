/*
 * A demonstration plugin that diverts account storage to an alternate location.
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <config.h>
#include <mission-control-plugins/mission-control-plugins.h>

#define DONT_DIVERT "fakecm/fakeprotocol/dontdivert"
#define CONFFILE "mcp-test-diverted-account-plugin.conf"

#define PLUGIN_NAME  "diverted-keyfile"
#define PLUGIN_PRIORITY MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_NORMAL
#define PLUGIN_DESCRIPTION \
  "Test plugin that grabs all accounts it receives (except '" \
  DONT_DIVERT "*') and diverts them to '" CONFFILE \
  "' in g_get_user_cache_dir () instead of the usual location."

#define DEBUG g_debug

typedef struct {
  GObject parent;
  GKeyFile *keyfile;
  gboolean save;
  gboolean loaded;
} AccountDiversionPlugin;

typedef struct {
  GObjectClass parent_class;
} AccountDiversionPluginClass;

GType account_diversion_plugin_get_type (void) G_GNUC_CONST;
static void account_storage_iface_init (McpAccountStorageIface *,
    gpointer);


#define ACCOUNT_DIVERSION_PLUGIN(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), account_diversion_plugin_get_type (), \
      AccountDiversionPlugin))

G_DEFINE_TYPE_WITH_CODE (AccountDiversionPlugin, account_diversion_plugin,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_STORAGE,
        account_storage_iface_init));

static void
account_diversion_plugin_init (AccountDiversionPlugin *self)
{
  DEBUG ("account_diversion_plugin_init");
  self->keyfile = g_key_file_new ();
  self->save = FALSE;
  self->loaded = FALSE;
}

static void
account_diversion_plugin_class_init (AccountDiversionPluginClass *cls)
{
  DEBUG ("account_diversion_plugin_class_init");
}

static gchar *
_conf_filename (void)
{
  static gchar *file = NULL;

  if (file == NULL)
    {
      const gchar *dir = g_get_user_cache_dir ();

      file = g_build_path (G_DIR_SEPARATOR_S, dir, CONFFILE, NULL);
    }

  return file;
}

static gboolean
_have_config (void)
{
  const gchar *file = _conf_filename ();

  DEBUG ("checking for %s", file);
  return g_file_test (file, G_FILE_TEST_EXISTS);
}

static void
_create_config (void)
{
  gchar *file = _conf_filename ();
  gchar *dir = g_path_get_dirname (file);

  g_mkdir_with_parents (dir, 0700);
  g_free (dir);

  g_file_set_contents (file, "# diverted accounts\n", -1, NULL);
  DEBUG ("created %s", file);
}

static McpAccountStorageSetResult
_set (McpAccountStorage *self,
      McpAccountManager *am,
      const gchar *account,
      const gchar *key,
      GVariant *val,
      McpParameterFlags flags)
{
  AccountDiversionPlugin *adp = ACCOUNT_DIVERSION_PLUGIN (self);
  gchar *val_str;
  gboolean changed;

  if (g_str_has_prefix (account, DONT_DIVERT))
    return MCP_ACCOUNT_STORAGE_SET_RESULT_FAILED;

  if (val == NULL)
    {
      gsize n;
      GStrv keys;

      if (g_key_file_remove_key (adp->keyfile, account, key, NULL))
        {
          adp->save = TRUE;
          changed = TRUE;
        }

      keys = g_key_file_get_keys (adp->keyfile, account, &n, NULL);

      if (keys == NULL || n == 0)
        g_key_file_remove_group (adp->keyfile, account, NULL);

      g_strfreev (keys);
    }
  else
    {
      gchar *old;

      val_str = mcp_account_manager_escape_variant_for_keyfile (am, val);

      old = g_key_file_get_value (adp->keyfile, account, key, NULL);

      if (tp_strdiff (old, val_str))
        {
          g_key_file_set_value (adp->keyfile, account, key, val_str);
          adp->save = TRUE;
          changed = TRUE;
        }

      g_free (val_str);
      g_free (old);
    }

  if (changed)
    return MCP_ACCOUNT_STORAGE_SET_RESULT_CHANGED;
  else
    return MCP_ACCOUNT_STORAGE_SET_RESULT_UNCHANGED;
}

static McpAccountStorageSetResult
_set_attribute (McpAccountStorage *self,
      McpAccountManager *am,
      const gchar *account,
      const gchar *attribute,
      GVariant *val,
      McpAttributeFlags flags)
{
  return _set (self, am, account, attribute, val, flags);
}

static McpAccountStorageSetResult
_set_parameter (McpAccountStorage *self,
      McpAccountManager *am,
      const gchar *account,
      const gchar *parameter,
      GVariant *val,
      McpParameterFlags flags)
{
  gchar *param = g_strdup_printf ("param-%s", parameter);
  gboolean ret;

  ret = _set (self, am, account, param, val, flags);
  g_free (param);

  return ret;
}

static GVariant *
_get_attribute (McpAccountStorage *self,
    McpAccountManager *am,
    const gchar *account,
    const gchar *attribute,
    const GVariantType *type,
    McpAttributeFlags *flags)
{
  AccountDiversionPlugin *adp = ACCOUNT_DIVERSION_PLUGIN (self);
  gchar *v;
  GVariant *ret;

  if (flags != NULL)
    *flags = 0;

  v = g_key_file_get_value (adp->keyfile, account, attribute, NULL);

  if (v == NULL)
    return NULL;

  ret = mcp_account_manager_unescape_variant_from_keyfile (am, v, type, NULL);
  g_free (v);
  return ret;
}

static GVariant *
_get_parameter (McpAccountStorage *self,
    McpAccountManager *am,
    const gchar *account,
    const gchar *parameter,
    const GVariantType *type,
    McpParameterFlags *flags)
{
  AccountDiversionPlugin *adp = ACCOUNT_DIVERSION_PLUGIN (self);
  gchar *key;
  gchar *v;
  GVariant *ret;

  if (flags != NULL)
    *flags = 0;

  key = g_strdup_printf ("param-%s", parameter);
  v = g_key_file_get_value (adp->keyfile, account, key, NULL);
  g_free (key);

  if (v == NULL)
    return NULL;

  ret = mcp_account_manager_unescape_variant_from_keyfile (am, v, type, NULL);
  g_free (v);
  return ret;
}

static gboolean _commit (McpAccountStorage *self,
    McpAccountManager *am,
    const gchar *account_name);

static void
delete_async (McpAccountStorage *self,
    McpAccountManager *am,
    const gchar *account,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  AccountDiversionPlugin *adp = ACCOUNT_DIVERSION_PLUGIN (self);
  GTask *task = g_task_new (adp, cancellable, callback, user_data);

  if (g_key_file_remove_group (adp->keyfile, account, NULL))
    adp->save = TRUE;

  if (_commit (self, am, account))
    {
      mcp_account_storage_emit_deleted (self, account);
      g_task_return_boolean (task, TRUE);
    }
  else
    {
      g_task_return_new_error (task, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "_commit()'s error handling is not good enough to know why "
          "I couldn't commit the deletion of %s", account);
    }

  g_object_unref (task);
}

static gboolean
delete_finish (McpAccountStorage *storage,
    GAsyncResult *res,
    GError **error)
{
  return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
_commit (McpAccountStorage *self,
    McpAccountManager *am,
    const gchar *account_name G_GNUC_UNUSED)
{
  gsize n;
  gchar *data;
  AccountDiversionPlugin *adp = ACCOUNT_DIVERSION_PLUGIN (self);
  gboolean rval = FALSE;

  /* This simple implementation ignores account_name and commits everything:
   * we're writing out the whole keyfile anyway. If MC is looping over
   * accounts, the second and subsequent accounts will find that
   * adp->save is false, so there's no write-amplification. */

  if (!adp->save)
    return TRUE;

  if (!_have_config ())
    _create_config ();

  data = g_key_file_to_data (adp->keyfile, &n, NULL);
  rval = g_file_set_contents (_conf_filename (), data, n, NULL);
  adp->save = !rval;
  g_free (data);

  return rval;
}

static GList *
_list (McpAccountStorage *self,
    McpAccountManager *am)
{
  gsize i;
  gsize n;
  GStrv accounts;
  GList *rval = NULL;
  AccountDiversionPlugin *adp = ACCOUNT_DIVERSION_PLUGIN (self);

  if (!_have_config ())
    _create_config ();

  if (!adp->loaded)
    adp->loaded = g_key_file_load_from_file (adp->keyfile, _conf_filename (),
        G_KEY_FILE_KEEP_COMMENTS, NULL);

  accounts = g_key_file_get_groups (adp->keyfile, &n);

  for (i = 0; i < n; i++)
    rval = g_list_prepend (rval, g_strdup (accounts[i]));

  g_strfreev (accounts);

  return rval;
}

static gchar *
create (McpAccountStorage *self,
    McpAccountManager *am,
    const gchar *manager,
    const gchar *protocol,
    const gchar *identification,
    GError **error)
{
  gchar *unique_name;

  unique_name = mcp_account_manager_get_unique_name (MCP_ACCOUNT_MANAGER (am),
                                                     manager, protocol,
                                                     identification);

  g_return_val_if_fail (unique_name != NULL, NULL);

  if (g_str_has_prefix (unique_name, DONT_DIVERT))
    {
      g_free (unique_name);
      return NULL;
    }

  /* No need to actually create anything: we'll happily return values
   * from get(., ., ., NULL) regardless of whether we have that account
   * in our list */

  return unique_name;
}

static void
account_storage_iface_init (McpAccountStorageIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  iface->name = PLUGIN_NAME;
  iface->desc = PLUGIN_DESCRIPTION;
  iface->priority = PLUGIN_PRIORITY;

  iface->get_attribute = _get_attribute;
  iface->get_parameter = _get_parameter;
  iface->set_attribute = _set_attribute;
  iface->set_parameter = _set_parameter;
  iface->delete_async = delete_async;
  iface->delete_finish = delete_finish;
  iface->commit = _commit;
  iface->list = _list;
  iface->create = create;
}


GObject *
mcp_plugin_ref_nth_object (guint n)
{
  DEBUG ("Initializing mcp-account-diversion-plugin (n=%u)", n);

  switch (n)
    {
    case 0:
      return g_object_new (account_diversion_plugin_get_type (), NULL);

    default:
      return NULL;
    }
}
