/* Mission Control plugin API - DBus Channel Observer/Handler ACLs
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010 Collabora Ltd.
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

#ifndef MCP_DBUS_CHANNEL_ACL_H
#define MCP_DBUS_CHANNEL_ACL_H

#ifndef _MCP_IN_MISSION_CONTROL_PLUGINS_H
#error Use <mission-control-plugins/mission-control-plugins.h> instead
#endif

#include <mission-control-plugins/dispatch-operation.h>

#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/dbus.h>

G_BEGIN_DECLS

/* API for plugins to implement */
typedef struct _McpDBusChannelAcl McpDBusChannelAcl;
typedef struct _McpDBusChannelAclIface McpDBusChannelAclIface;

#define MCP_TYPE_DBUS_CHANNEL_ACL (mcp_dbus_channel_acl_get_type ())

#define MCP_DBUS_CHANNEL_ACL(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCP_TYPE_DBUS_CHANNEL_ACL, \
      McpDBusChannelAcl))

#define MCP_IS_DBUS_CHANNEL_ACL(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCP_TYPE_DBUS_CHANNEL_ACL))

#define MCP_DBUS_CHANNEL_ACL_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), MCP_TYPE_DBUS_CHANNEL_ACL, \
      McpDBusChannelAclIface))

GType mcp_dbus_channel_acl_get_type (void) G_GNUC_CONST;

typedef gboolean (*DBusChannelAclAuthoriser) (McpDBusChannelAcl *,
    TpProxy *,
    McpDispatchOperation *dispatch_op);

gboolean mcp_dbus_channel_acl_authorised (McpDBusChannelAcl *self,
    TpProxy *recipient,
    McpDispatchOperation *dispatch_op);

/* virtual methods */
void mcp_dbus_channel_acl_iface_set_name (McpDBusChannelAclIface *iface,
    const gchar *name);

void mcp_dbus_channel_acl_iface_set_desc (McpDBusChannelAclIface *iface,
    const gchar *desc);

void mcp_dbus_channel_acl_iface_implement_authorised (
    McpDBusChannelAclIface *iface,
    DBusChannelAclAuthoriser method);

const gchar *mcp_dbus_channel_acl_name (const McpDBusChannelAcl *acl);

const gchar *mcp_dbus_channel_acl_description (const McpDBusChannelAcl *acl);

G_END_DECLS

#endif
