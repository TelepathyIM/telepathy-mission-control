/*
 * Fake GNetworkMonitor, using ConnMan's D-Bus API (it's as good as any).
 *
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
 */

#include "config.h"

#include <gio/gio.h>
#include <telepathy-glib/telepathy-glib.h>

typedef struct {
    GObject parent;
    GDBusProxy *proxy;
    gboolean available;
} FakeNetworkMonitor;

typedef struct {
    GObjectClass parent_class;
} FakeNetworkMonitorClass;

static GType fake_network_monitor_get_type (void);

#define FAKE_NETWORK_MONITOR(x) \
  G_TYPE_CHECK_INSTANCE_CAST (x, fake_network_monitor_get_type (), FakeNetworkMonitor)

#define DEBUG(format, ...) \
  g_debug ("%s: " format, G_STRFUNC, ##__VA_ARGS__)

enum
{
  PROP_0,
  PROP_NETWORK_AVAILABLE,
  PROP_NETWORK_METERED,
  PROP_CONNECTIVITY
};

static void initable_iface_init (GInitableIface *);
static void monitor_iface_init (GNetworkMonitorInterface *);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (FakeNetworkMonitor,
    fake_network_monitor,
    G_TYPE_OBJECT,
    0,
    G_IMPLEMENT_INTERFACE_DYNAMIC (G_TYPE_INITABLE,
      initable_iface_init);
    G_IMPLEMENT_INTERFACE_DYNAMIC (G_TYPE_NETWORK_MONITOR,
      monitor_iface_init))

static void
fake_network_monitor_init (FakeNetworkMonitor *self)
{
  DEBUG ("enter");

  self->available = FALSE;
}

static void
fake_network_monitor_get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  FakeNetworkMonitor *self = FAKE_NETWORK_MONITOR (object);

  switch (param_id)
    {
      case PROP_NETWORK_AVAILABLE:
        g_value_set_boolean (value, self->available);
        break;
      case PROP_NETWORK_METERED:
        g_value_set_boolean (value, FALSE);
        break;
      case PROP_CONNECTIVITY:
        g_value_set_enum (value, self->available ?
            G_NETWORK_CONNECTIVITY_FULL : G_NETWORK_CONNECTIVITY_LOCAL);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    };
}

static void
fake_network_monitor_dispose (GObject *object)
{
  FakeNetworkMonitor *self = FAKE_NETWORK_MONITOR (object);

  DEBUG ("enter");

  g_clear_object (&self->proxy);

  G_OBJECT_CLASS (fake_network_monitor_parent_class)->dispose (object);
}

static void
fake_network_monitor_class_init (FakeNetworkMonitorClass *cls)
{
  GObjectClass *oclass = G_OBJECT_CLASS (cls);

  DEBUG ("enter");

  oclass->dispose = fake_network_monitor_dispose;
  oclass->get_property = fake_network_monitor_get_property;

  g_object_class_override_property (oclass, PROP_NETWORK_AVAILABLE,
      "network-available");
  g_object_class_override_property (oclass, PROP_NETWORK_METERED,
      "network-metered");
  g_object_class_override_property (oclass, PROP_CONNECTIVITY,
      "connectivity");
}

static void
fake_network_monitor_class_finalize (FakeNetworkMonitorClass *cls)
{
}

static void
fake_network_monitor_emit_network_changed (FakeNetworkMonitor *self)
{
  DEBUG ("available=%d", self->available);
  g_signal_emit_by_name (self, "network-changed", self->available);
}

static gboolean
fake_network_monitor_can_reach (GNetworkMonitor *monitor,
    GSocketConnectable *connectable,
    GCancellable *cancellable,
    GError **error)
{
  g_error ("Telepathy components should not block like this");
  return FALSE;
}

static void
fake_network_monitor_can_reach_async (GNetworkMonitor *monitor,
    GSocketConnectable *connectable,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  /* The Mission Control tests don't need this, so I didn't implement it.
   * In principle, it should wait for the initial GetProperties() to finish. */
  g_error ("FIXME: implement this");
}

static gboolean
fake_network_monitor_can_reach_finish (GNetworkMonitor *monitor,
    GAsyncResult *result,
    GError **error)
{
  /* See can_reach_async */
  g_error ("FIXME: implement this");
  return FALSE;
}

static void
fake_network_monitor_set_state (FakeNetworkMonitor *self,
    const gchar *state)
{
  gboolean available = (!tp_strdiff (state, "online") ||
      !tp_strdiff (state, "ready"));

  DEBUG ("New fake ConnMan network state %s (%san available state)",
      state, available ? "" : "not ");

  if (available != self->available)
    {
      DEBUG ("notify::network-available");
      self->available = available;
      g_object_notify (G_OBJECT (self), "network-available");
    }

  fake_network_monitor_emit_network_changed (self);
}

static void
fake_network_monitor_dbus_signal_cb (GDBusProxy *proxy,
    const gchar *sender_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
  FakeNetworkMonitor *self = FAKE_NETWORK_MONITOR (user_data);
  const gchar *name;
  GVariant *value;

  if (tp_strdiff (signal_name, "PropertyChanged") ||
      !g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(sv)")))
    return;

  g_variant_get (parameters, "(&sv)", &name, &value);

  if (!tp_strdiff (name, "State"))
    {
      fake_network_monitor_set_state (self, g_variant_get_string (value, NULL));
    }

  g_variant_unref (value);
}

static void
fake_network_monitor_get_properties_cb (GObject *proxy,
    GAsyncResult *result,
    gpointer user_data)
{
  FakeNetworkMonitor *self = tp_weak_ref_dup_object (user_data);
  GVariant *tuple = NULL;
  GVariant *asv = NULL;
  const gchar *state;

  if (self == NULL)
    return;

  tuple = g_dbus_proxy_call_finish (G_DBUS_PROXY (proxy), result, NULL);

  if (tuple == NULL)
    {
      DEBUG ("GetProperties() failed");
      goto finally;
    }

  if (!g_variant_is_of_type (tuple, G_VARIANT_TYPE ("(a{sv})")))
    {
      DEBUG ("GetProperties() returned wrong type");
      goto finally;
    }

  asv = g_variant_get_child_value (tuple, 0);

  if (g_variant_lookup (asv, "State", "&s", &state))
    {
      DEBUG ("Initial state: %s", state);
      fake_network_monitor_set_state (self, state);
    }
  else
    {
      DEBUG ("Failed to get initial state, not in GetProperties return");
      fake_network_monitor_set_state (self, "offline");
    }

finally:
  if (asv != NULL)
    g_variant_unref (asv);

  if (tuple != NULL)
    g_variant_unref (tuple);

  g_object_unref (self);
}

static gboolean
fake_network_monitor_initable_init (GInitable *initable,
    GCancellable *cancellable,
    GError **error)
{
  FakeNetworkMonitor *self = FAKE_NETWORK_MONITOR (initable);

  if (self->proxy != NULL)
    return TRUE;

  self->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
      G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
      NULL, "net.connman", "/", "net.connman.Manager", cancellable, error);

  if (self->proxy == NULL)
    return FALSE;

  tp_g_signal_connect_object (self->proxy,
      "g-signal", G_CALLBACK (fake_network_monitor_dbus_signal_cb), self, 0);
  g_dbus_proxy_call (self->proxy,
      "GetProperties", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
      fake_network_monitor_get_properties_cb, tp_weak_ref_new (self, NULL, NULL));

  return TRUE;
}

static void
initable_iface_init (GInitableIface *iface)
{
  iface->init = fake_network_monitor_initable_init;
}

static void
monitor_iface_init (GNetworkMonitorInterface *iface)
{
  iface->can_reach = fake_network_monitor_can_reach;
  iface->can_reach_async = fake_network_monitor_can_reach_async;
  iface->can_reach_finish = fake_network_monitor_can_reach_finish;
}

void
g_io_module_load (GIOModule *module)
{
  DEBUG ("FakeNetworkMonitor plugin");

  fake_network_monitor_register_type (G_TYPE_MODULE (module));

  g_io_extension_point_implement (G_NETWORK_MONITOR_EXTENSION_POINT_NAME,
      fake_network_monitor_get_type (), "fake", G_MAXINT);
}

void
g_io_module_unload (GIOModule *module)
{
}

gchar **
g_io_module_query (void)
{
  gchar *contents[] = {
      G_NETWORK_MONITOR_EXTENSION_POINT_NAME,
      NULL
  };

  DEBUG ("FakeNetworkMonitor plugin");

  return g_strdupv (contents);
}
