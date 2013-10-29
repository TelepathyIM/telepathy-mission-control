/*
 * The default account manager storage pseudo-plugin
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010-2012 Collabora Ltd.
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

#define PLUGIN_NAME "default"
#define PLUGIN_PRIORITY MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_DEFAULT
#define PLUGIN_DESCRIPTION "Default account storage backend"

typedef struct {
    /* owned string, attribute => owned string, value
     * attributes to be stored in the variant-file */
    GHashTable *attributes;
    /* owned string, parameter (without "param-") => owned string, value
     * parameters to be stored in the variant-file */
    GHashTable *untyped_parameters;
    /* TRUE if the entire account is pending deletion */
    gboolean pending_deletion;
    /* TRUE if the account doesn't really exist, but is here to stop us
     * loading it from a lower-priority file */
    gboolean absent;
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
  sa->absent = FALSE;
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
accounts_cfg_in (const gchar *dir)
{
  return g_build_filename (dir, "telepathy", "mission-control", "accounts.cfg",
      NULL);
}

static gchar *
account_directory_in (const gchar *dir)
{
  return g_build_filename (dir, "telepathy", "mission-control", NULL);
}

static gchar *
account_file_in (const gchar *dir,
    const gchar *account)
{
  gchar *basename = g_strdup_printf ("%s.account", account);
  gchar *ret;

  g_strdelimit (basename, "/", '-');
  ret = g_build_filename (dir, "telepathy", "mission-control",
      basename, NULL);
  g_free (basename);
  return ret;
}

static void
mcd_account_manager_default_init (McdAccountManagerDefault *self)
{
  DEBUG ("mcd_account_manager_default_init");
  self->directory = account_directory_in (g_get_user_data_dir ());
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

/* The value is escaped as if for a keyfile */
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

      if (sa == NULL || sa->absent)
        return FALSE;

      v = g_hash_table_lookup (sa->untyped_parameters, parameter);

      if (v == NULL)
        return FALSE;

      mcp_account_manager_set_value (am, account, prefixed, v);
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

  if (sa == NULL || sa->absent)
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

  if (sa == NULL || sa->absent)
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
am_default_commit_one (McdAccountManagerDefault *self,
    const gchar *account_name,
    McdDefaultStoredAccount *sa)
{
  gchar *filename;
  GHashTableIter inner;
  gpointer k, v;
  GVariantBuilder params_builder;
  GVariantBuilder attrs_builder;
  GVariant *content;
  gchar *content_text;
  gboolean ret;
  GError *error = NULL;

  filename = account_file_in (g_get_user_data_dir (), account_name);

  if (sa->pending_deletion)
    {
      const gchar * const *iter;

      DEBUG ("Deleting account %s from %s", account_name, filename);

      if (g_unlink (filename) != 0)
        {
          int e = errno;

          /* ENOENT is OK, anything else is more upsetting */
          if (e != ENOENT)
            {
              WARNING ("Unable to delete %s: %s", filename,
                  g_strerror (e));
              g_free (filename);
              return FALSE;
            }
        }

      for (iter = g_get_system_data_dirs ();
          iter != NULL && *iter != NULL;
          iter++)
        {
          gchar *other = account_file_in (*iter, account_name);
          gboolean other_exists = g_file_test (other, G_FILE_TEST_EXISTS);

          g_free (other);

          if (other_exists)
            {
              /* There is a lower-priority file that would provide this
               * account. We can't delete a file from XDG_DATA_DIRS which
               * are conceptually read-only, but we can mask it with an
               * empty file (prior art: systemd) */
              if (!g_file_set_contents (filename, "", 0, &error))
                {
                  WARNING ("Unable to save empty account file to %s: %s",
                      filename, error->message);
                  g_clear_error (&error);
                  g_free (filename);
                  return FALSE;
                }

              break;
            }
        }

      g_free (filename);
      return TRUE;
    }

  DEBUG ("Saving account %s to %s", account_name, filename);

  g_variant_builder_init (&attrs_builder, G_VARIANT_TYPE_VARDICT);

  g_hash_table_iter_init (&inner, sa->attributes);

  while (g_hash_table_iter_next (&inner, &k, &v))
    {
      /* FIXME: for now, we put the keyfile-escaped value in a string,
       * and store that. This needs fixing to use typed attributes
       * before this code gets released. */
      g_variant_builder_add (&attrs_builder, "{sv}",
          k, g_variant_new_string (v));
    }

  g_variant_builder_init (&params_builder, G_VARIANT_TYPE ("a{ss}"));
  g_hash_table_iter_init (&inner, sa->untyped_parameters);

  while (g_hash_table_iter_next (&inner, &k, &v))
    {
      g_variant_builder_add (&params_builder, "{ss}", k, v);
    }

  g_variant_builder_add (&attrs_builder, "{sv}",
      "KeyFileParameters", g_variant_builder_end (&params_builder));

  content = g_variant_ref_sink (g_variant_builder_end (&attrs_builder));
  content_text = g_variant_print (content, TRUE);
  DEBUG ("%s", content_text);
  g_variant_unref (content);

  if (g_file_set_contents (filename, content_text, -1, &error))
    {
      ret = TRUE;
    }
  else
    {
      WARNING ("Unable to save account to %s: %s", filename,
          error->message);
      g_clear_error (&error);
      ret = FALSE;
    }

  g_free (filename);
  g_free (content_text);
  return ret;
}

static gboolean
_commit (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);
  gboolean all_succeeded = TRUE;
  GError *error = NULL;
  GHashTableIter outer;
  gpointer account_p, sa_p;

  if (!amd->save)
    return TRUE;

  DEBUG ("Saving accounts to %s", amd->directory);

  if (!mcd_ensure_directory (amd->directory, &error))
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      /* fall through anyway: writing to the files will fail, but it does
       * give us a chance to commit to the keyring too */
    }

  g_hash_table_iter_init (&outer, amd->accounts);

  while (g_hash_table_iter_next (&outer, &account_p, &sa_p))
    {
      if (account == NULL || !tp_strdiff (account, account_p))
        {
          if (!am_default_commit_one (amd, account_p, sa_p))
            all_succeeded = FALSE;
        }
    }

  if (all_succeeded)
    {
      amd->save = FALSE;
    }

  g_hash_table_iter_init (&outer, amd->accounts);

  /* forget about any entirely removed accounts */
  while (g_hash_table_iter_next (&outer, NULL, &sa_p))
    {
      McdDefaultStoredAccount *sa = sa_p;

      if (sa->pending_deletion)
        g_hash_table_iter_remove (&outer);
    }

  return all_succeeded;
}

static gboolean
am_default_load_keyfile (McdAccountManagerDefault *self,
    const gchar *filename)
{
  GError *error = NULL;
  GKeyFile *keyfile = g_key_file_new ();
  gsize i;
  gsize n = 0;
  GStrv account_tails;
  gboolean all_ok = TRUE;

  /* We shouldn't call this function without modification if we think we've
   * migrated to a newer storage format, because it doesn't check whether
   * each account has already been loaded. */
  g_assert (!self->loaded);
  g_assert (g_hash_table_size (self->accounts) == 0);

  if (g_key_file_load_from_file (keyfile, filename,
        G_KEY_FILE_KEEP_COMMENTS, &error))
    {
      DEBUG ("Loaded accounts from %s", filename);
    }
  else
    {
      DEBUG ("Failed to load accounts from %s: %s", filename, error->message);
      g_error_free (error);

      /* Start with a blank configuration. */
      g_key_file_load_from_data (keyfile, "", -1, 0, NULL);
      /* Don't delete the old file, which might be recoverable. */
      all_ok = FALSE;
    }

  account_tails = g_key_file_get_groups (keyfile, &n);

  for (i = 0; i < n; i++)
    {
      const gchar *account = account_tails[i];
      McdDefaultStoredAccount *sa = ensure_stored_account (self, account);
      gsize j;
      gsize m = 0;
      GStrv keys = g_key_file_get_keys (keyfile, account, &m, NULL);

      /* We're going to need to migrate this account. */
      self->save = TRUE;

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
  return all_ok;
}

static void
am_default_load_variant_file (McdAccountManagerDefault *self,
    const gchar *account_tail,
    const gchar *full_name)
{
  McdDefaultStoredAccount *sa;
  gchar *text = NULL;
  gsize len;
  GVariant *contents = NULL;
  GVariantIter iter;
  const gchar *k;
  GVariant *v;
  GError *error = NULL;

  DEBUG ("%s from %s", account_tail, full_name);

  sa = lookup_stored_account (self, account_tail);

  if (sa != NULL)
    {
      DEBUG ("Ignoring %s: account %s already %s",
          full_name, account_tail, sa->absent ? "masked" : "loaded");
      goto finally;
    }

  if (!g_file_get_contents (full_name, &text, &len, &error))
    {
      WARNING ("Unable to read account %s from %s: %s",
          account_tail, full_name, error->message);
      g_error_free (error);
      goto finally;
    }

  if (len == 0)
    {
      DEBUG ("Empty file %s masks account %s", full_name, account_tail);
      ensure_stored_account (self, account_tail)->absent = TRUE;
      goto finally;
    }

  contents = g_variant_parse (G_VARIANT_TYPE_VARDICT,
      text, text + len, NULL, &error);

  if (contents == NULL)
    {
      WARNING ("Unable to parse account %s from %s: %s",
          account_tail, full_name, error->message);
      g_error_free (error);
      goto finally;
    }

  sa = ensure_stored_account (self, account_tail);

  g_variant_iter_init (&iter, contents);

  while (g_variant_iter_loop (&iter, "{sv}", &k, &v))
    {
      if (!tp_strdiff (k, "KeyFileParameters"))
        {
          GVariantIter param_iter;
          gchar *parameter;
          gchar *param_value;

          if (!g_variant_is_of_type (v, G_VARIANT_TYPE ("a{ss}")))
            {
              gchar *repr = g_variant_print (v, TRUE);

              WARNING ("invalid KeyFileParameters found in %s, "
                  "ignoring: %s", full_name, repr);
              g_free (repr);
              continue;
            }

          g_variant_iter_init (&param_iter, v);

          while (g_variant_iter_next (&param_iter, "{ss}", &parameter,
                &param_value))
            {
              /* steals parameter, param_value */
              g_hash_table_insert (sa->untyped_parameters, parameter,
                  param_value);
            }
        }
      else
        {
          /* an ordinary attribute */
          /* FIXME: this is temporary and should not be released:
           * it assumes that all attributes are keyfile-escaped
           * strings */
          if (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
            {
              g_hash_table_insert (sa->attributes,
                  g_strdup (k), g_variant_dup_string (v, NULL));
            }
          else
            {
              gchar *repr = g_variant_print (v, TRUE);

              WARNING ("Not a string: %s", repr);
              g_free (repr);
            }
        }
    }

finally:
  tp_clear_pointer (&contents, g_variant_unref);
  g_free (text);
}

static void
am_default_load_directory (McdAccountManagerDefault *self,
    const gchar *directory)
{
  GDir *dir_handle;
  const gchar *basename;
  GRegex *regex;
  GError *error = NULL;

  dir_handle = g_dir_open (directory, 0, &error);

  if (dir_handle == NULL)
    {
      /* We expect ENOENT. Anything else is a cause for (minor) concern. */
      if (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        DEBUG ("%s", error->message);
      else
        WARNING ("%s", error->message);

      g_error_free (error);
      return;
    }

  DEBUG ("Looking for accounts in %s", directory);

  regex = g_regex_new ("^[A-Za-z][A-Za-z0-9_]*-"  /* CM name */
      "[A-Za-z][A-Za-z0-9_]*-"                    /* protocol with s/-/_/ */
      "[A-Za-z_][A-Za-z0-9_]*\\.account$",        /* account-specific part */
      G_REGEX_DOLLAR_ENDONLY, 0, &error);
  g_assert_no_error (error);

  while ((basename = g_dir_read_name (dir_handle)) != NULL)
    {
      gchar *full_name;
      gchar *account_tail;

      /* skip it silently if it's obviously not an account */
      if (!g_str_has_suffix (basename, ".account"))
        continue;

      /* We consider ourselves to have migrated to the new storage format
       * as soon as we find something that looks as though it ought to be an
       * account. */
      self->loaded = TRUE;

      if (!g_regex_match (regex, basename, 0, NULL))
        {
          WARNING ("Ignoring %s/%s: not a syntactically valid account",
              directory, basename);
        }

      full_name = g_build_filename (directory, basename, NULL);
      account_tail = g_strdup (basename);
      g_strdelimit (account_tail, "-", '/');
      g_strdelimit (account_tail, ".", '\0');

      am_default_load_variant_file (self, account_tail, full_name);

      g_free (account_tail);
      g_free (full_name);
    }
}

static GList *
_list (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  GList *rval = NULL;
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);
  GHashTableIter hash_iter;
  gchar *migrate_from = NULL;
  gpointer k, v;

  if (!amd->loaded)
    {
      const gchar * const *iter;

      am_default_load_directory (amd, amd->directory);

      /* We do this even if am_default_load_directory() succeeded, and
       * do not stop when amd->loaded becomes true. If XDG_DATA_HOME
       * contains gabble-jabber-example_2eexample_40com.account, that doesn't
       * mean a directory in XDG_DATA_DIRS doesn't also contain
       * haze-msn-example_2ehotmail_40com.account or something, which
       * should also be loaded. */
      for (iter = g_get_system_data_dirs ();
          iter != NULL && *iter != NULL;
          iter++)
        {
          gchar *dir = account_directory_in (*iter);

          am_default_load_directory (amd, dir);
          g_free (dir);
        }
    }

  if (!amd->loaded)
    {
      migrate_from = accounts_cfg_in (g_get_user_data_dir ());

      if (g_file_test (migrate_from, G_FILE_TEST_EXISTS))
        {
          if (!am_default_load_keyfile (amd, migrate_from))
            tp_clear_pointer (&migrate_from, g_free);

          amd->loaded = TRUE;
        }
      else
        {
          tp_clear_pointer (&migrate_from, g_free);
        }
    }

  if (!amd->loaded)
    {
      const gchar * const *iter;

      for (iter = g_get_system_data_dirs ();
          iter != NULL && *iter != NULL;
          iter++)
        {
          /* not setting migrate_from here - XDG_DATA_DIRS are conceptually
           * read-only, so we don't want to delete these files */
          gchar *filename = accounts_cfg_in (*iter);

          if (g_file_test (filename, G_FILE_TEST_EXISTS))
            {
              am_default_load_keyfile (amd, filename);
              amd->loaded = TRUE;
            }

          g_free (filename);

          if (amd->loaded)
            break;
        }
    }

  if (!amd->loaded)
    {
      migrate_from = get_old_filename ();

      if (g_file_test (migrate_from, G_FILE_TEST_EXISTS))
        {
          if (!am_default_load_keyfile (amd, migrate_from))
            tp_clear_pointer (&migrate_from, g_free);
          amd->loaded = TRUE;
          amd->save = TRUE;
        }
      else
        {
          tp_clear_pointer (&migrate_from, g_free);
        }
    }

  if (!amd->loaded)
    {
      DEBUG ("Creating initial account data");
      amd->loaded = TRUE;
      amd->save = TRUE;
    }

  if (amd->save)
    {
      DEBUG ("Saving initial or migrated account data");

      if (_commit (self, am, NULL))
        {
          if (migrate_from != NULL)
            {
              DEBUG ("Migrated %s to new location: deleting old copy",
                  migrate_from);

              if (g_unlink (migrate_from) != 0)
                WARNING ("Unable to delete %s: %s", migrate_from,
                    g_strerror (errno));
            }
        }
    }

  tp_clear_pointer (&migrate_from, g_free);

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
