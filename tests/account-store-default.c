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
  return g_key_file_get_string (default_keyfile (), account, key, NULL);
}

gboolean
default_set (const gchar *account,
    const gchar *key,
    const gchar *value)
{
  GKeyFile *keyfile = NULL;

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
