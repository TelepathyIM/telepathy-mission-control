/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/*
 * Copyright © 2009–2011 Collabora Ltd.
 * Copyright © 2013 Intel Corporation
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

#include <errno.h>

#ifdef HAVE_GIO_UNIX
#include <gio/gunixfdlist.h>
#endif

#ifdef HAVE_NM
#include <NetworkManager.h>
#endif

#ifdef HAVE_UPOWER
#include <upower.h>
#endif

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-debug.h"

#define LOGIN1_BUS_NAME "org.freedesktop.login1"
#define LOGIN1_MANAGER_OBJECT_PATH "/org/freedesktop/login1"
#define LOGIN1_MANAGER_IFACE "org.freedesktop.login1.Manager"
#define LOGIN1_MANAGER_PREPARE_FOR_SLEEP "PrepareForSleep"
#define LOGIN1_MANAGER_PREPARE_FOR_SHUTDOWN "PrepareForShutdown"
#define LOGIN1_MANAGER_INHIBIT "Inhibit"

struct _McdInhibit {
    /* The number of reasons why we should delay sleep/shutdown. This behaves
     * like a refcount: when it reaches 0, we close the fd and free the
     * McdInhibit structure.
     *
     * 1 when we are waiting for PrepareForSleep/PrepareForShutdown;
     * the number of extra "holds" (currently one per McdAccount) when we have
     * received that signal and are waiting for each McdAccount to disconnect;
     * temporarily 1 + number of extra "holds" while we are dealing with the
     * signal.
     */
    gsize holds;

    /* fd encapsulating the delay, provided by logind. We close this
     * when we no longer have any reason to delay sleep/shutdown. */
    int fd;
};

typedef enum {
    CONNECTIVITY_NONE = 0,

    /* Set if the device is not suspended; clear if it is suspending
     * (or suspended, but we don't get scheduled then). */
    CONNECTIVITY_AWAKE = (1 << 0),
    /* Set if GNetworkMonitor says we're up. */
    CONNECTIVITY_UP = (1 << 1),
    /* Clear if NetworkManager says we're in a shaky state like
     * disconnecting (the GNetworkMonitor can't tell this). Set otherwise. */
    CONNECTIVITY_STABLE = (1 << 2),
    /* Set if the device is not shutting down, clear if it is. */
    CONNECTIVITY_RUNNING = (1 << 3)
} Connectivity;

struct _McdConnectivityMonitorPrivate {
  GNetworkMonitor *network_monitor;

  GDBusConnection *system_bus;
  guint login1_prepare_for_sleep_id;
  guint login1_prepare_for_shutdown_id;
  McdInhibit *login1_inhibit;

#ifdef HAVE_NM
  NMClient *nm_client;
  gulong state_change_signal_id;
#endif

#ifdef HAVE_UPOWER
  UpClient *upower_client;
#endif

#ifdef ENABLE_CONN_SETTING
    /* Application settings we steal from under Empathy's nose. */
    GSettings *settings;
#endif

  Connectivity connectivity;
  gboolean use_conn;
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

static gboolean
is_connected (Connectivity connectivity)
{
  return ((connectivity & CONNECTIVITY_AWAKE) &&
      (connectivity & CONNECTIVITY_UP) &&
      (connectivity & CONNECTIVITY_STABLE) &&
      (connectivity & CONNECTIVITY_RUNNING));
}

static void
connectivity_monitor_change_states (
    McdConnectivityMonitor *self,
    Connectivity set,
    Connectivity clear,
    McdInhibit *inhibit)
{
  McdConnectivityMonitorPrivate *priv = self->priv;
  Connectivity connectivity = ((priv->connectivity | set) & (~clear));
  gboolean old_total = is_connected (priv->connectivity);
  gboolean new_total = is_connected (connectivity);

  if (priv->connectivity == connectivity)
    return;

  DEBUG ("awake: %d -> %d; up: %d -> %d; stable: %d -> %d; running: %d -> %d",
      (priv->connectivity & CONNECTIVITY_AWAKE),
      (connectivity & CONNECTIVITY_AWAKE),
      (priv->connectivity & CONNECTIVITY_UP),
      (connectivity & CONNECTIVITY_UP),
      (priv->connectivity & CONNECTIVITY_STABLE),
      (connectivity & CONNECTIVITY_STABLE),
      (priv->connectivity & CONNECTIVITY_RUNNING),
      (connectivity & CONNECTIVITY_RUNNING));

  priv->connectivity = connectivity;

  if (old_total != new_total)
    {
      DEBUG ("%s", new_total ? "connected" : "disconnected");
      g_signal_emit (self, signals[STATE_CHANGE], 0, new_total,
          inhibit);
    }
}

/* Calling this function makes us "more online" or has no effect */
static inline void
connectivity_monitor_add_states (
    McdConnectivityMonitor *self,
    Connectivity set,
    McdInhibit *inhibit)
{
  connectivity_monitor_change_states (self, set, CONNECTIVITY_NONE, inhibit);
}

/* Calling this function makes us "less online" or has no effect */
static inline void
connectivity_monitor_remove_states (
    McdConnectivityMonitor *self,
    Connectivity clear,
    McdInhibit *inhibit)
{
  connectivity_monitor_change_states (self, CONNECTIVITY_NONE, clear, inhibit);
}

#ifdef HAVE_NM

static void
connectivity_monitor_nm_state_change_cb (NMClient *client,
    const GParamSpec *pspec,
    McdConnectivityMonitor *connectivity_monitor)
{
  McdConnectivityMonitorPrivate *priv;
  NMState state;

  priv = connectivity_monitor->priv;

  if (!priv->use_conn)
    return;

  state = nm_client_get_state (priv->nm_client);

  if (state == NM_STATE_CONNECTING
      || state == NM_STATE_DISCONNECTING
      || state == NM_STATE_ASLEEP)
    {
      DEBUG ("New NetworkManager network state %d (unstable state)", state);

      connectivity_monitor_remove_states (connectivity_monitor,
          CONNECTIVITY_STABLE, NULL);
    }
  else if (state == NM_STATE_DISCONNECTED)
    {
      DEBUG ("New NetworkManager network state %d (disconnected)", state);

      connectivity_monitor_remove_states (connectivity_monitor,
          CONNECTIVITY_UP|CONNECTIVITY_STABLE, NULL);
    }
  else
    {
      DEBUG ("New NetworkManager network state %d (stable state)", state);
      connectivity_monitor_add_states (connectivity_monitor,
          CONNECTIVITY_STABLE, NULL);
    }
}
#endif

static void
connectivity_monitor_network_changed (GNetworkMonitor *monitor,
    gboolean available,
    McdConnectivityMonitor *connectivity_monitor)
{
  McdConnectivityMonitorPrivate *priv;

  priv = connectivity_monitor->priv;

  if (!priv->use_conn)
    return;

  if (available)
    {
      DEBUG ("GNetworkMonitor (%s) says we are at least partially online",
          G_OBJECT_TYPE_NAME (monitor));
      connectivity_monitor_add_states (connectivity_monitor, CONNECTIVITY_UP,
          NULL);
    }
  else
    {
      DEBUG ("GNetworkMonitor (%s) says we are offline",
          G_OBJECT_TYPE_NAME (monitor));
      connectivity_monitor_remove_states (connectivity_monitor,
          CONNECTIVITY_UP, NULL);
    }
}

#ifdef HAVE_UPOWER
static void
connectivity_monitor_set_awake (
    McdConnectivityMonitor *self,
    gboolean awake)
{
  if (awake)
    connectivity_monitor_add_states (self, CONNECTIVITY_AWAKE, NULL);
  else
    connectivity_monitor_remove_states (self, CONNECTIVITY_AWAKE, NULL);
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

#ifdef HAVE_GIO_UNIX
static void
login1_inhibit_cb (GObject *source G_GNUC_UNUSED,
    GAsyncResult *result,
    gpointer user_data)
{
  McdConnectivityMonitor *self = MCD_CONNECTIVITY_MONITOR (user_data);
  GUnixFDList *fds = NULL;
  GError *error = NULL;
  GVariant *tuple = g_dbus_connection_call_with_unix_fd_list_finish (
      self->priv->system_bus, &fds, result, &error);

  if (tuple != NULL)
    {
      gint32 i;

      g_variant_get (tuple, "(h)", &i);

      if (g_unix_fd_list_get_length (fds) > i)
        {
          g_warn_if_fail (self->priv->login1_inhibit->fd == -1);
          self->priv->login1_inhibit->fd = g_unix_fd_list_get (fds, i, &error);

          if (self->priv->login1_inhibit->fd >= 0)
            {
              DEBUG ("fd %d inhibits login1 sleep/shutdown",
                  self->priv->login1_inhibit->fd);
            }
          else
            {
              DEBUG ("unable to duplicate fd: %s #%d: %s",
                  g_quark_to_string (error->domain), error->code,
                  error->message);
              g_error_free (error);
              mcd_inhibit_release (self->priv->login1_inhibit);
              self->priv->login1_inhibit = NULL;
            }
        }
      else
        {
          DEBUG ("Inhibit() didn't return enough fds?");
        }

      g_variant_unref (tuple);
    }
  else
    {
      DEBUG ("unable to delay sleep and shutdown: %s #%d: %s",
          g_quark_to_string (error->domain), error->code, error->message);
      g_error_free (error);
    }

  g_clear_object (&fds);
  g_object_unref (self);
}
#endif

static void
connectivity_monitor_renew_inhibit (McdConnectivityMonitor *self)
{
#ifdef HAVE_GIO_UNIX
  if (self->priv->login1_inhibit != NULL)
    return;

  self->priv->login1_inhibit = g_slice_new (McdInhibit);
  self->priv->login1_inhibit->holds = 1;
  self->priv->login1_inhibit->fd = -1;

  g_dbus_connection_call_with_unix_fd_list (self->priv->system_bus,
      LOGIN1_BUS_NAME, LOGIN1_MANAGER_OBJECT_PATH,
      LOGIN1_MANAGER_IFACE, LOGIN1_MANAGER_INHIBIT,
      g_variant_new ("(ssss)", "sleep:shutdown",
        "Telepathy", "Disconnecting IM accounts before suspend/shutdown...",
        "delay"),
      G_VARIANT_TYPE ("(h)"), G_DBUS_CALL_FLAGS_NONE,
      -1, NULL, NULL, login1_inhibit_cb, g_object_ref (self));
#endif
}

static void
login1_prepare_for_sleep_cb (GDBusConnection *system_bus G_GNUC_UNUSED,
    const gchar *sender_name G_GNUC_UNUSED,
    const gchar *object_path G_GNUC_UNUSED,
    const gchar *interface_name G_GNUC_UNUSED,
    const gchar *signal_name G_GNUC_UNUSED,
    GVariant *parameters,
    gpointer user_data)
{
  McdConnectivityMonitor *self = MCD_CONNECTIVITY_MONITOR (user_data);

  if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(b)")))
    {
      gboolean sleeping;

      g_variant_get (parameters, "(b)", &sleeping);

      if (sleeping)
        {
          DEBUG ("about to suspend");
          connectivity_monitor_remove_states (self, CONNECTIVITY_AWAKE,
              self->priv->login1_inhibit);
          tp_clear_pointer (&self->priv->login1_inhibit, mcd_inhibit_release);
        }
      else
        {
          DEBUG ("woke up, or suspend was cancelled");
          connectivity_monitor_renew_inhibit (self);
          connectivity_monitor_add_states (self, CONNECTIVITY_AWAKE,
              self->priv->login1_inhibit);
        }
    }
  else if (DEBUGGING)
    {
      gchar *pretty = g_variant_print (parameters, TRUE);

      DEBUG ("ignoring PrepareForSleep signal not of type (b): %s", pretty);
      g_free (pretty);
    }
}

static void
login1_prepare_for_shutdown_cb (GDBusConnection *system_bus G_GNUC_UNUSED,
    const gchar *sender_name G_GNUC_UNUSED,
    const gchar *object_path G_GNUC_UNUSED,
    const gchar *interface_name G_GNUC_UNUSED,
    const gchar *signal_name G_GNUC_UNUSED,
    GVariant *parameters,
    gpointer user_data)
{
  McdConnectivityMonitor *self = MCD_CONNECTIVITY_MONITOR (user_data);

  if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(b)")))
    {
      gboolean shutting_down;

      g_variant_get (parameters, "(b)", &shutting_down);

      if (shutting_down)
        {
          DEBUG ("about to shut down");
          connectivity_monitor_remove_states (self, CONNECTIVITY_RUNNING,
              self->priv->login1_inhibit);
          tp_clear_pointer (&self->priv->login1_inhibit, mcd_inhibit_release);
        }
      else
        {
          DEBUG ("shutdown was cancelled");
          connectivity_monitor_renew_inhibit (self);
          connectivity_monitor_add_states (self, CONNECTIVITY_RUNNING,
              self->priv->login1_inhibit);
        }
    }
  else if (DEBUGGING)
    {
      gchar *pretty = g_variant_print (parameters, TRUE);

      DEBUG ("ignoring PrepareForShutdown signal not of type (b): %s", pretty);
      g_free (pretty);
    }
}

static void
got_system_bus_cb (GObject *source G_GNUC_UNUSED,
    GAsyncResult *result,
    gpointer user_data)
{
  McdConnectivityMonitor *self = MCD_CONNECTIVITY_MONITOR (user_data);
  GError *error = NULL;

  self->priv->system_bus = g_bus_get_finish (result, &error);

  if (self->priv->system_bus != NULL)
    {
      self->priv->login1_prepare_for_sleep_id =
        g_dbus_connection_signal_subscribe (self->priv->system_bus,
            LOGIN1_BUS_NAME, LOGIN1_MANAGER_IFACE,
            LOGIN1_MANAGER_PREPARE_FOR_SLEEP, LOGIN1_MANAGER_OBJECT_PATH,
            NULL, G_DBUS_SIGNAL_FLAGS_NONE, login1_prepare_for_sleep_cb,
            self, NULL);

      self->priv->login1_prepare_for_shutdown_id =
        g_dbus_connection_signal_subscribe (self->priv->system_bus,
            LOGIN1_BUS_NAME, LOGIN1_MANAGER_IFACE,
            LOGIN1_MANAGER_PREPARE_FOR_SHUTDOWN, LOGIN1_MANAGER_OBJECT_PATH,
            NULL, G_DBUS_SIGNAL_FLAGS_NONE, login1_prepare_for_shutdown_cb,
            self, NULL);

      connectivity_monitor_renew_inhibit (self);
    }
  else
    {
      DEBUG ("unable to connect to system bus: %s #%d: %s",
          g_quark_to_string (error->domain), error->code, error->message);
      g_error_free (error);
    }

  g_object_unref (self);
}

static void
mcd_connectivity_monitor_init (McdConnectivityMonitor *connectivity_monitor)
{
  McdConnectivityMonitorPrivate *priv;

  priv = G_TYPE_INSTANCE_GET_PRIVATE (connectivity_monitor,
      MCD_TYPE_CONNECTIVITY_MONITOR, McdConnectivityMonitorPrivate);

  connectivity_monitor->priv = priv;

  priv->use_conn = TRUE;
  /* Initially, assume everything is good. */
  priv->connectivity = CONNECTIVITY_AWAKE | CONNECTIVITY_STABLE |
    CONNECTIVITY_UP | CONNECTIVITY_RUNNING;

  priv->network_monitor = g_network_monitor_get_default ();

  tp_g_signal_connect_object (priv->network_monitor, "network-changed",
      G_CALLBACK (connectivity_monitor_network_changed),
      connectivity_monitor, 0);
  connectivity_monitor_network_changed (priv->network_monitor,
      g_network_monitor_get_network_available (priv->network_monitor),
      connectivity_monitor);

#ifdef ENABLE_CONN_SETTING
  priv->settings = g_settings_new ("im.telepathy.MissionControl.FromEmpathy");
  /* We'll call g_settings_bind() in constructed because default values of
   * properties haven't been set yet at this point and we don't want them to
   * override the value from GSettings. */
#endif

#ifdef HAVE_NM
  {
    GError *error = NULL;
    priv->nm_client = nm_client_new (NULL, &error);
    if (priv->nm_client != NULL)
      {
        priv->state_change_signal_id = g_signal_connect (priv->nm_client,
            "notify::" NM_CLIENT_STATE,
            G_CALLBACK (connectivity_monitor_nm_state_change_cb),
            connectivity_monitor);

        connectivity_monitor_nm_state_change_cb (priv->nm_client, NULL,
            connectivity_monitor);
      }
    else
      {
        DEBUG ("Failed to get NetworkManager proxy: %s", error->message);
      }
  }
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

  g_bus_get (G_BUS_TYPE_SYSTEM, NULL, got_system_bus_cb,
      g_object_ref (connectivity_monitor));
}

static void
connectivity_monitor_constructed (GObject *object)
{
#ifdef ENABLE_CONN_SETTING
  McdConnectivityMonitor *self = MCD_CONNECTIVITY_MONITOR (object);

  g_settings_bind (self->priv->settings, "use-conn",
      self, "use-conn", G_SETTINGS_BIND_GET);
#endif
}

static void
connectivity_monitor_finalize (GObject *object)
{
#if defined(HAVE_NM) || defined(HAVE_UPOWER)
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

#ifdef HAVE_UPOWER
  tp_clear_object (&priv->upower_client);
#endif

  G_OBJECT_CLASS (mcd_connectivity_monitor_parent_class)->finalize (object);
}

static inline void
clear_subscription (GDBusConnection *conn,
    guint *subscription)
{
  if (*subscription == 0)
    return;

  g_dbus_connection_signal_unsubscribe (conn, *subscription);
  *subscription = 0;
}

static void
connectivity_monitor_dispose (GObject *object)
{
  McdConnectivityMonitor *self = MCD_CONNECTIVITY_MONITOR (object);

  g_clear_object (&self->priv->network_monitor);

#ifdef ENABLE_CONN_SETTING
  g_clear_object (&self->priv->settings);
#endif

  clear_subscription (self->priv->system_bus,
      &self->priv->login1_prepare_for_sleep_id);
  clear_subscription (self->priv->system_bus,
      &self->priv->login1_prepare_for_shutdown_id);
  tp_clear_pointer (&self->priv->login1_inhibit, mcd_inhibit_release);

  g_clear_object (&self->priv->system_bus);

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
  oclass->constructed = connectivity_monitor_constructed;
  oclass->get_property = connectivity_monitor_get_property;
  oclass->set_property = connectivity_monitor_set_property;

  signals[STATE_CHANGE] =
    g_signal_new ("state-change",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL, NULL,
        G_TYPE_NONE,
        2, G_TYPE_BOOLEAN, G_TYPE_POINTER);

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

  return is_connected (priv->connectivity);
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

  if (use_conn)
    {
#if defined(HAVE_NM)
      connectivity_monitor_nm_state_change_cb (priv->nm_client, NULL, connectivity_monitor);
#endif

      connectivity_monitor_network_changed (priv->network_monitor,
          g_network_monitor_get_network_available (priv->network_monitor),
          connectivity_monitor);
    }
  else
    {
      /* !use_conn basically means "always assume it's stable and up". */
      connectivity_monitor_add_states (connectivity_monitor,
          CONNECTIVITY_STABLE|CONNECTIVITY_UP, NULL);
    }

  g_object_notify (G_OBJECT (connectivity_monitor), "use-conn");
}

McdInhibit *
mcd_inhibit_hold (McdInhibit *inhibit)
{
  DEBUG ("%p (fd %d): %" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
      inhibit, inhibit->fd, inhibit->holds, inhibit->holds + 1);

  inhibit->holds++;
  return inhibit;
}

void
mcd_inhibit_release (McdInhibit *inhibit)
{
  DEBUG ("%p (fd %d): %" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
      inhibit, inhibit->fd, inhibit->holds, inhibit->holds - 1);

  if (--inhibit->holds == 0)
    {
      /* Not using the retry-on-EINTR idiom: see g_close() in GLib 2.36.
       * After we depend on GLib 2.36, we could use g_close(). */
      if (inhibit->fd != -1 &&
          close (inhibit->fd) != 0)
        {
          WARNING ("unable to close fd, ignoring: %s", g_strerror (errno));
        }

      g_slice_free (McdInhibit, inhibit);
    }
}
