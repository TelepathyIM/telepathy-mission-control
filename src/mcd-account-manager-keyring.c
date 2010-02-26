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

#include <string.h>
#include "mcd-account-manager-keyring.h"
#include "mcd-debug.h"

#define PLUGIN_NAME "gnome-keyring"
#define PLUGIN_PRIORITY ACCOUNT_STORAGE_PLUGIN_PRIO_KEYRING
#define PLUGIN_DESCRIPTION "gnome keyring account storage backend"

#if ENABLE_GNOME_KEYRING
#include <gnome-keyring.h>

GnomeKeyringPasswordSchema keyring_schema =
  { GNOME_KEYRING_ITEM_GENERIC_SECRET,
    { { "account", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { "param",   GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { NULL,      0 } } };

typedef struct
{
  gchar *acct;
  gchar *name;
  gboolean set;
} KeyringSetData;

#endif

static void account_storage_iface_init (McpAccountStorageIface *,
    gpointer);

G_DEFINE_TYPE_WITH_CODE (McdAccountManagerKeyring, mcd_account_manager_keyring,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_STORAGE,
        account_storage_iface_init));

static void
mcd_account_manager_keyring_init (McdAccountManagerKeyring *self)
{
  DEBUG ("mcd_account_manager_keyring_init");
  self->keyfile = g_key_file_new ();
  self->removed = g_key_file_new ();
  self->removed_accounts =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
mcd_account_manager_keyring_class_init (McdAccountManagerKeyringClass *cls)
{
  DEBUG ("mcd_account_manager_keyring_class_init");
}

#if ENABLE_GNOME_KEYRING
static gboolean
_set (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *acct,
    const gchar *key,
    const gchar *val)
{
  McdAccountManagerKeyring *amk = MCD_ACCOUNT_MANAGER_KEYRING (self);

  /* uninterested in non-secret parameters */
  DEBUG ("parameter %s", key);
  if (!mcp_account_manager_parameter_is_secret (am, acct, key))
    return FALSE;

  DEBUG ("parameter %s is SECRET", key);
  if (!gnome_keyring_is_available ())
    return FALSE;

  DEBUG ("KEYRING AVAILABLE");
  amk->save = TRUE;
  g_key_file_set_string (amk->keyfile, acct, key, val);

  /* if we removed the account before, it now exists again, so... */
  g_hash_table_remove (amk->removed_accounts, acct);

  /* likewise the param should no longer be on the deleted list */
  g_key_file_remove_key (amk->removed, acct, key, NULL);

  return TRUE;
}

static gboolean
_get (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *acct,
    const gchar *key)
{
  McdAccountManagerKeyring *amk = MCD_ACCOUNT_MANAGER_KEYRING (self);

  /* no need to check for the keyring daemon here: if there's no daemon, *
   * we'll have no value for the key and will thus return false          */

  if (key != NULL)
    {
      gchar *v = g_key_file_get_string (amk->keyfile, acct, key, NULL);

      if (v == NULL)
        return FALSE;

      /* if it's from the keyring, we remember it is a secret */
      mcp_account_manager_parameter_make_secret (am, acct, key);
      mcp_account_manager_set_value (am, acct, key, v);

      g_free (v);
    }
  else
    {
      gsize i;
      gsize n;
      GStrv keys = g_key_file_get_keys (amk->keyfile, acct, &n, NULL);

      for (i = 0; i < n; i++)
        {
          gchar *v = g_key_file_get_string (amk->keyfile, acct, keys[i], NULL);
          if (v == NULL)
            continue;

          /* if it's from the keyring, we remember it is a secret */
          mcp_account_manager_parameter_make_secret (am, acct, keys[i]);
          mcp_account_manager_set_value (am, acct, keys[i], v);

          g_free (v);
        }

      g_strfreev (keys);
    }

  return TRUE;
}

static gboolean
_delete (const McpAccountStorage *self,
      const McpAccountManager *am,
      const gchar *acct,
      const gchar *key)
{
  McdAccountManagerKeyring *amk = MCD_ACCOUNT_MANAGER_KEYRING (self);

  if (key == NULL)
    {
      if (g_key_file_remove_group (amk->keyfile, acct, NULL))
        {
          gchar *removed = g_strdup (acct);
          amk->save = TRUE;
          g_hash_table_insert (amk->removed_accounts, removed, removed);
        }
    }
  else
    {
      gsize n;
      GStrv keys;
      if (g_key_file_remove_key (amk->keyfile, acct, key, NULL))
        amk->save = TRUE;
      keys = g_key_file_get_keys (amk->keyfile, acct, &n, NULL);

      /* if we deleted the last param, flag the account as purged   */
      if (keys == NULL || n == 0)
        {
          gchar *removed = g_strdup (acct);
          g_key_file_remove_group (amk->keyfile, acct, NULL);
          amk->save = TRUE;
          g_hash_table_insert (amk->removed_accounts, removed, removed);
        }
      /* if we just zapped a parameter, remember to purge on commit */
      else
        {
          g_key_file_set_value (amk->removed, acct, key, "");
        }

      g_strfreev (keys);
    }

  return TRUE;
}

static void
_commit_remove_account (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *acct)
{
  GList *i;
  GList *items;
  GnomeKeyringAttributeList *match = gnome_keyring_attribute_list_new ();
  GnomeKeyringResult ok;

  gnome_keyring_attribute_list_append_string (match, "account", acct);

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

/* theoretically this could be badgered to try and recover if the keyring *
 * is on fire and sinking, but we won't bother for now.                   */
static void
_commit_set_cb (GnomeKeyringResult result,
    gpointer data)
{
  KeyringSetData *ksd = data;

  if (result != GNOME_KEYRING_RESULT_OK)
    g_warning ("failed to save %s.%s : %s", ksd->acct, ksd->name,
        gnome_keyring_result_to_message (result));
  else
    DEBUG ("%s %s.%s in gnome keyring",
        ksd->set ? "saved" : "deleted",
        ksd->acct,
        ksd->name);

  g_free (ksd->acct);
  g_free (ksd->name);
  g_slice_free(KeyringSetData, ksd);
}

static gboolean
_commit (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  gpointer acct;
  GHashTableIter account;
  McdAccountManagerKeyring *amk = MCD_ACCOUNT_MANAGER_KEYRING (self);
  gsize i;
  gsize n;
  GStrv accts;

  if (!gnome_keyring_is_available ())
    return FALSE;

  if (!amk->save)
    return TRUE;

  /* purge any entirely removed accounts */
  g_hash_table_iter_init (&account, amk->removed_accounts);
  while (g_hash_table_iter_next (&account, &acct, NULL))
    _commit_remove_account (self, am, (gchar *) acct);
  g_hash_table_remove_all (amk->removed_accounts);

  /* purge deleted parameters for remaining accounts */
  accts = g_key_file_get_groups (amk->removed, &n);
  for (i = 0; i < n; i++)
    {
      gsize j;
      gsize k;
      GStrv keys = g_key_file_get_keys (amk->keyfile, accts[i], &k, NULL);
      for (j = 0; j < k; j++)
        {
          KeyringSetData *ksd = g_slice_new0 (KeyringSetData);

          ksd->acct = g_strdup (accts[i]);
          ksd->name = g_strdup (keys[j]);
          ksd->set = FALSE;

          gnome_keyring_delete_password (&keyring_schema,
              _commit_set_cb, ksd, NULL,
              "account", accts[i],
              "param", keys[j],
              NULL);
        }
      g_strfreev (keys);
    }
  g_strfreev (accts);

  /* forget about all the purged params completely */
  g_key_file_load_from_data (amk->removed, "#\n", -1, 0, NULL);

  /* ok, now write out the values for the accounts we have: */
  accts = g_key_file_get_groups (amk->keyfile, &n);
  for (i = 0; i < n; i++)
    {
      gsize j;
      gsize k;
      GStrv keys = g_key_file_get_keys (amk->keyfile, accts[i], &k, NULL);
      for (j = 0; j < k; j++)
        {
          gchar *name = g_strdup_printf ("account: %s; param: %s",
              accts[i], keys[j]);
          gchar *val = g_key_file_get_value (amk->keyfile,
              accts[i], keys[j], NULL);
          gchar *key = keys[j];
          KeyringSetData *ksd = g_slice_new0 (KeyringSetData);

          /* for compatibility with the old gnome keyring code we must strip *
           * the param- prefix from the name before saving to the keyring    */
          if (g_str_has_prefix (key, "param-"))
            key += strlen ("param-");

          ksd->acct = g_strdup (accts[i]);
          ksd->name = g_strdup (keys[j]);
          ksd->set = TRUE;

          gnome_keyring_store_password (&keyring_schema, NULL,
              name, val, _commit_set_cb, ksd, NULL,
              "account", accts[i],
              "param", key,
              NULL);

          g_free (val);
          g_free (name);
        }
      g_strfreev (keys);
    }
  g_strfreev (accts);

  /* any pending changes should now have been pushed, clear the save-me flag */
  amk->save = FALSE;

  return TRUE;
}

static void
_load_from_keyring (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  GList *i;
  GList *items;
  McdAccountManagerKeyring *amk = MCD_ACCOUNT_MANAGER_KEYRING (self);
  GnomeKeyringResult ok;
  GnomeKeyringAttributeList *attr = NULL;

  ok = gnome_keyring_list_item_ids_sync (NULL, &items);

  DEBUG ("%d: %s; %d items in keyring",
      ok, gnome_keyring_result_to_message (ok), g_list_length (items));

  if (ok != GNOME_KEYRING_RESULT_OK)
    goto finished;

  for (i = items; i != NULL; i = g_list_next (i))
    {
      gsize j;
      guint id = GPOINTER_TO_UINT (i->data);
      GnomeKeyringAttribute *a;
      gchar *account = NULL;

      if (attr != NULL)
        {
          gnome_keyring_attribute_list_free (attr);
          attr = NULL;
        }

      ok = gnome_keyring_item_get_attributes_sync (NULL,
          id, &attr);

      if (ok != GNOME_KEYRING_RESULT_OK)
        {
          DEBUG ("access to item #%u failed: %s", id,
              gnome_keyring_result_to_message (ok));
          continue;
        }

      for (j = 0; j < attr->len; j++)
        {
          GStrv akey;
          a = &(gnome_keyring_attribute_list_index (attr, j));

          if (strcmp (a->name, "account") != 0)
            continue;

          if (a->type != GNOME_KEYRING_ATTRIBUTE_TYPE_STRING)
            break;

          DEBUG ("possible account '%s'", a->value.string);

          /* does it look "enough" like an account for us to bother *
           * looking at it more closely?                            */
          akey = g_strsplit (a->value.string, "/", 3);
          if ((akey[0] != NULL && *akey[0] != '\0') &&
              (akey[1] != NULL && *akey[1] != '\0') &&
              (akey[2] != NULL && *akey[2] != '\0'))
            account = a->value.string;
          g_strfreev (akey);
        }

      /* nope, didn't look like an account to us */
      if (account == NULL)
        continue;

      for (j = 0; j < attr->len; j++)
        {
          gchar *secret = NULL;
          gchar *pkey = NULL;
          GnomeKeyringItemInfo *info;

          a = &(gnome_keyring_attribute_list_index (attr, j));

          /* to get this far it had an account entry which matched *
           * our schema: let's see if it has a param entry too:    */
          if (strcmp (a->name, "param") != 0)
            continue;

          if (a->type != GNOME_KEYRING_ATTRIBUTE_TYPE_STRING)
            break;

          /* apparently it did: extract the secret and push it up *
           * to the account manager via the shim interface:       */
          pkey = g_strdup_printf ("param-%s", a->value.string);
          ok = gnome_keyring_item_get_info_sync (NULL, id, &info);

          if (ok == GNOME_KEYRING_RESULT_OK)
            secret = gnome_keyring_item_info_get_secret (info);
          else
            DEBUG ("failed to retrieve secret from keyring: %s",
                gnome_keyring_result_to_message (ok));

          if (secret != NULL)
            g_key_file_set_value (amk->keyfile, account, pkey, secret);

          g_free (secret);
          g_free (pkey);

          /* we're done looking at params, go on to the next item */
          break;
        }
    }

  if (attr != NULL)
    {
      gnome_keyring_attribute_list_free (attr);
      attr = NULL;
    }

 finished:
  g_list_free (items);
}

static GList *
_list (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  gsize i;
  gsize n;
  GStrv accounts;
  GList *rval = NULL;
  McdAccountManagerKeyring *amk = MCD_ACCOUNT_MANAGER_KEYRING (self);

  if (!gnome_keyring_is_available ())
    return NULL;

  if (!amk->loaded)
    _load_from_keyring (self, am);

  accounts = g_key_file_get_groups (amk->keyfile, &n);
  for (i = 0; i < n; i++)
    rval = g_list_prepend (rval, g_strdup (accounts[i]));
  g_strfreev (accounts);

  return rval;
}

#else /* ENABLE_GNOME_KEYRING */
#define DISABLED_WARNING DEBUG (PLUGIN_NAME " disabled at build time")

static gboolean
_set (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *acct,
    const gchar *key,
    const gchar *val)
{
  DISABLED_WARNING;
  return FALSE;
}

static gboolean
_get (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *acct,
    const gchar *key)
{
  DISABLED_WARNING;
  return FALSE;
}

static gboolean
_delete (const McpAccountStorage *self,
      const McpAccountManager *am,
      const gchar *acct,
      const gchar *key)
{
  DISABLED_WARNING;
  return FALSE;
}


static gboolean
_commit (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  DISABLED_WARNING;
  return FALSE;
}

static GList *
_list (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  DISABLED_WARNING;
  return NULL;
}

#endif /* ENABLE_GNOME_KEYRING */

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

McdAccountManagerKeyring *
mcd_account_manager_keyring_new ()
{
  return g_object_new (MCD_TYPE_ACCOUNT_MANAGER_KEYRING, NULL);
}
