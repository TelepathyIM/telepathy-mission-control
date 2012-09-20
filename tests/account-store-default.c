/*
 * MC account storage backend inspector, default backend
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
#include <glib.h>
#include <string.h>

#include "account-store-default.h"

#if ENABLE_GNOME_KEYRING
#include <gnome-keyring.h>

GnomeKeyringPasswordSchema keyring_schema =
  { GNOME_KEYRING_ITEM_GENERIC_SECRET,
    { { "account", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { "param",   GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { NULL,      0 } } };

static gboolean
_keyring_remove_account (const gchar *acct)
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
      ok = gnome_keyring_item_delete_sync (found->keyring, found->item_id);
      if (ok != GNOME_KEYRING_RESULT_OK)
        break;
    }

 finished:
  gnome_keyring_attribute_list_free (match);

  return ok = GNOME_KEYRING_RESULT_OK;
}

static gchar *
_get_secret_from_keyring (const gchar *account, const gchar *key)
{
  GnomeKeyringResult ok = GNOME_KEYRING_RESULT_NO_KEYRING_DAEMON;
  GnomeKeyringAttributeList *match = gnome_keyring_attribute_list_new ();
  GList *items = NULL;
  GList *i;
  gchar *secret = NULL;

  /* for compatibility with old gnome keyring code we must strip  *
   * the param- prefix from the name before loading from the keyring */
  if (g_str_has_prefix (key, "param-"))
    key += strlen ("param-");

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

          if (param != NULL && value != NULL && g_str_equal (param, key))
            secret = g_strdup (value);
        }
    }

  gnome_keyring_found_list_free (items);

 finished:
  gnome_keyring_attribute_list_free (match);

  return secret;
}

#else

static gchar *
_get_secret_from_keyring (const gchar *account,
    const gchar *key)
{
  return NULL;
}

static gboolean
_keyring_remove_account (const gchar *acct)
{
  return TRUE;
}

#endif

static const gchar *default_config (void)
{
  return g_build_filename (g_get_user_data_dir (), "telepathy",
      "mission-control", "accounts.cfg", NULL);
}

static GKeyFile * default_keyfile (void)
{
  GError *error = NULL;
  static GKeyFile *keyfile = NULL;
  const gchar *path = NULL;

  if (keyfile != NULL)
    return keyfile;

  path = default_config ();

  keyfile = g_key_file_new ();

  if (!g_key_file_load_from_file (keyfile, path, 0, &error))
    {
      if (error != NULL)
        g_warning ("keyfile '%s' error: %s", path, error->message);
      else
        g_warning ("keyfile '%s' error: unknown error", path);

      g_key_file_free (keyfile);
      g_error_free (error);
      keyfile = NULL;
    }

  return keyfile;
}

static gboolean commit_changes (void)
{
  gsize n = 0;
  gchar *data = NULL;
  gboolean done = FALSE;
  GKeyFile *keyfile = default_keyfile ();
  const gchar *config = default_config ();

  data = g_key_file_to_data (keyfile, &n, NULL);
  done = g_file_set_contents (config, data, n, NULL);

  g_free (data);

  return done;
}

gchar *
default_get (const gchar *account,
    const gchar *key)
{
  const gchar *pkey = key;
  gchar *value = NULL;

  if (g_str_has_prefix (key, "param-"))
    pkey = key + strlen("param-");

  value = _get_secret_from_keyring (account, pkey);

  if (value == NULL)
    value = g_key_file_get_string (default_keyfile (), account, key, NULL);

  return value;
}

gboolean
default_set (const gchar *account,
    const gchar *key,
    const gchar *value)
{
  GKeyFile *keyfile = NULL;

#if ENABLE_GNOME_KEYRING
  /* we want to catch, for instance, param-password or param-proxy-password */
  if (g_str_has_prefix (key, "param-") && g_str_has_suffix (key, "-password"))
    {
      GnomeKeyringResult result = GNOME_KEYRING_RESULT_CANCELLED;
      gchar *name =
        g_strdup_printf ("account: %s; param: %s", account,
            key + strlen ("param-"));

      result = gnome_keyring_store_password_sync (&keyring_schema, NULL,
          name, value,
          "account", account,
          "param", key + strlen ("param-"),
          NULL);

      g_free (name);

      return result == GNOME_KEYRING_RESULT_OK;
    }
#endif

  keyfile = default_keyfile ();

  if (keyfile == NULL)
    return FALSE;

  g_key_file_set_string (keyfile, account, key, value);

  return commit_changes ();
}

gboolean
default_delete (const gchar *account)
{
  GKeyFile *keyfile = default_keyfile ();

  g_key_file_remove_group (keyfile, account, NULL);
  _keyring_remove_account (account);

  return commit_changes ();
}

gboolean
default_exists (const gchar *account)
{
  return g_key_file_has_group (default_keyfile (), account);
}

GStrv
default_list (void)
{
  return g_key_file_get_groups (default_keyfile (), NULL);
}

guint
default_count_passwords (void)
{
#if ENABLE_GNOME_KEYRING
  GnomeKeyringResult ok = GNOME_KEYRING_RESULT_NO_KEYRING_DAEMON;
  GnomeKeyringAttributeList *match = gnome_keyring_attribute_list_new ();
  GList *items = NULL;
  guint n = 0;

  ok = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
      match, &items);

  if (ok != GNOME_KEYRING_RESULT_OK)
    goto finished;

  n = g_list_length (items);
  gnome_keyring_found_list_free (items);

 finished:
  gnome_keyring_attribute_list_free (match);

  return n;
#else
  return 0;
#endif
}
