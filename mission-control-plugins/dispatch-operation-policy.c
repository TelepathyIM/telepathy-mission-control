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
 * The contents of the #McpDispatchOperationPolicyIface struct are not public,
 * so to provide an implementation of the check method,
 * plugins should call mcp_dispatch_operation_policy_iface_implement_check()
 * from the interface initialization function, like this:
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
 *   mcp_dispatch_operation_policy_iface_implement_check (iface,
 *       my_plugin_check_cdo);
 * }
 * </programlisting></example>
 *
 * A single object can implement more than one interface; for instance, it
 * may be useful to combine this interface with #McpRequestPolicy.
 */

#include <mission-control-plugins/mission-control-plugins.h>

struct _McpDispatchOperationPolicyIface {
    GTypeInterface parent;

    void (*check) (McpDispatchOperationPolicy *, McpDispatchOperation *);
};

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
 */
void
mcp_dispatch_operation_policy_iface_implement_check (
    McpDispatchOperationPolicyIface *iface,
    void (*impl) (McpDispatchOperationPolicy *, McpDispatchOperation *))
{
  iface->check = impl;
}
