/*
 * slacker.c - Idleness monitor
 * Copyright ©2010 Collabora Ltd.
 * Copyright ©2008-2010 Nokia Corporation
 * Copyright ©2013 Intel Corporation
 *
 * Derived from code in e-book-backend-tp.c in eds-backend-telepathy; thanks!
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

#include "mcd-slacker.h"

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-debug.h"

struct _McdSlackerPrivate {
    GDBusProxy *proxy;

    gboolean is_inactive;
};

G_DEFINE_TYPE (McdSlacker, mcd_slacker, G_TYPE_OBJECT)

enum {
    SIG_INACTIVITY_CHANGED = 0,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* GNOME Session Manager interface description:
 * https://git.gnome.org/browse/gnome-session/tree/gnome-session/org.gnome.SessionManager.Presence.xml
 */
enum {
    STATUS_AVAILABLE = 0,
    STATUS_INVISIBLE,
    STATUS_BUSY,
    STATUS_IDLE
};

#define SERVICE_NAME "org.gnome.SessionManager"
#define SERVICE_OBJECT_PATH "/org/gnome/SessionManager/Presence"
#define SERVICE_INTERFACE "org.gnome.SessionManager.Presence"
#define SERVICE_PROP_NAME "status"
#define SERVICE_SIG_NAME "StatusChanged"

/**
 * mcd_slacker_is_inactive:
 * @self: do some work!
 *
 * <!-- -->
 *
 * Returns: %TRUE if the device is known to be inactive; false otherwise.
 */
gboolean
mcd_slacker_is_inactive (McdSlacker *self)
{
  g_return_val_if_fail (MCD_IS_SLACKER (self), FALSE);

  return self->priv->is_inactive;
}

static void
status_changed (McdSlacker *self,
    GVariant *prop)
{
  gboolean old = self->priv->is_inactive;

  if (g_variant_classify (prop) != G_VARIANT_CLASS_UINT32)
    {
      WARNING ("%s.%s property is of type %s and we expected u",
          SERVICE_INTERFACE, SERVICE_PROP_NAME,
          g_variant_get_type_string (prop));
      return;
    }

  self->priv->is_inactive = (g_variant_get_uint32 (prop) == STATUS_IDLE);

  if (self->priv->is_inactive != old)
    {
      DEBUG ("device became %s",
          self->priv->is_inactive ? "inactive" : "active");
      g_signal_emit (self, signals[SIG_INACTIVITY_CHANGED], 0,
          self->priv->is_inactive);
    }
}

static void
signal_cb (GDBusProxy *proxy,
    gchar *sender_name,
    gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
  McdSlacker *self = user_data;
  GVariant *prop;

  if (tp_strdiff (signal_name, SERVICE_SIG_NAME))
    return;

  if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(u)")))
    {
      WARNING ("%s.%s arguments are of type %s and we expected (u)",
          SERVICE_INTERFACE, SERVICE_PROP_NAME,
          g_variant_get_type_string (parameters));
      return;
    }

  prop = g_variant_get_child_value (parameters, 0);
  status_changed (self, prop);
  g_variant_unref (prop);
}

static void
proxy_new_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  McdSlacker *self = user_data;
  GVariant *prop;
  GError *error = NULL;

  self->priv->proxy = g_dbus_proxy_new_finish (result, &error);
  if (self->priv->proxy == NULL)
    {
      DEBUG ("Error while creating slacker proxy: %s", error->message);
      goto out;
    }

  g_signal_connect (self->priv->proxy, "g-signal",
      G_CALLBACK (signal_cb), self);

  prop = g_dbus_proxy_get_cached_property (self->priv->proxy, SERVICE_PROP_NAME);

  if (g_dbus_proxy_get_name_owner (self->priv->proxy) == NULL)
    {
      DEBUG ("%s service not found", SERVICE_NAME);
    }
  else if (prop == NULL)
    {
      DEBUG ("%s.%s property is missing", SERVICE_INTERFACE, SERVICE_PROP_NAME);
    }
  else
    {
      status_changed (self, prop);
      g_variant_unref (prop);
    }

out:
  g_object_unref (self);
}

static void
mcd_slacker_init (McdSlacker *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MCD_TYPE_SLACKER,
      McdSlackerPrivate);
}

static gpointer slacker = NULL;

static GObject *
mcd_slacker_constructor (
    GType type,
    guint n_construct_properties,
    GObjectConstructParam *construct_properties)
{
  GObject *retval;

  if (slacker == NULL)
    {
      slacker = G_OBJECT_CLASS (mcd_slacker_parent_class)->constructor (
        type, n_construct_properties, construct_properties);
      retval = slacker;
      g_object_add_weak_pointer (retval, &slacker);
    }
  else
    {
      retval = g_object_ref (slacker);
    }

  return retval;
}

static void
mcd_slacker_constructed (GObject *object)
{
  McdSlacker *self = MCD_SLACKER (object);

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START, NULL,
      SERVICE_NAME, SERVICE_OBJECT_PATH, SERVICE_INTERFACE,
      NULL,
      proxy_new_cb, g_object_ref (self));
}

static void
mcd_slacker_dispose (GObject *object)
{
  McdSlacker *self = MCD_SLACKER (object);

  g_clear_object (&self->priv->proxy);

  ((GObjectClass *) mcd_slacker_parent_class)->dispose (object);
}

static void
mcd_slacker_class_init (McdSlackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructor = mcd_slacker_constructor;
  object_class->constructed = mcd_slacker_constructed;
  object_class->dispose = mcd_slacker_dispose;

  g_type_class_add_private (klass, sizeof (McdSlackerPrivate));

  /**
   * McdSlacker::inactivity-changed:
   * @self: what a slacker
   * @inactive: %TRUE if the device is inactive.
   *
   * The ::inactivity-changed is emitted when session becomes idle.
   */
  signals[SIG_INACTIVITY_CHANGED] = g_signal_new ("inactivity-changed",
      MCD_TYPE_SLACKER, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

McdSlacker *
mcd_slacker_new ()
{
  return g_object_new (MCD_TYPE_SLACKER, NULL);
}
