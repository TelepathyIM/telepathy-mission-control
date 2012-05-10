/* Mission Control plugin API - ChannelRequest policy hook.
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
 * SECTION:request-policy
 * @title: McpRequestPolicy
 * @short_description: Request-policy object, implemented by plugins
 * @see_also: #McpRequest
 * @include: mission-control-plugins/mission-control-plugins.h
 *
 * Plugins may implement #McpRequestPolicy in order to apply policy to
 * Telepathy channel requests passing through the Channel Dispatcher part of
 * Mission Control. The plugins are run just after the requesting client calls
 * the ChannelRequest.Proceed method, and can inspect the request, delay its
 * processing, and/or make it fail.
 *
 * To do so, the plugin must implement a #GObject subclass that implements
 * #McpRequestPolicy, then return an instance of that subclass from
 * mcp_plugin_ref_nth_object().
 *
 * An implementation of this plugin might look like this:
 *
 * <example><programlisting>
 * G_DEFINE_TYPE_WITH_CODE (MyPlugin, my_plugin,
 *    G_TYPE_OBJECT,
 *    G_IMPLEMENT_INTERFACE (...);
 *    G_IMPLEMENT_INTERFACE (MCP_TYPE_REQUEST_POLICY,
 *      request_policy_iface_init);
 *    G_IMPLEMENT_INTERFACE (...))
 * /<!-- -->* ... *<!-- -->/
 * static void
 * request_policy_iface_init (McpRequestPolicyIface *iface,
 *     gpointer unused G_GNUC_UNUSED)
 * {
 *   iface-&gt;check = my_plugin_check_request;
 * }
 * </programlisting></example>
 *
 * A single object can implement more than one interface; for instance, it
 * may be useful to combine this interface with #McpDispatchOperationPolicy.
 */

#include "config.h"

#include <mission-control-plugins/mission-control-plugins.h>

/**
 * McpRequestPolicyIface:
 * @parent: the parent type
 * @check: an implementation of mcp_request_policy_check(), or %NULL
 *  to do nothing
 */

GType
mcp_request_policy_get_type (void)
{
  static gsize once = 0;
  static GType type = 0;

  if (g_once_init_enter (&once))
    {
      static const GTypeInfo info = {
          sizeof (McpRequestPolicyIface),
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

      type = g_type_register_static (G_TYPE_INTERFACE, "McpRequestPolicy",
          &info, 0);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);

      g_once_init_leave (&once, 1);
    }

  return type;
}

/**
 * McpRequestPolicyCb:
 * @policy: an implementation of this interface, provided by a plugin
 * @request: an object representing a channel request
 *
 * Signature of an implementation of mcp_request_policy_check().
 */

/**
 * mcp_request_policy_check:
 * @policy: an implementation of this interface, provided by a plugin
 * @request: an object representing a channel request
 *
 * Check what to do with a channel request. Implementations of this method
 * can use methods on @request to examine the request, delay
 * processing, make the request fail, etc. in order to impose whatever policy
 * the plugin requires.
 *
 * Mission Control calls this function in each plugin just after the requesting
 * client calls the Proceed method on the Telepathy ChannelRequest. If the
 * plugin makes the request fail, this does not take effect until all plugins
 * have been notified.
 */
void
mcp_request_policy_check (McpRequestPolicy *policy,
    McpRequest *request)
{
  McpRequestPolicyIface *iface = MCP_REQUEST_POLICY_GET_IFACE (policy);

  g_return_if_fail (iface != NULL);

  if (iface->check != NULL)
    iface->check (policy, request);
}

/**
 * mcp_request_policy_iface_implement_check:
 * @iface: the interface
 * @impl: an implementation of the virtual method mcp_request_policy_check()
 *
 * This function is no longer needed, since #McpRequestPolicyIface is now
 * public API. Use "iface->check = impl" instead.
 */
void
mcp_request_policy_iface_implement_check (McpRequestPolicyIface *iface,
    void (*impl) (McpRequestPolicy *, McpRequest *))
{
  iface->check = impl;
}
