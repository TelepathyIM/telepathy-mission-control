/*
 * A demonstration plugin that acts as a channel filter.
 *
 * Copyright © 2008-2009 Nokia Corporation
 * Copyright © 2009-2010 Collabora Ltd.
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

#undef MC_DISABLE_DEPRECATED
#include "config.h"

#include <mission-control-plugins/mission-control-plugins.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "dbus-account-plugin.h"

#define DEBUG g_debug

/* ------ TestNoOpPlugin -------------------------------------- */
/* doesn't implement anything, to check that NULL pointers are OK */

typedef struct {
    GObject parent;
} TestNoOpPlugin;

typedef struct {
    GObjectClass parent_class;
} TestNoOpPluginClass;

GType test_no_op_plugin_get_type (void) G_GNUC_CONST;

G_DEFINE_TYPE_WITH_CODE (TestNoOpPlugin, test_no_op_plugin,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_DBUS_ACL, NULL);
    G_IMPLEMENT_INTERFACE (MCP_TYPE_REQUEST_POLICY, NULL);
    G_IMPLEMENT_INTERFACE (MCP_TYPE_DISPATCH_OPERATION_POLICY, NULL))

static void
test_no_op_plugin_init (TestNoOpPlugin *self)
{
}

static void
test_no_op_plugin_class_init (TestNoOpPluginClass *cls)
{
}

/* ------ TestPermissionPlugin -------------------------------------- */

typedef struct {
    GObject parent;
} TestPermissionPlugin;

typedef struct {
    GObjectClass parent_class;
} TestPermissionPluginClass;

GType test_permission_plugin_get_type (void) G_GNUC_CONST;
static void cdo_policy_iface_init (McpDispatchOperationPolicyIface *,
    gpointer);
static void req_policy_iface_init (McpRequestPolicyIface *, gpointer);

G_DEFINE_TYPE_WITH_CODE (TestPermissionPlugin, test_permission_plugin,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_REQUEST_POLICY,
      req_policy_iface_init);
    G_IMPLEMENT_INTERFACE (MCP_TYPE_DISPATCH_OPERATION_POLICY,
      cdo_policy_iface_init))

static void
test_permission_plugin_init (TestPermissionPlugin *self)
{
}

static void
test_permission_plugin_class_init (TestPermissionPluginClass *cls)
{
}

typedef struct {
    McpDispatchOperation *dispatch_operation;
    McpDispatchOperationDelay *delay;
    GSimpleAsyncResult *result;
} PermissionContext;

static void
permission_context_free (gpointer p)
{
  PermissionContext *ctx = p;

  if (ctx->delay != NULL)
    mcp_dispatch_operation_end_delay (ctx->dispatch_operation, ctx->delay);

  if (ctx->result != NULL)
    {
      g_simple_async_result_complete_in_idle (ctx->result);
      g_object_unref (ctx->result);
    }

  g_object_unref (ctx->dispatch_operation);
  g_slice_free (PermissionContext, ctx);
}

static void
permission_cb (DBusPendingCall *pc,
    gpointer data)
{
  PermissionContext *ctx = data;
  DBusMessage *message = dbus_pending_call_steal_reply (pc);

  if (dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_ERROR)
    {
      DEBUG ("Permission denied");

      if (ctx->result != NULL)
        {
          g_simple_async_result_set_error (ctx->result, TP_ERROR,
              TP_ERROR_PERMISSION_DENIED, "No, sorry");
        }
      else
        {
          G_GNUC_BEGIN_IGNORE_DEPRECATIONS
          mcp_dispatch_operation_leave_channels (ctx->dispatch_operation,
              TRUE, TP_CHANNEL_GROUP_CHANGE_REASON_PERMISSION_DENIED,
              "Computer says no");
          G_GNUC_END_IGNORE_DEPRECATIONS
        }
    }
  else
    {
      DEBUG ("Permission granted");
    }

  dbus_message_unref (message);
  dbus_pending_call_unref (pc);
}

static void
test_permission_plugin_check_cdo (McpDispatchOperationPolicy *policy,
    McpDispatchOperation *dispatch_operation)
{
  GHashTable *properties = mcp_dispatch_operation_ref_nth_channel_properties (
      dispatch_operation, 0);
  PermissionContext *ctx = NULL;

  DEBUG ("enter");

  if (properties == NULL)
    {
      DEBUG ("no channels!?");
      return;
    }

  /* currently this example just checks the first channel */

  if (!tp_strdiff (tp_asv_get_string (properties,
          TP_IFACE_CHANNEL ".TargetID"),
        "policy@example.net"))
    {
      TpDBusDaemon *dbus_daemon = tp_dbus_daemon_dup (NULL);
      DBusGConnection *gconn = tp_proxy_get_dbus_connection (dbus_daemon);
      DBusConnection *libdbus = dbus_g_connection_get_connection (gconn);
      DBusPendingCall *pc = NULL;
      DBusMessage *message;

      ctx = g_slice_new0 (PermissionContext);
      ctx->dispatch_operation = g_object_ref (dispatch_operation);
      ctx->delay = mcp_dispatch_operation_start_delay (dispatch_operation);

      /* in a real policy-mechanism you'd give some details, like the
       * channel's properties or object path */
      message = dbus_message_new_method_call ("com.example.Policy",
          "/com/example/Policy", "com.example.Policy", "RequestPermission");

      if (!dbus_connection_send_with_reply (libdbus, message,
            &pc, -1))
        {
          g_error ("out of memory");
        }

      dbus_message_unref (message);

      if (pc == NULL)
        {
          DEBUG ("got disconnected from D-Bus...");

          goto finally;
        }

      /* pc is unreffed by permission_cb */

      DEBUG ("Waiting for permission");

      if (dbus_pending_call_get_completed (pc))
        {
          permission_cb (pc, ctx);
          goto finally;
        }

      if (!dbus_pending_call_set_notify (pc, permission_cb, ctx,
            permission_context_free))
        {
          g_error ("Out of memory");
        }

      /* ctx will be freed later */
      ctx = NULL;
  }

finally:
  if (ctx != NULL)
    permission_context_free (ctx);

  g_hash_table_unref (properties);
}

static void
handler_is_suitable_async (McpDispatchOperationPolicy *self,
    TpClient *recipient,
    const gchar *unique_name,
    McpDispatchOperation *dispatch_operation,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = g_simple_async_result_new ((GObject *) self,
      callback, user_data, handler_is_suitable_async);
  GHashTable *properties = mcp_dispatch_operation_ref_nth_channel_properties (
      dispatch_operation, 0);
  PermissionContext *ctx = NULL;

  DEBUG ("enter");

  if (properties == NULL)
    {
      DEBUG ("no channels!?");
      goto finally;
    }

  /* currently this example just checks the first channel */

  if (!tp_strdiff (tp_asv_get_string (properties,
          TP_IFACE_CHANNEL ".TargetID"),
        "policy@example.net"))
    {
      TpDBusDaemon *dbus_daemon = tp_dbus_daemon_dup (NULL);
      DBusGConnection *gconn = tp_proxy_get_dbus_connection (dbus_daemon);
      DBusConnection *libdbus = dbus_g_connection_get_connection (gconn);
      DBusPendingCall *pc = NULL;
      DBusMessage *message;

      ctx = g_slice_new0 (PermissionContext);
      ctx->dispatch_operation = g_object_ref (dispatch_operation);
      ctx->result = simple;
      /* take ownership */
      simple = NULL;

      /* in a real policy-mechanism you'd give some details, like the
       * channel's properties or object path, and the name of the handler */
      message = dbus_message_new_method_call ("com.example.Policy",
          "/com/example/Policy", "com.example.Policy", "CheckHandler");

      if (!dbus_connection_send_with_reply (libdbus, message,
            &pc, -1))
        {
          g_error ("out of memory");
        }

      dbus_message_unref (message);

      if (pc == NULL)
        {
          DEBUG ("got disconnected from D-Bus...");

          goto finally;
        }

      /* pc is unreffed by permission_cb */

      DEBUG ("Waiting for permission");

      if (dbus_pending_call_get_completed (pc))
        {
          permission_cb (pc, ctx);
          goto finally;
        }

      if (!dbus_pending_call_set_notify (pc, permission_cb, ctx,
            permission_context_free))
        {
          g_error ("Out of memory");
        }

      /* ctx will be freed later */
      ctx = NULL;
  }

finally:
  if (ctx != NULL)
    permission_context_free (ctx);

  g_hash_table_unref (properties);

  if (simple != NULL)
    {
      g_simple_async_result_complete_in_idle (simple);
      g_object_unref (simple);
    }
}

static void
cdo_policy_iface_init (McpDispatchOperationPolicyIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  iface->check = test_permission_plugin_check_cdo;

  iface->handler_is_suitable_async = handler_is_suitable_async;
  /* the default finish function accepts our GSimpleAsyncResult */
}

typedef struct {
    McpRequest *request;
    McpRequestDelay *delay;
} RequestPermissionContext;

static void
request_permission_context_free (gpointer p)
{
  RequestPermissionContext *ctx = p;

  mcp_request_end_delay (ctx->request, ctx->delay);
  g_object_unref (ctx->request);
  g_slice_free (RequestPermissionContext, ctx);
}

static void
request_permission_cb (DBusPendingCall *pc,
    gpointer data)
{
  RequestPermissionContext *ctx = data;
  DBusMessage *message = dbus_pending_call_steal_reply (pc);

  if (dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_ERROR)
    {
      DEBUG ("Permission denied");
      mcp_request_deny (ctx->request,
          TP_ERROR, TP_CHANNEL_GROUP_CHANGE_REASON_PERMISSION_DENIED,
          "Computer says no");
    }
  else
    {
      DEBUG ("Permission granted");
    }

  dbus_message_unref (message);
  dbus_pending_call_unref (pc);
}

static void
test_permission_plugin_check_request (McpRequestPolicy *policy,
    McpRequest *request)
{
  GHashTable *properties = mcp_request_ref_nth_request (request, 0);
  RequestPermissionContext *ctx = NULL;

  DEBUG ("%s", G_STRFUNC);

  if (mcp_request_find_request_by_type (request,
        0, g_quark_from_static_string ("com.example.QuestionableChannel"),
        NULL, NULL))
    {
      TpDBusDaemon *dbus_daemon = tp_dbus_daemon_dup (NULL);
      DBusGConnection *gconn = tp_proxy_get_dbus_connection (dbus_daemon);
      DBusConnection *libdbus = dbus_g_connection_get_connection (gconn);
      DBusPendingCall *pc = NULL;
      DBusMessage *message;

      DEBUG ("Questionable channel detected, asking for permission");

      ctx = g_slice_new0 (RequestPermissionContext);
      ctx->request = g_object_ref (request);
      ctx->delay = mcp_request_start_delay (request);

      /* in a real policy-mechanism you'd give some details, like the
       * channel's properties or object path */
      message = dbus_message_new_method_call ("com.example.Policy",
          "/com/example/Policy", "com.example.Policy", "RequestRequest");

      if (!dbus_connection_send_with_reply (libdbus, message,
            &pc, -1))
        {
          g_error ("out of memory");
        }

      dbus_message_unref (message);

      if (pc == NULL)
        {
          DEBUG ("got disconnected from D-Bus...");

          goto finally;
        }

      /* pc is unreffed by permission_cb */

      DEBUG ("Waiting for permission");

      if (dbus_pending_call_get_completed (pc))
        {
          request_permission_cb (pc, ctx);
          goto finally;
        }

      if (!dbus_pending_call_set_notify (pc, request_permission_cb, ctx,
            request_permission_context_free))
        {
          g_error ("Out of memory");
        }

      /* ctx will be freed later */
      ctx = NULL;
    }

finally:
  if (ctx != NULL)
    request_permission_context_free (ctx);

  g_hash_table_unref (properties);
}

static void
req_policy_iface_init (McpRequestPolicyIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  mcp_request_policy_iface_implement_check (iface,
      test_permission_plugin_check_request);
}

/* ------ TestRejectionPlugin --------------------------------------- */

typedef struct {
    GObject parent;
} TestRejectionPlugin;

typedef struct {
    GObjectClass parent_class;
} TestRejectionPluginClass;

GType test_rejection_plugin_get_type (void) G_GNUC_CONST;
static void rej_cdo_policy_iface_init (McpDispatchOperationPolicyIface *,
    gpointer);
static void rej_req_policy_iface_init (McpRequestPolicyIface *,
    gpointer);

G_DEFINE_TYPE_WITH_CODE (TestRejectionPlugin, test_rejection_plugin,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_REQUEST_POLICY,
      rej_req_policy_iface_init);
    G_IMPLEMENT_INTERFACE (MCP_TYPE_DISPATCH_OPERATION_POLICY,
      rej_cdo_policy_iface_init))

static void
test_rejection_plugin_init (TestRejectionPlugin *self)
{
}

static void
test_rejection_plugin_class_init (TestRejectionPluginClass *cls)
{
}

static void
test_rejection_plugin_check_cdo (McpDispatchOperationPolicy *policy,
    McpDispatchOperation *dispatch_operation)
{
  GHashTable *properties = mcp_dispatch_operation_ref_nth_channel_properties (
      dispatch_operation, 0);
  const gchar *target_id;

  DEBUG ("enter");

  if (properties == NULL)
    {
      DEBUG ("no channels!?");
      return;
    }

  /* currently this example just checks the first channel */

  target_id = tp_asv_get_string (properties, TP_IFACE_CHANNEL ".TargetID");

  if (!tp_strdiff (target_id, "rick.astley@example.net"))
    {
      DEBUG ("rickrolling detected, destroying channels immediately!");
      mcp_dispatch_operation_destroy_channels (dispatch_operation, FALSE);
    }
  else if (!tp_strdiff (target_id, "mc.hammer@example.net"))
    {
      DEBUG ("MC Hammer detected, leaving channels when observers have run");
      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      mcp_dispatch_operation_leave_channels (dispatch_operation, TRUE,
          TP_CHANNEL_GROUP_CHANGE_REASON_PERMISSION_DENIED,
          "Can't touch this");
      G_GNUC_END_IGNORE_DEPRECATIONS
    }

  g_hash_table_unref (properties);
}

static void
rej_cdo_policy_iface_init (McpDispatchOperationPolicyIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  iface->check = test_rejection_plugin_check_cdo;
}

static void
test_rejection_plugin_check_request (McpRequestPolicy *policy,
    McpRequest *request)
{
  GHashTable *properties = mcp_request_ref_nth_request (request, 0);

  DEBUG ("%s", G_STRFUNC);

  if (!tp_strdiff (
        tp_asv_get_string (properties, TP_IFACE_CHANNEL ".ChannelType"),
        "com.example.ForbiddenChannel"))
    {
      DEBUG ("Forbidden channel detected, denying request");
      mcp_request_deny (request, TP_ERROR, TP_ERROR_PERMISSION_DENIED,
          "No, you don't");
    }

  if (mcp_request_find_request_by_type (request,
        0, g_quark_from_static_string ("com.example.ForbiddenChannel"),
        NULL, NULL))
    {
      DEBUG ("Forbidden channel detected, denying request");
      mcp_request_deny (request, TP_ERROR, TP_ERROR_PERMISSION_DENIED,
          "No, you don't");
    }

  g_hash_table_unref (properties);
}

static void
rej_req_policy_iface_init (McpRequestPolicyIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  mcp_request_policy_iface_implement_check (iface,
      test_rejection_plugin_check_request);
}

/* ------ Initialization -------------------------------------------- */

GObject *
mcp_plugin_ref_nth_object (guint n)
{
  DEBUG ("Initializing mcp-plugin (n=%u)", n);

  switch (n)
    {
    case 0:
      return g_object_new (test_no_op_plugin_get_type (),
          NULL);

    case 1:
      return g_object_new (test_permission_plugin_get_type (),
          NULL);

    case 2:
      return g_object_new (test_rejection_plugin_get_type (),
          NULL);

    case 3:
      return g_object_new (test_no_op_plugin_get_type (),
          NULL);

    case 4:
      return g_object_new (test_dbus_account_plugin_get_type (),
          NULL);

    default:
      return NULL;
    }
}
