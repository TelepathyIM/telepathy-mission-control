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

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>

#include <glib.h>

#include "mc.h"
#include "mc-account.h"
#include "mc-account-monitor.h"
#include "mc-profile.h"

void print_profile (McProfile *profile)
{
  const gchar *name, *protocol_name;
  McProtocol *protocol;

  g_assert (NULL != profile);
  name = mc_profile_get_unique_name (profile);
  protocol = mc_profile_get_protocol (profile);
  protocol_name = mc_protocol_get_name (protocol);
  printf ("profile: %s (%s)\n", name, protocol_name);
}

void print_account (McAccount *account)
{
  const gchar *name = mc_account_get_unique_name (account);
  printf ("account: %p (%s)\n", account, name);
}

void print_manager (McManager *manager)
{
  const gchar *name = mc_manager_get_unique_name (manager);
  printf ("manager: %p (%s)\n", manager, name);
}

void print_protocol (McProtocol *protocol)
{
  printf ("protocol: %s/%s\n",
    mc_manager_get_unique_name (
      mc_protocol_get_manager (protocol)),
    mc_protocol_get_name (protocol));
}

void print_protocol_detailed (McProtocol *protocol)
{
  GSList *i;

  g_assert (NULL != protocol);
  print_protocol (protocol);

  for (i = mc_protocol_get_params (protocol); NULL != i; i = i->next)
    {
      McProtocolParam *param = (McProtocolParam *) i->data;

      printf("  %s:%s\n", param->signature, param->name);
    }
}

void test_profile ()
{
  McProfile *profile1, *profile2;
  McProtocol *protocol;
  const gchar * protocol_name;

  profile1 = mc_profile_lookup ("testprofile");
  g_assert (profile1);
  g_assert (0 == strcmp ("testprofile",
    mc_profile_get_unique_name (profile1)));
  protocol_name = mc_profile_get_protocol_name (profile1);
  g_assert (0 == strcmp ("testproto", protocol_name));
  protocol = mc_profile_get_protocol (profile1);
  g_assert (protocol);
  g_assert (0 == strcmp ("testproto", mc_protocol_get_name (protocol)));
  profile2 = mc_profile_lookup ("testprofile");
  g_assert (profile1 == profile2);

  g_object_unref (profile1);
  g_object_unref (profile2);
}

void test_profile_list ()
{
  GList *list, *i;
  McProfile *profile1, *profile2;

  list = mc_profiles_list ();
  g_assert (3 == g_list_length (list));
  i = list;
  profile1 = (McProfile *) i->data;
  i = i->next;
  profile2 = (McProfile *) i->data;
  g_assert (0 == strcmp ("jabber",
    mc_profile_get_unique_name (profile1)));
  g_assert (0 == strcmp ("google-talk",
    mc_profile_get_unique_name (profile2)));
  g_assert (0 == strcmp ("testprofile",
    mc_profile_get_unique_name ((McProfile *) i->next->data)));

  mc_profiles_free_list (list);

  list = mc_profiles_list ();
  g_assert (3 == g_list_length (list));
  i = list;
  g_assert (profile1 == (McProfile *) i->data);
  i = i->next;
  g_assert (profile2 == (McProfile *) i->data);

  mc_profiles_free_list (list);
}

void test_profile_stat ()
{
  McProfile *profile1, *profile2;

  profile1 = mc_profile_lookup ("jabber");
  utime ("../test/jabber.profile", NULL);
  profile2 = mc_profile_lookup("jabber");
  g_assert (profile1 != profile2);
}

void check_account_param (gpointer key, gpointer value, gpointer data)
{
  if (0 == strcmp (key, "account"))
    {
      g_assert (G_VALUE_HOLDS_STRING (value));
      g_assert (0 == strcmp ("daf@foo", g_value_get_string (value)));
      return;
    }

  if (0 == strcmp (key, "password"))
    {
      g_assert (G_VALUE_HOLDS_STRING (value));
      g_assert (0 == strcmp ("badger", g_value_get_string (value)));
      return;
    }

  g_warning ("got unexpected parameter \"%s\" for account", (gchar *) key);
}

void test_account()
{
  McAccount *account1, *account2;

  account1 = mc_account_lookup ("jabber1");
  g_assert (account1);
  g_assert (0 == strcmp ("jabber1", mc_account_get_unique_name (account1)));
  account2 = mc_account_lookup ("jabber1");
  g_assert (account2);
  g_assert (account1 == account2);

  g_assert (mc_account_set_param_string(account1, "account", "daf@foo"));
  g_assert (mc_account_set_param_string(account1, "password", "badger"));

  g_hash_table_foreach(
    mc_account_get_params(account1), check_account_param, account1);

  g_object_unref(account1);
  g_object_unref(account2);
}

void print_accounts_list ()
{
  GList *i, *accounts;

  accounts = mc_accounts_list ();

  for (i = accounts; NULL != i; i = i->next)
    {
      McAccount *account = (McAccount *) i->data;
      const gchar *name = mc_account_get_unique_name (account);
      const gchar *display_name = mc_account_get_display_name (account);

      if (display_name)
        g_print (" %s (\"%s\")\n", name, display_name);
      else
        g_print (" %s\n", name);
    }
}

gint account_has_name (gconstpointer account_p, gconstpointer name_p)
{
  McAccount *account = (McAccount *) account_p;
  const gchar *name = (gchar *) name_p;

  return strcmp (mc_account_get_unique_name (account), name);
}

void test_mc_account_list ()
{
  GList *accounts;
  McAccount *account;
  McProfile *profile;
  const gchar *name;

  profile = mc_profile_lookup ("jabber");
  account = mc_account_create (profile);
  sleep(1);
  while (g_main_context_iteration (NULL, FALSE));
  name = mc_account_get_unique_name (account);
  accounts = mc_accounts_list ();
  g_assert (NULL != g_list_find_custom (accounts, name, account_has_name));
  mc_accounts_list_free (accounts);
  mc_account_delete (account);
}

void cb_account_created(McAccountMonitor *monitor, gchar *name, gpointer data)
{
  GSList **created = (GSList **) data;
  *created = g_slist_append (*created, g_strdup (name));
  /*printf ("account created: %s\n", name); */
}

void cb_account_deleted(McAccountMonitor *monitor, gchar *name, gpointer data)
{
  GSList **deleted = (GSList **) data;
  *deleted = g_slist_append (*deleted, g_strdup (name));
  /*printf ("account deleted: %s\n", name); */
}

void cb_account_enabled (McAccountMonitor *monitor, gchar *name, gpointer data)
{
  GSList **enabled = (GSList **) data;
  *enabled = g_slist_append (*enabled, g_strdup (name));
  /* printf ("account enabled: %s\n", name); */
}

void cb_account_disabled (McAccountMonitor *monitor, gchar *name, gpointer data)
{
  GSList **disabled = (GSList **) data;
  *disabled = g_slist_append (*disabled, g_strdup (name));
  /* printf ("account disabled: %s\n", name); */
}

void cb_account_changed (McAccountMonitor *monitor, gchar *name, gpointer data)
{
  GSList **disabled = (GSList **) data;
  *disabled = g_slist_append (*disabled, g_strdup (name));
}

void test_account_monitor()
{
  McAccountMonitor *monitor;
  McProfile *profile1, *profile2;
  McAccount *account1, *account2;
  GSList *created = NULL;
  GSList *deleted = NULL;
  GSList *enabled = NULL;
  GSList *disabled = NULL;
  GSList *changed = NULL;
  const gchar *name1, *name2;

  monitor = mc_account_monitor_new ();
  g_signal_connect (monitor, "account-created", (GCallback) cb_account_created, &created);
  g_signal_connect (monitor, "account-deleted", (GCallback) cb_account_deleted, &deleted);
  g_signal_connect (monitor, "account-enabled", (GCallback) cb_account_enabled, &enabled);
  g_signal_connect (monitor, "account-disabled", (GCallback) cb_account_disabled, &disabled);
  g_signal_connect (monitor, "account-changed", (GCallback) cb_account_changed, &changed);

  profile1 = mc_profile_lookup ("jabber");
  g_assert (NULL != profile1);
  g_assert (NULL != mc_profile_get_protocol (profile1));

  profile2 = mc_profile_lookup ("google-talk");
  g_assert (NULL != mc_profile_get_protocol (profile2));
  g_assert (NULL != profile2);

  /* test 1: creating */

  account1 = mc_account_create (profile1);
  name1 = mc_account_get_unique_name (account1);
  /* printf ("new account: %s\n", name1);*/

  account2 = mc_account_create (profile2);
  name2 = mc_account_get_unique_name (account2);
  /* printf ("new account: %s\n", name2); */

  sleep(1);
  while (g_main_context_iteration (NULL, FALSE));

  g_assert (2 == g_slist_length (created));
  g_assert (0 == g_slist_length (deleted));
  g_assert (2 == g_slist_length (enabled));
  g_assert (0 == g_slist_length (disabled));
  g_assert (0 < g_slist_length (changed));

  g_assert (NULL != g_slist_find_custom (created, name1, (GCompareFunc) strcmp));
  g_assert (NULL != g_slist_find_custom (created, name2, (GCompareFunc) strcmp));

  g_assert (NULL != g_slist_find_custom (enabled, name1, (GCompareFunc) strcmp));
  g_assert (NULL != g_slist_find_custom (enabled, name2, (GCompareFunc) strcmp));

  created = deleted = enabled = disabled = changed = NULL;

  /* test 2: disabling */

  mc_account_set_enabled (account1, FALSE);
  mc_account_set_enabled (account2, FALSE);

  sleep(1);
  while (g_main_context_iteration (NULL, FALSE));

  g_assert (0 == g_slist_length (created));
  g_assert (0 == g_slist_length (deleted));
  g_assert (0 == g_slist_length (enabled));
  g_assert (2 == g_slist_length (disabled));
  g_assert (0 == g_slist_length (changed));

  g_assert (NULL != g_slist_find_custom (disabled, name1, (GCompareFunc) strcmp));
  g_assert (NULL != g_slist_find_custom (disabled, name2, (GCompareFunc) strcmp));

  created = deleted = enabled = disabled = changed = NULL;

  /* test 3: re-enabling */

  mc_account_set_enabled (account1, TRUE);
  mc_account_set_enabled (account2, TRUE);

  sleep(1);
  while (g_main_context_iteration (NULL, FALSE));

  g_assert (0 == g_slist_length (created));
  g_assert (0 == g_slist_length (deleted));
  g_assert (2 == g_slist_length (enabled));
  g_assert (0 == g_slist_length (disabled));
  g_assert (0 == g_slist_length (changed));

  g_assert (NULL != g_slist_find_custom (enabled, name1, (GCompareFunc) strcmp));
  g_assert (NULL != g_slist_find_custom (enabled, name2, (GCompareFunc) strcmp));

  created = deleted = enabled = disabled = changed = NULL;

  /* test 4: deleting */

  mc_account_delete (account2);
  mc_account_delete (account1);

  sleep(1);
  while (g_main_context_iteration (NULL, FALSE));

  g_assert (0 == g_slist_length (created));
  g_assert (2 == g_slist_length (deleted));
  g_assert (0 == g_slist_length (enabled));
  g_assert (2 == g_slist_length (disabled));
  g_assert (0 < g_slist_length (changed));


  g_assert (NULL != g_slist_find_custom (deleted, name1, (GCompareFunc) strcmp));
  g_assert (NULL != g_slist_find_custom (deleted, name2, (GCompareFunc) strcmp));

  g_object_unref (profile1);
  g_object_unref (profile2);
  g_object_unref (account1);
  g_object_unref (account2);
  g_object_unref (monitor);
}

void test_manager()
{
  McManager *manager1, *manager2;

  manager1 = mc_manager_lookup ("testmanager");
  g_assert (manager1);
  g_assert (0 ==
    strcmp ("testmanager", mc_manager_get_unique_name (manager1)));

  g_assert (0 == strcmp ("testmanager",
    mc_manager_get_unique_name (manager1)));
  g_assert (0 == strcmp ("org.freedesktop.Telepathy.ConnectionManager.test",
    mc_manager_get_bus_name (manager1)));
  g_assert (0 == strcmp ("/org/freedesktop/Telepathy/ConnectionManager/test",
    mc_manager_get_object_path (manager1)));

  manager2 = mc_manager_lookup ("testmanager");
  g_assert (manager2);
  g_assert (manager1 == manager2);

  g_object_unref (manager1);
  g_object_unref (manager2);
}

void test_protocol ()
{
  McManager *manager1, *manager2;
  McProtocol *protocol1, *protocol2;
  GSList *params;
  McProtocolParam expected_params[] = {
        {"account", "s", NULL,
            MC_PROTOCOL_PARAM_REQUIRED | MC_PROTOCOL_PARAM_REGISTER},
        {"password", "s", NULL,
            MC_PROTOCOL_PARAM_REQUIRED | MC_PROTOCOL_PARAM_REGISTER},
        {"server", "s", NULL,
            MC_PROTOCOL_PARAM_REQUIRED},
        {"port", "q", NULL, 0},
        {"register", "b", NULL, 0},
        {NULL, NULL, NULL, 0}
  }, *i;

  manager1 = mc_manager_lookup ("testmanager");
  manager2 = mc_manager_lookup ("testmanager");
  protocol1 = mc_protocol_lookup (manager1, "testproto");
  protocol2 = mc_protocol_lookup (manager2, "testproto");
  g_assert (protocol1 == protocol2);

  params = mc_protocol_get_params (protocol1);

  for (i = expected_params; i->name; i++)
    {
      GSList *j;
      gboolean found = FALSE;

      for (j = params; j; j = j->next)
        {
          McProtocolParam *p = (McProtocolParam *) j->data;

          if (0 == strcmp (i->name, p->name))
            {
              found = TRUE;
              g_assert (0 == strcmp (i->name, p->name));
              g_assert (0 == strcmp (i->signature, p->signature));
            }
        }

      g_assert (found);
    }

  mc_protocol_free_params_list (params);

  g_object_unref (manager1);
  g_object_unref (manager2);
  g_object_unref (protocol1);
  g_object_unref (protocol2);
}

int main ()
{
  g_setenv ("MC_PROFILE_DIR", "../test", FALSE);
  g_setenv ("MC_MANAGER_DIR", "../test", FALSE);

  g_type_init ();

  mc_make_resident ();
  mc_make_resident ();

  test_profile ();
  test_profile_list ();
  test_profile_stat ();
  test_account ();
  test_mc_account_list ();

  /* this is a hack to workaround an apparent race condition when catching
   * GConf signals in the process that caused them */
  sleep(1);
  while (g_main_context_iteration (NULL, FALSE));

  test_account_monitor ();
  test_manager ();
  test_protocol ();

  mc_profile_clear_cache ();
  mc_account_clear_cache ();
  mc_manager_clear_cache ();

  return 0;
}


