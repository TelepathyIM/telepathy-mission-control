/*
 * A demonstration plugin that acts as a channel filter.
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
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

#include <mission-control-plugins/mission-control-plugins.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#define DEBUG g_debug

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

G_DEFINE_TYPE_WITH_CODE (TestPermissionPlugin, test_permission_plugin,
    G_TYPE_OBJECT,
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
} PermissionContext;

static void
permission_context_free (gpointer p)
{
  PermissionContext *ctx = p;

  mcp_dispatch_operation_end_delay (ctx->dispatch_operation, ctx->delay);
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
      mcp_dispatch_operation_leave_channels (ctx->dispatch_operation,
          TRUE, TP_CHANNEL_GROUP_CHANGE_REASON_PERMISSION_DENIED,
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
cdo_policy_iface_init (McpDispatchOperationPolicyIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  mcp_dispatch_operation_policy_iface_implement_check (iface,
      test_permission_plugin_check_cdo);
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
      mcp_dispatch_operation_leave_channels (dispatch_operation, TRUE,
          TP_CHANNEL_GROUP_CHANGE_REASON_PERMISSION_DENIED,
          "Can't touch this");
    }

  g_hash_table_unref (properties);
}

static void
rej_cdo_policy_iface_init (McpDispatchOperationPolicyIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  mcp_dispatch_operation_policy_iface_implement_check (iface,
      test_rejection_plugin_check_cdo);
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
      mcp_request_deny (request, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
          "No, you don't");
    }

  if (mcp_request_find_request_by_type (request,
        0, g_quark_from_static_string ("com.example.ForbiddenChannel"),
        NULL, NULL))
    {
      DEBUG ("Forbidden channel detected, denying request");
      mcp_request_deny (request, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
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
      return g_object_new (test_permission_plugin_get_type (),
          NULL);

    case 1:
      return g_object_new (test_rejection_plugin_get_type (),
          NULL);

    default:
      return NULL;
    }
}
