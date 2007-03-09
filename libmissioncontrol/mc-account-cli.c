/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>

#include <glib-object.h>
#include "mc-account.h"

static gchar *app_name;

static void
show_help (gchar * err)
{
  if (err)
    printf ("Error: %s\n", err);

  printf ("Usage:\n"
	  "    %1$s list\n"
	  "    %1$s add <profile> <display name> string:account=<user_id> string:password=<password> [(int|bool|string):<key>=<value> ...]\n"
	  "    %1$s set <account name> (int|bool|string):<key>=<value> [...]\n"
	  "    %1$s display <account name> <display name>\n"
	  "    %1$s show <account name>\n"
	  "    %1$s enable <account name>\n"
	  "    %1$s disable <account name>\n"
	  "    %1$s delete <account name>\n",
	  app_name);

  if (err)
    exit (-1);
  else
    exit (0);
}

static void
on_params_foreach (const gchar * key, const GValue * value)
{
  switch (G_VALUE_TYPE (value))
    {
    case G_TYPE_INT:
      printf ("        (int) %s = %d\n", key, g_value_get_int (value));
      break;
    case G_TYPE_UINT:
      printf ("        (int) %s = %d\n", key, g_value_get_uint (value));
      break;
    case G_TYPE_BOOLEAN:
      printf ("       (bool) %s = %s\n", key,
              g_value_get_boolean (value) ? "true" : "false");
      break;
    case G_TYPE_STRING:
      printf ("     (string) %s = %s\n", key, g_value_get_string (value));
      break;
    default:
      g_warning ("Unknown account setting type.");
    }
}

static gboolean
set_account_param (McAccount *account, gchar *param_value)
{
  gchar **strv_param_value = NULL;
  gchar **strv_type_key = NULL;
  const gchar *param, *type, *key, *value;
  gboolean ret = FALSE;
  
  if (!param_value)
    return FALSE;
  
  strv_param_value = g_strsplit (param_value, "=", -1);
  if (strv_param_value[0] == NULL ||
      strv_param_value[1] == NULL ||
      strv_param_value[2] != NULL)
    goto CLEANUP;
  param = strv_param_value[0];
  value = strv_param_value[1];
  
  strv_type_key = g_strsplit (param, ":", -1);
  if (strv_type_key[0] == NULL ||
      strv_type_key[1] == NULL ||
      strv_type_key[2] != NULL)
    goto CLEANUP;
  type = strv_type_key[0];
  key = strv_type_key[1];
  
  /* Set the key */
  if (strcmp (type, "int") == 0)
    {
      mc_account_set_param_int (account, key, atoi (value));
      ret = TRUE;
    }
  else if (strcmp (type, "bool") == 0)
    {
      mc_account_set_param_boolean (account, key, atoi (value));
      ret = TRUE;
    }
  else if (strcmp (type, "string") == 0)
    {
      mc_account_set_param_string (account, key, value);
      ret = TRUE;
    }
CLEANUP:
    if (strv_param_value)
    g_strfreev (strv_param_value);
  if (strv_type_key)
    g_strfreev (strv_type_key);
  return ret;
}

int
main (int argc, char **argv)
{
  app_name = basename (argv[0]);

  if (argc < 2)
    show_help ("No command specified");

  g_type_init ();
  /* Command processing */

  if (strcmp (argv[1], "add") == 0)
    {
      /* Add */
      McProfile *profile;
      McAccount *account;

      if (argc < 6)
        show_help ("Invalid add command.");

      profile = mc_profile_lookup (argv[2]);
      if (profile == NULL)
        {
          printf ("Error: No such profile: %s\n", argv[2]);
        }
      else
        {
          account = mc_account_create (profile);
          if (account == NULL)
            {
              printf ("Error: Error creating account.\n");
            }
          else
            {
              const gchar *name;
              gint i;
              gboolean status = TRUE;
              
              mc_account_set_display_name (account, argv[3]);
              for (i = 4; i < argc; i++)
                {
                  status = set_account_param (account, argv[i]);
                  if (!status)
                    break;
                }
              name = mc_account_get_unique_name (account);
              if (!status)
                {
                  mc_account_delete (account);
                  printf ("Account not added successfully: %s\n", name);
                  show_help ("Invalid account paramenters");
                }
              if (!mc_account_is_complete (account))
                {
                  mc_account_delete (account);
                  printf ("Account not added successfully: %s\n", name);
                  show_help ("Given account paramenters does not define a complete account");
                }
              printf ("Account added successfully: %s\n", name);
              g_object_unref (account);
            }
          g_object_unref (profile);
        }
    }
  else if (strcmp (argv[1], "delete") == 0)
    {
      /* Delete account */
      McAccount *account;

      if (argc != 3)
        show_help ("Invalid delete command.");

      account = mc_account_lookup (argv[2]);
      if (account == NULL)
        {
          printf ("Error: No such account: %s\n", argv[2]);
        }
      else
        {
          if (mc_account_delete (account))
            {
              printf ("Account %s deleted sucessfully.\n", argv[2]);
            }
          else
            {
              printf ("Error: Error deleting account: %s\n", argv[2]);
            }
          mc_account_free (account);
        }
    }
  else if (strcmp (argv[1], "list") == 0)
    {
      /* List accounts */
      GList *accounts, *tmp;

      if (argc != 2)
        show_help ("Invalid list command.");

      accounts = mc_accounts_list ();
      for (tmp = accounts; tmp != NULL; tmp = tmp->next)
        {
          McAccount *account;

          account = (McAccount *) tmp->data;
          printf ("%s (%s)\n",
		  mc_account_get_unique_name (account),
		  mc_account_get_display_name (account));

        }
      mc_accounts_list_free (accounts);
    }
  else if (strcmp (argv[1], "show") == 0)
    {
      /* Show account details */
      McAccount *account;
      GHashTable *params;

      if (argc != 3)
        show_help ("Invalid show command.");

      account = mc_account_lookup (argv[2]);
      if (account == NULL)
        {
          printf ("Error: No such account: %s\n", argv[2]);
          exit (1);
        }

      params = mc_account_get_params (account);
      if (params == NULL)
        {
          printf ("Error: Failed to retreive params: %s\n", argv[2]);
        }
      else
        {
          gboolean enabled;

          enabled = mc_account_is_enabled (account);

          printf ("     Account: %s\n", argv[2]);
          printf ("Display Name: %s\n", mc_account_get_display_name (account));
          printf ("     Enabled: %s\n\n", enabled ? "enabled" : "disabled");
          g_hash_table_foreach (params, (GHFunc) on_params_foreach, NULL);

          g_hash_table_destroy (params);
        }
      mc_account_free (account);
    }
  else if (strcmp (argv[1], "enable") == 0)
    {
      /* Enable account */
      McAccount *account;

      if (argc != 3)
        show_help ("Invalid enable command.");

      account = mc_account_lookup (argv[2]);
      if (account == NULL)
        {
          printf ("Error: No such account: %s\n", argv[2]);
          exit (-1);
        }
      mc_account_set_enabled (account, TRUE);
      mc_account_free (account);
    }
  else if (strcmp (argv[1], "disable") == 0)
    {
      /* Disable account */
      McAccount *account;

      if (argc != 3)
        show_help ("Invalid disable command.");

      account = mc_account_lookup (argv[2]);
      if (account == NULL)
        {
          printf ("Error: No such account: %s\n", argv[2]);
          exit (-1);
        }
      mc_account_set_enabled (account, FALSE);
      mc_account_free (account);
    }
  else if (strcmp (argv[1], "display") == 0)
    {
      /* Set display name */
      McAccount *account;

      if (argc != 4)
        show_help ("Invalid display command.");

      account = mc_account_lookup (argv[2]);
      if (account == NULL)
        {
          printf ("Error: No such account: %s\n", argv[2]);
          exit (-1);
        }
      mc_account_set_display_name (account, argv[3]);
      mc_account_free (account);
    }
  else if (strcmp (argv[1], "set") == 0)
    {
      /* Set account parameter */
      McAccount *account;

      if (argc != 4)
        show_help ("Invalid set command.");

      account = mc_account_lookup (argv[2]);
      if (!account)
        {
          printf ("Error: No such account: %s\n", argv[2]);
          exit (-1);
        }
      if (!set_account_param (account, argv[3]))
        {
          show_help ("Invalid set command.");
        }
      g_object_unref (account);
    }
  else if (strcmp (argv[1], "help") == 0 ||
           strcmp (argv[1], "-h") == 0 || strcmp (argv[1], "--help") == 0)
    {
      show_help (NULL);
    }
  else
    {
      show_help ("Unknown command.");
    }
  return 0;
}
