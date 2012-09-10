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

static gboolean
_set (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key,
    const gchar *val)
{
  AccountDiversionPlugin *adp = ACCOUNT_DIVERSION_PLUGIN (self);

  if (g_str_has_prefix (account, DONT_DIVERT))
      return FALSE;

  adp->save = TRUE;
  g_key_file_set_value (adp->keyfile, account, key, val);

  return TRUE;
}

static gboolean
_get (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key)
{
  AccountDiversionPlugin *adp = ACCOUNT_DIVERSION_PLUGIN (self);

  if (key != NULL)
    {
      gchar *v = g_key_file_get_value (adp->keyfile, account, key, NULL);

      if (v == NULL)
        return FALSE;

      mcp_account_manager_set_value (am, account, key, v);
      g_free (v);
    }
  else
    {
      gsize i;
      gsize n;
      GStrv keys = g_key_file_get_keys (adp->keyfile, account, &n, NULL);

      if (keys == NULL)
        n = 0;

      for (i = 0; i < n; i++)
        {
          gchar *v = g_key_file_get_value (adp->keyfile, account, keys[i], NULL);

          if (v != NULL)
            mcp_account_manager_set_value (am, account, keys[i], v);

          g_free (v);
        }

      g_strfreev (keys);
    }

  return TRUE;
}

static gboolean
_delete (const McpAccountStorage *self,
      const McpAccountManager *am,
      const gchar *account,
      const gchar *key)
{
  AccountDiversionPlugin *adp = ACCOUNT_DIVERSION_PLUGIN (self);

  if (key == NULL)
    {
      if (g_key_file_remove_group (adp->keyfile, account, NULL))
        adp->save = TRUE;
    }
  else
    {
      gsize n;
      GStrv keys;

      if (g_key_file_remove_key (adp->keyfile, account, key, NULL))
        adp->save = TRUE;

      keys = g_key_file_get_keys (adp->keyfile, account, &n, NULL);

      if (keys == NULL || n == 0)
        g_key_file_remove_group (adp->keyfile, account, NULL);

      g_strfreev (keys);
    }

  return TRUE;
}


static gboolean
_commit (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  gsize n;
  gchar *data;
  AccountDiversionPlugin *adp = ACCOUNT_DIVERSION_PLUGIN (self);
  gboolean rval = FALSE;

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
_list (const McpAccountStorage *self,
    const McpAccountManager *am)
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

static void
account_storage_iface_init (McpAccountStorageIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  iface->name = PLUGIN_NAME;
  iface->desc = PLUGIN_DESCRIPTION;
  iface->priority = PLUGIN_PRIORITY;

  iface->get = _get;
  iface->set = _set;
  iface->delete = _delete;
  iface->commit = _commit;
  iface->list = _list;
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
