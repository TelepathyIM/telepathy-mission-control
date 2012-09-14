/* A Telepathy ChannelRequest object
 *
 * Copyright © 2009-2011 Nokia Corporation.
 * Copyright © 2009-2011 Collabora Ltd.
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

#include "request.h"

#include <dbus/dbus-glib.h>
#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "mcd-account-priv.h"
#include "mcd-connection-priv.h"
#include "mcd-debug.h"
#include "mcd-misc.h"
#include "plugin-loader.h"
#include "plugin-request.h"
#include "_gen/interfaces.h"

enum {
    PROP_0,
    PROP_CLIENT_REGISTRY,
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
    McdClientRegistry *clients;
    TpDBusDaemon *dbus_daemon;
    McdAccount *account;
    GHashTable *properties;
    gint64 user_action_time;
    gchar *preferred_handler;
    GHashTable *hints;
    gchar *object_path;

    /* if the request is an internally handled special case: */
    McdRequestInternalHandler internal_handler;
    GFreeFunc internal_handler_clear;
    gpointer internal_handler_data;

    /* Number of reasons to not make the request yet.
     *
     * We hold one extra ref to ourselves per delay. The object starts with
     * one delay in _mcd_request_init, representing the Proceed() call
     * that hasn't happened; to get the refcounting right, we take the
     * corresponding ref in _mcd_request_constructed. */
    gsize delay;
    TpClient *predicted_handler;

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
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
                           tp_dbus_properties_mixin_iface_init))

#define REQUEST_OBJ_BASE "/org/freedesktop/Telepathy/ChannelDispatcher/Request"

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
  g_return_if_fail (self->clients != NULL);

  self->dbus_daemon = _mcd_client_registry_get_dbus_daemon (self->clients);
  tp_dbus_daemon_register_object (self->dbus_daemon, self->object_path, self);
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

    case PROP_CLIENT_REGISTRY:
      g_value_set_object (value, self->clients);
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

    case PROP_CLIENT_REGISTRY:
      g_assert (self->clients == NULL); /* construct-only */
      self->clients = g_value_dup_object (value);
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

  /* shouldn't ever actually get this far with a blocked account, *
   * but we have to clear the lock if we do or we'll deadlock     */
  if (_mcd_request_is_internal (self) && self->account != NULL)
    {
      const gchar *path = mcd_account_get_object_path (self->account);
      _mcd_request_unblock_account (path);
      g_warning ("internal request disposed without being handled or failed");
    }

  tp_clear_object (&self->account);
  tp_clear_object (&self->clients);
  tp_clear_object (&self->predicted_handler);
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

  _mcd_request_clear_internal_handler (self);

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
      { "Hints", "hints", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL_REQUEST,
          tp_dbus_properties_mixin_getter_gobject_properties,
          NULL,
          request_props,
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

  g_object_class_install_property (object_class, PROP_CLIENT_REGISTRY,
      g_param_spec_object ("client-registry", "Client registry",
          "The client registry",
          MCD_TYPE_CLIENT_REGISTRY,
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
_mcd_request_new (McdClientRegistry *clients,
    gboolean use_existing,
    McdAccount *account,
    GHashTable *properties,
    gint64 user_action_time,
    const gchar *preferred_handler,
    GHashTable *hints)
{
  McdRequest *self;

  self = g_object_new (MCD_TYPE_REQUEST,
      "client-registry", clients,
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

void
_mcd_request_set_internal_handler (McdRequest *self,
    McdRequestInternalHandler handler,
    GFreeFunc free_func,
    gpointer data)
{
  g_assert (self->internal_handler == NULL);
  g_assert (self->internal_handler_data == NULL);
  g_assert (self->internal_handler_clear == NULL);

  self->internal_handler = handler;
  self->internal_handler_clear = free_func;
  self->internal_handler_data = data;
}

gboolean
_mcd_request_handle_internally (McdRequest *self,
    McdChannel *channel,
    gboolean close_after)
{
  if (self->internal_handler != NULL)
    {
      DEBUG ("Handling request %p, channel %p internally", self, channel);
      self->internal_handler (self, channel, self->internal_handler_data,
          close_after);

      return TRUE;
    }

  return FALSE;
}

void
_mcd_request_clear_internal_handler (McdRequest *self)
{
  if (self->internal_handler_clear != NULL)
    self->internal_handler_clear (self->internal_handler_data);

  self->internal_handler = NULL;
  self->internal_handler_data = NULL;
  self->internal_handler_clear = NULL;
}

gboolean
_mcd_request_is_internal (McdRequest *self)
{
  return self != NULL && self->internal_handler != NULL;
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

static GList *
_mcd_request_policy_plugins (void)
{
  static gboolean cached = FALSE;
  static GList *policies = NULL;

  if (G_UNLIKELY (!cached))
    {
      const GList *p = NULL;

      for (p = mcp_list_objects (); p != NULL; p = g_list_next (p))
        {
          if (MCP_IS_REQUEST_POLICY (p->data))
            policies = g_list_prepend (policies, g_object_ref (p->data));
        }

      cached = TRUE;
    }

  return policies;
}

/* hash keys on account paths: value is the lock-count */
static GHashTable *account_locks = NULL;
static GHashTable *blocked_reqs = NULL;

static void
_init_blocked_account_request_queue (void)
{
  account_locks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  blocked_reqs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

guint
_mcd_request_block_account (const gchar *account)
{
  guint count;
  gchar *key = g_strdup (account);

  if (G_UNLIKELY (account_locks == NULL))
    _init_blocked_account_request_queue ();

  count = GPOINTER_TO_UINT (g_hash_table_lookup (account_locks, account));
  g_hash_table_replace (account_locks, key, GUINT_TO_POINTER (++count));
  DEBUG ("lock count for account %s is now: %u", account, count);

  return count;
}

static void
_unblock_request (gpointer object, gpointer data)
{
  DEBUG ("ending delay for internally locked request %p on account %s",
      object, (const gchar *) data);
  _mcd_request_end_delay (MCD_REQUEST (object));
}

guint
_mcd_request_unblock_account (const gchar *account)
{
  guint count = 0;
  gchar *key = NULL;

  if (G_UNLIKELY (account_locks == NULL))
    {
      g_warning ("Unbalanced account-request-unblock for %s", account);
      return 0;
    }

  count = GPOINTER_TO_UINT (g_hash_table_lookup (account_locks, account));

  switch (count)
    {
      GQueue *queue;

      case 0:
        g_warning ("Unbalanced account-request-unblock for %s", account);
        return 0;

      case 1:
        DEBUG ("removing lock from account %s", account);
        g_hash_table_remove (account_locks, account);
        queue = g_hash_table_lookup (blocked_reqs, account);

        if (queue == NULL)
          return 0;

        g_queue_foreach (queue, _unblock_request, NULL);
        g_queue_clear (queue);

        return 0;

      default:
        DEBUG ("reducing lock count for %s", account);
        key = g_strdup (account);
        g_hash_table_replace (account_locks, key, GUINT_TO_POINTER (--count));
        return count;
    }
}

static gboolean
_queue_blocked_requests (McdRequest *self)
{
  const gchar *path = NULL;
  guint locks = 0;

  /* this is an internal request and therefore not subject to blocking *
     BUT the fact that this internal request is in-flight means other  *
     requests on the same account/handle type should be blocked        */
  if (self->internal_handler != NULL)
    {
      path = mcd_account_get_object_path (_mcd_request_get_account (self));
      _mcd_request_block_account (path);

      return FALSE;
    }

  /* no internal requests in flight, nothing to queue, nothing to do */
  if (account_locks == NULL)
    return FALSE;

  if (path == NULL)
    path = mcd_account_get_object_path (_mcd_request_get_account (self));

  /* account_locks tracks the # of in-flight internal requests per account */
  locks = GPOINTER_TO_UINT (g_hash_table_lookup (account_locks, path));

  /* internal reqeusts in flight => other requests on that account must wait */
  if (locks > 0)
    {
      GQueue *queue;

      queue = g_hash_table_lookup (blocked_reqs, path);

      if (queue == NULL)
        {
          queue = g_queue_new ();
          g_hash_table_insert (blocked_reqs, g_strdup (path), queue);
        }

      _mcd_request_start_delay (self);
      g_queue_push_tail (queue, self);

      return TRUE;
    }

  return FALSE;
}

void
_mcd_request_proceed (McdRequest *self,
    DBusGMethodInvocation *context)
{
  McdConnection *connection = NULL;
  McdPluginRequest *plugin_api = NULL;
  gboolean urgent = FALSE;
  gboolean blocked = FALSE;
  const GList *mini_plugins;

  if (self->proceeding)
    {
      GError na = { TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Proceed has already been called; stop calling it" };

      if (context != NULL)
        dbus_g_method_return_error (context, &na);

      return;
    }

  self->proceeding = TRUE;

  tp_clear_pointer (&context, tp_svc_channel_request_return_from_proceed);

  connection = mcd_account_get_connection (self->account);

  if (connection != NULL)
    {
      const gchar *name =
        tp_asv_get_string (self->properties, TP_PROP_CHANNEL_TARGET_ID);

      if (name != NULL)
        {
          urgent = _mcd_connection_target_id_is_urgent (connection, name);
        }
      else
        {
          guint handle = tp_asv_get_uint32 (self->properties,
              TP_PROP_CHANNEL_TARGET_HANDLE, NULL);

          urgent = _mcd_connection_target_handle_is_urgent (connection, handle);
        }
    }

  /* urgent calls (eg emergency numbers) are not subject to policy *
   * delays: they automatically pass go and collect 200 qwatloos   */
  if (urgent)
    goto proceed;

  /* requests can pick up an extra delay (and ref) here */
  blocked = _queue_blocked_requests (self);

  if (blocked)
    DEBUG ("Request delayed in favour of internal request on %s",
        mcd_account_get_object_path (self->account));

  /* now regular request policy plugins get their shot at denying/delaying */
  for (mini_plugins = _mcd_request_policy_plugins ();
       mini_plugins != NULL;
       mini_plugins = mini_plugins->next)
    {
      DEBUG ("Checking request with policy");

      /* Lazily create a plugin-API object if anything cares */
      if (plugin_api == NULL)
        {
          plugin_api = _mcd_plugin_request_new (self->account, self);
        }

      mcp_request_policy_check (mini_plugins->data, MCP_REQUEST (plugin_api));
    }

  /* this is paired with the delay set when the request was created */
 proceed:
  _mcd_request_end_delay (self);

  tp_clear_object (&plugin_api);
}

GHashTable *
_mcd_request_get_properties (McdRequest *self)
{
  return self->properties;
}

GVariant *
mcd_request_dup_properties (McdRequest *self)
{
  GValue value = G_VALUE_INIT;
  GVariant *ret;

  g_value_init (&value, TP_HASH_TYPE_STRING_VARIANT_MAP);
  g_value_set_boxed (&value, self->properties);
  ret = dbus_g_value_build_g_variant (&value);
  g_value_unset (&value);
  return g_variant_ref_sink (ret);
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

static void
_mcd_request_clean_up (McdRequest *self)
{
  tp_clear_object (&self->predicted_handler);
  tp_dbus_daemon_unregister_object (self->dbus_daemon, self);
}

void
_mcd_request_set_success (McdRequest *self,
    TpChannel *channel)
{
  g_return_if_fail (TP_IS_CHANNEL (channel));

  if (!self->is_complete)
    {
      /* might be used for the connection's properties in future; empty
       * for now */
      GHashTable *future_conn_props = g_hash_table_new (g_str_hash,
          g_str_equal);
      GVariant *variant;
      GValue value = G_VALUE_INIT;

      DEBUG ("Request succeeded");
      self->is_complete = TRUE;
      self->cancellable = FALSE;

      variant = tp_channel_dup_immutable_properties (channel);
      dbus_g_value_parse_g_variant (variant, &value);
      g_assert (G_VALUE_HOLDS (&value, TP_HASH_TYPE_STRING_VARIANT_MAP));

      tp_svc_channel_request_emit_succeeded_with_channel (self,
          tp_proxy_get_object_path (tp_channel_get_connection (channel)),
          future_conn_props,
          tp_proxy_get_object_path (channel),
          g_value_get_boxed (&value));
      tp_svc_channel_request_emit_succeeded (self);

      g_hash_table_unref (future_conn_props);
      g_value_unset (&value);
      g_variant_unref (variant);

      _mcd_request_clean_up (self);
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

      if (self->predicted_handler != NULL)
        {
          /* no callback, as we don't really care: this method call acts as a
           * pseudo-signal */
          DEBUG ("calling RemoveRequest on %s for %s",
                 tp_proxy_get_object_path (self->predicted_handler),
                 self->object_path);
          tp_cli_client_interface_requests_call_remove_request (
              self->predicted_handler, -1, self->object_path, err_string,
              message, NULL, NULL, NULL, NULL);
        }

      tp_svc_channel_request_emit_failed (self, err_string, message);

      g_free (err_string);

      _mcd_request_clean_up (self);
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
channel_request_proceed (TpSvcChannelRequest *iface,
    DBusGMethodInvocation *context)
{
  McdRequest *self = MCD_REQUEST (iface);

  _mcd_request_proceed (self, context);
}

static void
request_iface_init (TpSvcChannelRequestClass *iface)
{
#define IMPLEMENT(x) tp_svc_channel_request_implement_##x (\
    iface, channel_request_##x)
  IMPLEMENT (proceed);
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
      TP_IFACE_CHANNEL_REQUEST, "Hints",
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
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "ChannelRequest is no longer cancellable");
      return FALSE;
    }
}

static TpClient *
guess_request_handler (McdRequest *self)
{
  GList *sorted_handlers;
  GVariant *properties;

  if (!tp_str_empty (self->preferred_handler))
    {
      McdClientProxy *client = _mcd_client_registry_lookup (
          self->clients, self->preferred_handler);

        if (client != NULL)
            return (TpClient *) client;
    }

  properties = mcd_request_dup_properties (self);
  sorted_handlers = _mcd_client_registry_list_possible_handlers (
      self->clients, self->preferred_handler, properties,
      NULL, NULL);
  g_variant_unref (properties);

  if (sorted_handlers != NULL)
    {
      McdClientProxy *first = sorted_handlers->data;

      g_list_free (sorted_handlers);
      return (TpClient *) first;
    }

  return NULL;
}

void
_mcd_request_predict_handler (McdRequest *self)
{
  GHashTable *properties;
  TpClient *predicted_handler;

  g_return_if_fail (!self->is_complete);
  g_return_if_fail (self->predicted_handler == NULL);

  predicted_handler = guess_request_handler (self);

  if (!predicted_handler)
    {
      /* No handler found. But it's possible that by the time that the
       * channel will be created some handler will have popped up, so we
       * must not destroy it. */
      DEBUG ("No known handler for request %s", self->object_path);
      return;
    }

  if (!tp_proxy_has_interface_by_id (predicted_handler,
      TP_IFACE_QUARK_CLIENT_INTERFACE_REQUESTS))
    {
      DEBUG ("Default handler %s for request %s doesn't want AddRequest",
             tp_proxy_get_bus_name (predicted_handler), self->object_path);
      return;
    }

  DEBUG ("Calling AddRequest on default handler %s for request %s",
      tp_proxy_get_bus_name (predicted_handler), self->object_path);

  properties = _mcd_request_dup_immutable_properties (self);
  tp_cli_client_interface_requests_call_add_request (predicted_handler, -1,
      self->object_path, properties,
      NULL, NULL, NULL, NULL);
  g_hash_table_unref (properties);

  /* Remember it so we can call RemoveRequest when appropriate */
  self->predicted_handler = g_object_ref (predicted_handler);
}
