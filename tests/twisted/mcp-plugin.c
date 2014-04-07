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
    McpDispatchOperationDelay *dispatch_operation_delay;
    McpRequest *request;
    McpRequestDelay *request_delay;
    GSimpleAsyncResult *result;
} PermissionContext;

static void
permission_context_free (gpointer p)
{
  PermissionContext *ctx = p;

  if (ctx->dispatch_operation_delay != NULL)
    mcp_dispatch_operation_end_delay (ctx->dispatch_operation,
        ctx->dispatch_operation_delay);

  if (ctx->request_delay != NULL)
    mcp_request_end_delay (ctx->request, ctx->request_delay);

  if (ctx->result != NULL)
    {
      g_simple_async_result_complete_in_idle (ctx->result);
      g_object_unref (ctx->result);
    }

  g_clear_object (&ctx->dispatch_operation);
  g_clear_object (&ctx->request);
  g_slice_free (PermissionContext, ctx);
}

static void
permission_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer data)
{
  GDBusConnection *conn = G_DBUS_CONNECTION (source_object);
  PermissionContext *ctx = data;
  GVariant *tuple;

  /* In a real implementation you'd probably take the error from the
   * error reply, or even from a "successful" reply's parameters,
   * but this is a simple regression test so we don't bother. */

  tuple = g_dbus_connection_call_finish (conn, result, NULL);

  if (tuple == NULL)
    {
      DEBUG ("Permission denied");

      if (ctx->result != NULL)
        {
          g_simple_async_result_set_error (ctx->result, TP_ERROR,
              TP_ERROR_PERMISSION_DENIED, "No, sorry");
        }
      else
        {
          mcp_dispatch_operation_destroy_channels (ctx->dispatch_operation,
              TRUE);
        }
    }
  else
    {
      DEBUG ("Permission granted");
      g_variant_unref (tuple);
    }

  permission_context_free (ctx);
}

static void
test_permission_plugin_check_cdo (McpDispatchOperationPolicy *policy,
    McpDispatchOperation *dispatch_operation)
{
  GVariant *properties = mcp_dispatch_operation_ref_nth_channel_properties (
      dispatch_operation, 0);

  DEBUG ("enter");

  if (properties == NULL)
    {
      DEBUG ("no channels!?");
      return;
    }

  /* currently this example just checks the first channel */

  if (!tp_strdiff (tp_vardict_get_string (properties,
          TP_IFACE_CHANNEL ".TargetID"),
        "policy@example.net"))
    {
      GError *error = NULL;
      GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
      PermissionContext *ctx;

      g_assert_no_error (error);

      ctx = g_slice_new0 (PermissionContext);
      ctx->dispatch_operation = g_object_ref (dispatch_operation);
      ctx->dispatch_operation_delay = mcp_dispatch_operation_start_delay (
          dispatch_operation);

      g_dbus_connection_call (bus,
          "com.example.Policy", "/com/example/Policy", "com.example.Policy",
          "RequestPermission",
          /* in a real policy-mechanism you'd give some details, like the
           * channel's properties or object path, but this is a simple
           * regression test so we don't bother */
          NULL,
          NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, permission_cb, ctx);

      DEBUG ("Waiting for permission");
      g_object_unref (bus);
  }

  g_variant_unref (properties);
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
  GVariant *properties = mcp_dispatch_operation_ref_nth_channel_properties (
      dispatch_operation, 0);

  DEBUG ("enter");

  if (properties == NULL)
    {
      DEBUG ("no channels!?");
      goto finally;
    }

  /* currently this example just checks the first channel */

  if (!tp_strdiff (tp_vardict_get_string (properties,
          TP_IFACE_CHANNEL ".TargetID"),
        "policy@example.net"))
    {
      GError *error = NULL;
      GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
      PermissionContext *ctx;

      g_assert_no_error (error);

      ctx = g_slice_new0 (PermissionContext);
      ctx->dispatch_operation = g_object_ref (dispatch_operation);
      ctx->result = simple;
      /* take ownership */
      simple = NULL;

      g_dbus_connection_call (bus,
          "com.example.Policy", "/com/example/Policy", "com.example.Policy",
          "CheckHandler",
          /* in a real policy-mechanism you'd give some details, like the
           * channel's properties or object path, and the name of the
           * handler */
          NULL,
          NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, permission_cb, ctx);
      g_object_unref (bus);
  }

finally:
  g_clear_pointer (&properties, g_variant_unref);

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

static void
test_permission_plugin_check_request (McpRequestPolicy *policy,
    McpRequest *request)
{
  DEBUG ("%s", G_STRFUNC);

  if (mcp_request_find_request_by_type (request,
        0, g_quark_from_static_string ("com.example.QuestionableChannel"),
        NULL, NULL))
    {
      GError *error = NULL;
      GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
      PermissionContext *ctx;

      g_assert_no_error (error);

      DEBUG ("Questionable channel detected, asking for permission");

      ctx = g_slice_new0 (PermissionContext);
      ctx->request = g_object_ref (request);
      ctx->request_delay = mcp_request_start_delay (request);

      g_dbus_connection_call (bus,
          "com.example.Policy", "/com/example/Policy", "com.example.Policy",
          "RequestRequest",
          /* in a real policy-mechanism you'd give some details, like the
           * channel's properties or object path, but this is a simple
           * regression test so we don't bother */
          NULL,
          NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, permission_cb, ctx);
      g_object_unref (bus);
    }
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
  GVariant *properties = mcp_dispatch_operation_ref_nth_channel_properties (
      dispatch_operation, 0);
  const gchar *target_id;

  DEBUG ("enter");

  if (properties == NULL)
    {
      DEBUG ("no channels!?");
      return;
    }

  /* currently this example just checks the first channel */

  target_id = tp_vardict_get_string (properties, TP_IFACE_CHANNEL ".TargetID");

  if (!tp_strdiff (target_id, "rick.astley@example.net"))
    {
      DEBUG ("rickrolling detected, destroying channels immediately!");
      mcp_dispatch_operation_destroy_channels (dispatch_operation, FALSE);
    }
  else if (!tp_strdiff (target_id, "mc.hammer@example.net"))
    {
      DEBUG ("MC Hammer detected, destroying channels when observers have run");
      mcp_dispatch_operation_destroy_channels (dispatch_operation, TRUE);
    }

  g_variant_unref (properties);
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
  GVariant *properties = mcp_request_ref_nth_request (request, 0);

  DEBUG ("%s", G_STRFUNC);

  if (!tp_strdiff (
        tp_vardict_get_string (properties, TP_IFACE_CHANNEL ".ChannelType"),
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

  g_variant_unref (properties);
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
