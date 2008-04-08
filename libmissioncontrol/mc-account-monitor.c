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

#include <telepathy-glib/proxy.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/svc-generic.h>
#include "_gen/interfaces.h"
#include <gconf/gconf-client.h>

#include "mc-account.h"
#include "mc-account-priv.h"
#include "mc-account-monitor.h"
#include "mc-account-monitor-priv.h"
#include "mc-account-manager-proxy.h"
#include "mc-signals-marshal.h"
#include "mc.h"

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
    TpProxy *proxy;
  GHashTable *accounts;
} McAccountMonitorPrivate;

static void
mc_account_monitor_finalize (GObject *object)
{
  McAccountMonitor *self = MC_ACCOUNT_MONITOR (object);
  McAccountMonitorPrivate *priv = MC_ACCOUNT_MONITOR_PRIV (self);

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
   * NOTE: this signal is no longer emitted in this version.
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

static void
on_account_removed (TpProxy *proxy, const gchar *object_path,
		    gpointer user_data, GObject *weak_object)
{
    McAccountMonitor *monitor = MC_ACCOUNT_MONITOR (weak_object);
    McAccountMonitorPrivate *priv = user_data;
    const gchar *name;
    McAccount *account;

    name = MC_ACCOUNT_UNIQUE_NAME_FROM_PATH (object_path);
    g_debug ("%s called for account %s", G_STRFUNC, name);

    account = g_hash_table_lookup (priv->accounts, name);
    g_debug ("Account is %sknown", account ? "" : "not ");
    if (account)
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

static void
on_account_validity_changed (TpProxy *proxy, const gchar *object_path,
			     gboolean valid, gpointer user_data,
			     GObject *weak_object)
{
    McAccountMonitor *monitor = MC_ACCOUNT_MONITOR (weak_object);
    McAccountMonitorPrivate *priv = user_data;
    const gchar *name;
    McAccount *account;

    name = MC_ACCOUNT_UNIQUE_NAME_FROM_PATH (object_path);
    g_debug ("%s called for account %s (%d)", G_STRFUNC, name, valid);

    account = g_hash_table_lookup (priv->accounts, name);
    g_debug ("Account is %sknown", account ? "" : "not ");
    if (account)
    {
	/* the old implementation didn't report signals for account
	 * completeness, and for account deletion we have another signal; so,
	 * we have nothing to do here */
    }
    else if (valid)
    {
	account = _mc_account_new (priv->proxy->dbus_daemon, object_path);
	g_hash_table_insert (priv->accounts, g_strdup (name), account);
	g_signal_emit (monitor, signals[SIGNAL_CREATED], 0, name);

	/* check if the account is enabled and, in case, emit the respective
	 * signal */
	if (mc_account_is_enabled (account))
	{
	    g_signal_emit (monitor, signals[SIGNAL_ENABLED], 0, name);
	}
    }
}

static void
mc_account_monitor_init (McAccountMonitor *self)
{
    GError *error = NULL;
    McAccountMonitorPrivate *priv;
    TpDBusDaemon *dbus_daemon;
    DBusGConnection *connection;
    GValue *val_accounts;
    const gchar **accounts, **name;

    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
					     MC_TYPE_ACCOUNT_MONITOR, McAccountMonitorPrivate);
    priv = MC_ACCOUNT_MONITOR_PRIV (self);
    connection = dbus_g_bus_get (DBUS_BUS_STARTER, &error);
    if (error)
    {
	g_printerr ("Failed to open connection to bus: %s", error->message);
	g_error_free (error);
	return;
    }
    dbus_daemon = tp_dbus_daemon_new (connection);
    priv->proxy = g_object_new (MC_TYPE_ACCOUNT_MANAGER_PROXY,
				"dbus-daemon", dbus_daemon,
				"bus-name", MC_ACCOUNT_MANAGER_DBUS_SERVICE,
				"object-path", MC_ACCOUNT_MANAGER_DBUS_OBJECT,
				NULL);
    g_assert (priv->proxy != NULL);
    priv->accounts = NULL;

    if (NULL != error)
    {
	g_print ("Error: %s\n", error->message);
	g_assert_not_reached ();
    }

    priv->accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
					    (GDestroyNotify) g_free,
					    g_object_unref);

    mc_cli_dbus_properties_do_get (priv->proxy, -1,
				   MC_IFACE_ACCOUNT_MANAGER, "ValidAccounts",
				   &val_accounts, &error);
    if (error)
    {
	g_warning ("Error getting accounts: %s", error->message);
	g_error_free (error);
	error = NULL;
    }
    accounts = g_value_get_boxed (val_accounts);

    for (name = accounts; *name != NULL; name++)
    {
	McAccount *account;
	const gchar *unique_name;

	account = _mc_account_new (dbus_daemon, *name);
	unique_name = MC_ACCOUNT_UNIQUE_NAME_FROM_PATH (*name);
	g_hash_table_insert (priv->accounts, g_strdup (unique_name), account);
    }
    g_value_unset (val_accounts);
    g_free (val_accounts);

    mc_cli_account_manager_connect_to_account_removed (priv->proxy,
						       on_account_removed,
						       priv, NULL,
						       (GObject *)self, NULL);
    mc_cli_account_manager_connect_to_account_validity_changed (priv->proxy,
								on_account_validity_changed,
								priv, NULL,
								(GObject *)self, NULL);
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

McAccount *
_mc_account_monitor_create_account (McAccountMonitor *monitor,
				    const gchar *manager,
				    const gchar *protocol,
				    const gchar *display_name,
				    GHashTable *parameters)
{
    McAccountMonitorPrivate *priv = MC_ACCOUNT_MONITOR_PRIV (monitor);
    gchar *object_path;
    GError *error = NULL;

    mc_cli_account_manager_do_create_account (priv->proxy, -1,
					      manager, protocol,
					      display_name, parameters,
					      &object_path, &error);
    if (error)
    {
	g_warning ("%s failed: %s", G_STRFUNC, error->message);
	g_error_free (error);
	return NULL;
    }

    return _mc_account_new (priv->proxy->dbus_daemon, object_path);
}

