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

#include <string.h>
#include <strings.h>

#include <gconf/gconf-client.h>

#include "mc-account.h"
#include "mc-account-priv.h"
#include "mc-account-monitor.h"
#include "mc-signals-marshal.h"

G_DEFINE_TYPE (McAccountMonitor, mc_account_monitor, G_TYPE_OBJECT);

#define MC_ACCOUNT_MONITOR_PRIV(monitor) \
  ((McAccountMonitorPrivate *)monitor->priv)

enum
{
  SIGNAL_CREATED,
  SIGNAL_DELETED,
  SIGNAL_ENABLED,
  SIGNAL_DISABLED,
  SIGNAL_CHANGED,
  PARAM_CHANGED,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS];

typedef struct
{
  GConfClient *gconf_client;
  guint gconf_connection;
  GHashTable *accounts;
} McAccountMonitorPrivate;

static void
mc_account_monitor_finalize (GObject *object)
{
  McAccountMonitor *self = MC_ACCOUNT_MONITOR (object);
  McAccountMonitorPrivate *priv = MC_ACCOUNT_MONITOR_PRIV (self);

  gconf_client_notify_remove (priv->gconf_client, priv->gconf_connection);
  gconf_client_remove_dir (
    priv->gconf_client, MC_ACCOUNTS_GCONF_BASE, NULL);
  g_object_unref (priv->gconf_client);

  g_hash_table_destroy (priv->accounts);
}

static void
mc_account_monitor_class_init (McAccountMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (object_class, sizeof (McAccountMonitorPrivate));
  object_class->finalize = mc_account_monitor_finalize;

  /**
   * McAccountMonitor::account-created:
   * @self: The #McAccountMonitor.
   * @name: The name of the account being created (use mc_account_lookup() to
   * retrieve the account object).
   *
   * Emitted when a new account is created.
   */
  signals[SIGNAL_CREATED] = g_signal_new (
    "account-created",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__STRING,
    G_TYPE_NONE, 1,
    G_TYPE_STRING);
  /**
   * McAccountMonitor::account-deleted:
   * @self: The #McAccountMonitor.
   * @name: The name of the account being deleted (use mc_account_lookup() to
   * retrieve the account object).
   *
   * Emitted when an account is deleted.
   */
  signals[SIGNAL_DELETED] = g_signal_new (
    "account-deleted",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__STRING,
    G_TYPE_NONE, 1,
    G_TYPE_STRING);
  /**
   * McAccountMonitor::account-enabled:
   * @self: The #McAccountMonitor.
   * @name: The name of the account being enabled (use mc_account_lookup() to
   * retrieve the account object).
   *
   * Emitted when an account is enabled.
   */
  signals[SIGNAL_ENABLED] = g_signal_new (
    "account-enabled",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__STRING,
    G_TYPE_NONE, 1,
    G_TYPE_STRING);
  /**
   * McAccountMonitor::account-disabled:
   * @self: The #McAccountMonitor.
   * @name: The name of the account being disabled (use mc_account_lookup() to
   * retrieve the account object).
   *
   * Emitted when an account is disabled.
   */
  signals[SIGNAL_DISABLED] = g_signal_new (
    "account-disabled",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__STRING,
    G_TYPE_NONE, 1,
    G_TYPE_STRING);
  /**
   * McAccountMonitor::account-changed:
   * @self: The #McAccountMonitor.
   * @name: The name of the account being changed (use mc_account_lookup() to
   * retrieve the account object).
   *
   * Emitted when an account is changed.
   */
  signals[SIGNAL_CHANGED] = g_signal_new (
    "account-changed",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__STRING,
    G_TYPE_NONE, 1,
    G_TYPE_STRING);
  /**
   * McAccountMonitor::param-changed:
   * @self: The #McAccountMonitor.
   * @name: The name of the account being changed (use mc_account_lookup() to
   * retrieve the account object).
   * @param: The name of the parameter which changed.
   *
   * Emitted when an account parameter is changed.
   */
  signals[PARAM_CHANGED] = g_signal_new (
    "param-changed",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    mc_signals_marshal_VOID__STRING_STRING,
    G_TYPE_NONE, 2,
    G_TYPE_STRING, G_TYPE_STRING);
}

static gchar *
_account_name_from_key (const gchar *key)
{
  guint base_len = strlen (MC_ACCOUNTS_GCONF_BASE);
  const gchar *base, *slash;

  g_assert (key == strstr (key, MC_ACCOUNTS_GCONF_BASE));
  if (strlen (key) <= base_len + 1) return NULL;

  base = key + base_len + 1;
  slash = index (base, '/');

  if (slash == NULL)
    return g_strdup (base);
  else
    return g_strndup (base, slash - base);
}

static const gchar *
key_name (const gchar *path_key)
{
    const gchar *key = 0;

    while (*path_key != 0)
    {
	if (*path_key == '/') key = path_key + 1;
	path_key++;
    }
    return key;
}

static void
_gconf_notify_cb (GConfClient *client, guint conn_id, GConfEntry *entry,
                  gpointer user_data)
{
  McAccountMonitor *monitor = MC_ACCOUNT_MONITOR (user_data);
  McAccountMonitorPrivate *priv = MC_ACCOUNT_MONITOR_PRIV (monitor);
  gchar *name = NULL;
  McAccount *account;
  gboolean key_is_enabledness;
  const gchar *key;

  key = key_name (entry->key);
  key_is_enabledness = strcmp (key, MC_ACCOUNTS_GCONF_KEY_ENABLED) == 0;
  name = _account_name_from_key (entry->key);
  if (!name) return;
  account = g_hash_table_lookup (priv->accounts, name);
  
  /* Was account complete before? */
  if (account == NULL)
    {
      account = _mc_account_new (name);
      
      /* Account was not complete before and it just become complete */
      if (mc_account_is_complete (account))
        {
          g_hash_table_insert (priv->accounts, g_strdup (name), account);
          g_signal_emit (monitor, signals[SIGNAL_CREATED], 0, name);

	  /* check if the account is enabled and, in case, emit the respective
	   * signal */
	  if (mc_account_is_enabled (account))
	  {
	      g_signal_emit (monitor, signals[SIGNAL_ENABLED], 0, name);
	  }
        }
       else
        {
          g_object_unref (account);
          /* We don't do anything for incomplete accounts */
        }
    }
  else if (strcmp (key, MC_ACCOUNTS_GCONF_KEY_DELETED) == 0)
    {
      /* if account is deleted, remove it */
      if (entry->value != NULL && entry->value->type == GCONF_VALUE_BOOL &&
          gconf_value_get_bool (entry->value))
        {
          if (mc_account_is_enabled (account))
            {
	      _mc_account_set_enabled_priv (account, FALSE);
              g_signal_emit (monitor, signals[SIGNAL_DISABLED], 0, name);
            }
          g_signal_emit (monitor, signals[SIGNAL_DELETED], 0, name);
          g_hash_table_remove (priv->accounts, name);
        }
    }
  else if (strcmp (key, MC_ACCOUNTS_GCONF_KEY_ENABLED) == 0)
    {
      if (entry->value != NULL && entry->value->type == GCONF_VALUE_BOOL)
        {
          if (gconf_value_get_bool (entry->value))
            {
	      _mc_account_set_enabled_priv (account, TRUE);
              g_signal_emit (monitor, signals[SIGNAL_ENABLED], 0, name);
            }
          else
            {
	      _mc_account_set_enabled_priv (account, FALSE);
              g_signal_emit (monitor, signals[SIGNAL_DISABLED], 0, name);
            }
        }
    }
  else if (strcmp (key, MC_ACCOUNTS_GCONF_KEY_AVATAR_TOKEN) != 0)
    {
      /* Emit the rest as value changed signal */
      g_signal_emit (monitor, signals[SIGNAL_CHANGED], 0, name);

      if (strncmp (key, "param-", 6) == 0)
	  g_signal_emit (monitor, signals[PARAM_CHANGED], 0,
			 name, key + 6);

      /* report the changed value to the McAccount, if it's a cached setting */
      if (entry->value != NULL && entry->value->type == GCONF_VALUE_STRING)
      {
	  const gchar *value;
	 
	  value = gconf_value_get_string (entry->value);
	  if (strcmp (key, MC_ACCOUNTS_GCONF_KEY_NORMALIZED_NAME) == 0)
	      _mc_account_set_normalized_name_priv (account, value);
	  else if (strcmp (key, MC_ACCOUNTS_GCONF_KEY_DISPLAY_NAME) == 0)
	      _mc_account_set_display_name_priv (account, value);
      }
    }
  
  /* If we are enabling/disabling account */
  g_free (name);
}

static void
mc_account_monitor_init (McAccountMonitor *self)
{
  GError *error = NULL;
  GConfClient *client = gconf_client_get_default ();
  GSList *i, *dirs;
  McAccountMonitorPrivate *priv;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
      MC_TYPE_ACCOUNT_MONITOR, McAccountMonitorPrivate);
  priv = MC_ACCOUNT_MONITOR_PRIV (self);
  priv->accounts = NULL;
  priv->gconf_client = client;
  dirs = gconf_client_all_dirs (client, MC_ACCOUNTS_GCONF_BASE, &error);

  if (NULL != error)
    {
      g_print ("Error: %s\n", error->message);
      g_assert_not_reached ();
    }

  priv->accounts = g_hash_table_new_full (
    g_str_hash, g_str_equal, (GDestroyNotify) g_free, g_object_unref);

  for (i = dirs; NULL != i; i = i->next)
    {
      gchar *name = _account_name_from_key (i->data);
      McAccount *account = _mc_account_new (name);

      /* Only pick up accounts which are not yet deleted. */
      if (mc_account_is_complete (account))
        {
           g_hash_table_insert (priv->accounts, g_strdup (name), account);
        }
      else
        {
           g_object_unref (account);
        }
      g_free (i->data);
      g_free (name);
    }

  g_slist_free (dirs);

  gconf_client_add_dir (
    client, MC_ACCOUNTS_GCONF_BASE, GCONF_CLIENT_PRELOAD_NONE, &error);

  if (NULL != error)
    {
      g_print ("Error: %s\n", error->message);
      g_assert_not_reached ();
    }

  priv->gconf_connection = gconf_client_notify_add (
    client, MC_ACCOUNTS_GCONF_BASE, _gconf_notify_cb, self, NULL, &error);

  if (NULL != error)
    {
      g_print ("Error: %s\n", error->message);
      g_assert_not_reached ();
    }
}

McAccount *
_mc_account_monitor_lookup (McAccountMonitor *monitor, const gchar *unique_name)
{
  McAccountMonitorPrivate *priv = MC_ACCOUNT_MONITOR_PRIV (monitor);
  McAccount *ret;

  g_return_val_if_fail (unique_name != NULL, NULL);
  g_return_val_if_fail (*unique_name != '\0', NULL);

  ret = g_hash_table_lookup (priv->accounts, unique_name);

  if (NULL != ret)
    g_object_ref (ret);

  return ret;
}

/**
 * mc_account_monitor_new:
 *
 * Get a #McAccountMonitor object. The object is a singleton: it is created
 * only if another instance of itself is not alive, otherwise the same instance
 * is returned.
 *
 * Returns: the #McAccountMonitor object.
 */
McAccountMonitor *
mc_account_monitor_new (void)
{
  static McAccountMonitor *monitor = NULL;

  if (NULL == monitor)
    {
      monitor = g_object_new (MC_TYPE_ACCOUNT_MONITOR, NULL);
      g_object_add_weak_pointer (G_OBJECT (monitor), (gpointer) &monitor);
    }
  /* The object is never to be disposed, or mc_account_lookup() might return
   * different objects at each invocation. */
  g_object_ref (monitor);
  return monitor;
}

static void
_list_append (gpointer key, gpointer value, gpointer user_data)
{
  GList **list = (GList **) user_data;

  *list = g_list_prepend (*list, value);
}

GList *
_mc_account_monitor_list (McAccountMonitor *monitor)
{
  McAccountMonitorPrivate *priv = MC_ACCOUNT_MONITOR_PRIV (monitor);
  GList *accounts = NULL;

  g_hash_table_foreach (priv->accounts, _list_append, &accounts);
  return accounts;
}

static void
merge_presences (gpointer key, McAccount *account, GArray *presences)
{
  const McPresence *account_presences;
  gint i;

  if (!mc_account_is_enabled (account))
      return;

  account_presences = mc_account_get_supported_presences (account);
  if (account_presences)
  {
    while (*account_presences)
    {
      /* check if this presence is already in the array */
      for (i = 0; i < presences->len; i++)
	if (g_array_index (presences, McPresence, i) == *account_presences)
	  break;
      if (i >= presences->len) /* no, it's not: let's add it */
	g_array_append_vals (presences, account_presences, 1);
      account_presences++;
    }
  }
}

/**
 * mc_account_monitor_get_supported_presences:
 * @monitor: The #McAccountMonitor.
 *
 * Get a list of all the presences supported in any account: a presence is
 * considered as supported if there is at least one enabled account which
 * supports it. Support fot the basic presences @MC_PRESENCE_AVAILABLE and
 * @MC_PRESENCE_OFFLINE is taken for granted and therefore these presences are
 * not returned.
 *
 * Returns: a NULL-terminated array of presences, which must be freed with
 * g_free().
 */
McPresence *
mc_account_monitor_get_supported_presences (McAccountMonitor *monitor)
{
  McAccountMonitorPrivate *priv = MC_ACCOUNT_MONITOR_PRIV (monitor);
  GArray *presences;
  McPresence *data;

  presences = g_array_new (TRUE, FALSE, sizeof (McPresence));
  g_hash_table_foreach (priv->accounts, (GHFunc)merge_presences, presences);
  data = (McPresence *)presences->data;
  g_array_free (presences, FALSE);
  return data;
}

