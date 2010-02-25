/*
 * The default account manager keyfile storage pseudo-plugin
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

#include "config.h"
#include "mcd-account-manager-default.h"
#include "mcd-debug.h"

#define PLUGIN_NAME "default-gkeyfile"
#define PLUGIN_PRIORITY ACCOUNT_STORAGE_PLUGIN_PRIO_DEFAULT
#define PLUGIN_DESCRIPTION "GKeyFile (default) account storage backend"
#define INITIAL_CONFIG "# Telepathy accounts\n"

static void account_storage_iface_init (McpAccountStorageIface *,
    gpointer);

G_DEFINE_TYPE_WITH_CODE (McdAccountManagerDefault, mcd_account_manager_default,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_STORAGE,
        account_storage_iface_init));

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
	return g_build_filename (g_get_home_dir(), base + 1, "accounts.cfg", NULL);
  else
	return g_build_filename (base, "accounts.cfg", NULL);
}

static void
mcd_account_manager_default_init (McdAccountManagerDefault *self)
{
  DEBUG ("mcd_account_manager_default_init");
  self->filename = get_account_conf_filename ();
  self->keyfile = g_key_file_new ();
  self->save = FALSE;
  self->loaded = FALSE;
}

static void
mcd_account_manager_default_class_init (McdAccountManagerDefaultClass *cls)
{
  DEBUG ("mcd_account_manager_default_class_init");
}

static gboolean
_have_config (McdAccountManagerDefault *self)
{
  DEBUG ("checking for %s", self->filename);
  return g_file_test (self->filename, G_FILE_TEST_EXISTS);
}

static void
_create_config (McdAccountManagerDefault *self)
{
  gchar *dir = g_path_get_dirname (self->filename);
  DEBUG ();
  g_mkdir_with_parents (dir, 0700);
  g_free (dir);
  g_file_set_contents (self->filename, INITIAL_CONFIG, -1, NULL);
  DEBUG ("created %s", self->filename);
}

static gboolean
_set (const McpAccountStorage *self,
    const McpAccount *am,
    const gchar *acct,
    const gchar *key,
    const gchar *val)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);

  amd->save = TRUE;
  g_key_file_set_string (amd->keyfile, acct, key, val);

  return TRUE;
}

static gboolean
_get (const McpAccountStorage *self,
    const McpAccount *am,
    const gchar *acct,
    const gchar *key)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);

  if (key != NULL)
    {
      gchar *v = g_key_file_get_string (amd->keyfile, acct, key, NULL);

      if (v == NULL)
        return FALSE;

      mcp_account_set_value (am, acct, key, v);
      g_free (v);
    }
  else
    {
      gsize i;
      gsize n;
      GStrv keys = g_key_file_get_keys (amd->keyfile, acct, &n, NULL);

      for (i = 0; i < n; i++)
        {
          gchar *v = g_key_file_get_string (amd->keyfile, acct, keys[i], NULL);
          if (v != NULL)
            mcp_account_set_value (am, acct, keys[i], v);
          g_free (v);
        }

      g_strfreev (keys);
    }

  return TRUE;
}

static gboolean
_delete (const McpAccountStorage *self,
      const McpAccount *am,
      const gchar *acct,
      const gchar *key)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);

  if (key == NULL)
    {
      if (g_key_file_remove_group (amd->keyfile, acct, NULL))
        amd->save = TRUE;
    }
  else
    {
      gsize n;
      GStrv keys;
      if (g_key_file_remove_key (amd->keyfile, acct, key, NULL))
        amd->save = TRUE;
      keys = g_key_file_get_keys (amd->keyfile, acct, &n, NULL);
      if (keys == NULL || n == 0)
        g_key_file_remove_group (amd->keyfile, acct, NULL);
      g_strfreev (keys);
    }

  return TRUE;
}


static gboolean
_commit (const McpAccountStorage *self,
    const McpAccount *am)
{
  gsize n;
  gchar *data;
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);
  gboolean rval = FALSE;

  if (!amd->save)
    return TRUE;

  if (!_have_config (amd))
    _create_config (amd);

  data = g_key_file_to_data (amd->keyfile, &n, NULL);
  rval = g_file_set_contents (amd->filename, data, n, NULL);
  amd->save = !rval;
  g_free (data);

  return rval;
}

static GList *
_list (const McpAccountStorage *self,
    const McpAccount *am)
{
  gsize i;
  gsize n;
  GStrv accounts;
  GList *rval = NULL;
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);

  if (!_have_config (amd))
    _create_config (amd);

  if (!amd->loaded)
    amd->loaded = g_key_file_load_from_file (amd->keyfile, amd->filename,
        G_KEY_FILE_KEEP_COMMENTS, NULL);

  accounts = g_key_file_get_groups (amd->keyfile, &n);
  for (i = 0; i < n; i++)
    rval = g_list_prepend (rval, g_strdup (accounts[i]));
  g_strfreev (accounts);

  return rval;
}

static void
account_storage_iface_init (McpAccountStorageIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  mcp_account_storage_iface_set_name (iface, PLUGIN_NAME);
  mcp_account_storage_iface_set_desc (iface, PLUGIN_DESCRIPTION);
  mcp_account_storage_iface_set_priority (iface, PLUGIN_PRIORITY);

  mcp_account_storage_iface_implement_get (iface, _get);
  mcp_account_storage_iface_implement_set (iface, _set);
  mcp_account_storage_iface_implement_delete (iface, _delete);
  mcp_account_storage_iface_implement_commit (iface, _commit);
  mcp_account_storage_iface_implement_list (iface, _list);

}

McdAccountManagerDefault *
mcd_account_manager_default_new ()
{
  return g_object_new (MCD_TYPE_ACCOUNT_MANAGER_DEFAULT, NULL);
}
