/*
 * slacker.c - Maemo device state monitor
 * Copyright ©2010 Collabora Ltd.
 * Copyright ©2008-2010 Nokia Corporation
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

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/telepathy-glib.h>

#ifdef HAVE_MCE
#include <mce/dbus-names.h>
#else /* HAVE_MCE */

/* Use some dummy interfaces etc. for the test suite.
 *
 * In a perfect world of sweetness and light these would not be enabled for the
 * real build, but we do not live in a perfect world of sweetness and light: we
 * live below a dark cloud of bitter ash, the charred remains of a defunct
 * economy and cygnine clothing.
 */
#define MCE_SERVICE "org.freedesktop.Telepathy.MissionControl.Tests.MCE"

#define MCE_SIGNAL_IF "org.freedesktop.Telepathy.MissionControl.Tests.MCE"
#define MCE_INACTIVITY_SIG "InactivityChanged"

#define MCE_REQUEST_IF "org.freedesktop.Telepathy.MissionControl.Tests.MCE"
#define MCE_REQUEST_PATH "/org/freedesktop/Telepathy/MissionControl/Tests/MCE"
#define MCE_INACTIVITY_STATUS_GET "GetInactivity"

#endif /* HAVE_MCE */

#include "mcd-debug.h"

struct _McdSlackerPrivate {
    DBusGConnection *bus;
    DBusGProxy *mce_request_proxy;

    gboolean is_inactive;
};

G_DEFINE_TYPE (McdSlacker, mcd_slacker, G_TYPE_OBJECT)

enum {
    SIG_INACTIVITY_CHANGED = 0,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

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
slacker_inactivity_changed (
    McdSlacker *self,
    gboolean is_inactive)
{
  McdSlackerPrivate *priv = self->priv;
  gboolean old = priv->is_inactive;

  priv->is_inactive = is_inactive;

  if (!!old != !!is_inactive)
    {
      DEBUG ("device became %s", (is_inactive ? "inactive" : "active"));
      g_signal_emit (self, signals[SIG_INACTIVITY_CHANGED], 0, is_inactive);
    }
}

static GQuark mce_signal_interface_quark = 0;
static GQuark mce_inactivity_signal_quark = 0;

#define INACTIVITY_MATCH_RULE \
  "type='signal',interface='" MCE_SIGNAL_IF "',member='" MCE_INACTIVITY_SIG "'"

static DBusHandlerResult
slacker_message_filter (
    DBusConnection *connection,
    DBusMessage *message,
    gpointer user_data)
{
  McdSlacker *self = MCD_SLACKER (user_data);
  GQuark interface, member;
  const gchar *interface_name = NULL;
  const gchar *member_name = NULL;

  if (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_SIGNAL)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  interface_name = dbus_message_get_interface (message);
  if (interface_name == NULL)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  member_name = dbus_message_get_member (message);
  if (member_name == NULL)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  interface = g_quark_try_string (interface_name);
  member = g_quark_try_string (member_name);

  if (interface == mce_signal_interface_quark &&
      member == mce_inactivity_signal_quark)
    {
      gboolean is_inactive;

      if (dbus_message_get_args (message, NULL, DBUS_TYPE_BOOLEAN, &is_inactive,
              DBUS_TYPE_INVALID))
        slacker_inactivity_changed (self, is_inactive);
      else
        DEBUG (MCE_INACTIVITY_SIG " without a boolean argument, ignoring");
    }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
get_inactivity_status_cb (
    DBusGProxy *proxy,
    DBusGProxyCall *call,
    gpointer user_data)
{
  McdSlacker *self = MCD_SLACKER (user_data);
  gboolean is_inactive;
  GError *error = NULL;

  if (!dbus_g_proxy_end_call (proxy, call, &error /* ignore errors */,
          G_TYPE_BOOLEAN, &is_inactive, G_TYPE_INVALID))
    {
      DEBUG ("error getting inactivity status: %s", error->message);
      g_error_free (error);
    }
    else
    {
      slacker_inactivity_changed (self, is_inactive);
    }

  tp_clear_object (&self->priv->mce_request_proxy);
}

static void
slacker_add_filter (McdSlacker *self)
{
  McdSlackerPrivate *priv = self->priv;
  DBusConnection *c = dbus_g_connection_get_connection (self->priv->bus);

  dbus_connection_add_filter (c, slacker_message_filter, self, NULL);
  dbus_bus_add_match (c, INACTIVITY_MATCH_RULE, NULL);

  priv->mce_request_proxy = dbus_g_proxy_new_for_name (priv->bus,
      MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF);
  dbus_g_proxy_begin_call (priv->mce_request_proxy, MCE_INACTIVITY_STATUS_GET,
      get_inactivity_status_cb, self, NULL, G_TYPE_INVALID);
}

static void
slacker_remove_filter (McdSlacker *self)
{
  DBusConnection *c = dbus_g_connection_get_connection (self->priv->bus);

  dbus_connection_remove_filter (c, slacker_message_filter, self);
  dbus_bus_remove_match (c, INACTIVITY_MATCH_RULE, NULL);
}

/* GObject boilerplate */

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
  GError *error = NULL;

#ifdef HAVE_MCE
  self->priv->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
#else
  self->priv->bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
#endif

  if (self->priv->bus == NULL)
    {
      g_warning ("help! where did my system bus go? %s", error->message);
      g_clear_error (&error);
    }
  else
    {
      slacker_add_filter (self);
    }
}

static void
mcd_slacker_dispose (GObject *object)
{
  McdSlacker *self = MCD_SLACKER (object);
  McdSlackerPrivate *priv = self->priv;

  tp_clear_object (&priv->mce_request_proxy); /* this cancels pending calls */

  if (priv->bus != NULL)
    slacker_remove_filter (self);

  tp_clear_pointer (&priv->bus, dbus_g_connection_unref);

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
   * The ::inactivity-changed is emitted whenever MCE declares that the device
   * has become active or inactive. Note that there is a lag (of around 30
   * seconds, at the time of writing) between the screen blanking and MCE
   * declaring the device inactive.
   */
  signals[SIG_INACTIVITY_CHANGED] = g_signal_new ("inactivity-changed",
      MCD_TYPE_SLACKER, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  if (!mce_signal_interface_quark)
    {
      mce_signal_interface_quark = g_quark_from_static_string (MCE_SIGNAL_IF);
      mce_inactivity_signal_quark = g_quark_from_static_string (
          MCE_INACTIVITY_SIG);
    }
}

McdSlacker *
mcd_slacker_new ()
{
  return g_object_new (MCD_TYPE_SLACKER, NULL);
}
