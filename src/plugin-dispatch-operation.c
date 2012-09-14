/* Representation of a dispatch operation as presented to plugins. This is
 * deliberately a "smaller" API than McdDispatchOperation.
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include "config.h"

#include "plugin-dispatch-operation.h"

#include "mission-control-plugins/implementation.h"

#include "mcd-channel-priv.h"
#include "mcd-debug.h"

/* a larger numeric value will override a smaller one */
typedef enum {
    PLUGIN_ACTION_NONE,
    PLUGIN_ACTION_CLOSE,
    PLUGIN_ACTION_LEAVE,
    PLUGIN_ACTION_DESTROY
} PluginAction;

enum {
    PROP_0,
    PROP_REAL_CDO
};

struct _McdPluginDispatchOperation {
    GObject parent;
    McdDispatchOperation *real_cdo;
    PluginAction after_observers;
    TpChannelGroupChangeReason reason;
    gchar *message;
};

struct _McdPluginDispatchOperationClass {
    GObjectClass parent;
};

static void plugin_iface_init (McpDispatchOperationIface *iface,
    gpointer unused G_GNUC_UNUSED);

G_DEFINE_TYPE_WITH_CODE (McdPluginDispatchOperation,
    _mcd_plugin_dispatch_operation, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_DISPATCH_OPERATION, plugin_iface_init))

static void
_mcd_plugin_dispatch_operation_init (McdPluginDispatchOperation *self)
{
  DEBUG ("%p", self);
}

static void
plugin_do_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  McdPluginDispatchOperation *self = (McdPluginDispatchOperation *) object;

  switch (prop_id)
    {
    case PROP_REAL_CDO:
      g_assert (self->real_cdo == NULL); /* construct-only */
      /* The real CDO is borrowed, because this plugin API is owned by it */
      self->real_cdo = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
plugin_do_finalize (GObject *object)
{
  McdPluginDispatchOperation *self = (McdPluginDispatchOperation *) object;
  GObjectFinalizeFunc finalize =
    G_OBJECT_CLASS (_mcd_plugin_dispatch_operation_parent_class)->finalize;

  DEBUG ("%p", object);

  g_free (self->message);

  if (finalize != NULL)
    finalize (object);
}

static void
_mcd_plugin_dispatch_operation_class_init (
    McdPluginDispatchOperationClass *cls)
{
  GObjectClass *object_class = (GObjectClass *) cls;

  object_class->set_property = plugin_do_set_property;
  object_class->finalize = plugin_do_finalize;

  g_object_class_install_property (object_class, PROP_REAL_CDO,
      g_param_spec_object ("real-cdo", "Real channel dispatch operation",
          "Borrowed pointer to the underlying McdDispatchOperation",
          MCD_TYPE_DISPATCH_OPERATION,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

McdPluginDispatchOperation *
_mcd_plugin_dispatch_operation_new (McdDispatchOperation *real_cdo)
{
  McdPluginDispatchOperation *self;

  self = g_object_new (MCD_TYPE_PLUGIN_DISPATCH_OPERATION,
      "real-cdo", real_cdo,
      NULL);
  DEBUG ("%p (for %p)", self, real_cdo);

  return self;
}

static const gchar *
plugin_do_get_account_path (McpDispatchOperation *obj)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);

  g_return_val_if_fail (self != NULL, NULL);
  return _mcd_dispatch_operation_get_account_path (self->real_cdo);
}

static const gchar *
plugin_do_get_connection_path (McpDispatchOperation *obj)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);

  g_return_val_if_fail (self != NULL, NULL);
  return _mcd_dispatch_operation_get_connection_path (self->real_cdo);
}

static const gchar *
plugin_do_get_protocol (McpDispatchOperation *obj)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);

  g_return_val_if_fail (self != NULL, NULL);
  return _mcd_dispatch_operation_get_protocol (self->real_cdo);
}

static const gchar *
plugin_do_get_cm_name (McpDispatchOperation *obj)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);

  g_return_val_if_fail (self != NULL, NULL);
  return _mcd_dispatch_operation_get_cm_name (self->real_cdo);
}


/* Channels */
static guint
plugin_do_get_n_channels (McpDispatchOperation *obj)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);

  g_return_val_if_fail (self != NULL, 0);

  if (_mcd_dispatch_operation_peek_channel (self->real_cdo) != NULL)
    return 1;

  return 0;
}

static const gchar *
plugin_do_get_nth_channel_path (McpDispatchOperation *obj,
    guint n)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);
  McdChannel *channel;

  g_return_val_if_fail (self != NULL, NULL);

  channel = _mcd_dispatch_operation_peek_channel (self->real_cdo);

  if (channel == NULL || n != 0)
    return NULL;

  return mcd_channel_get_object_path (channel);
}

static GHashTable *
plugin_do_ref_nth_channel_properties (McpDispatchOperation *obj,
    guint n)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);
  McdChannel *channel;
  GVariant *variant;
  GValue value = G_VALUE_INIT;
  GHashTable *ret;

  g_return_val_if_fail (self != NULL, NULL);

  channel = _mcd_dispatch_operation_peek_channel (self->real_cdo);

  if (channel == NULL || n != 0)
    return NULL;

  variant = mcd_channel_dup_immutable_properties (channel);

  if (variant == NULL)
    return NULL;

  /* For compatibility, we have to return the older type here. */
  dbus_g_value_parse_g_variant (variant, &value);
  g_assert (G_VALUE_HOLDS (&value, TP_HASH_TYPE_STRING_VARIANT_MAP));
  ret = g_value_dup_boxed (&value);
  g_value_unset (&value);

  return ret;
}


/* Delay the dispatch */

/* an arbitrary constant, to detect use-after-free or wrong pointers */
#define DELAY_MAGIC 0xCD053

typedef struct {
    gsize magic;
    McdPluginDispatchOperation *self;
} RealDelay;

static McpDispatchOperationDelay *
plugin_do_start_delay (McpDispatchOperation *obj)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);
  RealDelay *delay;

  DEBUG ("%p", self);

  g_return_val_if_fail (self != NULL, NULL);
  delay = g_slice_new (RealDelay);
  delay->magic = DELAY_MAGIC;
  delay->self = g_object_ref (obj);
  _mcd_dispatch_operation_start_plugin_delay (self->real_cdo);
  return (McpDispatchOperationDelay *) delay;
}

static void
plugin_do_end_delay (McpDispatchOperation *obj,
    McpDispatchOperationDelay *delay)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);
  RealDelay *real_delay = (RealDelay *) delay;

  DEBUG ("%p", self);

  g_return_if_fail (self != NULL);
  g_return_if_fail (real_delay->self == self);
  g_return_if_fail (real_delay->magic == DELAY_MAGIC);

  real_delay->magic = ~(DELAY_MAGIC);
  real_delay->self = NULL;
  _mcd_dispatch_operation_end_plugin_delay (self->real_cdo);
  g_object_unref (self);
}


/* Close */
static void
plugin_do_leave_channels (McpDispatchOperation *obj,
    gboolean wait_for_observers, TpChannelGroupChangeReason reason,
    const gchar *message)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);

  DEBUG ("%p (wait=%c reason=%d message=%s)", self,
      wait_for_observers ? 'T' : 'F', reason, message);

  g_return_if_fail (self != NULL);

  if (wait_for_observers)
    {
      if (self->after_observers < PLUGIN_ACTION_LEAVE)
        {
          DEBUG ("Remembering for later");
          self->after_observers = PLUGIN_ACTION_LEAVE;
          self->reason = reason;
          g_free (self->message);
          self->message = g_strdup (message);
        }
    }
  else
    {
      DEBUG ("Leaving now");
      _mcd_dispatch_operation_leave_channels (self->real_cdo, reason, message);
    }
}

static void
plugin_do_close_channels (McpDispatchOperation *obj,
    gboolean wait_for_observers)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);

  DEBUG ("%p (wait=%c)", self, wait_for_observers ? 'T' : 'F');

  g_return_if_fail (self != NULL);

  if (wait_for_observers)
    {
      if (self->after_observers < PLUGIN_ACTION_CLOSE)
        {
          DEBUG ("Remembering for later");
          self->after_observers = PLUGIN_ACTION_CLOSE;
        }
    }
  else
    {
      DEBUG ("Closing now");
      _mcd_dispatch_operation_close_channels (self->real_cdo);
    }
}

static void
plugin_do_destroy_channels (McpDispatchOperation *obj,
    gboolean wait_for_observers)
{
  McdPluginDispatchOperation *self = MCD_PLUGIN_DISPATCH_OPERATION (obj);

  DEBUG ("%p (wait=%c)", self, wait_for_observers ? 'T' : 'F');

  g_return_if_fail (self != NULL);

  if (wait_for_observers)
    {
      if (self->after_observers < PLUGIN_ACTION_DESTROY)
        self->after_observers = PLUGIN_ACTION_DESTROY;
    }
  else
    {
      _mcd_dispatch_operation_destroy_channels (self->real_cdo);
    }
}

void
_mcd_plugin_dispatch_operation_observers_finished (
    McdPluginDispatchOperation *self)
{
  DEBUG ("%p", self);

  switch (self->after_observers)
    {
    case PLUGIN_ACTION_DESTROY:
      DEBUG ("destroying now");
      _mcd_dispatch_operation_destroy_channels (self->real_cdo);
      break;

    case PLUGIN_ACTION_LEAVE:
      DEBUG ("leaving now: %d %s", self->reason, self->message);
      _mcd_dispatch_operation_leave_channels (self->real_cdo,
          self->reason, self->message);
      break;

    case PLUGIN_ACTION_CLOSE:
      DEBUG ("closing now");
      _mcd_dispatch_operation_close_channels (self->real_cdo);
      break;

    case PLUGIN_ACTION_NONE:
      /* nothing to do */
      break;
    }
}

gboolean
_mcd_plugin_dispatch_operation_will_terminate (
    McdPluginDispatchOperation *self)
{
  return (self->after_observers != PLUGIN_ACTION_NONE);
}

static void
plugin_iface_init (McpDispatchOperationIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  DEBUG ("called");

  iface->get_account_path = plugin_do_get_account_path;
  iface->get_connection_path = plugin_do_get_connection_path;
  iface->get_protocol = plugin_do_get_protocol;
  iface->get_cm_name = plugin_do_get_cm_name;

  iface->get_n_channels = plugin_do_get_n_channels;
  iface->get_nth_channel_path = plugin_do_get_nth_channel_path;
  iface->ref_nth_channel_properties = plugin_do_ref_nth_channel_properties;

  iface->start_delay = plugin_do_start_delay;
  iface->end_delay = plugin_do_end_delay;

  iface->leave_channels = plugin_do_leave_channels;
  iface->close_channels = plugin_do_close_channels;
  iface->destroy_channels = plugin_do_destroy_channels;
}
