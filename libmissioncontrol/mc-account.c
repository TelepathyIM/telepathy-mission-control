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

#define DBUS_API_SUBJECT_TO_CHANGE

#include <dbus/dbus.h>
#include <errno.h>
#include <gconf/gconf-client.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <glib/gstdio.h>

#include "mc-account.h"
#include "mc-account-priv.h"
#include "mc-account-monitor.h"
#include "mc-account-monitor-priv.h"
#include "mc-profile.h"
#include <config.h>

#define MC_ACCOUNTS_MAX 1024
#define MC_AVATAR_FILENAME	"avatar.bin"

#define MC_ACCOUNT_PRIV(account) ((McAccountPrivate *)account->priv)

G_DEFINE_TYPE (McAccount, mc_account, G_TYPE_OBJECT);

typedef struct
{
  gchar *unique_name;
  GSList *display_names;
  GSList *normalized_names;
} McAccountPrivate;

static gboolean mc_account_set_deleted (McAccount *account, gboolean deleted);
static gboolean mc_account_is_deleted (McAccount *account);

static void
mc_account_finalize (GObject *object)
{
  McAccount *self = MC_ACCOUNT(object);
  McAccountPrivate *priv = MC_ACCOUNT_PRIV (self);
  
  g_free (priv->unique_name);
  g_slist_foreach (priv->display_names, (GFunc)g_free, NULL);
  g_slist_free (priv->display_names);
  g_slist_foreach (priv->normalized_names, (GFunc)g_free, NULL);
  g_slist_free (priv->normalized_names);
}

static void
mc_account_init (McAccount *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
      MC_TYPE_ACCOUNT, McAccountPrivate);
}

void
mc_account_clear_cache (void)
{
}

McAccount *
_mc_account_new (const gchar *unique_name)
{
  McAccount *new;
  new = (McAccount *)g_object_new (MC_TYPE_ACCOUNT, NULL);
  MC_ACCOUNT_PRIV (new)->unique_name = g_strdup (unique_name);

  return new;
}

static void
mc_account_class_init (McAccountClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (object_class, sizeof (McAccountPrivate));
  object_class->finalize = mc_account_finalize;
}

/* Returns the data dir for the given account name.
 * Returned string must be freed by caller. */
static gchar *
get_account_data_path (const gchar *unique_name)
{
  const gchar *base;

  base = g_getenv ("MC_ACCOUNT_DIR");
  if (!base)
    base = ACCOUNTS_DIR;
  if (!base)
    return NULL;

  if (base[0] == '~')
    return g_build_filename (g_get_home_dir(), base + 1, unique_name, NULL);
  else
    return g_build_filename (base, unique_name, NULL);
}

/* if key is NULL, returns the gconf dir for the given account name,
 * otherwise returns the full key. prepends key with param- if
 * param is TRUE. returned string must be freed by caller. */
static gchar *
_mc_account_path (const gchar *unique_name,
                     const gchar *key,
                     gboolean param)
{
  if (key != NULL)
    {
      if (param)
        {
          return g_strconcat (MC_ACCOUNTS_GCONF_BASE, "/",
                              unique_name, "/param-",
                              key, NULL);
        }
      else
        {
          return g_strconcat (MC_ACCOUNTS_GCONF_BASE, "/",
                              unique_name, "/",
                              key, NULL);
        }
    }
  else
    {
      return g_strconcat (MC_ACCOUNTS_GCONF_BASE, "/",
                          unique_name, NULL);
    }
}

static GConfValue *
_mc_account_gconf_get (McAccount *account,
                          const gchar *name,
                          gboolean param)
{
  GConfClient *client;
  gchar *key;
  GConfValue *value;

  g_return_val_if_fail (account != NULL, NULL);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        NULL);
  g_return_val_if_fail (name != NULL, NULL);

  client = gconf_client_get_default ();
  g_return_val_if_fail (client != NULL, NULL);

  key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name,
                             name, param);
  value = gconf_client_get (client, key, NULL);

  g_object_unref (client);
  g_free (key);

  return value;
}

static gboolean
_mc_account_gconf_get_boolean (McAccount *account,
                                  const gchar *name,
                                  gboolean param,
                                  gboolean *value)
{
  GConfValue *val;

  g_return_val_if_fail (value != NULL, FALSE);

  val = _mc_account_gconf_get (account, name, param);

  if (val == NULL)
    {
      return FALSE;
    }

  if (val->type != GCONF_VALUE_BOOL)
    {
      gconf_value_free (val);
      return FALSE;
    }

  *value = gconf_value_get_bool (val);
  gconf_value_free (val);

  return TRUE;
}

static gboolean
_mc_account_gconf_get_int (McAccount *account,
                              const gchar *name,
                              gboolean param,
                              gint *value)
{
  GConfValue *val;

  g_return_val_if_fail (value != NULL, FALSE);

  val = _mc_account_gconf_get (account, name, param);

  if (val == NULL)
    {
      return FALSE;
    }

  if (val->type != GCONF_VALUE_INT)
    {
      gconf_value_free (val);
      return FALSE;
    }

  *value = gconf_value_get_int (val);
  gconf_value_free (val);

  return TRUE;
}

static gboolean
_mc_account_gconf_get_string (McAccount *account,
                                 const gchar *name,
                                 gboolean param,
                                 gchar **value)
{
  GConfValue *val;

  g_return_val_if_fail (value != NULL, FALSE);

  val = _mc_account_gconf_get (account, name, param);

  if (val == NULL)
    {
      return FALSE;
    }

  if (val->type != GCONF_VALUE_STRING)
    {
      gconf_value_free (val);
      return FALSE;
    }

  *value = g_strdup (gconf_value_get_string (val));
  gconf_value_free (val);

  return TRUE;
}

static gboolean
_mc_account_gconf_set_string (McAccount *account, const gchar *name,
			      const gchar *value)
{
    GConfClient *client;
    gchar *key;
    gboolean ok;

    g_return_val_if_fail (account != NULL, FALSE);
    g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
			  FALSE);

    client = gconf_client_get_default ();
    g_return_val_if_fail (client != NULL, FALSE);

    key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name,
			    name, FALSE);
    ok = gconf_client_set_string (client, key, value, NULL);

    g_free (key);
    g_object_unref (client);

    return ok;
}

/**
 * mc_account_lookup:
 * @unique_name: The unique name of the account.
 *
 * Look-up an account from its unique name. The reference count of the returned
 * account is incremented.
 *
 * Return value: The requested #McAccount, or NULL if not found.
 */
McAccount *
mc_account_lookup (const gchar *unique_name)
{
  McAccountMonitor *monitor = mc_account_monitor_new ();
  McAccount *ret = _mc_account_monitor_lookup (monitor, unique_name);

  g_object_unref (monitor);
  return ret;
}

gboolean
_filter_account (McAccount *acct, gpointer data)
{
  const gchar *compare_account;
  gchar *gconf_account;
  gboolean ret;

  g_return_val_if_fail (acct != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (acct)->unique_name != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  compare_account = (const gchar *) data;

  if (!_mc_account_gconf_get_string (acct,
        MC_ACCOUNTS_GCONF_KEY_PARAM_ACCOUNT,
        TRUE, &gconf_account))
    return FALSE;

  ret = (0 == strcmp(gconf_account, compare_account));

  g_free (gconf_account);

  return ret;
}

McAccount *
_free_all_but_one (GList *list)
{
  McAccount *ret = NULL;

  if (list != NULL)
    {
      GList *tmp;

      tmp = g_list_remove_link (list, list);
      mc_accounts_list_free (tmp);

      ret = (McAccount *) list->data;
      g_list_free (list);
    }

  return ret;
}

/**
 * mc_account_lookup_with_profile:
 * @profile: A #McProfile.
 * @account: The name of the account.
 *
 * Look-up an account from its name in the given #McProfile. The reference
 * count of the returned account is incremented.
 *
 * Return value: The requested #McAccount, or NULL if not found.
 */
McAccount *
mc_account_lookup_with_profile (McProfile *profile,
                                   const gchar *account)
{
  GList *accounts;

  g_return_val_if_fail (profile != NULL, NULL);
  g_return_val_if_fail (account != NULL, NULL);

  accounts = mc_accounts_list_by_profile (profile);
  accounts = mc_accounts_filter (accounts, _filter_account, (gpointer) account);

  return _free_all_but_one (accounts);
}

/**
 * mc_account_lookup_with_vcard_field:
 * @vcard_field: The VCard field.
 * @account: The name of the account.
 *
 * Look-up an account from its name in the given VCard field. The reference
 * count of the returned account is incremented.
 *
 * Return value: The requested #McAccount, or NULL if not found.
 */
McAccount *
mc_account_lookup_with_vcard_field (const gchar *vcard_field,
                                       const gchar *account)
{
  GList *accounts;

  g_return_val_if_fail (vcard_field != NULL, NULL);
  g_return_val_if_fail (account != NULL, NULL);

  accounts = mc_accounts_list_by_vcard_field (vcard_field);
  accounts = mc_accounts_filter (accounts, _filter_account, (gpointer) account);

  return _free_all_but_one (accounts);
}

/**
 * mc_account_free:
 * @account: The #McAccount.
 *
 * Free an account.
 * DEPRECATED, use g_object_unref() instead.
 */
void
mc_account_free (McAccount* account)
{
  g_object_unref (account);
}

/**
 * mc_account_create:
 * @profile: A #McProfile.
 *
 * Create a new account of the given #McProfile.
 *
 * Return value: the newly created #McAccount.
 */
McAccount *
mc_account_create (McProfile *profile)
{
  McAccount *ret = NULL;
  GConfClient *client;
  const gchar *profile_name;
  gchar *unique_name, *key, *data_dir = NULL;
  guint i = 0;
  gboolean ok;

  g_return_val_if_fail (profile != NULL, NULL);

  client = gconf_client_get_default ();
  g_return_val_if_fail (client != NULL, NULL);

  profile_name = mc_profile_get_unique_name (profile);

  /* find the first free account with this profile */
  unique_name = NULL;
  while (unique_name == NULL && i < MC_ACCOUNTS_MAX)
    {
      gchar *path;

      unique_name = g_strdup_printf ("%s%u", profile_name, i);

      path = _mc_account_path (unique_name, NULL, FALSE);
      if (gconf_client_dir_exists (client, path, NULL))
        {
          g_free (unique_name);
          unique_name = NULL;
          i++;
        }
      g_free (path);
    }

  if (unique_name == NULL)
    goto OUT;

  key = _mc_account_path (unique_name, MC_ACCOUNTS_GCONF_KEY_PROFILE, FALSE);
  ok = gconf_client_set_string (client, key, profile_name, NULL);
  g_free (key);

  if (!ok)
    goto OUT;

  /* create the directory for storing the binary objects (i.e., avatar) */
  data_dir = get_account_data_path (unique_name);
  if (g_mkdir_with_parents (data_dir, 0777) != 0)
      goto OUT;
  /* and store it in GConf */
  key = _mc_account_path (unique_name, MC_ACCOUNTS_GCONF_KEY_DATA_DIR, FALSE);
  ok = gconf_client_set_string (client, key, data_dir, NULL);
  g_free (key);

  /* Account is disabled by default, because there is no guarantee
   * the account is usable at this point. The one who created the
   * account should enable it when its ready.
   */
  ret = _mc_account_new (unique_name);

OUT:
  g_free (data_dir);
  g_free (unique_name);
  g_object_unref (client);

  return ret;
}

static gchar *
_account_name_from_key (const gchar *key)
{
  guint base_len = strlen (MC_ACCOUNTS_GCONF_BASE);
  const gchar *base, *slash;

  g_assert (key == strstr (key, MC_ACCOUNTS_GCONF_BASE));
  g_assert (strlen (key) > base_len + 1);

  base = key + base_len + 1;
  slash = strchr (base, '/');

  if (slash == NULL)
    return g_strdup (base);
  else
    return g_strndup (base, slash - base);
}

static gboolean
mc_account_expunge_deleted (gpointer user_data)
{
  GConfClient *client;
  gchar *key;
  GSList *entries, *tmp;
  gboolean ok = TRUE;
  GError *error = NULL;
  GSList *i, *dirs;
  
  client = gconf_client_get_default ();
  g_return_val_if_fail (client != NULL, FALSE);

  dirs = gconf_client_all_dirs (client, MC_ACCOUNTS_GCONF_BASE, &error);

  if (NULL != error)
    {
      g_print ("Error: %s\n", error->message);
      g_assert_not_reached ();
    }
  for (i = dirs; NULL != i; i = i->next)
    {
      gchar *unique_name = _account_name_from_key (i->data);
      gchar *data_dir_str;
      GDir *data_dir;

      McAccount *account = _mc_account_new (unique_name);

      if (!mc_account_is_deleted (account))
      {
        g_object_unref (account);
        g_free (i->data);
        g_free (unique_name);
        continue;
      }
      
      key = _mc_account_path (unique_name, NULL, FALSE);
      entries = gconf_client_all_entries (client, key, NULL);
      g_free (key);

      for (tmp = entries; tmp != NULL; tmp = tmp->next)
        {
          GConfEntry *entry;
          entry = (GConfEntry*) tmp->data;

          if (!gconf_client_unset (client, entry->key, NULL))
            ok = FALSE;

          gconf_entry_free (entry);
        }

      data_dir_str = get_account_data_path (unique_name);
      data_dir = g_dir_open (data_dir_str, 0, NULL);
      if (data_dir)
      {
	  const gchar *filename;
	  while ((filename = g_dir_read_name (data_dir)) != NULL)
	  {
	      gchar *path;
	      path = g_build_filename (data_dir_str, filename, NULL);
	      g_remove (path);
	      g_free (path);
	  }
	  g_dir_close (data_dir);
	  g_rmdir (data_dir_str);
      }
      g_free (data_dir_str);

      g_free (i->data);
      g_free (unique_name);
      g_slist_free (entries);
    }
  g_object_unref (client);
  return FALSE;
}

/**
 * mc_account_delete:
 * @account: The #McAccount.
 *
 * Delete the given account from the accounts configuration. The object itself
 * remains valid and must be free separately.
 *
 * Return value: %TRUE if the account was deleted, %FALSE otherwise.
 */
gboolean
mc_account_delete (McAccount *account)
{
  gboolean ok;
  
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV(account)->unique_name != NULL,
                        FALSE);
  mc_account_set_enabled (account, FALSE);
  
  ok = mc_account_set_deleted (account, TRUE);

  /* Expunge deleted accounts after 5 secs. We can't expunge the accounts
   * immediately because there might be other people using the accounts
   * and gconf signaling is really async.
   */
  g_timeout_add (2000, (GSourceFunc)mc_account_expunge_deleted, NULL);
  return ok;
}

/**
 * mc_accounts_list:
 *
 * Lists all configured accounts.
 *
 * Return value: a #GList of all accounts. Must be freed with
 * #mc_accounts_list_free.
 */
GList *
mc_accounts_list (void)
{
  McAccountMonitor *monitor = mc_account_monitor_new ();
  GList *ret, *i;

  ret = _mc_account_monitor_list (monitor);

  for (i = ret; NULL != i; i = i->next)
    g_object_ref (G_OBJECT (i->data));

  g_object_unref (monitor);
  return ret;
}

static gboolean
_filter_enabled (McAccount *acct, gpointer data)
{
  gboolean enabled;

  g_return_val_if_fail (acct != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (acct)->unique_name != NULL, FALSE);

  enabled = GPOINTER_TO_INT (data);

  return (mc_account_is_enabled (acct) == enabled);
}

/**
 * mc_accounts_list_by_enabled:
 * @enabled: a boolean to select if enabled accounts should be returned.
 *
 * Lists all enabled/disabled accounts.
 *
 * Return value: a #GList of all the enabled accounts. Must be freed with
 * #mc_accounts_list_free.
 */
GList *
mc_accounts_list_by_enabled (gboolean enabled)
{
  GList *ret;

  ret = mc_accounts_list ();
  ret = mc_accounts_filter (ret, _filter_enabled, GINT_TO_POINTER (enabled));

  return ret;
}

static gboolean
_filter_profile (McAccount *acct, gpointer data)
{
  McProfile *profile;
  gchar *profile_name;
  gboolean ret;

  g_return_val_if_fail (acct != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (acct)->unique_name != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  profile = (McProfile *) data;

  if (!_mc_account_gconf_get_string (acct, MC_ACCOUNTS_GCONF_KEY_PROFILE,
                                        FALSE, &profile_name))
    return FALSE;

  ret = (0 == strcmp (profile_name, mc_profile_get_unique_name (profile)));
  g_free (profile_name);

  return ret;
}

/**
 * mc_accounts_list_by_profile:
 * @profile: a #McProfile.
 *
 * Lists all accounts of a #McProfile.
 *
 * Return value: a #GList of the accounts. Must be freed with
 * #mc_accounts_list_free.
 */
GList *
mc_accounts_list_by_profile (McProfile *profile)
{
  GList *ret;

  g_return_val_if_fail (profile != NULL, NULL);

  ret = mc_accounts_list ();
  ret = mc_accounts_filter (ret, _filter_profile, profile);

  return ret;
}

static gboolean
_filter_vcard_field (McAccount *acct, gpointer data)
{
  McProfile *profile;
  const gchar *vcard_field;
  const gchar *profile_vcard_field;
  gboolean ret;

  g_return_val_if_fail (acct != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (acct)->unique_name != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  vcard_field = (const gchar *) data;
  profile = mc_account_get_profile (acct);

  if (profile == NULL)
    return FALSE;

  profile_vcard_field = mc_profile_get_vcard_field (profile);

  if (profile_vcard_field == NULL)
    ret = FALSE;
  else
    ret = (0 == strcmp (vcard_field, profile_vcard_field));

  g_object_unref (profile);
  return ret;
}

/**
 * mc_accounts_list_by_vcard_field:
 * @vcard_field: the VCard field.
 *
 * Lists all accounts of a VCard field.
 *
 * Return value: a #GList of the accounts. Must be freed with
 * #mc_accounts_list_free.
 */
GList *
mc_accounts_list_by_vcard_field (const gchar *vcard_field)
{
  GList *ret;

  ret = mc_accounts_list ();
  ret = mc_accounts_filter (ret, _filter_vcard_field, (gpointer) vcard_field);

  return ret;
}

/**
 * mc_accounts_list_free:
 * @list: A #GList of #McAccount.
 *
 * Frees the lists of accounts returned by the mc_accounts_list* family of
 * functions.
 */
void
mc_accounts_list_free (GList *list)
{
  GList *i;

  for (i = list; NULL != i; i = i->next)
    g_object_unref (G_OBJECT (i->data));

  g_list_free (list);
}

/**
 * mc_accounts_filter:
 * @accounts: a #GList of #McAccount.
 * @filter: a #McAccountFilter function.
 * @data: user data to be passed to the filter function.
 *
 * Filter a list of accounts according to whether a function returns TRUE,
 * freeing the list and any accounts which are filtered out.
 *
 * Return value: a #GList of the accounts. Must be freed with
 * #mc_accounts_list_free.
 */
GList *
mc_accounts_filter (GList *accounts,
                       McAccountFilter filter,
                       gpointer data)
{
  GList *tmp, *ret = NULL;

  for (tmp = accounts; tmp != NULL; tmp = tmp->next)
    {
      McAccount *account = (McAccount *) tmp->data;

      if (filter (account, data))
        {
          ret = g_list_prepend (ret, account);
        }
      else
        {
          g_object_unref (account);
        }
    }

  g_list_free (accounts);

  return ret;
}

/**
 * mc_account_get_normalized_name:
 * @account: The #McAccount.
 *
 * Gets the normalized name for the account.
 *
 * Return value: the normalized name (must free with #g_free), or NULL.
 */
const gchar *
mc_account_get_normalized_name (McAccount *account)
{
  McAccountPrivate *priv;
  GSList *list;
  gchar *name;

  g_return_val_if_fail (account != NULL, NULL);
  priv = MC_ACCOUNT_PRIV (account);

  if (!_mc_account_gconf_get_string (account,
				     MC_ACCOUNTS_GCONF_KEY_NORMALIZED_NAME,
				     FALSE, &name))
    return NULL;

  if ((list = g_slist_find_custom(priv->normalized_names, name,
				  (GCompareFunc)strcmp)) != NULL)
  {
      g_free (name);
      name = list->data;
  }
  else
  {
      priv->normalized_names = g_slist_prepend (priv->normalized_names,
						name);
  }
  return name;
}

/**
 * mc_account_set_normalized_name:
 * @account: The #McAccount.
 * @name: The name to set.
 *
 * Sets the normalized name of the account.
 *
 * Return value: %TRUE, or %FALSE if some error occurs.
 */
gboolean
mc_account_set_normalized_name (McAccount *account, const gchar *name)
{
    return _mc_account_gconf_set_string (account,
				      MC_ACCOUNTS_GCONF_KEY_NORMALIZED_NAME,
				      name);
}
  
/**
 * mc_account_get_unique_name:
 * @account: The #McAccount.
 *
 * Gets the unique name for the account.
 *
 * Return value: the unique name, or NULL.
 */
const gchar *
mc_account_get_unique_name (McAccount *account)
{
  g_return_val_if_fail (account != NULL, NULL);

  return MC_ACCOUNT_PRIV (account)->unique_name;
}

/**
 * mc_account_get_profile:
 * @account: The #McAccount.
 *
 * Get the #McProfile this #McAccount belongs to.
 *
 * Return value: the #McProfile, or NULL.
 */
McProfile *
mc_account_get_profile (McAccount *account)
{
  McProfile *ret;
  gchar *profile_name;

  g_return_val_if_fail (account != NULL, NULL);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        NULL);

  if (!_mc_account_gconf_get_string (account, MC_ACCOUNTS_GCONF_KEY_PROFILE,
                                        FALSE, &profile_name))
    return NULL;

  ret = mc_profile_lookup (profile_name);

  g_free (profile_name);

  return ret;
}

/**
 * mc_account_get_display_name:
 * @account: The #McAccount.
 *
 * Gets the display name for the account.
 *
 * Return value: the display name, or NULL.
 */
const gchar *
mc_account_get_display_name (McAccount *account)
{
  McAccountPrivate *priv;
  GSList *list;
  gchar *name;

  g_return_val_if_fail (account != NULL, NULL);
  priv = MC_ACCOUNT_PRIV (account);

  if (!_mc_account_gconf_get_string (account, MC_ACCOUNTS_GCONF_KEY_DISPLAY_NAME,
                                        FALSE, &name))
    return NULL;

  if ((list = g_slist_find_custom(priv->display_names, name,
				  (GCompareFunc)strcmp)) != NULL)
  {
      g_free (name);
      name = list->data;
  }
  else
  {
      priv->display_names = g_slist_prepend (priv->display_names, name);
  }
  return name;
}

/**
 * mc_account_set_display_name:
 * @account: The #McAccount.
 * @name: The name to set.
 *
 * Sets the display name of the account.
 *
 * Return value: %TRUE, or %FALSE if some error occurs.
 */
gboolean
mc_account_set_display_name (McAccount *account, const gchar *name)
{
    return _mc_account_gconf_set_string (account,
					 MC_ACCOUNTS_GCONF_KEY_DISPLAY_NAME,
					 name);
}

/**
 * mc_account_is_enabled:
 * @account: The #McAccount.
 *
 * Checks if the account is enabled.
 *
 * Return value: %TRUE if enabled, %FALSE otherwise.
 */
gboolean
mc_account_is_enabled (McAccount *account)
{
  gboolean enabled;

  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL, FALSE);

  if (!_mc_account_gconf_get_boolean (account, MC_ACCOUNTS_GCONF_KEY_ENABLED,
                                         FALSE, &enabled))
    return FALSE;

  return enabled;
}

static gboolean
mc_account_is_deleted (McAccount *account)
{
  gboolean deleted;

  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL, FALSE);

  if (!_mc_account_gconf_get_boolean (account, MC_ACCOUNTS_GCONF_KEY_DELETED,
                                         FALSE, &deleted))
    return FALSE;

  return deleted;
}

static gboolean
mc_account_set_deleted (McAccount *account, gboolean deleted)
{
  GConfClient *client;
  gchar *key;
  gboolean ok;

  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL, FALSE);

  client = gconf_client_get_default ();
  g_return_val_if_fail (client != NULL, FALSE);

  key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name,
                             MC_ACCOUNTS_GCONF_KEY_DELETED, FALSE);
  ok = gconf_client_set_bool (client, key, deleted, NULL);

  g_free (key);
  g_object_unref (client);

  return ok;
}

/**
 * mc_account_set_enabled:
 * @account: The #McAccount.
 * @enabled: whether the account must be enabled.
 *
 * Enables or disables an account.
 *
 * Return value: %TRUE, or %FALSE if some error occurred.
 */
gboolean
mc_account_set_enabled (McAccount *account, const gboolean enabled)
{
  GConfClient *client;
  gchar *key;
  gboolean ok;

  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL, FALSE);

  client = gconf_client_get_default ();
  g_return_val_if_fail (client != NULL, FALSE);

  gconf_client_suggest_sync (client, NULL);
  key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name,
                             MC_ACCOUNTS_GCONF_KEY_ENABLED, FALSE);
  ok = gconf_client_set_bool (client, key, enabled, NULL);

  g_free (key);
  g_object_unref (client);

  return ok;
}

/**
 * mc_account_get_param_boolean:
 * @account: The #McAccount.
 * @name: the parameter to retrieve.
 * @value: a pointer to the boolean variable.
 *
 * Gets a boolean parameter from the account settings.
 * 
 * Return value: a #McAccountSettingState.
 */
McAccountSettingState
mc_account_get_param_boolean (McAccount *account,
                                 const gchar *name,
                                 gboolean *value)
{
  McAccountSettingState ret;

  g_return_val_if_fail (account != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (name != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (value != NULL, MC_ACCOUNT_SETTING_ABSENT);

  /* TODO: retreive type from protocol and check it matches */

  ret = MC_ACCOUNT_SETTING_ABSENT;

  if (_mc_account_gconf_get_boolean (account, name, TRUE, value))
    {
      ret = MC_ACCOUNT_SETTING_FROM_ACCOUNT;
    }
  else
    {
      McProfile *profile;
      const gchar *def;

      profile = mc_account_get_profile (account);
      def = mc_profile_get_default_setting (profile, name);

      if (def != NULL)
        {
          if (strcmp (def, "true") == 0 || strcmp (def, "1") == 0)
            {
              *value = TRUE;
              ret = MC_ACCOUNT_SETTING_FROM_PROFILE;
            }
          else if (strcmp (def, "false") == 0 || strcmp (def, "0") == 0)
            {
              *value = FALSE;
              ret = MC_ACCOUNT_SETTING_FROM_PROFILE;
            }
          else
            {
              g_warning ("%s: unable to parse boolean %s on account %s parameter %s",
                  G_STRFUNC, def, MC_ACCOUNT_PRIV (account)->unique_name,
                  name);
              ret = MC_ACCOUNT_SETTING_ABSENT;
            }
        }
    }

  return ret;
}

static gboolean
_get_system_http_proxy (gboolean https, gchar **host, guint *port)
{
  gchar *ret_host;
  guint ret_port;
  GConfValue *value;
  GConfClient *client = gconf_client_get_default ();

  g_return_val_if_fail (client != NULL, FALSE);

  if (!https)
    {
      /* Plain HTTP. If use_http_proxy is not true, give up. */

      value = gconf_client_get (
          client, "/system/http_proxy/use_http_proxy", NULL);

      if (NULL == value)
        goto NONE;

      if (GCONF_VALUE_BOOL != value->type)
        {
          gconf_value_free (value);
          goto NONE;
        }

      if (FALSE == gconf_value_get_bool (value))
        {
          gconf_value_free (value);
          goto NONE;
        }

      gconf_value_free (value);

      /* If we're supposed to authenticate, give up. */

      value = gconf_client_get (
          client, "/system/http_proxy/use_authentication", NULL);

      if (NULL == value)
        goto NONE;

      if (GCONF_VALUE_BOOL != value->type)
        {
          gconf_value_free (value);
          goto NONE;
        }

      if (TRUE == gconf_value_get_bool (value))
        {
          gconf_value_free (value);
          goto NONE;
        }

      gconf_value_free (value);
    }

  /* If the proxy mode is not manual (i.e. it's none or automatic), give up. */

  value = gconf_client_get (client, "/system/proxy/mode", NULL);

  if (NULL == value)
    goto NONE;

  if (GCONF_VALUE_STRING != value->type)
    {
      gconf_value_free (value);
      goto NONE;
    }

  if (0 != strcmp ("manual", gconf_value_get_string (value)))
    {
      gconf_value_free (value);
      goto NONE;
    }

  gconf_value_free (value);

  if (https)
    {
      value = gconf_client_get (client, "/system/proxy/secure_host", NULL);

      if (NULL == value)
        goto NONE;

      if (GCONF_VALUE_STRING != value->type)
        {
          gconf_value_free (value);
          goto NONE;
        }

      ret_host = g_strdup (gconf_value_get_string (value));
      gconf_value_free (value);
      value = NULL;

      value = gconf_client_get (client, "/system/proxy/secure_port", NULL);

      if (NULL == value)
        goto NONE;

      if (GCONF_VALUE_INT != value->type)
        {
          gconf_value_free (value);
          goto NONE;
        }

      ret_port = gconf_value_get_int (value);
      gconf_value_free (value);
    }
  else
    {
      value = gconf_client_get (client, "/system/http_proxy/host", NULL);

      if (NULL == value)
        goto NONE;

      if (GCONF_VALUE_STRING != value->type)
        {
          gconf_value_free (value);
          goto NONE;
        }

      ret_host = g_strdup (gconf_value_get_string (value));
      gconf_value_free (value);

      value = gconf_client_get (client, "/system/http_proxy/port", NULL);

      if (NULL == value)
        goto NONE;

      if (GCONF_VALUE_INT != value->type)
        {
          gconf_value_free (value);
          g_free (ret_host);
          goto NONE;
        }

      ret_port = gconf_value_get_int (value);
      gconf_value_free (value);
    }

  if (0 == strcmp ("", ret_host) || ret_port <= 0)
    {
      g_free (ret_host);
      goto NONE;
    }

  g_object_unref (client);
  *host = ret_host;
  *port = ret_port;
  return TRUE;

NONE:
  g_object_unref (client);
  return FALSE;
}

/**
 * mc_account_get_param_int:
 * @account: The #McAccount.
 * @name: the parameter to retrieve.
 * @value: a pointer to the integer variable.
 *
 * Gets a integer parameter from the account settings.
 * 
 * Return value: a #McAccountSettingState.
 */
McAccountSettingState
mc_account_get_param_int (McAccount *account,
                             const gchar *name,
                             gint *value)
{
  gchar *end;
  glong long_val;
  gint int_val;
  McProfile *profile;
  const gchar *def;

  g_return_val_if_fail (account != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (name != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (value != NULL, MC_ACCOUNT_SETTING_ABSENT);

  /* TODO: retreive type from protocol and check it matches */

  if (_mc_account_gconf_get_int (account, name, TRUE, value))
      return MC_ACCOUNT_SETTING_FROM_ACCOUNT;

  profile = mc_account_get_profile (account);
  def = mc_profile_get_default_setting (profile, name);

  if (def != NULL)
    {
      errno = 0;
      long_val = strtol (def, &end, 10);

      if (*def == '\0' || *end != '\0')
        {
          g_warning ("%s: unable to parse integer %s on account %s parameter %s",
              G_STRFUNC, def, MC_ACCOUNT_PRIV (account)->unique_name,
              name);
          return MC_ACCOUNT_SETTING_ABSENT;
        }

      int_val = long_val;

      if (int_val != long_val || errno == ERANGE)
        {
          g_warning ("%s: integer %s out of range on account %s parameter %s",
              G_STRFUNC, def, MC_ACCOUNT_PRIV (account)->unique_name,
              name);
          return MC_ACCOUNT_SETTING_ABSENT;
        }

      *value = int_val;
      return MC_ACCOUNT_SETTING_FROM_PROFILE;
    }

  if (0 == strcmp(name, "http-proxy-port") ||
      0 == strcmp(name, "https-proxy-port"))
    {
      gchar *host;
      guint port;
      gboolean https;

      if (0 == strcmp (name, "https-proxy-port"))
        https = TRUE;
      else
        https = FALSE;

      if (_get_system_http_proxy (https, &host, &port))
        {
          *value = port;
          return MC_ACCOUNT_SETTING_FROM_PROXY;
        }
    }

  return MC_ACCOUNT_SETTING_ABSENT;
}

/**
 * mc_account_get_param_string:
 * @account: The #McAccount.
 * @name: the parameter to retrieve.
 * @value: a pointer to the string variable which will receive the setting.
 *
 * Gets a string parameter from the account settings. The string will have to
 * be freed with #g_free.
 * 
 * Return value: a #McAccountSettingState.
 */
McAccountSettingState
mc_account_get_param_string (McAccount *account,
                                const gchar *name,
                                gchar **value)
{
  McProfile *profile;
  const gchar *def;

  g_return_val_if_fail (account != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (name != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (value != NULL, MC_ACCOUNT_SETTING_ABSENT);

  /* TODO: retreive type from protocol and check it matches */

  if (_mc_account_gconf_get_string (account, name, TRUE, value))
      return MC_ACCOUNT_SETTING_FROM_ACCOUNT;

  profile = mc_account_get_profile (account);
  def = mc_profile_get_default_setting (profile, name);

  if (def != NULL)
    {
      *value = g_strdup (def);
      return MC_ACCOUNT_SETTING_FROM_PROFILE;
    }

  if (0 == strcmp(name, "http-proxy-server") ||
      0 == strcmp(name, "https-proxy-server"))
    {
      gchar *host;
      guint port;
      gboolean https;

      if (0 == strcmp (name, "https-proxy-server"))
        https = TRUE;
      else
        https = FALSE;

      if (_get_system_http_proxy (https, &host, &port))
        {
          *value = host;
          return MC_ACCOUNT_SETTING_FROM_PROXY;
        }
    }

  return MC_ACCOUNT_SETTING_ABSENT;
}

/**
 * mc_account_set_param_boolean:
 * @account: The #McAccount.
 * @name: the parameter to set.
 * @value: a boolean value.
 *
 * Sets a boolean parameter in the account settings.
 *
 * Return value: %TRUE, or %FALSE if some error occurred.
 */
gboolean
mc_account_set_param_boolean (McAccount *account,
                                 const gchar *name,
                                 gboolean value)
{
  GConfClient *client;
  gchar *key;
  gboolean ok;

  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  client = gconf_client_get_default ();
  g_return_val_if_fail (client != NULL, FALSE);

  key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name, name,
                             TRUE);
  ok = gconf_client_set_bool (client, key, value, NULL);

  g_free (key);
  g_object_unref (client);

  return ok;
}

/**
 * mc_account_set_param_int:
 * @account: The #McAccount.
 * @name: the parameter to set.
 * @value: a integer value.
 *
 * Sets a integer parameter in the account settings.
 *
 * Return value: %TRUE, or %FALSE if some error occurred.
 */
gboolean
mc_account_set_param_int (McAccount *account,
                             const gchar *name,
                             gint value)
{
  GConfClient *client;
  gchar *key;
  gboolean ok;

  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  client = gconf_client_get_default ();
  g_return_val_if_fail (client != NULL, FALSE);

  key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name, name,
                             TRUE);
  ok = gconf_client_set_int (client, key, value, NULL);

  g_free (key);
  g_object_unref (client);

  return ok;
}

/**
 * mc_account_set_param_string:
 * @account: The #McAccount.
 * @name: the parameter to set.
 * @value: a string value.
 *
 * Sets a string parameter in the account settings.
 *
 * Return value: %TRUE, or %FALSE if some error occurred.
 */
gboolean
mc_account_set_param_string (McAccount *account,
                                const gchar *name,
                                const gchar *value)
{
  GConfClient *client;
  gchar *key;
  gboolean ok;

  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  client = gconf_client_get_default ();
  g_return_val_if_fail (client != NULL, FALSE);

  key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name, name,
                             TRUE);
  ok = gconf_client_set_string (client, key, value, NULL);

  g_free (key);
  g_object_unref (client);

  return ok;
}

/**
 * mc_account_unset_param:
 * @account: The #McAccount.
 * @name: the parameter to unset.
 *
 * Unsets (removes) a parameter from the account settings.
 *
 * Return value: %TRUE, or %FALSE if some error occurred.
 */
gboolean
mc_account_unset_param (McAccount *account, const gchar *name)
{
  McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);
  GConfClient *client;
  gchar *key;
  gboolean ok;

  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  client = gconf_client_get_default ();
  g_return_val_if_fail (client != NULL, FALSE);

  key = _mc_account_path (priv->unique_name, name, TRUE);
  ok = gconf_client_unset (client, key, NULL);

  g_free (key);
  g_object_unref (client);

  return ok;
}

static void
_g_value_free (gpointer data)
{
  GValue *value = (GValue *) data;
  g_value_unset (value);
  g_free (value);
}

static void
_add_one_setting (McAccount *account,
                  McProtocolParam *param,
                  GHashTable *hash)
{
  GValue *value = NULL;
  McAccountSettingState ret = MC_ACCOUNT_SETTING_ABSENT;

  g_return_if_fail (account != NULL);
  g_return_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL);
  g_return_if_fail (param != NULL);
  g_return_if_fail (param->name != NULL);
  g_return_if_fail (param->signature != NULL);

  switch (param->signature[0])
    {
    case DBUS_TYPE_STRING:
      {
        char *tmp;
        ret = mc_account_get_param_string (account, param->name, &tmp);
        if (ret != MC_ACCOUNT_SETTING_ABSENT)
          {
            value = g_new0(GValue, 1);
            g_value_init (value, G_TYPE_STRING);
            g_value_take_string (value, tmp);
          }
        break;
      }
    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
      {
        gint tmp;
        ret = mc_account_get_param_int (account, param->name, &tmp);
        if (ret != MC_ACCOUNT_SETTING_ABSENT)
          {
            value = g_new0(GValue, 1);
            g_value_init (value, G_TYPE_INT);
            g_value_set_int (value, tmp);
          }
        break;
      }
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
      {
        gint tmp;
        ret = mc_account_get_param_int (account, param->name, &tmp);
        if (ret != MC_ACCOUNT_SETTING_ABSENT)
          {
            value = g_new0(GValue, 1);
            g_value_init (value, G_TYPE_UINT);
            g_value_set_uint (value, tmp);
          }
        break;
      }
    case DBUS_TYPE_BOOLEAN:
      {
        gboolean tmp;
        ret = mc_account_get_param_boolean (account, param->name, &tmp);
        if (ret != MC_ACCOUNT_SETTING_ABSENT)
          {
            value = g_new0(GValue, 1);
            g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, tmp);
          }
        break;
      }
    default:
      g_warning ("%s: skipping parameter %s, unknown type %s", G_STRFUNC, param->name, param->signature);
    }

  if (ret != MC_ACCOUNT_SETTING_ABSENT && hash != NULL)
    {
      g_return_if_fail (value != NULL);
      g_hash_table_insert (hash, g_strdup (param->name), value);
    }
}

static gboolean
copy_file (const gchar *filename_in, const gchar *filename_out)
{
    FILE *f_in, *f_out;
    char buffer[2048];
    size_t read, written;
    gboolean ret = TRUE;

    f_in = fopen (filename_in, "r");
    f_out = fopen (filename_out, "w");
    if (!f_in || !f_out) return FALSE;
    while ((read = fread (buffer, 1, sizeof (buffer), f_in)) > 0)
    {
	written = fwrite (buffer, 1, read, f_out);
	if (written < read)
	{
	    g_warning ("%s: fwrite failed (errno = %d)",
		       G_STRFUNC, errno);
	    ret = FALSE;
	    break;
	}
    }
    if (ferror (f_in))
    {
	g_warning ("%s: fread failed (errno = %d)",
		   G_STRFUNC, errno);
	ret = FALSE;
    }
    fclose (f_in);
    fclose (f_out);
    return ret;
}

/**
 * mc_account_get_params:
 * @account: The #McAccount.
 *
 * Gets all the parameters for this account. The returned hash table must be
 * freed.
 *
 * Return value: a #GHashTable containing all the account settings, or NULL.
 */
GHashTable *
mc_account_get_params (McAccount *account)
{
  McProfile *profile = NULL;
  McProtocol *protocol = NULL;
  GSList *params, *tmp;
  GHashTable *ret = NULL;

  g_return_val_if_fail (account != NULL, NULL);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        NULL);

  profile = mc_account_get_profile (account);
  if (profile == NULL)
    {
      g_debug ("%s: getting profile failed", G_STRFUNC);
      goto OUT;
    }

  protocol = mc_profile_get_protocol (profile);
  if (protocol == NULL)
    {
      g_debug ("%s: getting protocol failed", G_STRFUNC);
      goto OUT;
    }

  ret = g_hash_table_new_full (g_str_hash, g_str_equal,
                               (GDestroyNotify) g_free,
                               (GDestroyNotify) _g_value_free);

  params = mc_protocol_get_params (protocol);

  for (tmp = params; tmp != NULL; tmp = tmp->next)
    _add_one_setting (account, (McProtocolParam *) tmp->data, ret);

  mc_protocol_free_params_list (params);

OUT:
  if (protocol)
    g_object_unref (protocol);

  if (profile)
    g_object_unref (profile);

  return ret;
}

/**
 * mc_account_is_complete:
 * @account: The #McAccount.
 *
 * Checks if all the mandatory parameters declared by the protocol are present
 * in this account's settings.
 *
 * Return value: a gboolean.
 */
gboolean
mc_account_is_complete (McAccount *account)
{
  McProfile *profile = NULL;
  McProtocol *protocol = NULL;
  GSList *params = NULL, *tmp;
  gboolean ret = TRUE;

  g_return_val_if_fail (account != NULL, FALSE);
  
  /* Check if account was expunged */
  if (MC_ACCOUNT_PRIV (account)->unique_name == NULL)
        return FALSE;

  /* Check if account was deleted */
  if (mc_account_is_deleted (account))
    return FALSE;
  
  profile = mc_account_get_profile (account);
  if (profile == NULL)
    {
      ret = FALSE;
      goto OUT;
    }

  protocol = mc_profile_get_protocol (profile);
  if (protocol == NULL)
    {
      ret = FALSE;
      goto OUT;
    }

  params = mc_protocol_get_params (protocol);

  for (tmp = params; tmp != NULL; tmp = tmp->next)
    {
      McProtocolParam *param;
      const gchar *def;
      GConfValue *val;

      param = (McProtocolParam *) tmp->data;

      if (!(param->flags & MC_PROTOCOL_PARAM_REQUIRED))
        continue;

      if (param == NULL || param->name == NULL || param->signature == NULL)
        {
          ret = FALSE;
          break;
        }

      /* TODO: check this value can be mapped to the desired type */
      def = mc_profile_get_default_setting (profile, param->name);
      if (def)
        {
          continue;
        }

      val = _mc_account_gconf_get (account, param->name, TRUE);
      if (val == NULL)
        {
          ret = FALSE;
          break;
        }

      /* TODO: unduplicate this type mapping */
      switch (param->signature[0])
        {
        case DBUS_TYPE_BOOLEAN:
          if (val->type != GCONF_VALUE_BOOL)
            ret = FALSE;
          break;
        case DBUS_TYPE_INT16:
        case DBUS_TYPE_UINT16:
          if (val->type != GCONF_VALUE_INT)
            ret = FALSE;
          break;
        case DBUS_TYPE_STRING:
          if (val->type != GCONF_VALUE_STRING)
            ret = FALSE;
          break;
        default:
          ret = FALSE;
        }

      gconf_value_free (val);

      if (ret == FALSE)
        break;
    }

  mc_protocol_free_params_list (params);

OUT:
  if (profile != NULL)
    g_object_unref (profile);

  if (protocol != NULL)
    g_object_unref (protocol);

  return ret;
}

/**
 * mc_account_get_supported_presences:
 * @account: the #McAccount.
 *
 * Checks what presence states are supported by this account.
 *
 * Returns: a zero-terminated array listing all the supported #McPresence.
 * It must not be freed.
 */
const McPresence *
mc_account_get_supported_presences (McAccount *account)
{
    McProfile *profile = mc_account_get_profile (account);
    const McPresence *presences;

    presences = mc_profile_get_supported_presences (profile);
    g_object_unref (profile);
    return presences;
}

/*
 * mc_account_supports_presence:
 * @account: The #McAccount.
 * @presence: The #McPresence.
 *
 * Tests whether the account supports the presence @presence.
 *
 * Returns: a #gboolean.
 */
gboolean
mc_account_supports_presence (McAccount *account, McPresence presence)
{
    McProfile *profile = mc_account_get_profile (account);
    gboolean supported;

    supported = mc_profile_supports_presence (profile, presence);
    g_object_unref (profile);
    return supported;
}

/**
 * mc_account_set_avatar:
 * @account: The #McAccount.
 * @filename: the path of the image file to be used as avatar.
 * @mime_type: the MIME type of the image.
 *
 * Set the avatar for this account. If @filename is %NULL, the avatar is
 * cleared.
 *
 * Return value: %TRUE, or %FALSE if some error occurred.
 */
gboolean
mc_account_set_avatar (McAccount *account, const gchar *filename,
		       const gchar *mime_type)
{
    gchar *data_dir, *filename_out;
    GConfClient *client;
    gboolean ret = TRUE;
    gchar *key;

    g_return_val_if_fail (account != NULL, FALSE);

    data_dir = get_account_data_path (MC_ACCOUNT_PRIV(account)->unique_name);
    filename_out = g_build_filename (data_dir, MC_AVATAR_FILENAME, NULL);
    if (!g_file_test (data_dir, G_FILE_TEST_EXISTS))
	g_mkdir_with_parents (data_dir, 0777);
    g_free (data_dir);

    if (filename)
    {
	/* copy the file in the account data directory */
	if (strcmp (filename_out, filename) != 0)
	{
	    if (copy_file (filename, filename_out) == FALSE)
	    {
		g_warning ("%s: copy_file failed", G_STRLOC);
		g_free (filename_out);
		return FALSE;
	    }
	}
    }
    else
    {
	/* create an empty file; this will cause MC to clear the current
	 * avatar */
	FILE *f_out = fopen (filename_out, "w");
	fclose (f_out);
    }

    client = gconf_client_get_default ();
    g_return_val_if_fail (client != NULL, FALSE);

    key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name,
			    MC_ACCOUNTS_GCONF_KEY_AVATAR_TOKEN, FALSE);
    ret = gconf_client_unset(client, key, NULL);
    g_free (key);
    if (!ret) goto error;

    /* put an ID for the avatar, so that listeners of the "account-changed"
     * signal will be able to determine whether the avatar has changed without
     * having to load the file */
    key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name,
			    MC_ACCOUNTS_GCONF_KEY_AVATAR_ID, FALSE);
    ret = gconf_client_set_int(client, key, g_random_int(), NULL);
    g_free (key);

    if (mime_type)
    {
	key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name,
				MC_ACCOUNTS_GCONF_KEY_AVATAR_MIME, FALSE);
	ret = gconf_client_set_string (client, key, mime_type, NULL);
	g_free (key);
    }

error:
    g_object_unref (client);
    g_free (filename_out);

    return ret;
}

/**
 * mc_account_set_avatar_token:
 * @account: The #McAccount.
 * @token: string holding the Telepathy token for the avatar.
 *
 * Set the avatar token for this account. This function is to be used only by
 * the mission-control server.
 *
 * Return value: %TRUE, or %FALSE if some error occurred.
 */
gboolean
mc_account_set_avatar_token (McAccount *account, const gchar *token)
{
    return _mc_account_gconf_set_string (account,
					 MC_ACCOUNTS_GCONF_KEY_AVATAR_TOKEN,
					 token);
}

/**
 * mc_account_set_avatar_mime_type:
 * @account: The #McAccount.
 * @mime_type: string holding the mime-type of the avatar.
 *
 * Set the avatar mime-type for this account. This function is to be used only
 * by the mission-control server.
 *
 * Return value: %TRUE, or %FALSE if some error occurred.
 */
gboolean
mc_account_set_avatar_mime_type (McAccount *account, const gchar *mime_type)
{
    return _mc_account_gconf_set_string (account,
					 MC_ACCOUNTS_GCONF_KEY_AVATAR_MIME,
					 mime_type);
}

/**
 * mc_account_get_avatar:
 * @account: The #McAccount.
 * @filename: address of the variable to hold the path of the image file used
 * as avatar.
 * @mime_type: address of the variable for the MIME type of the image.
 * @token: address of the variable for the Telepathy token of the avatar.
 *
 * Get the avatar for this account. Any of the parameters for the output can be
 * NULL, if that information is not needed.
 *
 * Return value: %TRUE, or %FALSE if some error occurred.
 */
gboolean
mc_account_get_avatar (McAccount *account, gchar **filename,
		       gchar **mime_type, gchar **token)
{
    gchar *data_dir;
    GConfClient *client;
    gchar *key;

    g_return_val_if_fail (account != NULL, FALSE);

    if (filename)
    {
	data_dir =
	    get_account_data_path (MC_ACCOUNT_PRIV (account)->unique_name);
	*filename = g_build_filename (data_dir, MC_AVATAR_FILENAME, NULL);
	if (!g_file_test (data_dir, G_FILE_TEST_EXISTS))
	    g_mkdir_with_parents (data_dir, 0777);
	g_free (data_dir);
    }

    client = gconf_client_get_default ();
    g_return_val_if_fail (client != NULL, FALSE);

    if (token)
    {
	key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name,
				MC_ACCOUNTS_GCONF_KEY_AVATAR_TOKEN, FALSE);
	*token = gconf_client_get_string (client, key, NULL);
	g_free (key);
    }

    if (mime_type)
    {
	key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name,
				MC_ACCOUNTS_GCONF_KEY_AVATAR_MIME, FALSE);
	*mime_type = gconf_client_get_string (client, key, NULL);
	g_free (key);
    }

    g_object_unref (client);

    return TRUE;
}

/**
 * mc_account_get_avatar_id:
 * @account: The #McAccount.
 *
 * Get the avatar ID for this account. The ID is a number that changes
 * everytime the avatar image changes.
 *
 * Returns: the avatar ID.
 */
gint
mc_account_get_avatar_id (McAccount *account)
{
    GConfClient *client;
    gchar *key;
    gint ret;

    g_return_val_if_fail (account != NULL, 0);

    client = gconf_client_get_default ();
    g_return_val_if_fail (client != NULL, 0);

    key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name,
			    MC_ACCOUNTS_GCONF_KEY_AVATAR_ID, FALSE);
    ret = gconf_client_get_int (client, key, NULL);
    g_free (key);

    g_object_unref (client);
    return ret;
}

/**
 * mc_account_reset_avatar_id:
 * @account: The #McAccount.
 *
 * Calculates a new avatar ID for this account. This function is to be called
 * when the avatar image file has been changed by a direct modification of its
 * binary content.
 *
 * Returns: %TRUE, or %FALSE if some error occurred.
 */
gboolean
mc_account_reset_avatar_id (McAccount *account)
{
    GConfClient *client;
    gchar *key;
    gboolean ok;

    g_return_val_if_fail (account != FALSE, 0);

    client = gconf_client_get_default ();
    g_return_val_if_fail (client != FALSE, 0);

    key = _mc_account_path (MC_ACCOUNT_PRIV (account)->unique_name,
			    MC_ACCOUNTS_GCONF_KEY_AVATAR_ID, FALSE);
    ok = gconf_client_set_int (client, key, g_random_int(), NULL);
    g_free (key);

    g_object_unref (client);
    return ok;
}

