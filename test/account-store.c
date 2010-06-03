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

static void usage (const gchar *fmt, ...);

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

  if (argc < 3)
    usage ("%s OP BACKEND ACCOUNT [KEY [VALUE]]", argv[0]);

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
    usage ("No such backend %s", backend);

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
          usage ("%s %s requires an account and key", argv[0], op_name);

        account = argv[3];
        setting = argv[4];

        if (account == NULL || *account == '\0')
          usage ("%s %s requires an account", argv[0], op_name);

        if (setting == NULL || *setting == '\0')
          usage ("%s %s requires a key", argv[0], op_name);

        break;

      case OP_DELETE:
      case OP_EXISTS:

        if (argc < 4)
          usage ("%s %s requires an account", argv[0], op_name);

        account = argv[3];
        break;

      case OP_UNKNOWN:
        usage ("%s: Unknown operation: %s", argv[0], op_name);
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

static void usage (const gchar *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);

  exit (1);
}
