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

#ifndef _MCP_IN_MISSION_CONTROL_PLUGINS_H
#error Use <mission-control-plugins/mission-control-plugins.h> instead
#endif

G_BEGIN_DECLS

#define MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_READONLY -1
#define MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_DEFAULT   0
#define MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_NORMAL    100
#define MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_KEYRING   10000

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
void
mcp_account_storage_iface_set_priority (McpAccountStorageIface *iface,
    guint prio);

void mcp_account_storage_iface_set_name (McpAccountStorageIface *iface,
    const gchar *name);

void mcp_account_storage_iface_set_desc (McpAccountStorageIface *iface,
    const gchar *desc);

void mcp_account_storage_iface_implement_get (McpAccountStorageIface *iface,
    gboolean (*method) (
        const McpAccountStorage *,
        const McpAccountManager *,
        const gchar *,
        const gchar *));

void mcp_account_storage_iface_implement_set (McpAccountStorageIface *iface,
    gboolean (*method) (
        const McpAccountStorage *,
        const McpAccountManager *,
        const gchar *,
        const gchar *,
        const gchar *));

void mcp_account_storage_iface_implement_delete (McpAccountStorageIface *iface,
    gboolean (*method) (
        const McpAccountStorage *,
        const McpAccountManager *,
        const gchar *,
        const gchar *));

void mcp_account_storage_iface_implement_list (McpAccountStorageIface *iface,
    GList * (*method) (
        const McpAccountStorage *,
        const McpAccountManager *));

void
mcp_account_storage_iface_implement_commit (McpAccountStorageIface *iface,
    gboolean (*method) (
        const McpAccountStorage *,
        const McpAccountManager *));

void
mcp_account_storage_iface_implement_commit_one (McpAccountStorageIface *iface,
    gboolean (*method) (
        const McpAccountStorage *,
        const McpAccountManager *,
        const gchar *));

void
mcp_account_storage_iface_implement_ready (McpAccountStorageIface *iface,
    void (*method) (
        const McpAccountStorage *storage,
        const McpAccountManager *am));

gint mcp_account_storage_priority (const McpAccountStorage *storage);

gboolean mcp_account_storage_get (const McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account,
    const gchar *key);

gboolean mcp_account_storage_set (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key,
    const gchar *val);

gboolean mcp_account_storage_delete (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key);

void mcp_account_storage_ready (const McpAccountStorage *storage,
    const McpAccountManager *am);

gboolean
mcp_account_storage_commit (const McpAccountStorage *storage,
    const McpAccountManager *am);

gboolean
mcp_account_storage_commit_one (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account);

GList *mcp_account_storage_list (const McpAccountStorage *storage,
    const McpAccountManager *am);

const gchar *mcp_account_storage_name (const McpAccountStorage *storage);

const gchar *mcp_account_storage_description (const McpAccountStorage *storage);

G_END_DECLS

#endif
