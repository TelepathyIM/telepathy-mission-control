/* Mission Control plugin API - Account storage hook.
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

#ifndef MCP_ACCOUNT_STORAGE_H
#define MCP_ACCOUNT_STORAGE_H

#ifndef MCP_IN_MISSION_CONTROL_PLUGINS_H
#error Use <mission-control-plugins/mission-control-plugins.h> instead
#endif

G_BEGIN_DECLS

/* API for plugins to implement */
typedef struct _McpAccountStorage McpAccountStorage;
typedef struct _McpAccountStorageIface McpAccountStorageIface;

#define MCP_TYPE_ACCOUNT_STORAGE (mcp_account_storage_get_type ())

#define MCP_ACCOUNT_STORAGE(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCP_TYPE_ACCOUNT_STORAGE, \
      McpAccountStorage))

#define MCP_IS_ACCOUNT_STORAGE(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCP_TYPE_ACCOUNT_STORAGE))

#define MCP_ACCOUNT_STORAGE_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), MCP_TYPE_ACCOUNT_STORAGE, \
                                  McpAccountStorageIface))

GType mcp_account_storage_get_type (void) G_GNUC_CONST;

/* virtual methods */
gint mcp_account_storage_priority (const McpAccountStorage *storage);

gboolean mcp_account_storage_fetch (const McpAccountStorage *storage,
    GKeyFile *accts,
    const gchar *key);

gboolean mcp_account_storage_store (const McpAccountStorage *storage,
    const GKeyFile *accts,
    const gchar *key);

gboolean mcp_account_storage_remove (const McpAccountStorage *storage,
    const gchar *key);

GList *mcp_account_storage_list (const McpAccountStorage *storage);

void mcp_account_storage_iface_set_priority (McpAccountStorageIface *iface,
    guint prio);

void mcp_account_storage_iface_implement_fetch (McpAccountStorageIface *iface,
    gboolean (*method) (const McpAccountStorage *,
        GKeyFile *,
        const gchar *));

void mcp_account_storage_iface_implement_store (McpAccountStorageIface *iface,
    gboolean (*method) (const McpAccountStorage *,
        const GKeyFile *,
        const gchar *));

void mcp_account_storage_iface_implement_remove (McpAccountStorageIface *iface,
    gboolean (*method) (const McpAccountStorage *, const gchar *));

void mcp_account_storage_iface_implement_list (McpAccountStorageIface *iface,
    GList * (*method) (const McpAccountStorage *));

G_END_DECLS

#endif
