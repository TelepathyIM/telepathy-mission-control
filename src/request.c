/* A Telepathy ChannelRequest object
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

#include "request.h"

#include <dbus/dbus-glib.h>
#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel-request.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>

#include "mcd-debug.h"
#include "mcd-misc.h"
#include "_gen/interfaces.h"
#include "_gen/svc-Channel_Request_Future.h"

enum {
    PROP_0,
    PROP_USE_EXISTING,
    PROP_ACCOUNT,
    PROP_ACCOUNT_PATH,
    PROP_PROPERTIES,
    PROP_USER_ACTION_TIME,
    PROP_PREFERRED_HANDLER,
    PROP_HINTS,
    PROP_REQUESTS,
    PROP_INTERFACES
};

static guint sig_id_cancelling = 0;
static guint sig_id_ready_to_request = 0;

struct _McdRequest {
    GObject parent;

    gboolean use_existing;
    McdAccount *account;
    GHashTable *properties;
    gint64 user_action_time;
    gchar *preferred_handler;
    GHashTable *hints;
    gchar *object_path;

    /* Number of reasons to not make the request yet.
     *
     * We hold one extra ref to ourselves per delay. The object starts with
     * one delay in _mcd_request_init, representing the Proceed() call
     * that hasn't happened; to get the refcounting right, we take the
     * corresponding ref in _mcd_request_constructed. */
    gsize delay;

    /* TRUE if either succeeded[-with-channel] or failed was emitted */
    gboolean is_complete;

    gboolean cancellable;
    GQuark failure_domain;
    gint failure_code;
    gchar *failure_message;

    gboolean proceeding;
};

struct _McdRequestClass {
    GObjectClass parent;
    TpDBusPropertiesMixinClass dbus_properties_class;
};

static void request_iface_init (TpSvcChannelRequestClass *);

G_DEFINE_TYPE_WITH_CODE (McdRequest, _mcd_request, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_REQUEST, request_iface_init);
    G_IMPLEMENT_INTERFACE (MC_TYPE_SVC_CHANNEL_REQUEST_FUTURE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
                           tp_dbus_properties_mixin_iface_init))

#define REQUEST_OBJ_BASE "/com/nokia/MissionControl/requests/r"

static guint last_req_id = 1;

static void
_mcd_request_init (McdRequest *self)
{
  DEBUG ("%p", self);

  self->delay = 1;
  self->cancellable = TRUE;
  self->object_path = g_strdup_printf (REQUEST_OBJ_BASE "%u", last_req_id++);
}

static void
_mcd_request_constructed (GObject *object)
{
  McdRequest *self = (McdRequest *) object;
  void (*constructed) (GObject *) =
    G_OBJECT_CLASS (_mcd_request_parent_class)->constructed;

  /* this is paired with the delay in _mcd_request_init */
  g_object_ref (self);

  if (constructed != NULL)
    constructed (object);

  g_return_if_fail (self->account != NULL);
}

static void
_mcd_request_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  McdRequest *self = (McdRequest *) object;

  switch (prop_id)
    {
    case PROP_USE_EXISTING:
      g_value_set_boolean (value, self->use_existing);
      break;

    case PROP_ACCOUNT:
      g_value_set_object (value, self->account);
      break;

    case PROP_ACCOUNT_PATH:
      g_value_set_boxed (value, mcd_account_get_object_path (self->account));
      break;

    case PROP_PROPERTIES:
      g_value_set_boxed (value, self->properties);
      break;

    case PROP_PREFERRED_HANDLER:
      if (self->preferred_handler == NULL)
        {
          g_value_set_static_string (value, "");
        }
      else
        {
          g_value_set_string (value, self->preferred_handler);
        }
      break;

    case PROP_USER_ACTION_TIME:
      g_value_set_int64 (value, self->user_action_time);
      break;

    case PROP_HINTS:
      if (self->hints == NULL)
        {
          g_value_take_boxed (value, g_hash_table_new (NULL, NULL));
        }
      else
        {
          g_value_set_boxed (value, self->hints);
        }
      break;

    case PROP_REQUESTS:
        {
          GPtrArray *arr = g_ptr_array_sized_new (1);

          g_ptr_array_add (arr, g_hash_table_ref (self->properties));
          g_value_take_boxed (value, arr);
        }
      break;

    case PROP_INTERFACES:
      /* we have no interfaces */
      g_value_set_static_boxed (value, NULL);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
_mcd_request_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  McdRequest *self = (McdRequest *) object;

  switch (prop_id)
    {
    case PROP_USE_EXISTING:
      self->use_existing = g_value_get_boolean (value);
      break;

    case PROP_ACCOUNT:
      g_assert (self->account == NULL); /* construct-only */
      self->account = g_value_dup_object (value);
      break;

    case PROP_PROPERTIES:
      g_assert (self->properties == NULL); /* construct-only */
      self->properties = g_hash_table_ref (g_value_get_boxed (value));
      break;

    case PROP_USER_ACTION_TIME:
      g_assert (self->user_action_time == 0); /* construct-only */
      self->user_action_time = g_value_get_int64 (value);
      break;

    case PROP_PREFERRED_HANDLER:
      if (self->preferred_handler != NULL)
        g_free (self->preferred_handler);

      self->preferred_handler = g_value_dup_string (value);
      break;

    case PROP_HINTS:
      g_assert (self->hints == NULL); /* construct-only */
      self->hints = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
_mcd_request_dispose (GObject *object)
{
  McdRequest *self = (McdRequest *) object;
  GObjectFinalizeFunc dispose =
    G_OBJECT_CLASS (_mcd_request_parent_class)->dispose;

  DEBUG ("%p", object);

  tp_clear_object (&self->account);
  tp_clear_pointer (&self->hints, g_hash_table_unref);

  if (dispose != NULL)
    dispose (object);
}

static void
_mcd_request_finalize (GObject *object)
{
  McdRequest *self = (McdRequest *) object;
  GObjectFinalizeFunc finalize =
    G_OBJECT_CLASS (_mcd_request_parent_class)->finalize;

  DEBUG ("%p", object);

  g_free (self->preferred_handler);
  g_free (self->object_path);
  g_free (self->failure_message);
  tp_clear_pointer (&self->properties, g_hash_table_unref);

  if (finalize != NULL)
    finalize (object);
}

static void
_mcd_request_class_init (
    McdRequestClass *cls)
{
  static TpDBusPropertiesMixinPropImpl request_props[] = {
      { "Account", "account-path", NULL },
      { "UserActionTime", "user-action-time", NULL },
      { "PreferredHandler", "preferred-handler", NULL },
      { "Interfaces", "interfaces", NULL },
      { "Requests", "requests", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinPropImpl future_props[] = {
      { "Hints", "hints", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL_REQUEST,
          tp_dbus_properties_mixin_getter_gobject_properties,
          NULL,
          request_props,
      },
      { MC_IFACE_CHANNEL_REQUEST_FUTURE,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        future_props,
      },
      { NULL }
  };
  GObjectClass *object_class = (GObjectClass *) cls;

  object_class->constructed = _mcd_request_constructed;
  object_class->get_property = _mcd_request_get_property;
  object_class->set_property = _mcd_request_set_property;
  object_class->dispose = _mcd_request_dispose;
  object_class->finalize = _mcd_request_finalize;

  g_object_class_install_property (object_class, PROP_USE_EXISTING,
      g_param_spec_boolean ("use-existing", "Use EnsureChannel?",
          "TRUE if EnsureChannel should be used for this request",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ACCOUNT,
      g_param_spec_object ("account", "Account",
          "The underlying McdAccount",
          MCD_TYPE_ACCOUNT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ACCOUNT_PATH,
      g_param_spec_boxed ("account-path", "Account path",
          "The object path of McdRequest:account",
          DBUS_TYPE_G_OBJECT_PATH,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
       g_param_spec_boxed ("properties", "Properties",
         "Properties requested for the channel",
         TP_HASH_TYPE_QUALIFIED_PROPERTY_VALUE_MAP,
         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_USER_ACTION_TIME,
       g_param_spec_int64 ("user-action-time", "UserActionTime",
         "Time of user action as for TpAccountChannelRequest:user-action-time",
         G_MININT64, G_MAXINT64, TP_USER_ACTION_TIME_NOT_USER_ACTION,
         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PREFERRED_HANDLER,
       g_param_spec_string ("preferred-handler", "PreferredHandler",
         "Preferred handler for this request, or the empty string",
         "",
         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_HINTS,
      g_param_spec_boxed ("hints", "Hints",
        "GHashTable",
        TP_HASH_TYPE_STRING_VARIANT_MAP,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_REQUESTS,
      g_param_spec_boxed ("requests", "Requests", "A dbus-glib aa{sv}",
        TP_ARRAY_TYPE_QUALIFIED_PROPERTY_VALUE_MAP_LIST,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_INTERFACES,
      g_param_spec_boxed ("interfaces", "Interfaces", "A dbus-glib 'as'",
        G_TYPE_STRV, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  sig_id_cancelling = g_signal_new ("cancelling",
      G_OBJECT_CLASS_TYPE (cls), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  sig_id_ready_to_request = g_signal_new ("ready-to-request",
      G_OBJECT_CLASS_TYPE (cls), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  cls->dbus_properties_class.interfaces = prop_interfaces,
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (McdRequestClass, dbus_properties_class));
}

McdRequest *
_mcd_request_new (gboolean use_existing,
    McdAccount *account,
    GHashTable *properties,
    gint64 user_action_time,
    const gchar *preferred_handler,
    GHashTable *hints)
{
  McdRequest *self;

  self = g_object_new (MCD_TYPE_REQUEST,
      "use-existing", use_existing,
      "account", account,
      "properties", properties,
      "user-action-time", user_action_time,
      "preferred-handler", preferred_handler,
      "hints", hints,
      NULL);
  DEBUG ("%p (for %p)", self, account);

  return self;
}

gboolean
_mcd_request_get_use_existing (McdRequest *self)
{
  return self->use_existing;
}

McdAccount *
_mcd_request_get_account (McdRequest *self)
{
  return self->account;
}

gint64
_mcd_request_get_user_action_time (McdRequest *self)
{
  return self->user_action_time;
}

const gchar *
_mcd_request_get_preferred_handler (McdRequest *self)
{
  if (self->preferred_handler == NULL)
    return "";

  return self->preferred_handler;
}

const gchar *
_mcd_request_get_object_path (McdRequest *self)
{
  return self->object_path;
}

GHashTable *
_mcd_request_get_hints (McdRequest *self)
{
  return self->hints;
}

gboolean
_mcd_request_set_proceeding (McdRequest *self)
{
  if (self->proceeding)
    return FALSE;

  self->proceeding = TRUE;
  return TRUE;
}

GHashTable *
_mcd_request_get_properties (McdRequest *self)
{
  return self->properties;
}

void
_mcd_request_start_delay (McdRequest *self)
{
    g_object_ref (self);
    self->delay++;
}

void
_mcd_request_end_delay (McdRequest *self)
{
    g_return_if_fail (self->delay > 0);

    if (--self->delay == 0)
    {
      g_signal_emit (self, sig_id_ready_to_request, 0);
    }

    g_object_unref (self);
}

void
_mcd_request_set_success (McdRequest *self,
    TpChannel *channel)
{
  g_return_if_fail (TP_IS_CHANNEL (channel));

  if (!self->is_complete)
    {
      DEBUG ("Request succeeded");
      self->is_complete = TRUE;
      self->cancellable = FALSE;

      mc_svc_channel_request_future_emit_succeeded_with_channel (self,
          tp_proxy_get_object_path (tp_channel_borrow_connection (channel)),
          tp_proxy_get_object_path (channel));
      tp_svc_channel_request_emit_succeeded (self);
    }
  else
    {
      DEBUG ("Ignoring an attempt to succeed after already complete");
    }
}

void
_mcd_request_set_failure (McdRequest *self,
    GQuark domain,
    gint code,
    const gchar *message)
{
  if (!self->is_complete)
    {
      GError e = { domain, code, (gchar *) message };
      gchar *err_string;

      DEBUG ("Request failed: %s %d: %s", g_quark_to_string (domain),
          code, message);

      err_string = _mcd_build_error_string (&e);

      self->is_complete = TRUE;
      self->cancellable = FALSE;
      self->failure_domain = domain;
      self->failure_code = code;
      self->failure_message = g_strdup (message);
      tp_svc_channel_request_emit_failed (self, err_string, message);

      g_free (err_string);
    }
  else
    {
      DEBUG ("Ignoring an attempt to fail after already complete");
    }
}

gboolean
_mcd_request_is_complete (McdRequest *self)
{
  return self->is_complete;
}

GError *
_mcd_request_dup_failure (McdRequest *self)
{
  if (self->failure_domain == 0)
    return NULL;

  return g_error_new_literal (self->failure_domain, self->failure_code,
      self->failure_message);
}

void
_mcd_request_set_uncancellable (McdRequest *self)
{
  self->cancellable = FALSE;
}

static void
channel_request_cancel (TpSvcChannelRequest *iface,
                        DBusGMethodInvocation *context)
{
  McdRequest *self = MCD_REQUEST (iface);
  GError *error = NULL;

  if (_mcd_request_cancel (self, &error))
    {
      tp_svc_channel_request_return_from_cancel (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

static void
request_iface_init (TpSvcChannelRequestClass *iface)
{
#define IMPLEMENT(x) tp_svc_channel_request_implement_##x (\
    iface, channel_request_##x)
  /* We don't yet implement Proceed() */
  /* IMPLEMENT (proceed); */
  IMPLEMENT (cancel);
#undef IMPLEMENT
}

GHashTable *
_mcd_request_dup_immutable_properties (McdRequest *self)
{
  return tp_dbus_properties_mixin_make_properties_hash ((GObject *) self,
      TP_IFACE_CHANNEL_REQUEST, "Account",
      TP_IFACE_CHANNEL_REQUEST, "UserActionTime",
      TP_IFACE_CHANNEL_REQUEST, "PreferredHandler",
      TP_IFACE_CHANNEL_REQUEST, "Interfaces",
      TP_IFACE_CHANNEL_REQUEST, "Requests",
      MC_IFACE_CHANNEL_REQUEST_FUTURE, "Hints",
      NULL);
}

gboolean
_mcd_request_cancel (McdRequest *self,
    GError **error)
{
  if (self->cancellable)
    {
      /* for the moment, McdChannel has to do the actual work, because its
       * status/error track the failure state */
      g_signal_emit (self, sig_id_cancelling, 0);
      return TRUE;
    }
  else
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "ChannelRequest is no longer cancellable");
      return FALSE;
    }
}
