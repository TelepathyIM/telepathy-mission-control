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

typedef struct {
    /* owned string, attribute => owned string, value
     * attributes to be stored in the keyfile */
    GHashTable *attributes;
    /* owned string, parameter (without "param-") => owned string, value
     * parameters to be stored in the keyfile */
    GHashTable *untyped_parameters;
    /* TRUE if the entire account is pending deletion */
    gboolean pending_deletion;
} McdDefaultStoredAccount;

static McdDefaultStoredAccount *
lookup_stored_account (McdAccountManagerDefault *self,
    const gchar *account)
{
  return g_hash_table_lookup (self->accounts, account);
}

static McdDefaultStoredAccount *
ensure_stored_account (McdAccountManagerDefault *self,
    const gchar *account)
{
  McdDefaultStoredAccount *sa = lookup_stored_account (self, account);

  if (sa == NULL)
    {
      sa = g_slice_new0 (McdDefaultStoredAccount);
      sa->attributes = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, g_free);
      sa->untyped_parameters = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, g_free);
      g_hash_table_insert (self->accounts, g_strdup (account), sa);
    }

  sa->pending_deletion = FALSE;
  return sa;
}

static void
stored_account_free (gpointer p)
{
  McdDefaultStoredAccount *sa = p;

  g_hash_table_unref (sa->attributes);
  g_hash_table_unref (sa->untyped_parameters);
  g_slice_free (McdDefaultStoredAccount, sa);
}

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
  self->accounts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      stored_account_free);
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
set_parameter (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *prefixed,
    const gchar *parameter,
    const gchar *val)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);
  McdDefaultStoredAccount *sa;

  sa = ensure_stored_account (amd, account);
  amd->save = TRUE;

  if (val != NULL)
    g_hash_table_insert (sa->untyped_parameters, g_strdup (parameter),
        g_strdup (val));
  else
    g_hash_table_remove (sa->untyped_parameters, parameter);

  return TRUE;
}

/* As above, the string is escaped for a keyfile. */
static gboolean
set_attribute (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *attribute,
    const gchar *val)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);
  McdDefaultStoredAccount *sa = ensure_stored_account (amd, account);

  amd->save = TRUE;

  if (val != NULL)
    g_hash_table_insert (sa->attributes, g_strdup (attribute), g_strdup (val));
  else
    g_hash_table_remove (sa->attributes, attribute);

  return TRUE;
}

static gboolean
_set (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key,
    const gchar *val)
{
  if (g_str_has_prefix (key, "param-"))
    {
      return set_parameter (self, am, account, key, key + 6, val);
    }
  else
    {
      return set_attribute (self, am, account, key, val);
    }
}

static gboolean
get_parameter (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *prefixed,
    const gchar *parameter)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);
  McdDefaultStoredAccount *sa = lookup_stored_account (amd, account);

  if (parameter != NULL)
    {
      gchar *v = NULL;

      if (sa == NULL)
        return FALSE;

      v = g_hash_table_lookup (sa->untyped_parameters, parameter);

      if (v == NULL)
        return FALSE;

      mcp_account_manager_set_value (am, account, prefixed, v);
      g_free (v);
    }
  else
    {
      g_assert_not_reached ();
    }

  return TRUE;
}

static gboolean
_get (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);
  McdDefaultStoredAccount *sa = lookup_stored_account (amd, account);

  if (sa == NULL)
    return FALSE;

  if (key != NULL)
    {
      gchar *v = NULL;

      if (g_str_has_prefix (key, "param-"))
        {
          return get_parameter (self, am, account, key, key + 6);
        }

      v = g_hash_table_lookup (sa->attributes, key);

      if (v == NULL)
        return FALSE;

      mcp_account_manager_set_value (am, account, key, v);
      g_free (v);
    }
  else
    {
      GHashTableIter iter;
      gpointer k, v;

      g_hash_table_iter_init (&iter, sa->attributes);

      while (g_hash_table_iter_next (&iter, &k, &v))
        {
          if (v != NULL)
            mcp_account_manager_set_value (am, account, k, v);
        }

      g_hash_table_iter_init (&iter, sa->untyped_parameters);

      while (g_hash_table_iter_next (&iter, &k, &v))
        {
          if (v != NULL)
            {
              gchar *prefixed = g_strdup_printf ("param-%s",
                  (const gchar *) k);

              mcp_account_manager_set_value (am, account, prefixed, v);
              g_free (prefixed);
            }
        }
    }

  return TRUE;
}

static gchar *
_create (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *manager,
    const gchar *protocol,
    const gchar *identification,
    GError **error)
{
  gchar *unique_name;

  /* See comment in plugin-account.c::_storage_create_account() before changing
   * this implementation, it's more subtle than it looks */
  unique_name = mcp_account_manager_get_unique_name (MCP_ACCOUNT_MANAGER (am),
                                                     manager, protocol,
                                                     identification);
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
  McdDefaultStoredAccount *sa = lookup_stored_account (amd, account);

  if (sa == NULL)
    {
      /* Apparently we never had this account anyway. The plugin API
       * considers this to be "success". */
      return TRUE;
    }

  if (key == NULL)
    {
      amd->save = TRUE;

      /* flag the whole account as purged */
      sa->pending_deletion = TRUE;
      g_hash_table_remove_all (sa->attributes);
      g_hash_table_remove_all (sa->untyped_parameters);
    }
  else
    {
      if (g_str_has_prefix (key, "param-"))
        {
          if (g_hash_table_remove (sa->untyped_parameters, key + 6))
            amd->save = TRUE;
        }
      else
        {
          if (g_hash_table_remove (sa->attributes, key))
            amd->save = TRUE;
        }

      /* if that was the last attribute or parameter, the account is gone
       * too */
      if (g_hash_table_size (sa->attributes) == 0 &&
          g_hash_table_size (sa->untyped_parameters) == 0)
        {
          sa->pending_deletion = TRUE;
        }
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
  GHashTableIter outer;
  gpointer account_p, sa_p;
  GKeyFile *keyfile;

  if (!amd->save)
    return TRUE;

  dir = g_path_get_dirname (amd->filename);

  DEBUG ("Saving accounts to %s", amd->filename);

  if (!mcd_ensure_directory (dir, &error))
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      /* fall through anyway: writing to the file will fail, but it does
       * give us a chance to commit to the keyring too */
    }

  g_free (dir);

  keyfile = g_key_file_new ();

  g_hash_table_iter_init (&outer, amd->accounts);

  while (g_hash_table_iter_next (&outer, &account_p, &sa_p))
    {
      McdDefaultStoredAccount *sa = sa_p;
      GHashTableIter inner;
      gpointer k, v;

      /* don't save accounts that are being deleted */
      if (sa->pending_deletion)
        continue;

      g_hash_table_iter_init (&inner, sa->attributes);

      while (g_hash_table_iter_next (&inner, &k, &v))
        g_key_file_set_value (keyfile, account_p, k, v);

      g_hash_table_iter_init (&inner, sa->untyped_parameters);

      while (g_hash_table_iter_next (&inner, &k, &v))
        {
          gchar *prefixed = g_strdup_printf ("param-%s", (const gchar *) k);

          g_key_file_set_value (keyfile, account_p, prefixed, v);
          g_free (prefixed);
        }
    }

  data = g_key_file_to_data (keyfile, &n, NULL);
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
  g_key_file_unref (keyfile);

  g_hash_table_iter_init (&outer, amd->accounts);

  /* forget about any entirely removed accounts */
  while (g_hash_table_iter_next (&outer, NULL, &sa_p))
    {
      McdDefaultStoredAccount *sa = sa_p;

      if (sa->pending_deletion)
        g_hash_table_iter_remove (&outer);
    }

  return rval;
}

static void
am_default_load_keyfile (McdAccountManagerDefault *self,
    const gchar *filename)
{
  GError *error = NULL;
  GKeyFile *keyfile = g_key_file_new ();
  gsize i;
  gsize n = 0;
  GStrv account_tails;

  if (g_key_file_load_from_file (keyfile, filename,
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
      g_key_file_load_from_data (keyfile, INITIAL_CONFIG, -1,
          G_KEY_FILE_KEEP_COMMENTS, NULL);
    }

  account_tails = g_key_file_get_groups (keyfile, &n);

  for (i = 0; i < n; i++)
    {
      const gchar *account = account_tails[i];
      McdDefaultStoredAccount *sa = ensure_stored_account (self, account);
      gsize j;
      gsize m = 0;
      GStrv keys = g_key_file_get_keys (keyfile, account, &m, NULL);

      for (j = 0; j < m; j++)
        {
          gchar *key = keys[j];
          gchar *raw = g_key_file_get_value (keyfile, account, key, NULL);

          if (g_str_has_prefix (key, "param-"))
            {
              /* steals ownership of raw */
              g_hash_table_insert (sa->untyped_parameters, g_strdup (key + 6),
                  raw);
            }
          else
            {
              /* steals ownership of raw */
              g_hash_table_insert (sa->attributes, g_strdup (key), raw);
            }
        }

      g_strfreev (keys);
    }

  g_strfreev (account_tails);
  g_key_file_unref (keyfile);
}

static GList *
_list (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  GList *rval = NULL;
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);
  GHashTableIter hash_iter;
  gpointer k, v;

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
      amd->loaded = TRUE;
      amd->save = TRUE;
      _commit (self, am, NULL);
    }

  g_hash_table_iter_init (&hash_iter, amd->accounts);

  while (g_hash_table_iter_next (&hash_iter, &k, &v))
    {
      rval = g_list_prepend (rval, g_strdup (k));
    }

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
