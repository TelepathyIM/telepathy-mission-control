/* Mission Control plugin API - ChannelDispatchOperation policy hook.
 *
 * Copyright (C) 2009 Nokia Corporation
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

/**
 * SECTION:dispatch-operation-policy
 * @title: McpDispatchOperationPolicy
 * @short_description: Dispatch Operation policy object, implemented by plugins
 * @see_also: #McpDispatchOperation
 * @include: mission-control-plugins/mission-control-plugins.h
 *
 * Plugins may implement #McpDispatchOperationPolicy in order to apply policy
 * to Telepathy channel dispatch operations passing through the Channel
 * Dispatcher part of Mission Control. This interface behaves rather like the
 * Observer clients in the Telepathy specification, and has access to the same
 * information, but runs within the Mission Control process rather than being
 * invoked over D-Bus.
 *
 * To do so, the plugin must implement a #GObject subclass that implements
 * #McpDispatchOperationPolicy, then return an instance of that subclass from
 * mcp_plugin_ref_nth_object().
 *
 * A typical plugin might look like this:
 *
 * <example><programlisting>
 * G_DEFINE_TYPE_WITH_CODE (MyPlugin, my_plugin,
 *    G_TYPE_OBJECT,
 *    G_IMPLEMENT_INTERFACE (...);
 *    G_IMPLEMENT_INTERFACE (MCP_TYPE_DISPATCH_OPERATION_POLICY,
 *      cdo_policy_iface_init);
 *    G_IMPLEMENT_INTERFACE (...))
 * /<!-- -->* ... *<!-- -->/
 * static void
 * cdo_policy_iface_init (McpDispatchOperationPolicyIface *iface,
 *     gpointer unused G_GNUC_UNUSED)
 * {
 *   iface-&gt;check = my_plugin_check_cdo;
 *   iface-&gt;handler_is_suitable_async = my_plugin_handler_is_suitable_async;
 *   iface-&gt;handler_is_suitable_finish = my_plugin_handler_is_suitable_finish;
 * }
 * </programlisting></example>
 *
 * A single object can implement more than one interface; for instance, it
 * may be useful to combine this interface with #McpRequestPolicy.
 */

#include "config.h"

#include <mission-control-plugins/mission-control-plugins.h>

GType
mcp_dispatch_operation_policy_get_type (void)
{
  static gsize once = 0;
  static GType type = 0;

  if (g_once_init_enter (&once))
    {
      static const GTypeInfo info = {
          sizeof (McpDispatchOperationPolicyIface),
          NULL, /* base_init */
          NULL, /* base_finalize */
          NULL, /* class_init */
          NULL, /* class_finalize */
          NULL, /* class_data */
          0, /* instance_size */
          0, /* n_preallocs */
          NULL, /* instance_init */
          NULL /* value_table */
      };

      type = g_type_register_static (G_TYPE_INTERFACE,
          "McpDispatchOperationPolicy", &info, 0);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);

      g_once_init_leave (&once, 1);
    }

  return type;
}

/**
 * McpDispatchOperationPolicyIface:
 * @parent: the parent type
 * @check: an implementation of mcp_dispatch_operation_policy_check();
 *    %NULL is equivalent to an implementation that does nothing
 * @handler_is_suitable_async: an implementation of
 *    mcp_dispatch_operation_policy_handler_is_suitable_async();
 *    %NULL is treated as equivalent to an implementation that accepts
 *    every handler, i.e. always asynchronously returns %TRUE
 * @handler_is_suitable_finish: an implementation of
 *    mcp_dispatch_operation_policy_handler_is_suitable_finish();
 *    %NULL is treated as equivalent to an implementation that accepts any
 *    #GSimpleAsyncResult
 */

/**
 * McpDispatchOperationPolicyCb:
 * @policy: an implementation of this interface, provided by a plugin
 * @dispatch_operation: an object representing a dispatch operation, i.e.
 *  a bundle of channels being dispatched
 *
 * Signature of an implementation of mcp_dispatch_operation_policy_check().
 */

/**
 * McpDispatchOperationPolicyHandlerIsSuitableAsync:
 * @policy: an implementation of this interface, provided by a plugin
 * @handler: a proxy for the Handler's D-Bus API, or %NULL if the Handler
 *  is calling Claim (so its well-known name is not immediately obvious)
 * @unique_name: The Handler's unique name, or empty or %NULL if it has not yet
 *  been started
 * @dispatch_operation: an object representing a dispatch operation, i.e.
 *  a bundle of channels being dispatched
 * @callback: callback to be called on success or failure
 * @user_data: user data for the callback
 *
 * Signature of mcp_dispatch_operation_policy_handler_is_suitable_async()
 */

/**
 * McpDispatchOperationPolicyFinisher:
 * @policy: an implementation of this interface, provided by a plugin
 * @result: the asynchronous result passed to a #GAsyncReadyCallback
 * @error: (allow-none): used to return an error
 *
 * Signature of a virtual method used to finish an asynchronous operation
 * that succeeds or fails, but does not return any additional value.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE on error
 */

/**
 * mcp_dispatch_operation_policy_check:
 * @policy: an implementation of this interface, provided by a plugin
 * @dispatch_operation: an object representing a dispatch operation, i.e.
 *  a bundle of channels being dispatched
 *
 * Check what to do with a bundle of channels. Implementations of this method
 * can use methods on @dispatch_operation to examine the channels, delay
 * dispatching, close the channels, etc. in order to impose whatever policy
 * the plugin requires.
 *
 * Mission Control calls this function in each plugin after invoking
 * Observers, but before Approvers, and without waiting for Observers to
 * reply.
 */
void
mcp_dispatch_operation_policy_check (McpDispatchOperationPolicy *policy,
    McpDispatchOperation *dispatch_operation)
{
  McpDispatchOperationPolicyIface *iface =
    MCP_DISPATCH_OPERATION_POLICY_GET_IFACE (policy);

  g_return_if_fail (iface != NULL);

  if (iface->check != NULL)
    iface->check (policy, dispatch_operation);
}

/**
 * mcp_dispatch_operation_policy_iface_implement_check:
 * @iface: the interface
 * @impl: an implementation of the virtual method
 *  mcp_dispatch_operation_policy_check()
 *
 * This method is no longer necessary: just set iface->check = impl instead.
 */
void
mcp_dispatch_operation_policy_iface_implement_check (
    McpDispatchOperationPolicyIface *iface,
    McpDispatchOperationPolicyCb impl)
{
  iface->check = impl;
}

/**
 * mcp_dispatch_operation_policy_handler_is_suitable_async:
 * @policy: an implementation of this interface, provided by a plugin
 * @handler: a proxy for the Handler's D-Bus API, or %NULL if the Handler
 *  is calling Claim (so its well-known name is not immediately obvious)
 * @unique_name: The Handler's unique name, or empty or %NULL if it has not yet
 *  been started
 * @dispatch_operation: an object representing a dispatch operation, i.e.
 *  a bundle of channels being dispatched
 * @callback: callback to be called on success or failure
 * @user_data: user data for the callback
 *
 * Check whether a handler is "suitable" for these channels. For instance,
 * this could be used to ensure that only the platform's default UI can be
 * used for particular channels, even if MC would normally consider
 * a third-party UI to be a better match.
 *
 * Mission Control calls all implementations of this method in parallel
 * and waits for them all to return. If any of them raises an error,
 * the handler is considered to be unsuitable.
 */
void
mcp_dispatch_operation_policy_handler_is_suitable_async (
    McpDispatchOperationPolicy *policy,
    TpClient *handler,
    const gchar *unique_name,
    McpDispatchOperation *dispatch_operation,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  McpDispatchOperationPolicyIface *iface =
    MCP_DISPATCH_OPERATION_POLICY_GET_IFACE (policy);

  g_return_if_fail (iface != NULL);

  if (iface->handler_is_suitable_async != NULL)
    {
      iface->handler_is_suitable_async (policy, handler, unique_name,
          dispatch_operation, callback, user_data);
    }
  else
    {
      /* unimplemented: the default is to succeed */
      GSimpleAsyncResult *simple = g_simple_async_result_new (
          (GObject *) policy, callback, user_data,
          mcp_dispatch_operation_policy_handler_is_suitable_async);

      g_simple_async_result_complete_in_idle (simple);
      g_object_unref (simple);
    }
}

/**
 * mcp_dispatch_operation_policy_handler_is_suitable_finish:
 * @policy: an implementation of this interface, provided by a plugin
 * @result: the asynchronous result passed to the #GAsyncReadyCallback
 * @error: (allow-none): used to return an error
 *
 * Finish a call to mcp_dispatch_operation_policy_handler_is_suitable_async().
 *
 * Returns: %TRUE if the handler is suitable; %FALSE if the handler is
 *  unsuitable or there was an error
 */
gboolean
mcp_dispatch_operation_policy_handler_is_suitable_finish (
    McpDispatchOperationPolicy *policy,
    GAsyncResult *result,
    GError **error)
{
  McpDispatchOperationPolicyIface *iface =
    MCP_DISPATCH_OPERATION_POLICY_GET_IFACE (policy);

  g_return_val_if_fail (iface != NULL, FALSE);

  if (iface->handler_is_suitable_finish != NULL)
    {
      return iface->handler_is_suitable_finish (policy, result, error);
    }
  else
    {
      /* accept any GSimpleAsyncResult regardless of source tag, so we can
       * use it with the default implementation of _async or with most
       * user-supplied implementations */
      g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), FALSE);

      return !g_simple_async_result_propagate_error (
          (GSimpleAsyncResult *) result, error);
    }
}
