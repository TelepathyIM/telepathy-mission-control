/*
 * MC account storage inspector: MC 5.14 GVariant-file backend
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010-2013 Collabora Ltd.
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
#include "account-store-variant-file.h"

#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>

static gchar *
get_path (const gchar *account)
{
  gchar *ret;
  gchar *basename;

  basename = g_strdup_printf ("%s.account", account);
  g_strdelimit (basename, "/", '-');

  ret = g_build_filename (g_get_user_data_dir (), "telepathy-1",
      "mission-control", basename, NULL);
  g_free (basename);
  return ret;
}

static GVariant *
load (const gchar *account)
{
  GError *error = NULL;
  gchar *contents = NULL;
  gsize len;
  GVariant *ret = NULL;
  gchar *path = NULL;

  path = get_path (account);

  if (!g_file_get_contents (path, &contents, &len, &error))
    goto finally;

  ret = g_variant_parse (G_VARIANT_TYPE_VARDICT, contents, contents + len,
        NULL, &error);

finally:
  if (error != NULL)
      g_warning ("variant file '%s' error: %s", path, error->message);

  g_free (path);
  g_clear_error (&error);
  g_free (contents);
  return ret;
}

gchar *
variant_get (const gchar *account,
    const gchar *key)
{
  GVariant *asv = load (account);
  GVariant *v = NULL;
  GString *ret = NULL;

  if (asv == NULL)
    return NULL;

  if (g_str_has_prefix (key, "param-"))
    {
      GVariant *intermediate = g_variant_lookup_value (asv,
          "Parameters", NULL);

      if (intermediate != NULL)
        {
          g_assert (g_variant_is_of_type (intermediate,
                G_VARIANT_TYPE ("a{sv}")));
          v = g_variant_lookup_value (intermediate, key + 6, NULL);
        }

      intermediate = g_variant_lookup_value (asv,
          "KeyFileParameters", NULL);

      if (v == NULL && intermediate != NULL)
        {
          g_assert (g_variant_is_of_type (intermediate,
                G_VARIANT_TYPE ("a{ss}")));
          v = g_variant_lookup_value (intermediate, key + 6,
              G_VARIANT_TYPE_STRING);

          if (v != NULL)
            ret = g_string_new ("keyfile-escaped ");
        }
    }
  else
    {
      v = g_variant_lookup_value (asv, key, NULL);
    }

  if (v != NULL)
    {
      ret = g_variant_print_string (v, ret, TRUE);
      g_variant_unref (v);
    }

  g_variant_unref (asv);

  if (ret == NULL)
    return NULL;

  return g_string_free (ret, FALSE);
}

gboolean
variant_delete (const gchar *account)
{
  gchar *path = get_path (account);

  if (g_unlink (path) != 0)
    {
      g_warning ("%s", g_strerror (errno));
      g_free (path);
      return FALSE;
    }

  g_free (path);
  return TRUE;
}

gboolean
variant_exists (const gchar *account)
{
  gchar *path = get_path (account);
  gboolean ret = g_file_test (path, G_FILE_TEST_EXISTS);

  g_free (path);
  return ret;
}

GStrv
variant_list (void)
{
  GPtrArray *ret = g_ptr_array_new ();
  gchar *dir_path = g_build_filename (g_get_user_data_dir (), "telepathy-1",
      "mission-control", NULL);
  GDir *dir = g_dir_open (dir_path, 0, NULL);

  if (dir != NULL)
    {
      const gchar *name;

      for (name = g_dir_read_name (dir);
          name != NULL;
          name = g_dir_read_name (dir))
        {
          gchar *dup;

          if (!g_str_has_suffix (name, ".account"))
            continue;

          /* this is not production code so we're ignoring the possibility
           * of invalid account names here */
          dup = g_strdup (name);
          g_strdelimit (dup, "-", '/');
          g_strdelimit (dup, ".", '\0');
          g_ptr_array_add (ret, dup);
        }

      g_dir_close (dir);
    }

  g_free (dir_path);
  g_ptr_array_add (ret, NULL);
  return (GStrv) g_ptr_array_free (ret, FALSE);
}
