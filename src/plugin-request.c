/* Representation of a channel request as presented to plugins. This is
 * deliberately a "smaller" API than McdChannel or McdRequest.
 *
 * Copyright © 2009 Nokia Corporation.
 * Copyright © 2009-2010 Collabora Ltd.
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

#include "plugin-request.h"

#include <telepathy-glib/telepathy-glib.h>

#include "mission-control-plugins/implementation.h"

#include "mcd-channel-priv.h"
#include "mcd-debug.h"

enum {
    PROP_0,
    PROP_ACCOUNT,
    PROP_REAL_REQUEST
};

struct _McdPluginRequest {
    GObject parent;
    McdAccount *account;
    McdRequest *real_request;
};

struct _McdPluginRequestClass {
    GObjectClass parent;
};

static void plugin_iface_init (McpRequestIface *iface,
    gpointer unused G_GNUC_UNUSED);

G_DEFINE_TYPE_WITH_CODE (McdPluginRequest, _mcd_plugin_request, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_REQUEST, plugin_iface_init))

static void
_mcd_plugin_request_init (McdPluginRequest *self)
{
  DEBUG ("%p", self);
}

static void
plugin_req_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  McdPluginRequest *self = (McdPluginRequest *) object;

  switch (prop_id)
    {
    case PROP_REAL_REQUEST:
      g_assert (self->real_request == NULL); /* construct-only */
      self->real_request = g_value_dup_object (value);
      break;

    case PROP_ACCOUNT:
      g_assert (self->account == NULL); /* construct-only */
      self->account = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
plugin_req_dispose (GObject *object)
{
  McdPluginRequest *self = (McdPluginRequest *) object;
  GObjectFinalizeFunc dispose =
    G_OBJECT_CLASS (_mcd_plugin_request_parent_class)->dispose;

  DEBUG ("%p", object);

  tp_clear_object (&self->account);
  tp_clear_object (&self->real_request);

  if (dispose != NULL)
    dispose (object);
}

static void
_mcd_plugin_request_class_init (
    McdPluginRequestClass *cls)
{
  GObjectClass *object_class = (GObjectClass *) cls;

  object_class->set_property = plugin_req_set_property;
  object_class->dispose = plugin_req_dispose;

  g_object_class_install_property (object_class, PROP_REAL_REQUEST,
      g_param_spec_object ("real-request", "Real channel request",
          "The underlying McdRequest",
          MCD_TYPE_REQUEST,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ACCOUNT,
      g_param_spec_object ("account", "Account",
          "The underlying McdAccount",
          MCD_TYPE_ACCOUNT,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

McdPluginRequest *
_mcd_plugin_request_new (McdAccount *account,
    McdRequest *real_request)
{
  McdPluginRequest *self;

  self = g_object_new (MCD_TYPE_PLUGIN_REQUEST,
      "account", account,
      "real-request", real_request,
      NULL);
  DEBUG ("%p (for %p, %p)", self, account, real_request);

  return self;
}

static const gchar *
plugin_req_get_account_path (McpRequest *obj)
{
  McdPluginRequest *self = MCD_PLUGIN_REQUEST (obj);

  g_return_val_if_fail (self != NULL, NULL);

  return mcd_account_get_object_path (self->account);
}

static const gchar *
plugin_req_get_protocol (McpRequest *obj)
{
  McdPluginRequest *self = MCD_PLUGIN_REQUEST (obj);

  g_return_val_if_fail (self != NULL, NULL);

  return mcd_account_get_protocol_name (self->account);
}

static const gchar *
plugin_req_get_cm_name (McpRequest *obj)
{
  McdPluginRequest *self = MCD_PLUGIN_REQUEST (obj);

  g_return_val_if_fail (self != NULL, NULL);

  return mcd_account_get_manager_name (self->account);
}

static gint64
plugin_req_get_user_action_time (McpRequest *obj)
{
  McdPluginRequest *self = MCD_PLUGIN_REQUEST (obj);

  g_return_val_if_fail (self != NULL, 0);

  return _mcd_request_get_user_action_time (self->real_request);
}

static guint
plugin_req_get_n_requests (McpRequest *obj)
{
  McdPluginRequest *self = MCD_PLUGIN_REQUEST (obj);

  g_return_val_if_fail (self != NULL, 0);

  /* We only know how to request one channel at a time */
  return 1;
}

static GHashTable *
plugin_req_ref_nth_request (McpRequest *obj,
    guint n)
{
  McdPluginRequest *self = MCD_PLUGIN_REQUEST (obj);
  GHashTable *requested_properties;

  g_return_val_if_fail (self != NULL, NULL);

  if (n > 0)
    {
      /* not an error, for easy iteration */
      return NULL;
    }

  requested_properties = _mcd_request_get_properties (
      self->real_request);
  g_return_val_if_fail (requested_properties != NULL, NULL);
  return g_hash_table_ref (requested_properties);
}

static void
plugin_req_deny (McpRequest *obj,
    GQuark domain,
    gint code,
    const gchar *message)
{
  McdPluginRequest *self = MCD_PLUGIN_REQUEST (obj);

  g_return_if_fail (self != NULL);

  _mcd_request_set_failure (self->real_request, domain, code, message);
}

/* an arbitrary constant, to detect use-after-free or wrong pointers */
#define DELAY_MAGIC 0xC953

typedef struct {
    gsize magic;
    McdPluginRequest *self;
} RealDelay;

static McpRequestDelay *
plugin_req_start_delay (McpRequest *obj)
{
  McdPluginRequest *self = MCD_PLUGIN_REQUEST (obj);
  RealDelay *delay;

  DEBUG ("%p", self);

  g_return_val_if_fail (self != NULL, NULL);
  delay = g_slice_new (RealDelay);
  delay->magic = DELAY_MAGIC;
  delay->self = g_object_ref (obj);
  _mcd_request_start_delay (self->real_request);
  return (McpRequestDelay *) delay;
}

static void
plugin_req_end_delay (McpRequest *obj,
    McpRequestDelay *delay)
{
  McdPluginRequest *self = MCD_PLUGIN_REQUEST (obj);
  RealDelay *real_delay = (RealDelay *) delay;

  DEBUG ("%p", self);

  g_return_if_fail (self != NULL);
  g_return_if_fail (real_delay->self == self);
  g_return_if_fail (real_delay->magic == DELAY_MAGIC);

  real_delay->magic = ~(DELAY_MAGIC);
  real_delay->self = NULL;
  _mcd_request_end_delay (self->real_request);
  g_object_unref (self);
}

static void
plugin_iface_init (McpRequestIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  DEBUG ("called");

  iface->get_account_path = plugin_req_get_account_path;
  iface->get_protocol = plugin_req_get_protocol;
  iface->get_cm_name = plugin_req_get_cm_name;

  iface->get_user_action_time = plugin_req_get_user_action_time;
  iface->get_n_requests = plugin_req_get_n_requests;
  iface->ref_nth_request = plugin_req_ref_nth_request;

  iface->deny = plugin_req_deny;
  iface->start_delay = plugin_req_start_delay;
  iface->end_delay = plugin_req_end_delay;
}
