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
 *   iface-&gt;handler_is_suitable = my_plugin_handler_is_suitable;
 * }
 * </programlisting></example>
 *
 * A single object can implement more than one interface; for instance, it
 * may be useful to combine this interface with #McpRequestPolicy.
 */

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
 * @handler_is_suitable: an implementation of
 *    mcp_dispatch_operation_policy_handler_is_suitable();
 *    %NULL is equivalent to an implementation that accepts everything,
 *    i.e. always returns %TRUE
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
 * McpDispatchOperationPolicyClientPredicate:
 * @policy: an implementation of this interface, provided by a plugin
 * @client: a Telepathy Client
 * @dispatch_operation: an object representing a dispatch operation, i.e.
 *  a bundle of channels being dispatched
 *
 * Signature of a virtual method to ask a question about a Client in the
 * context of a dispatch operation, synchronously.
 *
 * Returns: a boolean result
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
 * mcp_dispatch_operation_policy_handler_is_suitable:
 * @policy: an implementation of this interface, provided by a plugin
 * @handler: a proxy for the Handler's D-Bus API
 * @dispatch_operation: an object representing a dispatch operation, i.e.
 *  a bundle of channels being dispatched
 *
 * Check whether a handler is "suitable" for these channels. For instance,
 * this could be used to ensure that only the platform's default UI can be
 * used for particular channels, even if MC would normally consider
 * a third-party UI to be a better match.
 *
 * Mission Control calls all implementations of this method in turn, stopping
 * when one of them returns %FALSE or when all implementations have been
 * called. If they all return %TRUE, the handler is considered to be suitable.
 *
 * Returns: %TRUE if @handler handle @dispatch_operation
 */
gboolean
mcp_dispatch_operation_policy_handler_is_suitable (
    McpDispatchOperationPolicy *policy,
    TpProxy *handler,
    McpDispatchOperation *dispatch_operation)
{
  McpDispatchOperationPolicyIface *iface =
    MCP_DISPATCH_OPERATION_POLICY_GET_IFACE (policy);

  g_return_val_if_fail (iface != NULL, TRUE);

  if (iface->handler_is_suitable != NULL)
    return iface->handler_is_suitable (policy, handler, dispatch_operation);
  else
    return TRUE;
}
