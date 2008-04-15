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
#include "mc-account-proxy.h"
#include "mc-account-priv.h"
#include "mc-account-monitor.h"
#include "mc-account-monitor-priv.h"
#include "mc-profile.h"
#include "mc.h"
#include <config.h>

#define MC_ACCOUNTS_MAX 1024
#define MC_AVATAR_FILENAME	"avatar.bin"

#define MC_ACCOUNT_PRIV(account) ((McAccountPrivate *)MC_ACCOUNT (account)->priv)

G_DEFINE_TYPE (McAccount, mc_account, G_TYPE_OBJECT);

typedef struct
{
    TpProxy *proxy;
    gchar *manager_name;
    gchar *protocol_name;
  gchar *unique_name;
  gchar *profile_name;
  GSList *display_names;
  GSList *normalized_names;
  gchar *alias;
  gboolean enabled;
  gboolean valid;
  gchar *last_name;
  gchar *last_value;
  gint avatar_id;
} McAccountPrivate;

static GSList *set_first_element (GSList *list, const gchar *value);

static void
mc_account_dispose (GObject *object)
{
    McAccount *self = MC_ACCOUNT(object);
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (self);

    if (priv->proxy)
    {
	g_object_unref (priv->proxy);
	priv->proxy = NULL;
    }
    G_OBJECT_CLASS (mc_account_parent_class)->dispose (object);
}

static void
mc_account_finalize (GObject *object)
{
  McAccount *self = MC_ACCOUNT(object);
  McAccountPrivate *priv = MC_ACCOUNT_PRIV (self);
  
  g_free (priv->manager_name);
  g_free (priv->protocol_name);
  g_free (priv->unique_name);
  g_free (priv->profile_name);
  g_slist_foreach (priv->display_names, (GFunc)g_free, NULL);
  g_slist_free (priv->display_names);
  g_slist_foreach (priv->normalized_names, (GFunc)g_free, NULL);
  g_slist_free (priv->normalized_names);
  g_free (priv->alias);
  g_free (priv->last_name);
  g_free (priv->last_value);
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

static inline gboolean
parse_object_path (McAccountPrivate *priv, const gchar *object_path)
{
    gchar manager[64], protocol[64], unique_name[256];
    gint n;

    n = sscanf (object_path, MC_ACCOUNT_DBUS_OBJECT_BASE "%[^/]/%[^/]/%s",
		manager, protocol, unique_name);
    if (n != 3) return FALSE;

    priv->manager_name = g_strdup (manager);
    priv->protocol_name = g_strdup (protocol);
    priv->unique_name = g_strdup (MC_ACCOUNT_UNIQUE_NAME_FROM_PATH (object_path));
    return TRUE;
}

static void
print_prop (gpointer key, gpointer ht_value, gpointer userdata)
{
    const gchar *name = key;
    const GValue *value = ht_value;

    g_debug ("prop: %s (%s)", name, G_VALUE_TYPE_NAME (value));
}

static void
on_account_property_changed (TpProxy *proxy, GHashTable *properties,
			     gpointer user_data, GObject *weak_object)
{
    //McAccount *account = MC_ACCOUNT (weak_object);
    McAccountPrivate *priv = user_data;
    const GValue *value;

    g_hash_table_foreach (properties, print_prop, NULL);

    McAccountMonitor *monitor = mc_account_monitor_new ();

    value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_VALID);
    if (value)
	priv->valid = g_value_get_boolean (value);

    value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_ENABLED);
    if (value)
    {
	priv->enabled = g_value_get_boolean (value);
	g_signal_emit_by_name (monitor, priv->enabled ?
			       "account-enabled" : "account-disabled",
			       priv->unique_name);
    }

    value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_NORMALIZED_NAME);
    if (value)
	priv->normalized_names = set_first_element (priv->normalized_names,
						 g_value_get_string (value));

    value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_DISPLAY_NAME);
    if (value)
    {
	priv->display_names = set_first_element (priv->display_names,
						 g_value_get_string (value));
    }

    value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_ALIAS);
    if (value)
    {
	g_free (priv->alias);
	priv->alias = g_value_dup_string (value);
    }

    value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_AVATAR);
    if (value)
	priv->avatar_id = time(0);

    /* FIXME: we should also emit the "param-changed" signal if an account
     * parameter changed, but since is was used only be the mission-control
     * process, we can skip now. Besides, we wouldn't know which parameter
     * actually changed */

    /* emit the account-changed signal */
    g_signal_emit_by_name (monitor, "account-changed", priv->unique_name);
    g_object_unref (monitor);
}

McAccount *
_mc_account_new (TpDBusDaemon *dbus_daemon, const gchar *object_path)
{
    McAccountPrivate *priv;
    McAccount *new;
    GHashTable *properties;
    GError *error = NULL;

    new = (McAccount *)g_object_new (MC_TYPE_ACCOUNT, NULL);
    priv = MC_ACCOUNT_PRIV (new);
    priv->proxy = g_object_new (MC_TYPE_ACCOUNT_PROXY,
				"dbus-daemon", dbus_daemon,
				"bus-name", MC_ACCOUNT_MANAGER_DBUS_SERVICE,
				"object-path", object_path,
				NULL);
    if (!priv->proxy ||
	!parse_object_path (priv, object_path))
	return NULL;

    mc_cli_account_connect_to_account_property_changed (priv->proxy,
							on_account_property_changed,
							priv, NULL,
							(GObject *)new, NULL);

    mc_cli_dbus_properties_do_get_all (priv->proxy, -1,
				       MC_IFACE_ACCOUNT,
				       &properties, &error);
    if (error)
    {
	g_warning ("Properties error: %s", error->message);
	g_error_free (error);
    }
    else
    {
	GValue *value;

	value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_ENABLED);
	if (value)
	    priv->enabled = g_value_get_boolean (value);

	value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_VALID);
	if (value)
	    priv->valid = g_value_get_boolean (value);

	value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_NORMALIZED_NAME);
	if (value)
	    priv->normalized_names = g_slist_prepend (NULL, g_value_dup_string (value));

	value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_DISPLAY_NAME);
	if (value)
	    priv->display_names = g_slist_prepend (NULL, g_value_dup_string (value));

	value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_ALIAS);
	if (value)
	    priv->alias = g_value_dup_string (value);

	g_hash_table_destroy (properties);
    }

    mc_cli_dbus_properties_do_get_all (priv->proxy, -1,
				       MC_IFACE_ACCOUNT_INTERFACE_COMPAT,
				       &properties, &error);
    if (error)
    {
	g_warning ("Compat properties error: %s", error->message);
	g_error_free (error);
    }
    else
    {
	GValue *value;

	value = g_hash_table_lookup (properties, MC_ACCOUNTS_GCONF_KEY_PROFILE);
	if (value)
	    priv->profile_name = g_value_dup_string (value);

	g_hash_table_destroy (properties);
    }

    return new;
}

void
_mc_account_set_enabled_priv (McAccount *account, gboolean enabled)
{
  g_return_if_fail (account != NULL);
  MC_ACCOUNT_PRIV (account)->enabled = enabled;
}

static gint
strcmp_null (gconstpointer a, gconstpointer b)
{
    if (a == b) return 0;
    if (!a || !b) return 1;
    return strcmp (a, b);
}

static GSList *
set_first_element (GSList *list, const gchar *value)
{
    GSList *elem;

    if (value && value[0] == 0) value = NULL;

    if ((elem = g_slist_find_custom(list, value, strcmp_null)) != NULL)
    {
	if (elem != list)
	{
	    /* move the new name at the beginning of the list */
	    list = g_slist_remove_link (list, elem);
	    list = g_slist_concat (elem, list);
	}
    }
    else
	list = g_slist_prepend (list, g_strdup (value));
    return list;
}

void
_mc_account_set_normalized_name_priv (McAccount *account, const gchar *name)
{
    McAccountPrivate *priv;

    g_return_if_fail (account != NULL);
    priv = MC_ACCOUNT_PRIV (account);
    priv->normalized_names = set_first_element (priv->normalized_names, name);
}

void
_mc_account_set_display_name_priv (McAccount *account, const gchar *name)
{
    McAccountPrivate *priv;

    g_return_if_fail (account != NULL);
    priv = MC_ACCOUNT_PRIV (account);
    priv->display_names = set_first_element (priv->display_names, name);
}

static void
mc_account_class_init (McAccountClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (object_class, sizeof (McAccountPrivate));
  object_class->dispose = mc_account_dispose;
  object_class->finalize = mc_account_finalize;
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
  const gchar *compare_account, *normalized_name;
  gchar *gconf_account;
  gboolean ret;

  g_return_val_if_fail (acct != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (acct)->unique_name != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  compare_account = (const gchar *) data;

  if (mc_account_get_param_string (acct, "account", &gconf_account) ==
      MC_ACCOUNT_SETTING_ABSENT)
      return FALSE;

  ret = (0 == strcmp(gconf_account, compare_account));

  g_free (gconf_account);

  if (!ret)
  {
      normalized_name = mc_account_get_normalized_name (acct);
      if (!normalized_name)
	return FALSE;

      ret = (0 == strcmp(normalized_name, compare_account));
  }

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
    McAccountMonitor *monitor;
    McAccount *account;
    McAccountPrivate *priv;
    McProtocol *protocol;
    McManager *manager;
    const gchar *manager_name, *protocol_name;
    GHashTable *params;
    GValue value = { 0 };

    protocol = mc_profile_get_protocol (profile);
    if (!protocol) return NULL;
    manager = mc_protocol_get_manager (protocol);
    if (!manager) return NULL;

    protocol_name = mc_protocol_get_name (protocol);
    manager_name = mc_manager_get_unique_name (manager);
    monitor = mc_account_monitor_new ();
    params = g_hash_table_new (g_str_hash, g_str_equal);
    account = _mc_account_monitor_create_account (monitor, manager_name,
						  protocol_name, NULL,
						  params);
    g_hash_table_destroy (params);
    g_object_unref (protocol);
    g_object_unref (monitor);
    g_object_unref (manager);

    if (account)
    {
	GError *error = NULL;
	priv = account->priv;
	g_value_init (&value, G_TYPE_STRING);
	g_value_set_static_string (&value, mc_profile_get_unique_name (profile));
	mc_cli_dbus_properties_do_set (priv->proxy, -1,
				       MC_IFACE_ACCOUNT_INTERFACE_COMPAT,
				       MC_ACCOUNTS_GCONF_KEY_PROFILE,
				       &value, &error);
	if (error)
	{
	    g_warning ("setting profile on %s failed: %s",
		       priv->unique_name, error->message);
	    g_error_free (error);
	}
    }
    return account;
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
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);
    GError *error = NULL;
  
    mc_account_set_enabled (account, FALSE);

    mc_cli_account_do_remove (priv->proxy, -1, &error);
    if (error)
    {
	g_warning ("%s on %s failed: %s",
		   G_STRFUNC, priv->unique_name, error->message);
	g_error_free (error);
	return FALSE;
    }

    return TRUE;
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
    McAccountPrivate *priv = acct->priv;
    gchar *profile_name = data;

    return strcmp (priv->profile_name, profile_name) == 0;
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
  const gchar *profile_name;
  GList *ret;

  g_return_val_if_fail (profile != NULL, NULL);
  profile_name = mc_profile_get_unique_name (profile);
  g_return_val_if_fail (profile_name != NULL, NULL);

  ret = mc_accounts_list ();
  ret = mc_accounts_filter (ret, _filter_profile, (gchar *)profile_name);

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

static gboolean
_filter_secondary_vcard_field (McAccount *acct, gpointer data)
{
  const gchar *vcard_field;
  GList *fields, *field;
  gboolean ret;

  g_return_val_if_fail (acct != NULL, FALSE);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (acct)->unique_name != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  vcard_field = (const gchar *) data;

  fields = mc_account_get_secondary_vcard_fields (acct);
  if (fields == NULL) return FALSE;

  ret = FALSE;
  for (field = fields; field != NULL; field = field->next) {
      if (0 == strcmp(vcard_field, fields->data)) {
          ret = TRUE;
      }
  }

  g_list_foreach (fields, (GFunc)g_free, NULL);
  g_list_free (fields);
  return ret;
}

/**
 * mc_account_set_secondary_vcard_fields:
 *
 * Set all configured secondary vcard fields for this account.
 *
 * Returns: %TRUE on success, %FALSE otherwise.
 */
gboolean
mc_account_set_secondary_vcard_fields (McAccount *account, const GList *fields)
{
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);
    GValue value = { 0 };
    GError *error = NULL;
    gchar **v_fields;
    guint len, i;

    len = g_list_length ((GList *)fields);
    v_fields = g_malloc (sizeof (gchar *) * (len + 1));
    for (i = 0; fields; fields = fields->next, i++)
	v_fields[i] = g_strdup (fields->data);
    v_fields[i] = NULL;

    g_value_init (&value, G_TYPE_STRV);
    g_value_take_boxed (&value, v_fields);
    mc_cli_dbus_properties_do_set (priv->proxy, -1,
				   MC_IFACE_ACCOUNT_INTERFACE_COMPAT,
				   MC_ACCOUNTS_GCONF_KEY_SECONDARY_VCARD_FIELDS,
				   &value, &error);
    g_value_unset (&value);
    if (error)
    {
	g_warning ("%s on %s failed: %s",
		   G_STRFUNC, priv->unique_name, error->message);
	g_error_free (error);
	return FALSE;
    }
    else
	return TRUE;
}

/**
 * mc_account_get_secondary_vcard_fields:
 * Get all configured secondairy vcard fields for this account.
 *
 * Return value: a #GList of all vcard fields (as char *). Is a copy, both data
 * and list must be freed by receiver.
 */
GList *
mc_account_get_secondary_vcard_fields (McAccount * acct)
{
#ifdef GET_SECONDARY_VCARD_FIELDS
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (acct);
    GValue *val_fields;
    GList *ret = NULL;
    gchar **fields, **field;
    GError *error = NULL;

    mc_cli_dbus_properties_do_get (priv->proxy, -1,
				   MC_IFACE_ACCOUNT_INTERFACE_COMPAT,
				   MC_ACCOUNTS_GCONF_KEY_SECONDARY_VCARD_FIELDS,
				   &val_fields, &error);
    if (error)
    {
	g_warning ("%s on %s failed: %s",
		   G_STRFUNC, priv->unique_name, error->message);
	g_error_free (error);
	return NULL;
    }
    fields = g_value_get_boxed (val_fields);
    g_free (val_fields);

    /* put the fields into a list */
    for (field = fields; *field != NULL; field++)
	ret = g_list_append (ret, *field);
    g_free (fields);

    return ret;
#else
    g_warning ("%s is disabled due to spamming", G_STRFUNC);
    return NULL;
#endif
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
 * mc_accounts_list_by_secondary_vcard_field:
 * @vcard_field: the VCard field.
 *
 * List all accounts that can use the secondary VCard field given.
 *
 * Return value: A #GList of the accounts. Must be freed with #mc_accounts_list_free.
 */
GList *
mc_accounts_list_by_secondary_vcard_field (const gchar *vcard_field)
{
  GList *ret;

  ret = mc_accounts_list ();
  ret = mc_accounts_filter (ret, _filter_secondary_vcard_field, (gpointer) vcard_field);

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
 * Return value: the normalized name, or NULL.
 */
const gchar *
mc_account_get_normalized_name (McAccount *account)
{
  McAccountPrivate *priv;

  g_return_val_if_fail (account != NULL, NULL);
  priv = MC_ACCOUNT_PRIV (account);

  return (priv->normalized_names) ? priv->normalized_names->data : NULL;
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
    g_warning ("%s: only mission-control should call this function!",
	       G_STRFUNC);
    return FALSE;
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
  McAccountPrivate *priv;

  g_return_val_if_fail (account != NULL, NULL);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        NULL);

  priv = MC_ACCOUNT_PRIV (account);
  if (G_UNLIKELY (!priv->profile_name))
  {
    GValue *val_profile;
    GError *error = NULL;

    mc_cli_dbus_properties_do_get (priv->proxy, -1,
				   MC_IFACE_ACCOUNT_INTERFACE_COMPAT,
				   MC_ACCOUNTS_GCONF_KEY_PROFILE,
				   &val_profile, &error);
    if (error)
    {
	g_warning ("%s: getting profile for %s failed: %s",
		   G_STRFUNC, priv->unique_name, error->message);
	g_error_free (error);
	return NULL;
    }
    priv->profile_name = (gchar *)g_value_get_string (val_profile);
    g_free (val_profile);
  }

  return mc_profile_lookup (priv->profile_name);
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

  g_return_val_if_fail (account != NULL, NULL);
  priv = MC_ACCOUNT_PRIV (account);

  return (priv->display_names) ? priv->display_names->data : NULL;
}

/**
 * mc_account_set_display_name:
 * @account: The #McAccount.
 * @name: The name to set.
 *
 * Sets the display name of the account. If @name is NULL or an empty string,
 * the display name is unset.
 *
 * Return value: %TRUE, or %FALSE if some error occurs.
 */
gboolean
mc_account_set_display_name (McAccount *account, const gchar *name)
{
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);
    GValue value = { 0 };
    GError *error = NULL;

    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, name);
    mc_cli_dbus_properties_do_set (priv->proxy, -1,
				   MC_IFACE_ACCOUNT,
				   MC_ACCOUNTS_GCONF_KEY_DISPLAY_NAME,
				   &value, &error);
    if (error)
    {
	g_warning ("%s on %s failed: %s",
		   G_STRFUNC, priv->unique_name, error->message);
	g_error_free (error);
	return FALSE;
    }
    else
	return TRUE;
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
  g_return_val_if_fail (account != NULL, FALSE);

  return MC_ACCOUNT_PRIV (account)->enabled;
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
mc_account_set_enabled (McAccount *account, gboolean enabled)
{
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);
    GValue value = { 0 };
    GError *error = NULL;

    g_value_init (&value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&value, enabled);
    mc_cli_dbus_properties_do_set (priv->proxy, -1,
				   MC_IFACE_ACCOUNT,
				   MC_ACCOUNTS_GCONF_KEY_ENABLED,
				   &value, &error);
    if (error)
    {
	g_warning ("%s on %s failed: %s",
		   G_STRFUNC, priv->unique_name, error->message);
	g_error_free (error);
	return FALSE;
    }
    else
	return TRUE;
}

static gboolean
mc_account_get_param (McAccount *account, const gchar *name,
		      GValue *dst_value)
{
    GHashTable *parameters;
    GValue *value;
    gboolean ok = FALSE;

    parameters = mc_account_get_params (account);
    if (!parameters) return FALSE;
    value = g_hash_table_lookup (parameters, name);
    if (value)
    {
	g_value_init (dst_value, G_VALUE_TYPE (value));
	g_value_copy (value, dst_value);
	ok = TRUE;
    }
    g_hash_table_destroy (parameters);
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
  GValue val = { 0 };

  g_return_val_if_fail (account != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (name != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (value != NULL, MC_ACCOUNT_SETTING_ABSENT);

  /* TODO: retreive type from protocol and check it matches */

  ret = MC_ACCOUNT_SETTING_ABSENT;

  if (mc_account_get_param (account, name, &val))
    {
      *value = g_value_get_boolean (&val);
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
      g_object_unref (profile);
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
  GValue val = { 0 };

  g_return_val_if_fail (account != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (name != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (value != NULL, MC_ACCOUNT_SETTING_ABSENT);

  /* TODO: retreive type from protocol and check it matches */

  if (mc_account_get_param (account, name, &val))
  {
      if (G_VALUE_TYPE (&val) == G_TYPE_INT)
	  *value = g_value_get_int (&val);
      else if (g_value_type_transformable (G_VALUE_TYPE (&val), G_TYPE_INT))
      {
	  GValue trans = { 0 };

	  g_value_init (&trans, G_TYPE_INT);
	  g_value_transform (&val, &trans);
	  *value = g_value_get_int (&trans);
      }
      else
      {
	  g_warning ("%s: param %s has type %s (expecting integer)",
		     G_STRFUNC, name, G_VALUE_TYPE_NAME (&val));
	  return MC_ACCOUNT_SETTING_ABSENT;
      }
      return MC_ACCOUNT_SETTING_FROM_ACCOUNT;
  }

  profile = mc_account_get_profile (account);
  def = mc_profile_get_default_setting (profile, name);

  if (def != NULL)
    {
      errno = 0;
      long_val = strtol (def, &end, 10);
      g_object_unref (profile);

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
  g_object_unref (profile);

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
  GValue val = { 0 };
  McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);

  g_return_val_if_fail (account != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
                        MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (name != NULL, MC_ACCOUNT_SETTING_ABSENT);
  g_return_val_if_fail (value != NULL, MC_ACCOUNT_SETTING_ABSENT);

  /* TODO: retreive type from protocol and check it matches */

  if (priv->last_name && strcmp (priv->last_name, name) == 0)
  {
      *value = g_strdup (priv->last_value);
      return MC_ACCOUNT_SETTING_FROM_ACCOUNT;
  }
  g_free (priv->last_name);
  priv->last_name = NULL;
  if (mc_account_get_param (account, name, &val))
  {
      *value = (gchar *)g_value_get_string (&val);
      g_free (priv->last_value);
      priv->last_name = g_strdup (name);
      priv->last_value = g_strdup (*value);
      return MC_ACCOUNT_SETTING_FROM_ACCOUNT;
  }

  profile = mc_account_get_profile (account);
  def = mc_profile_get_default_setting (profile, name);

  if (def != NULL)
    {
      *value = g_strdup (def);
      g_object_unref (profile);
      return MC_ACCOUNT_SETTING_FROM_PROFILE;
    }
  g_object_unref (profile);

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

static gboolean
mc_account_set_param (McAccount *account, const gchar *name,
		      const GValue *value)
{
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);
    const gchar *unset[] = { NULL, NULL };
    GHashTable *set;
    GError *error = NULL;

    set = g_hash_table_new (g_str_hash, g_str_equal);
    if (value)
	g_hash_table_insert (set, (gpointer)name, (gpointer)value);
    else
	unset[0] = name;
    mc_cli_account_do_update_parameters (priv->proxy, -1,
					 set, unset,
					 &error);
    g_hash_table_destroy (set);
    if (error)
    {
	g_warning ("%s on %s failed: %s",
		   G_STRFUNC, priv->unique_name, error->message);
	g_error_free (error);
	return FALSE;
    }
    else
	return TRUE;
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
    GValue val = { 0 };

    g_value_init (&val, G_TYPE_BOOLEAN);
    g_value_set_boolean (&val, value);
    return mc_account_set_param (account, name, &val);
}

static inline GType
get_param_type (McAccount *account, const gchar *name)
{
    McProfile *profile;
    McProtocol *protocol;
    GType ret = G_TYPE_INT;

    profile = mc_account_get_profile (account);
    if (profile)
    {
	protocol = mc_profile_get_protocol (profile);
	if (protocol)
	{
	    GSList *params, *param;

	    params = mc_protocol_get_params (protocol);
	    for (param = params; param != NULL; param = param->next)
	    {
		McProtocolParam *p = param->data;
		if (strcmp (p->name, name) == 0)
		{
		    switch (p->signature[0])
		    {
		    case DBUS_TYPE_INT16:
		    case DBUS_TYPE_INT32:
			ret = G_TYPE_INT;
			break;
		    case DBUS_TYPE_UINT16:
		    case DBUS_TYPE_UINT32:
			ret = G_TYPE_UINT;
			break;
		    }
		    break;
		}
	    }
	    g_object_unref (protocol);
	}
	g_object_unref (profile);
    }
    return ret;
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
    GValue val = { 0 };
    GType type;

    type = get_param_type (account, name);
    g_value_init (&val, type);
    if (type == G_TYPE_INT)
	g_value_set_int (&val, value);
    else
	g_value_set_uint (&val, (guint)value);
    return mc_account_set_param (account, name, &val);
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
    GValue val = { 0 };

    g_value_init (&val, G_TYPE_STRING);
    g_value_set_static_string (&val, value);
    return mc_account_set_param (account, name, &val);
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
    return mc_account_set_param (account, name, NULL);
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
    GHashTable *parameters;
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);
    GValue *val_parameters;
    GError *error = NULL;

    g_return_val_if_fail (account != NULL, NULL);
    g_return_val_if_fail (MC_ACCOUNT_PRIV (account)->unique_name != NULL,
			  NULL);

    mc_cli_dbus_properties_do_get (priv->proxy, -1,
				   MC_IFACE_ACCOUNT,
				   MC_ACCOUNTS_GCONF_KEY_PARAMETERS,
				   &val_parameters, &error);
    if (error)
    {
	g_warning ("%s: getting parameters for %s failed: %s",
		   G_STRFUNC, priv->unique_name, error->message);
	g_error_free (error);
	return NULL;
    }

    parameters = g_value_get_boxed (val_parameters);

    /* this does not free the hastable */
    g_free (val_parameters);

    return parameters;
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
    g_return_val_if_fail (account != NULL, FALSE);

    return MC_ACCOUNT_PRIV (account)->valid;
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
    gchar *data;
    gsize len;
    gboolean ret;

    g_return_val_if_fail (account != NULL, FALSE);

    if (filename)
    {
	if (!g_file_get_contents (filename, &data, &len, NULL))
	{
	    g_warning ("%s: reading file %s failed", G_STRLOC, filename);
	    return FALSE;
	}
    }
    else
    {
	data = NULL;
	len = 0;
    }

    ret = mc_account_set_avatar_from_data (account, data, len, mime_type);
    g_free (data);
    return ret;
}

/**
 * mc_account_set_avatar_from_data:
 * @account: The #McAccount.
 * @data: image binary contents.
 * @len: length of @data.
 * @mime_type: the MIME type of the image.
 *
 * Set the avatar for this account. If @data is %NULL, the avatar is cleared.
 *
 * Return value: %TRUE, or %FALSE if some error occurred.
 */
gboolean
mc_account_set_avatar_from_data (McAccount *account, const gchar *data,
				 gsize len, const gchar *mime_type)
{
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);
    GValue value = { 0 };
    GError *error = NULL;
    GArray avatar;
    GType type;

    avatar.data = (gchar *)data;
    avatar.len = len;
    type = dbus_g_type_get_struct ("GValueArray",
				   dbus_g_type_get_collection ("GArray",
							       G_TYPE_UCHAR),
				   G_TYPE_STRING,
				   G_TYPE_INVALID);
    g_value_init (&value, type);
    g_value_set_static_boxed (&value, dbus_g_type_specialized_construct (type));
    GValueArray *va = (GValueArray *) g_value_get_boxed (&value);
    g_value_take_boxed (va->values, &avatar);
    g_value_set_static_string (va->values + 1, mime_type);
    mc_cli_dbus_properties_do_set (priv->proxy, -1,
				   MC_IFACE_ACCOUNT,
				   MC_ACCOUNTS_GCONF_KEY_AVATAR,
				   &value, &error);
    if (error)
    {
	g_warning ("%s on %s failed: %s",
		   G_STRFUNC, priv->unique_name, error->message);
	g_error_free (error);
	return FALSE;
    }
    else
	return TRUE;
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
    g_warning ("%s: only mission-control should call this function!",
	       G_STRFUNC);
    return FALSE;
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
    g_warning ("%s: only mission-control should call this function!",
	       G_STRFUNC);
    return FALSE;
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
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);
    GValue *val_avatar;
    GError *error = NULL;

    if (filename)
    {
	mc_cli_dbus_properties_do_get (priv->proxy, -1,
				       MC_IFACE_ACCOUNT_INTERFACE_COMPAT,
				       MC_ACCOUNTS_GCONF_KEY_AVATAR_FILE,
				       &val_avatar, &error);
	if (error)
	{
	    g_warning ("%s: getting avatar file for %s failed: %s",
		       G_STRFUNC, priv->unique_name, error->message);
	    g_error_free (error);
	    return FALSE;
	}
	*filename = (gchar *)g_value_get_string (val_avatar);
	g_free (val_avatar);
    }

    if (mime_type)
    {
	mc_cli_dbus_properties_do_get (priv->proxy, -1,
				       MC_IFACE_ACCOUNT,
				       MC_ACCOUNTS_GCONF_KEY_AVATAR,
				       &val_avatar, &error);
	if (error)
	{
	    g_warning ("%s: getting avatar for %s failed: %s",
		       G_STRFUNC, priv->unique_name, error->message);
	    g_error_free (error);
	    return FALSE;
	}
	GValueArray *va = (GValueArray *) g_value_get_boxed (val_avatar);
	*mime_type = g_value_dup_string (va->values + 1);
	g_value_unset (val_avatar);
	g_free (val_avatar);
    }

    /* we cannot know the token, but it was used only for mission-control */
    if (token)
    {
	g_warning ("%s: only mission-control should retrieve the token!",
		   G_STRFUNC);
    }

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
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);

    return priv->avatar_id;
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
    /* does nothing */
    return TRUE;
}

/**
 * mc_account_get_alias:
 * @account: The #McAccount.
 *
 * Gets the alias for the account.
 *
 * Return value: the alias (must be freed with g_free()), or NULL.
 */
gchar *
mc_account_get_alias (McAccount *account)
{
    g_return_val_if_fail (account != NULL, NULL);

    return g_strdup (MC_ACCOUNT_PRIV (account)->alias);
}

/**
 * mc_account_set_alias:
 * @account: The #McAccount.
 * @alias: The alias to set.
 *
 * Sets the alias of the account.
 *
 * Return value: %TRUE, or %FALSE if some error occurs.
 */
gboolean
mc_account_set_alias (McAccount *account, const gchar *alias)
{
    McAccountPrivate *priv = MC_ACCOUNT_PRIV (account);
    GValue value = { 0 };
    GError *error = NULL;

    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, alias);
    mc_cli_dbus_properties_do_set (priv->proxy, -1,
				   MC_IFACE_ACCOUNT,
				   MC_ACCOUNTS_GCONF_KEY_ALIAS,
				   &value, &error);
    if (error)
    {
	g_warning ("%s on %s failed: %s",
		   G_STRFUNC, priv->unique_name, error->message);
	g_error_free (error);
	return FALSE;
    }
    else
	return TRUE;
}

