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

#include <errno.h>
#include <string.h>

#include <glib/gstdio.h>

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-account-manager-default.h"
#include "mcd-debug.h"
#include "mcd-misc.h"

#define PLUGIN_NAME "default-gkeyfile"
#define PLUGIN_PRIORITY MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_DEFAULT
#define PLUGIN_DESCRIPTION "GKeyFile (default) account storage backend"
#define INITIAL_CONFIG "# Telepathy accounts\n"

static void account_storage_iface_init (McpAccountStorageIface *,
    gpointer);

G_DEFINE_TYPE_WITH_CODE (McdAccountManagerDefault, mcd_account_manager_default,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_STORAGE,
        account_storage_iface_init));

static gchar *
get_old_filename (void)
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

static gchar *
account_filename_in (const gchar *dir)
{
  return g_build_filename (dir, "telepathy", "mission-control", "accounts.cfg",
      NULL);
}

static void
mcd_account_manager_default_init (McdAccountManagerDefault *self)
{
  DEBUG ("mcd_account_manager_default_init");
  self->filename = account_filename_in (g_get_user_data_dir ());
  self->keyfile = g_key_file_new ();
  self->removed = g_key_file_new ();
  self->removed_accounts =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  self->save = FALSE;
  self->loaded = FALSE;
}

static void
mcd_account_manager_default_class_init (McdAccountManagerDefaultClass *cls)
{
  DEBUG ("mcd_account_manager_default_class_init");
}

/* We happen to know that the string MC gave us is "sufficiently escaped" to
 * put it in the keyfile as-is. */
static gboolean
_set (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key,
    const gchar *val)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);

  amd->save = TRUE;

  if (val != NULL)
    g_key_file_set_value (amd->keyfile, account, key, val);
  else
    g_key_file_remove_key (amd->keyfile, account, key, NULL);

  return TRUE;
}

static gboolean
_get (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);

  if (key != NULL)
    {
      gchar *v = NULL;

      v = g_key_file_get_value (amd->keyfile, account, key, NULL);

      if (v == NULL)
        return FALSE;

      mcp_account_manager_set_value (am, account, key, v);
      g_free (v);
    }
  else
    {
      gsize i;
      gsize n;
      GStrv keys = g_key_file_get_keys (amd->keyfile, account, &n, NULL);

      if (keys == NULL)
        n = 0;

      for (i = 0; i < n; i++)
        {
          gchar *v = g_key_file_get_value (amd->keyfile, account, keys[i], NULL);

          if (v != NULL)
            mcp_account_manager_set_value (am, account, keys[i], v);

          g_free (v);
        }

      g_strfreev (keys);
    }

  return TRUE;
}

static gchar *
_create (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *manager,
    const gchar *protocol,
    GHashTable *params,
    GError **error)
{
  gchar *unique_name;

  /* See comment in plugin-account.c::_storage_create_account() before changing
   * this implementation, it's more subtle than it looks */
  unique_name = mcp_account_manager_get_unique_name (MCP_ACCOUNT_MANAGER (am),
                                                     manager, protocol, params);
  g_return_val_if_fail (unique_name != NULL, NULL);

  return unique_name;
}

static gboolean
_delete (const McpAccountStorage *self,
      const McpAccountManager *am,
      const gchar *account,
      const gchar *key)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);

  if (key == NULL)
    {
      if (g_key_file_remove_group (amd->keyfile, account, NULL))
        amd->save = TRUE;
    }
  else
    {
      gsize n;
      GStrv keys;
      gboolean save = FALSE;

      save = g_key_file_remove_key (amd->keyfile, account, key, NULL);

      if (save)
        amd->save = TRUE;

      keys = g_key_file_get_keys (amd->keyfile, account, &n, NULL);

      /* if that was the last parameter, the account is gone too */
      if (keys == NULL || n == 0)
        {
          g_key_file_remove_group (amd->keyfile, account, NULL);
        }

      g_strfreev (keys);
    }

  return TRUE;
}


static gboolean
_commit (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account)
{
  gsize n;
  gchar *data;
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);
  gboolean rval = FALSE;
  gchar *dir;
  GError *error = NULL;

  if (!amd->save)
    return TRUE;

  dir = g_path_get_dirname (amd->filename);

  DEBUG ("Saving accounts to %s", amd->filename);

  if (!mcd_ensure_directory (dir, &error))
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
      /* fall through anyway: writing to the file will fail, but it does
       * give us a chance to commit to the keyring too */
    }

  g_free (dir);

  data = g_key_file_to_data (amd->keyfile, &n, NULL);
  rval = g_file_set_contents (amd->filename, data, n, &error);

  if (rval)
    {
      amd->save = FALSE;
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  g_free (data);

  return rval;
}

static void
am_default_load_keyfile (McdAccountManagerDefault *self,
    const gchar *filename)
{
  GError *error = NULL;

  if (g_key_file_load_from_file (self->keyfile, filename,
        G_KEY_FILE_KEEP_COMMENTS, &error))
    {
      DEBUG ("Loaded accounts from %s", filename);
    }
  else
    {
      DEBUG ("Failed to load accounts from %s: %s", filename, error->message);
      g_error_free (error);

      /* Start with a blank configuration, but do not save straight away;
       * we don't want to overwrite a corrupt-but-maybe-recoverable
       * configuration file with an empty one until given a reason to
       * do so. */
      g_key_file_load_from_data (self->keyfile, INITIAL_CONFIG, -1,
          G_KEY_FILE_KEEP_COMMENTS, NULL);
    }
}

static GList *
_list (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  gsize i;
  gsize n;
  GStrv accounts;
  GList *rval = NULL;
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);

  if (!amd->loaded && g_file_test (amd->filename, G_FILE_TEST_EXISTS))
    {
      /* If the file exists, but loading it fails, we deliberately
       * do not fall through to the "initial configuration" case,
       * because we don't want to overwrite a corrupted file
       * with an empty one until an actual write takes place. */
      am_default_load_keyfile (amd, amd->filename);
      amd->loaded = TRUE;
    }

  if (!amd->loaded)
    {
      const gchar * const *iter;

      for (iter = g_get_system_data_dirs ();
          iter != NULL && *iter != NULL;
          iter++)
        {
          gchar *filename = account_filename_in (*iter);

          if (g_file_test (filename, G_FILE_TEST_EXISTS))
            {
              am_default_load_keyfile (amd, filename);
              amd->loaded = TRUE;
              /* Do not set amd->save: we don't need to write it to a
               * higher-priority directory until it actually changes. */
            }

          g_free (filename);

          if (amd->loaded)
            break;
        }
    }

  if (!amd->loaded)
    {
      gchar *old_filename = get_old_filename ();

      if (g_file_test (old_filename, G_FILE_TEST_EXISTS))
        {
          am_default_load_keyfile (amd, old_filename);
          amd->loaded = TRUE;
          amd->save = TRUE;

          if (_commit (self, am, NULL))
            {
              DEBUG ("Migrated %s to new location: deleting old copy",
                  old_filename);
              if (g_unlink (old_filename) != 0)
                g_warning ("Unable to delete %s: %s", old_filename,
                    g_strerror (errno));
            }
        }

      g_free (old_filename);
    }

  if (!amd->loaded)
    {
      DEBUG ("Creating initial account data");
      g_key_file_load_from_data (amd->keyfile, INITIAL_CONFIG, -1,
          G_KEY_FILE_KEEP_COMMENTS, NULL);
      amd->loaded = TRUE;
      amd->save = TRUE;
      _commit (self, am, NULL);
    }

  accounts = g_key_file_get_groups (amd->keyfile, &n);

  for (i = 0; i < n; i++)
    {
      rval = g_list_prepend (rval, g_strdup (accounts[i]));
    }

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
  iface->create = _create;
  iface->delete = _delete;
  iface->commit_one = _commit;
  iface->list = _list;

}

McdAccountManagerDefault *
mcd_account_manager_default_new (void)
{
  return g_object_new (MCD_TYPE_ACCOUNT_MANAGER_DEFAULT, NULL);
}
