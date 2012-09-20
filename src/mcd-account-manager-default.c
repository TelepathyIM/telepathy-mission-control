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

#if ENABLE_GNOME_KEYRING
#include <gnome-keyring.h>

GnomeKeyringPasswordSchema keyring_schema =
  { GNOME_KEYRING_ITEM_GENERIC_SECRET,
    { { "account", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { "param",   GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { NULL,      0 } } };

typedef struct
{
  gchar *account;
  gchar *name;
  gboolean set;
} KeyringSetData;

static void
_keyring_set_cb (GnomeKeyringResult result,
    gpointer data)
{
  KeyringSetData *ksd = data;

  if (result != GNOME_KEYRING_RESULT_OK)
    g_warning ("failed to save %s.%s : %s", ksd->account, ksd->name,
        gnome_keyring_result_to_message (result));
  else
    DEBUG ("%s %s.%s in gnome keyring",
        ksd->set ? "saved" : "deleted",
        ksd->account,
        ksd->name);

  g_free (ksd->account);
  g_free (ksd->name);
  g_slice_free(KeyringSetData, ksd);
}

static void
_delete_from_keyring (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);

  if (key == NULL)
    {
      /* flag the whole account as purged */
      gchar *removed = g_strdup (account);
      g_hash_table_replace (amd->removed_accounts, removed, removed);
      g_key_file_remove_group (amd->secrets, removed, NULL);
    }
  else
    {
      /* remember to forget this one param */
      g_key_file_set_value (amd->removed, account, key, "");
      g_key_file_remove_key (amd->secrets, account, key, NULL);
    }
}

static void
_keyring_remove_account (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account)
{
  GList *i;
  GList *items;
  GnomeKeyringAttributeList *match = gnome_keyring_attribute_list_new ();
  GnomeKeyringResult ok;

  gnome_keyring_attribute_list_append_string (match, "account", account);

  ok = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
      match, &items);

  if (ok != GNOME_KEYRING_RESULT_OK)
    goto finished;

  for (i = items; i != NULL; i = g_list_next (i))
    {
      GnomeKeyringFound *found = i->data;
      gnome_keyring_item_delete_sync (found->keyring, found->item_id);
    }

 finished:
  gnome_keyring_attribute_list_free (match);
}

static void
_keyring_commit_one (const McdAccountManagerDefault *amd,
    const McpAccountManager *am,
    const gchar *account_name)
{
  gsize j;
  gsize k;
  GStrv keys = g_key_file_get_keys (amd->secrets, account_name, &k, NULL);

  if (keys == NULL)
    k = 0;

  for (j = 0; j < k; j++)
    {
      gchar *name = g_strdup_printf ("account: %s; param: %s",
          account_name, keys[j]);
      gchar *val = g_key_file_get_value (amd->secrets,
          account_name, keys[j], NULL);
      gchar *key = keys[j];
      KeyringSetData *ksd = g_slice_new0 (KeyringSetData);

      /* for compatibility with old gnome keyring code we must strip  *
       * the param- prefix from the name before saving to the keyring */
      if (g_str_has_prefix (key, "param-"))
        key += strlen ("param-");

      ksd->account = g_strdup (account_name);
      ksd->name = g_strdup (keys[j]);
      ksd->set = TRUE;

      gnome_keyring_store_password (&keyring_schema, NULL,
          name, val, _keyring_set_cb, ksd, NULL,
          "account", account_name,
          "param", key,
          NULL);

      g_free (val);
      g_free (name);
    }

  g_strfreev (keys);
}

static void
_keyring_commit (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account_name)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);
  gsize n;
  gsize i;
  GStrv accts;
  GHashTableIter iter = { 0 };
  gchar *account = NULL;

  if (!gnome_keyring_is_available ())
    return;

  /* purge any entirely removed accounts */
  g_hash_table_iter_init (&iter, amd->removed_accounts);

  while (g_hash_table_iter_next (&iter, (gpointer *) &account, NULL))
    _keyring_remove_account (self, am, (gchar *) account);

  g_hash_table_remove_all (amd->removed_accounts);

  /* purge deleted parameters for remaining accounts */
  accts = g_key_file_get_groups (amd->removed, &n);

  for (i = 0; i < n; i++)
    {
      gsize j;
      gsize k;
      GStrv keys = g_key_file_get_keys (amd->removed, accts[i], &k, NULL);

      if (keys == NULL)
        k = 0;

      for (j = 0; j < k; j++)
        {
          KeyringSetData *ksd = g_slice_new0 (KeyringSetData);
          const gchar *key = keys[j];

          /* for compatibility with old gnome keyring code we must strip  *
           * the param- prefix from the name before saving to the keyring */
          if (g_str_has_prefix (key, "param-"))
            key += strlen ("param-");

          ksd->account = g_strdup (accts[i]);
          ksd->name = g_strdup (key);
          ksd->set = FALSE;

          gnome_keyring_delete_password (&keyring_schema,
              _keyring_set_cb, ksd, NULL,
              "account", accts[i],
              "param", key,
              NULL);
        }

      g_strfreev (keys);
    }

  g_strfreev (accts);

  /* forget about all the purged params completely */
  g_key_file_load_from_data (amd->removed, "#\n", -1, 0, NULL);

  if (account_name == NULL)
    {
      /* ok, write out the values for all the accounts we have: */
      accts = g_key_file_get_groups (amd->secrets, &n);

      for (i = 0; i < n; i++)
        _keyring_commit_one (amd, am, accts[i]);

      g_strfreev (accts);
    }
  else
    {
      _keyring_commit_one (amd, am, account_name);
    }
}

static void
_get_secrets_from_keyring (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account)
{
  McdAccountManagerDefault *amd = MCD_ACCOUNT_MANAGER_DEFAULT (self);
  GnomeKeyringResult ok = GNOME_KEYRING_RESULT_NO_KEYRING_DAEMON;
  GnomeKeyringAttributeList *match = gnome_keyring_attribute_list_new ();
  GList *items = NULL;
  GList *i;

  gnome_keyring_attribute_list_append_string (match, "account", account);

  ok = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
      match, &items);

  if (ok != GNOME_KEYRING_RESULT_OK)
    goto finished;

  for (i = items; i != NULL; i = g_list_next (i))
    {
      gsize j;
      GnomeKeyringFound *entry = i->data;
      GnomeKeyringAttributeList *data = entry->attributes;

      for (j = 0; j < data->len; j++)
        {
          GnomeKeyringAttribute *attr =
            &(gnome_keyring_attribute_list_index (data, j));
          const gchar *name = attr->name;
          const gchar *value = NULL;
          const gchar *param = NULL;

          switch (attr->type)
            {
              case GNOME_KEYRING_ATTRIBUTE_TYPE_STRING:
                if (g_strcmp0 ("param", name) == 0)
                  {
                    param = attr->value.string;
                    value = entry->secret;
                  }
                break;

              default:
                g_warning ("Unsupported value type for %s.%s", account, name);
            }

          if (!tp_strdiff (param, "password"))
            {
              /* Empathy 3.0 was meant to migrate passwords from MC to
               * itself, but it couldn't complete the migration by
               * deleting the password from MC, because MC had several
               * bugs that meant deleting passwords didn't work. To atone
               * for our past sins, detect an incomplete migration and
               * complete it. */
              GnomeKeyringResult empathy_ok =
                GNOME_KEYRING_RESULT_NO_KEYRING_DAEMON;
              GnomeKeyringAttributeList *empathy_match =
                gnome_keyring_attribute_list_new ();
              GList *empathy_items = NULL;

              gnome_keyring_attribute_list_append_string (empathy_match,
                  "account-id", account);
              gnome_keyring_attribute_list_append_string (empathy_match,
                  "param-name", "password");

              empathy_ok = gnome_keyring_find_items_sync (
                  GNOME_KEYRING_ITEM_GENERIC_SECRET, empathy_match,
                  &empathy_items);

              if (empathy_ok == GNOME_KEYRING_RESULT_OK &&
                  empathy_items != NULL)
                {
                  KeyringSetData *ksd = g_slice_new0 (KeyringSetData);

                  DEBUG ("An Empathy 3.0 password migration wasn't finished "
                      "due to fd.o #42088. Finishing it now by deleting the "
                      "password for %s", account);

                  ksd->account = g_strdup (account);
                  ksd->name = g_strdup ("password");
                  ksd->set = FALSE;

                  gnome_keyring_delete_password (&keyring_schema,
                      _keyring_set_cb, ksd, NULL,
                      "account", account,
                      "param", "password",
                      NULL);
                }

              gnome_keyring_found_list_free (empathy_items);

              /* behave as if it had already been deleted, i.e. we never
               * actually found it... */
              param = NULL;
              value = NULL;
            }

          if (param != NULL && value != NULL)
            {
              gchar *key = g_strdup_printf ("param-%s", param);

              g_key_file_set_value (amd->secrets, account, key, value);
              mcp_account_manager_parameter_make_secret (am, account, key);

              g_free (key);
            }
        }
    }

  gnome_keyring_found_list_free (items);

 finished:
  gnome_keyring_attribute_list_free (match);
}

#else

static void
_delete_from_keyring (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key)
{
  return;
}

static void
_keyring_commit (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account_name)
{
  return;
}

static void
_get_secrets_from_keyring (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account)
{
  return;
}

#endif

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
  self->secrets = g_key_file_new ();
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
#if ENABLE_GNOME_KEYRING
  gboolean secret;
#endif

  amd->save = TRUE;

#if ENABLE_GNOME_KEYRING
  /* if we have a keyring, secrets are segregated */
  secret = mcp_account_manager_parameter_is_secret (am, account, key);

  /* remove it from both sets, then re-add it to the right one if non-null */
  g_key_file_remove_key (amd->secrets, account, key, NULL);
  g_key_file_remove_key (amd->keyfile, account, key, NULL);

  if (val != NULL)
    {
      if (secret)
        g_key_file_set_value (amd->secrets, account, key, val);
      else
        g_key_file_set_value (amd->keyfile, account, key, val);
    }

  /* if we removed the account before, it now exists again, so... */
  g_hash_table_remove (amd->removed_accounts, account);

  /* likewise the param should no longer be on the deleted list */
  g_key_file_remove_key (amd->removed, account, key, NULL);
#else

  if (val != NULL)
    g_key_file_set_value (amd->keyfile, account, key, val);
  else
    g_key_file_remove_key (amd->keyfile, account, key, NULL);

#endif

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

#if ENABLE_GNOME_KEYRING
      if (mcp_account_manager_parameter_is_secret (am, account, key))
        v = g_key_file_get_value (amd->secrets, account, key, NULL);

      /* fall back to public source if secret was not in keyring */
      if (v == NULL)
        v = g_key_file_get_value (amd->keyfile, account, key, NULL);
#else
      v = g_key_file_get_value (amd->keyfile, account, key, NULL);
#endif

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

#if ENABLE_GNOME_KEYRING
      keys = g_key_file_get_keys (amd->secrets, account, &n, NULL);

      if (keys == NULL)
        n = 0;

      for (i = 0; i < n; i++)
        {
          gchar *v = g_key_file_get_value (amd->secrets, account, keys[i], NULL);

          if (v != NULL)
            {
              mcp_account_manager_set_value (am, account, keys[i], v);
              mcp_account_manager_parameter_make_secret (am, account, keys[i]);
            }

          g_free (v);
        }

      g_strfreev (keys);
#endif
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
      _delete_from_keyring (self, am, account, NULL);
    }
  else
    {
      gsize n;
      GStrv keys;
      gboolean save = FALSE;

#if ENABLE_GNOME_KEYRING
      save = g_key_file_remove_key (amd->secrets, account, key, NULL);
      if (g_key_file_remove_key (amd->keyfile, account, key, NULL))
        save = TRUE;
#else
      save = g_key_file_remove_key (amd->keyfile, account, key, NULL);
#endif

      if (save)
        amd->save = TRUE;

      keys = g_key_file_get_keys (amd->keyfile, account, &n, NULL);

      /* if that was the last parameter, the account is gone too:  *
       * note that secret parameters don't keep an account alive - *
       * when the last public param dies, the account dies with it */
      if (keys == NULL || n == 0)
        {
          g_key_file_remove_group (amd->secrets, account, NULL);
          g_key_file_remove_group (amd->keyfile, account, NULL);
          _delete_from_keyring (self, am, account, NULL);
        }
      else
        {
          /* always delete from keyring, even if we didn't previously
           * think it was secret - we might have been wrong */
          _delete_from_keyring (self, am, account, key);
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
      g_error_free (error);
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

  _keyring_commit (self, am, account);

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
              DEBUG ("Migrated %s to new location: deleting old copy");
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
      _get_secrets_from_keyring (self, am, accounts[i]);
      rval = g_list_prepend (rval, g_strdup (accounts[i]));
    }

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
  mcp_account_storage_iface_implement_create (iface, _create);
  mcp_account_storage_iface_implement_delete (iface, _delete);
  mcp_account_storage_iface_implement_commit_one (iface, _commit);
  mcp_account_storage_iface_implement_list (iface, _list);

}

McdAccountManagerDefault *
mcd_account_manager_default_new (void)
{
  return g_object_new (MCD_TYPE_ACCOUNT_MANAGER_DEFAULT, NULL);
}
