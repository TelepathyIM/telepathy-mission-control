/*
 * kludge-transport.c - the shortest path to NM integration
 * Copyright ©2011 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "kludge-transport.h"

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-debug.h"

#include "connectivity-monitor.h"

struct _McdKludgeTransportPrivate {
    /* Rawr! I'm a mythical creature. */
    McdConnectivityMonitor *minotaur;

    /* Pointers representing network connections, exposed to the application as
     * opaque McdTransport pointers.
     *
     * In this generate example of an McdTransportPlugin, we only have one
     * transport, representing "the internet". So in fact this list always
     * contains a single pointer, namely the McdKludgeTransport instance
     * itself.
     */
    GList *transports;

    /* Hold a set of McdAccounts which would like to go online. */
    GHashTable *pending_accounts;

#ifdef ENABLE_CONN_SETTING
    /* Application settings we steal from under Empathy's nose. */
    GSettings *settings;
#endif
};

static void transport_iface_init (
    gpointer g_iface,
    gpointer iface_data);
static void monitor_state_changed_cb (
    McdConnectivityMonitor *monitor,
    gboolean connected,
    gpointer user_data);

G_DEFINE_TYPE_WITH_CODE (McdKludgeTransport, mcd_kludge_transport,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCD_TYPE_TRANSPORT_PLUGIN, transport_iface_init);
    )

static void
mcd_kludge_transport_init (McdKludgeTransport *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      MCD_TYPE_KLUDGE_TRANSPORT, McdKludgeTransportPrivate);
}

static void
mcd_kludge_transport_constructed (GObject *object)
{
  McdKludgeTransport *self = MCD_KLUDGE_TRANSPORT (object);
  McdKludgeTransportPrivate *priv = self->priv;
  GObjectClass *parent_class = mcd_kludge_transport_parent_class;

  if (parent_class->constructed != NULL)
    parent_class->constructed (object);

  priv->minotaur = mcd_connectivity_monitor_new ();
  tp_g_signal_connect_object (priv->minotaur, "state-change",
      (GCallback) monitor_state_changed_cb, self, 0);

  /* We just use ourself as the McdTransport pointer... */
  priv->transports = g_list_prepend (NULL, self);

  priv->pending_accounts = g_hash_table_new_full (NULL, NULL,
      g_object_unref, NULL);

#ifdef ENABLE_CONN_SETTING
  priv->settings = g_settings_new ("im.telepathy.MissionControl.FromEmpathy");
  g_settings_bind (priv->settings, "use-conn", priv->minotaur, "use-conn",
      G_SETTINGS_BIND_GET);
#endif
}

static void
mcd_kludge_transport_dispose (GObject *object)
{
  McdKludgeTransport *self = MCD_KLUDGE_TRANSPORT (object);
  McdKludgeTransportPrivate *priv = self->priv;
  GObjectClass *parent_class = mcd_kludge_transport_parent_class;

  tp_clear_object (&priv->minotaur);
#ifdef ENABLE_CONN_SETTING
  tp_clear_object (&priv->settings);
#endif
  g_list_free (priv->transports);
  priv->transports = NULL;

  g_hash_table_unref (priv->pending_accounts);

  if (parent_class->dispose != NULL)
    parent_class->dispose (object);
}

static void
mcd_kludge_transport_class_init (McdKludgeTransportClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = mcd_kludge_transport_constructed;
  object_class->dispose = mcd_kludge_transport_dispose;

  g_type_class_add_private (klass, sizeof (McdKludgeTransportPrivate));
}

static const GList *
mcd_kludge_transport_get_transports (
    McdTransportPlugin *plugin)
{
  McdKludgeTransport *self = MCD_KLUDGE_TRANSPORT (plugin);

  g_return_val_if_fail (MCD_IS_KLUDGE_TRANSPORT (plugin), NULL);

  return self->priv->transports;
}

static const gchar *
mcd_kludge_transport_get_transport_name (
    McdTransportPlugin *plugin,
    McdTransport *transport)
{
  g_return_val_if_fail (MCD_IS_KLUDGE_TRANSPORT (plugin), NULL);
  g_return_val_if_fail (plugin == (McdTransportPlugin *) transport, NULL);

  return "i love the internet";
}

static McdTransportStatus
mcd_kludge_transport_get_transport_status (
    McdTransportPlugin *plugin,
    McdTransport *transport)
{
  McdKludgeTransport *self = MCD_KLUDGE_TRANSPORT (plugin);
  gboolean online;

  g_return_val_if_fail (MCD_IS_KLUDGE_TRANSPORT (plugin),
      MCD_TRANSPORT_STATUS_DISCONNECTED);
  g_return_val_if_fail (plugin == (McdTransportPlugin *) transport,
      MCD_TRANSPORT_STATUS_DISCONNECTED);

  online = mcd_connectivity_monitor_is_online (self->priv->minotaur);
  DEBUG ("we are allegedly %s", online ? "online" : "offline");

  if (online)
    return MCD_TRANSPORT_STATUS_CONNECTED;
  else
    return MCD_TRANSPORT_STATUS_DISCONNECTED;
}

static void
transport_iface_init (
    gpointer g_iface,
    gpointer iface_data)
{
  McdTransportPluginIface *klass = g_iface;

  klass->get_transports = mcd_kludge_transport_get_transports;
  klass->get_transport_name = mcd_kludge_transport_get_transport_name;
  klass->get_transport_status = mcd_kludge_transport_get_transport_status;
}

static void
monitor_state_changed_cb (
    McdConnectivityMonitor *monitor,
    gboolean connected,
    gpointer user_data)
{
  McdKludgeTransport *self = MCD_KLUDGE_TRANSPORT (user_data);
  McdTransportStatus new_status =
      connected ? MCD_TRANSPORT_STATUS_CONNECTED
                : MCD_TRANSPORT_STATUS_DISCONNECTED;
  GHashTableIter iter;
  gpointer key;

  g_signal_emit_by_name (self, "status-changed", self, new_status);

  g_hash_table_iter_init (&iter, self->priv->pending_accounts);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      McdAccount *account = MCD_ACCOUNT (key);

      /* If we've gone online, allow the account to actually try to connect;
       * if we've fallen offline, say as much. (I don't actually think this
       * code will be reached if !connected, but.)
       */
      DEBUG ("telling %s to %s", mcd_account_get_unique_name (account),
          connected ? "proceed" : "give up");
      mcd_account_connection_bind_transport (account, (McdTransport *) self);
      mcd_account_connection_proceed_with_reason (account, connected,
          connected ? TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED
                    : TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
      g_hash_table_iter_remove (&iter);
    }
}

/*
 * mcd_kludge_transport_account_connection_cb:
 * @account: an account which would like to go online
 * @parameters: the connection parameters to be used
 * @user_data: the McdKludgeTransport.
 *
 * Called when an account would like to sign in.
 */
static void
mcd_kludge_transport_account_connection_cb (
    McdAccount *account,
    GHashTable *parameters,
    gpointer user_data)
{
  McdKludgeTransport *self = MCD_KLUDGE_TRANSPORT (user_data);
  McdKludgeTransportPrivate *priv = self->priv;

  if (mcd_connectivity_monitor_is_online (priv->minotaur))
    {
      mcd_account_connection_bind_transport (account, (McdTransport *) self);
      mcd_account_connection_proceed (account, TRUE);
    }
  else if (g_hash_table_lookup (priv->pending_accounts, account) == NULL)
    {
      DEBUG ("%s wants to connect, but we're offline; queuing it up",
          mcd_account_get_unique_name (account));
      g_object_ref (account);
      g_hash_table_insert (priv->pending_accounts, account, account);
    }
  /* ... else we're already waiting, I guess */
}

static McdTransportPlugin *
mcd_kludge_transport_new (void)
{
  return g_object_new (MCD_TYPE_KLUDGE_TRANSPORT, NULL);
}

void
mcd_kludge_transport_install (McdMaster *master)
{
  McdTransportPlugin *self = mcd_kludge_transport_new ();

  mcd_master_register_transport (master, self);
  mcd_master_register_account_connection (master,
      mcd_kludge_transport_account_connection_cb,
      MCD_ACCOUNT_CONNECTION_PRIORITY_TRANSPORT, self);
}
