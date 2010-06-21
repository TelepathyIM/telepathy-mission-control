/*
 * MC account storage backend inspector
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>

#include "account-store-default.h"

#define DOCSTRING_A \
  "%s OP BACKEND ACCOUNT [KEY [VALUE]]\n\n" \
  "  OP      := <get | set | del | has>\n"  \
  "  BACKEND := <"

#define DOCSTRING_B \
  ">\n"                                                                   \
  "  ACCOUNT := <MANAGER>/<PROTOCOL>/<ACCOUNT-UID>\n"                    \
  "  KEY     := <manager | protocol | DisplayName | param-<PARAMETER>>\n" \
  "  VALUE   := <STRING>\n\n"

#if ENABLE_LIBACCOUNTS_SSO
#include "account-store-libaccounts.h"
#endif

typedef struct {
  const gchar *name;
  gchar *  (*get) (const gchar *account, const gchar *key);
  gboolean (*set) (const gchar *account, const gchar *key, const gchar *value);
  gboolean (*delete) (const gchar *account);
  gboolean (*exists) (const gchar *account);
} Backend;

typedef enum {
  OP_UNKNOWN,
  OP_GET,
  OP_SET,
  OP_DELETE,
  OP_EXISTS,
} Operation;

const Backend backends[] = {
  { "default",
    default_get,
    default_set,
    default_delete,
    default_exists },

#if ENABLE_LIBACCOUNTS_SSO
  { "libaccounts",
    libaccounts_get,
    libaccounts_set,
    libaccounts_delete,
    libaccounts_exists },
#endif

  { NULL }
};

static void usage (const gchar *name, const gchar *fmt, ...);

#if ENABLE_GNOME_KEYRING
#include <gnome-keyring.h>

static void
setup_default_keyring (void)
{
  GnomeKeyringResult result;

  g_debug ("Setting default keyring to: %s", g_getenv ("MC_KEYRING_NAME"));

  if (g_getenv ("MC_KEYRING_NAME") != NULL)
  {
      const gchar *keyring_name = g_getenv ("MC_KEYRING_NAME");

      g_debug ("MC Keyring name: %s", keyring_name);

      if ((result = gnome_keyring_set_default_keyring_sync (keyring_name)) ==
           GNOME_KEYRING_RESULT_OK)
      {
          g_debug ("Successfully set up temporary keyring %s for tests",
                   keyring_name);
      }
      else
      {
          g_warning ("Failed to set %s as the default keyring: %s",
                     keyring_name, gnome_keyring_result_to_message (result));
      }
  }
}
#endif

int main (int argc, char **argv)
{
  int i;
  const gchar *op_name = NULL;
  const gchar *backend = NULL;
  const gchar *account = NULL;
  const gchar *setting = NULL;
  const gchar *value = NULL;
  const Backend *store = NULL;
  Operation op = OP_UNKNOWN;
  gchar *output = NULL;
  gboolean success;

  g_type_init ();
  g_set_application_name (argv[0]);

#if ENABLE_GNOME_KEYRING
  setup_default_keyring ();
#endif

  if (argc < 3)
    usage (argv[0], "");

  op_name = argv[1];
  backend = argv[2];

  for (i = 0; backends[i].name != NULL; i++)
    {
      if (g_str_equal (backends[i].name, backend))
        {
          store = &backends[i];
          break;
        }
    }

  if (store == NULL)
    usage (argv[0], "No such backend %s", backend);

  if (g_str_equal (op_name, "get"))
    op = OP_GET;
  else if (g_str_equal (op_name, "set"))
    op = OP_SET;
  else if (g_str_equal (op_name, "del"))
    op = OP_DELETE;
  else if (g_str_equal (op_name, "has"))
    op = OP_EXISTS;

  switch (op)
    {
      case OP_SET:

        if (argc >= 6)
          value = argv[5];

      case OP_GET:

        if (argc < 5)
          usage (argv[0], "op '%s' requires an account and key", op_name);

        account = argv[3];
        setting = argv[4];

        if (account == NULL || *account == '\0')
          usage (argv[0], "op '%s' requires an account", op_name);

        if (setting == NULL || *setting == '\0')
          usage (argv[0], "op '%s' requires a key", op_name);

        break;

      case OP_DELETE:
      case OP_EXISTS:

        if (argc < 4)
          usage (argv[0], "op '%s' requires an account", op_name);

        account = argv[3];
        break;

      case OP_UNKNOWN:
        usage (argv[0], "Unknown operation: %s", op_name);
    }

  /* if we got this far, we have all the args we need: */
  switch (op)
    {
      case OP_GET:
        output = store->get (account, setting);
        success = output != NULL;
        break;

      case OP_SET:
        success = store->set (account, setting, value);
        output = g_strdup_printf ("%s.%s set to '%s' in %s",
            account, setting, value, store->name);
        break;

      case OP_DELETE:
        success = store->delete (account);
        output = g_strdup_printf ("%s deleted from %s", account, store->name);
        break;

      case OP_EXISTS:
        success = store->exists (account);
        if (success)
          output = g_strdup_printf ("Exists in %s", store->name);
        break;
    }

  if (output != NULL)
    printf ("%s\n", output);

  g_free (output);

  return success ? 0 : 1;
}

static void
usage (const gchar *name, const gchar *fmt, ...)
{
  guint i;
  va_list ap;

  fprintf (stderr, DOCSTRING_A, name);

  fprintf (stderr, "%s", backends[0].name);

  for (i = 1; backends[i].name != NULL; i++)
    fprintf (stderr, " | %s", backends[i].name);

  fprintf (stderr, DOCSTRING_B, name);

  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);

  exit (1);
}
