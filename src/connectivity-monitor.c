/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/*
 * Copyright © 2009–2011 Collabora Ltd.
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
 *
 * Authors:
 *   Jonny Lamb <jonny.lamb@collabora.co.uk>
 *   Will Thompson <will.thompson@collabora.co.uk>
 */

#include "config.h"
#include "connectivity-monitor.h"


#ifdef HAVE_NM
# ifdef HAVE_CONNMAN
#  error tried to build both Network Manager and ConnMan support simultaneously!
/* I mean, we could support both one day, but not for now. */
# endif
/* Moving swiftly on… */
#include <nm-client.h>
#endif

#ifdef HAVE_CONNMAN
#include <dbus/dbus-glib.h>
#endif

#ifdef HAVE_UPOWER
#include <upower.h>
#endif

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-debug.h"

struct _McdConnectivityMonitorPrivate {
#ifdef HAVE_NM
  NMClient *nm_client;
  gulong state_change_signal_id;
#endif

#ifdef HAVE_CONNMAN
  DBusGProxy *proxy;
#endif

#ifdef HAVE_UPOWER
  UpClient *upower_client;
#endif

  gboolean connected;
  gboolean use_conn;

  /* TRUE if the device is not suspended; FALSE while it is. */
  gboolean awake;
};

enum {
  STATE_CHANGE,
  LAST_SIGNAL
};

enum {
  PROP_0,
  PROP_USE_CONN,
};

static guint signals[LAST_SIGNAL];
static McdConnectivityMonitor *connectivity_monitor_singleton = NULL;

G_DEFINE_TYPE (McdConnectivityMonitor, mcd_connectivity_monitor, G_TYPE_OBJECT);

static void
connectivity_monitor_change_states (
    McdConnectivityMonitor *self,
    gboolean connected,
    gboolean awake)
{
  McdConnectivityMonitorPrivate *priv = self->priv;
  gboolean old_total = priv->connected && priv->awake;
  gboolean new_total = connected && awake;

  if (priv->connected == connected &&
      priv->awake == awake)
    return;

  priv->connected = connected;
  priv->awake = awake;

  if (old_total != new_total)
    g_signal_emit (self, signals[STATE_CHANGE], 0, new_total);
}

static void
connectivity_monitor_set_connected (
    McdConnectivityMonitor *self,
    gboolean connected)
{
  connectivity_monitor_change_states (self, connected, self->priv->awake);
}

#ifdef HAVE_NM

#if !defined(NM_CHECK_VERSION)
#define NM_CHECK_VERSION(x,y,z) 0
#endif

static void
connectivity_monitor_nm_state_change_cb (NMClient *client,
    const GParamSpec *pspec,
    McdConnectivityMonitor *connectivity_monitor)
{
  McdConnectivityMonitorPrivate *priv;
  gboolean new_nm_connected;
  NMState state;

  priv = connectivity_monitor->priv;

  if (!priv->use_conn)
    return;

  state = nm_client_get_state (priv->nm_client);
  new_nm_connected = !(state == NM_STATE_CONNECTING
#if NM_CHECK_VERSION(0,8,992)
      || state == NM_STATE_DISCONNECTING
#endif
      || state == NM_STATE_ASLEEP
      || state == NM_STATE_DISCONNECTED);

  DEBUG ("New NetworkManager network state %d (connected: %s)", state,
      new_nm_connected ? "true" : "false");

  connectivity_monitor_set_connected (connectivity_monitor, new_nm_connected);
}
#endif

#ifdef HAVE_CONNMAN
static void
connectivity_monitor_connman_state_changed_cb (DBusGProxy *proxy,
    const gchar *new_state,
    McdConnectivityMonitor *connectivity_monitor)
{
  McdConnectivityMonitorPrivate *priv;
  gboolean new_connected;

  priv = connectivity_monitor->priv;

  if (!priv->use_conn)
    return;

  new_connected = !tp_strdiff (new_state, "online");

  DEBUG ("New ConnMan network state %s", new_state);

  connectivity_monitor_set_connected (connectivity_monitor, new_connected);
}

static void
connectivity_monitor_connman_check_state_cb (DBusGProxy *proxy,
    DBusGProxyCall *call_id,
    gpointer user_data)
{
  McdConnectivityMonitor *connectivity_monitor = (McdConnectivityMonitor *) user_data;
  GError *error = NULL;
  gchar *state;

  if (dbus_g_proxy_end_call (proxy, call_id, &error,
          G_TYPE_STRING, &state, G_TYPE_INVALID))
    {
      connectivity_monitor_connman_state_changed_cb (proxy, state,
          connectivity_monitor);
      g_free (state);
    }
  else
    {
      DEBUG ("Failed to call GetState: %s", error->message);
      connectivity_monitor_connman_state_changed_cb (proxy, "offline",
          connectivity_monitor);
    }
}

static void
connectivity_monitor_connman_check_state (McdConnectivityMonitor *connectivity_monitor)
{
  McdConnectivityMonitorPrivate *priv;

  priv = connectivity_monitor->priv;

  dbus_g_proxy_begin_call (priv->proxy, "GetState",
      connectivity_monitor_connman_check_state_cb, connectivity_monitor, NULL,
      G_TYPE_INVALID);
}
#endif

#ifdef HAVE_UPOWER
static void
connectivity_monitor_set_awake (
    McdConnectivityMonitor *self,
    gboolean awake)
{
  connectivity_monitor_change_states (self, self->priv->connected, awake);
}

static void
notify_sleep_cb (
    UpClient *upower_client,
    UpSleepKind sleep_kind,
    gpointer user_data)
{
  McdConnectivityMonitor *self = MCD_CONNECTIVITY_MONITOR (user_data);

  DEBUG ("about to sleep! sleep_kind=%s", up_sleep_kind_to_string (sleep_kind));
  connectivity_monitor_set_awake (self, FALSE);
}

static void
notify_resume_cb (
    UpClient *upower_client,
    UpSleepKind sleep_kind,
    gpointer user_data)
{
  McdConnectivityMonitor *self = MCD_CONNECTIVITY_MONITOR (user_data);

  DEBUG ("woke up! sleep_kind=%s", up_sleep_kind_to_string (sleep_kind));
  connectivity_monitor_set_awake (self, TRUE);
}
#endif

static void
mcd_connectivity_monitor_init (McdConnectivityMonitor *connectivity_monitor)
{
  McdConnectivityMonitorPrivate *priv;
#ifdef HAVE_CONNMAN
  DBusGConnection *connection;
  GError *error = NULL;
#endif

  priv = G_TYPE_INSTANCE_GET_PRIVATE (connectivity_monitor,
      MCD_TYPE_CONNECTIVITY_MONITOR, McdConnectivityMonitorPrivate);

  connectivity_monitor->priv = priv;

  priv->use_conn = TRUE;
  priv->awake = TRUE;

#ifdef HAVE_NM
  priv->nm_client = nm_client_new ();
  if (priv->nm_client != NULL)
    {
      priv->state_change_signal_id = g_signal_connect (priv->nm_client,
          "notify::" NM_CLIENT_STATE,
          G_CALLBACK (connectivity_monitor_nm_state_change_cb), connectivity_monitor);

      connectivity_monitor_nm_state_change_cb (priv->nm_client, NULL, connectivity_monitor);
    }
  else
    {
      DEBUG ("Failed to get NetworkManager proxy");
    }
#endif

#ifdef HAVE_CONNMAN
  connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
  if (connection != NULL)
    {
      priv->proxy = dbus_g_proxy_new_for_name (connection,
          "net.connman", "/",
          "net.connman.Manager");

      dbus_g_object_register_marshaller (
          g_cclosure_marshal_VOID__STRING,
          G_TYPE_NONE, G_TYPE_STRING, G_TYPE_INVALID);

      dbus_g_proxy_add_signal (priv->proxy, "StateChanged",
          G_TYPE_STRING, G_TYPE_INVALID);

      dbus_g_proxy_connect_signal (priv->proxy, "StateChanged",
          G_CALLBACK (connectivity_monitor_connman_state_changed_cb),
          connectivity_monitor, NULL);

      connectivity_monitor_connman_check_state (connectivity_monitor);
    }
  else
    {
      DEBUG ("Failed to get system bus connection: %s", error->message);
      g_error_free (error);
    }
#endif

#if !defined(HAVE_NM) && !defined(HAVE_CONNMAN)
  priv->connected = TRUE;
#endif

#ifdef HAVE_UPOWER
  priv->upower_client = up_client_new ();
  tp_g_signal_connect_object (priv->upower_client,
      "notify-sleep", G_CALLBACK (notify_sleep_cb), connectivity_monitor,
      G_CONNECT_AFTER);
  tp_g_signal_connect_object (priv->upower_client,
      "notify-resume", G_CALLBACK (notify_resume_cb), connectivity_monitor,
      G_CONNECT_AFTER);
#endif
}

static void
connectivity_monitor_finalize (GObject *object)
{
#if defined(HAVE_NM) || defined(HAVE_CONNMAN) || defined(HAVE_UPOWER)
  McdConnectivityMonitor *connectivity_monitor = MCD_CONNECTIVITY_MONITOR (object);
  McdConnectivityMonitorPrivate *priv = connectivity_monitor->priv;
#endif

#ifdef HAVE_NM
  if (priv->nm_client != NULL)
    {
      g_signal_handler_disconnect (priv->nm_client,
          priv->state_change_signal_id);
      priv->state_change_signal_id = 0;
      g_object_unref (priv->nm_client);
      priv->nm_client = NULL;
    }
#endif

#ifdef HAVE_CONNMAN
  if (priv->proxy != NULL)
    {
      dbus_g_proxy_disconnect_signal (priv->proxy, "StateChanged",
          G_CALLBACK (connectivity_monitor_connman_state_changed_cb), connectivity_monitor);

      g_object_unref (priv->proxy);
      priv->proxy = NULL;
    }
#endif

#ifdef HAVE_UPOWER
  tp_clear_object (&priv->upower_client);
#endif

  G_OBJECT_CLASS (mcd_connectivity_monitor_parent_class)->finalize (object);
}

static void
connectivity_monitor_dispose (GObject *object)
{
  G_OBJECT_CLASS (mcd_connectivity_monitor_parent_class)->dispose (object);
}

static GObject *
connectivity_monitor_constructor (GType type,
    guint n_construct_params,
    GObjectConstructParam *construct_params)
{
  GObject *retval;

  if (!connectivity_monitor_singleton)
    {
      retval = G_OBJECT_CLASS (mcd_connectivity_monitor_parent_class)->constructor
        (type, n_construct_params, construct_params);

      connectivity_monitor_singleton = MCD_CONNECTIVITY_MONITOR (retval);
      g_object_add_weak_pointer (retval, (gpointer) &connectivity_monitor_singleton);
    }
  else
    {
      retval = g_object_ref (connectivity_monitor_singleton);
    }

  return retval;
}

static void
connectivity_monitor_get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  McdConnectivityMonitor *connectivity_monitor = MCD_CONNECTIVITY_MONITOR (object);

  switch (param_id)
    {
    case PROP_USE_CONN:
      g_value_set_boolean (value, mcd_connectivity_monitor_get_use_conn (
              connectivity_monitor));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    };
}

static void
connectivity_monitor_set_property (GObject *object,
    guint param_id,
    const GValue *value,
    GParamSpec *pspec)
{
  McdConnectivityMonitor *connectivity_monitor = MCD_CONNECTIVITY_MONITOR (object);

  switch (param_id)
    {
    case PROP_USE_CONN:
      mcd_connectivity_monitor_set_use_conn (connectivity_monitor,
          g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    };
}

static void
mcd_connectivity_monitor_class_init (McdConnectivityMonitorClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->finalize = connectivity_monitor_finalize;
  oclass->dispose = connectivity_monitor_dispose;
  oclass->constructor = connectivity_monitor_constructor;
  oclass->get_property = connectivity_monitor_get_property;
  oclass->set_property = connectivity_monitor_set_property;

  signals[STATE_CHANGE] =
    g_signal_new ("state-change",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__BOOLEAN,
        G_TYPE_NONE,
        1, G_TYPE_BOOLEAN, NULL);

  g_object_class_install_property (oclass,
      PROP_USE_CONN,
      g_param_spec_boolean ("use-conn",
          "Use connectivity managers",
          "Set presence according to connectivity managers",
          TRUE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  g_type_class_add_private (oclass, sizeof (McdConnectivityMonitorPrivate));
}

/* public methods */

McdConnectivityMonitor *
mcd_connectivity_monitor_new (void)
{
  return g_object_new (MCD_TYPE_CONNECTIVITY_MONITOR, NULL);
}

gboolean
mcd_connectivity_monitor_is_online (McdConnectivityMonitor *connectivity_monitor)
{
  McdConnectivityMonitorPrivate *priv = connectivity_monitor->priv;

  return priv->connected && priv->awake;
}

gboolean
mcd_connectivity_monitor_get_use_conn (McdConnectivityMonitor *connectivity_monitor)
{
  McdConnectivityMonitorPrivate *priv = connectivity_monitor->priv;

  return priv->use_conn;
}

void
mcd_connectivity_monitor_set_use_conn (McdConnectivityMonitor *connectivity_monitor,
    gboolean use_conn)
{
  McdConnectivityMonitorPrivate *priv = connectivity_monitor->priv;

  if (use_conn == priv->use_conn)
    return;

  DEBUG ("use-conn GSettings key changed; new value = %s",
      use_conn ? "true" : "false");

  priv->use_conn = use_conn;

#if defined(HAVE_NM) || defined(HAVE_CONNMAN)
  if (use_conn)
    {
#if defined(HAVE_NM)
      connectivity_monitor_nm_state_change_cb (priv->nm_client, NULL, connectivity_monitor);
#elif defined(HAVE_CONNMAN)
      connectivity_monitor_connman_check_state (connectivity_monitor);
#endif
    }
  else
#endif
    {
      connectivity_monitor_set_connected (connectivity_monitor, TRUE);
    }

  g_object_notify (G_OBJECT (connectivity_monitor), "use-conn");
}
