/* Mission Control plugin API - DBus Caller ID.
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

#ifndef MCP_DBUS_ACL_H
#define MCP_DBUS_ACL_H

#ifndef _MCP_IN_MISSION_CONTROL_PLUGINS_H
#error Use <mission-control-plugins/mission-control-plugins.h> instead
#endif

#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

/* API for plugins to implement */
typedef struct _McpDBusAcl McpDBusAcl;
typedef struct _McpDBusAclIface McpDBusAclIface;

#define MCP_TYPE_DBUS_ACL (mcp_dbus_acl_get_type ())

#define MCP_DBUS_ACL(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCP_TYPE_DBUS_ACL, McpDBusAcl))

#define MCP_IS_DBUS_ACL(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCP_TYPE_DBUS_ACL))

#define MCP_DBUS_ACL_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), MCP_TYPE_DBUS_ACL, \
                                  McpDBusAclIface))

GType mcp_dbus_acl_get_type (void) G_GNUC_CONST;

/* FIXME: when we break API, rename all types to start with Mcp */

typedef void (*DBusAclAuthorised) (DBusGMethodInvocation *call, gpointer data);

typedef enum {
  DBUS_ACL_TYPE_UNKNOWN = 0,
  DBUS_ACL_TYPE_METHOD,
  DBUS_ACL_TYPE_GET_PROPERTY,
  DBUS_ACL_TYPE_SET_PROPERTY,
} DBusAclType;

typedef struct {
  McpDBusAcl *acl;
  const GList *next_acl;
  DBusGMethodInvocation *context;
  DBusAclType type;
  gchar *name;
  GHashTable *params;
  TpDBusDaemon *dbus;
  DBusAclAuthorised handler;
  gpointer data;
  GDestroyNotify cleanup;
} DBusAclAuthData;

typedef gboolean (*DBusAclAuthoriser) (const McpDBusAcl *,
    const TpDBusDaemon *,
    const DBusGMethodInvocation *,
    DBusAclType type,
    const gchar *name,
    const GHashTable *);

typedef void (*DBusAclAsyncAuthoriser) (const McpDBusAcl *,
    DBusAclAuthData *);

/* real functions that handle multi-plugin acl logic  */
void mcp_dbus_acl_authorised_async_step (DBusAclAuthData *ad,
    gboolean permitted);

void mcp_dbus_acl_authorised_async (TpDBusDaemon *dbus,
    DBusGMethodInvocation *context,
    DBusAclType type,
    const gchar *name,
    GHashTable *params,
    DBusAclAuthorised handler,
    gpointer data,
    GDestroyNotify cleanup);

gboolean mcp_dbus_acl_authorised (const TpDBusDaemon *dbus,
    DBusGMethodInvocation *context,
    DBusAclType type,
    const gchar *name,
    const GHashTable *params);

/* virtual methods */
void mcp_dbus_acl_iface_set_name (McpDBusAclIface *iface,
    const gchar *name);

void mcp_dbus_acl_iface_set_desc (McpDBusAclIface *iface,
    const gchar *desc);

void mcp_dbus_acl_iface_implement_authorised (McpDBusAclIface *iface,
    DBusAclAuthoriser method);

void mcp_dbus_acl_iface_implement_authorised_async (McpDBusAclIface *iface,
    DBusAclAsyncAuthoriser method);

const gchar *mcp_dbus_acl_name (const McpDBusAcl *acl);

const gchar *mcp_dbus_acl_description (const McpDBusAcl *acl);

struct _McpDBusAclIface
{
  GTypeInterface parent;

  const gchar *name;
  const gchar *desc;

  DBusAclAuthoriser authorised;
  DBusAclAsyncAuthoriser authorised_async;
};

G_END_DECLS

#endif
