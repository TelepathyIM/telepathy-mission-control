/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2010 Nokia Corporation
 * Copyright (C) 2010 Collabora Ltd.
 *
 * Contact: Naba Kumar  <naba.kumar@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <glib.h>
#include <glib/gprintf.h>
#include <glib-object.h>
#include <gnome-keyring.h>


static gboolean
create_keyring (gchar *keyring_name)
{
  GnomeKeyringResult result;

  result = gnome_keyring_create_sync (keyring_name, "");

  if (result == GNOME_KEYRING_RESULT_OK)
    return TRUE;

  g_warning ("Failed to create keyring %s: %s", keyring_name,
    gnome_keyring_result_to_message (result));

  return FALSE;
}

static gchar *
create_random_keyring (void)
{
  gchar *keyring_name = NULL;
  GnomeKeyringResult result;

  while (TRUE)
    {
      keyring_name = g_strdup_printf ("mc-test-%u", g_random_int ());

      result = gnome_keyring_create_sync (keyring_name, "");

      if (result == GNOME_KEYRING_RESULT_OK)
        {
          return keyring_name;
        }
      else if (result == GNOME_KEYRING_RESULT_KEYRING_ALREADY_EXISTS)
        {
          g_free (keyring_name);
          keyring_name = NULL;
          continue;
        }
      else
        {
          g_warning ("Failed to create keyring %s: %s", keyring_name,
            gnome_keyring_result_to_message (result));
          g_free (keyring_name);
          return NULL;
        }
    }

  g_assert_not_reached ();
}

static gboolean
remove_keyring (gchar *keyring_name)
{
  GnomeKeyringResult result;

  result = gnome_keyring_delete_sync (keyring_name);

  if (result == GNOME_KEYRING_RESULT_OK)
    {
      return TRUE;
    }
  else
    {
      g_warning ("Failed to remove keyring %s: %s", keyring_name,
          gnome_keyring_result_to_message (result));
      return FALSE;
    }
}

static void
show_help (gchar *name)
{
  g_printf ("%s - utility for creating and removing gnome keyrings\n", name);
  g_printf ("Usage: %s create [KEYRING]\n", name);
  g_printf ("       %s remove KEYRING\n", name);
}

int
main (int argc, char **argv)
{
  g_type_init ();

  if (argc < 2)
    {
      show_help (argv[0]);
      return 0;
    }

  if (!g_strcmp0 (argv[1], "create"))
    {
      if (argc < 3)
        {
          gchar *keyring_name = create_random_keyring ();

          if (keyring_name)
            {
              g_printf("%s\n", keyring_name);
              g_free (keyring_name);
              return 0;
            }
          else
            {
              return -1;
            }
        }
      else
        {
          if (create_keyring (argv[2]))
            {
              g_printf("%s\n", argv[2]);
              return 0;
            }
          else
            {
              return -1;
            }
        }
    }

  if (!g_strcmp0 (argv[1], "remove"))
    {
      if (argc < 3)
        {
          show_help (argv[0]);
          return -1;
        }

      if (remove_keyring (argv[2]))
        {
          return 0;
        }
      else
        {
          return -1;
        }
    }

  show_help (argv[0]);
  return -1;
}

