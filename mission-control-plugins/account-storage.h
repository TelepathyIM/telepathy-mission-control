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

/* Virtual method implementation signatures */
typedef gboolean (*McpAccountStorageGetFunc) (
    const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key);
typedef gboolean (*McpAccountStorageSetFunc) (
    const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key,
    const gchar *val);
typedef gchar * (*McpAccountStorageCreate) (
    const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *manager,
    const gchar *protocol,
    GHashTable *params,
    GError **error);
typedef gboolean (*McpAccountStorageDeleteFunc) (
    const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key);
typedef GList * (*McpAccountStorageListFunc) (
    const McpAccountStorage *storage,
    const McpAccountManager *am);
typedef gboolean (*McpAccountStorageCommitFunc) (
    const McpAccountStorage *storage,
    const McpAccountManager *am);
typedef gboolean (*McpAccountStorageCommitOneFunc) (
    const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account);
typedef void (*McpAccountStorageReadyFunc) (
    const McpAccountStorage *storage,
    const McpAccountManager *am);
typedef void (*McpAccountStorageGetIdentifierFunc) (
    const McpAccountStorage *storage,
    const gchar *account,
    GValue *identifier);
typedef GHashTable * (*McpAccountStorageGetAdditionalInfoFunc) (
    const McpAccountStorage *storage,
    const gchar *account);
/* FIXME: when breaking API, make this return TpStorageRestrictionFlags */
typedef guint (*McpAccountStorageGetRestrictionsFunc) (
    const McpAccountStorage *storage,
    const gchar *account);

struct _McpAccountStorageIface
{
  GTypeInterface parent;

  gint priority;
  const gchar *name;
  const gchar *desc;
  const gchar *provider;

  McpAccountStorageSetFunc set;
  McpAccountStorageGetFunc get;
  McpAccountStorageDeleteFunc delete;
  McpAccountStorageCommitFunc commit;
  McpAccountStorageListFunc list;
  McpAccountStorageReadyFunc ready;
  McpAccountStorageCommitOneFunc commit_one;
  McpAccountStorageGetIdentifierFunc get_identifier;
  McpAccountStorageGetAdditionalInfoFunc get_additional_info;
  McpAccountStorageGetRestrictionsFunc get_restrictions;
  McpAccountStorageCreate create;
};

/* functions with which to fill in the vtable */
void mcp_account_storage_iface_set_priority (McpAccountStorageIface *iface,
    guint prio);

void mcp_account_storage_iface_set_name (McpAccountStorageIface *iface,
    const gchar *name);

void mcp_account_storage_iface_set_desc (McpAccountStorageIface *iface,
    const gchar *desc);

void mcp_account_storage_iface_set_provider (McpAccountStorageIface *iface,
    const gchar *provider);

void mcp_account_storage_iface_implement_get (McpAccountStorageIface *iface,
    McpAccountStorageGetFunc method);
void mcp_account_storage_iface_implement_set (McpAccountStorageIface *iface,
    McpAccountStorageSetFunc method);
void mcp_account_storage_iface_implement_create (
    McpAccountStorageIface *iface,
    McpAccountStorageCreate method);
void mcp_account_storage_iface_implement_delete (McpAccountStorageIface *iface,
    McpAccountStorageDeleteFunc method);
void mcp_account_storage_iface_implement_list (McpAccountStorageIface *iface,
    McpAccountStorageListFunc method);
void mcp_account_storage_iface_implement_commit (McpAccountStorageIface *iface,
    McpAccountStorageCommitFunc method);
void mcp_account_storage_iface_implement_commit_one (
    McpAccountStorageIface *iface,
    McpAccountStorageCommitOneFunc method);
void mcp_account_storage_iface_implement_ready (McpAccountStorageIface *iface,
    McpAccountStorageReadyFunc method);
void mcp_account_storage_iface_implement_get_identifier (
    McpAccountStorageIface *iface,
    McpAccountStorageGetIdentifierFunc method);
void mcp_account_storage_iface_implement_get_additional_info (
    McpAccountStorageIface *iface,
    McpAccountStorageGetAdditionalInfoFunc method);
void mcp_account_storage_iface_implement_get_restrictions (
    McpAccountStorageIface *iface,
    McpAccountStorageGetRestrictionsFunc method);

/* virtual methods */
gint mcp_account_storage_priority (const McpAccountStorage *storage);

gboolean mcp_account_storage_get (const McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account,
    const gchar *key);

gboolean mcp_account_storage_set (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *account,
    const gchar *key,
    const gchar *value);

gchar * mcp_account_storage_create (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *manager,
    const gchar *protocol,
    GHashTable *params,
    GError **error);

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

void mcp_account_storage_get_identifier (const McpAccountStorage *storage,
    const gchar *account,
    GValue *identifier);

GHashTable *mcp_account_storage_get_additional_info (
    const McpAccountStorage *storage,
    const gchar *account);

guint mcp_account_storage_get_restrictions (const McpAccountStorage *storage,
    const gchar *account);

const gchar *mcp_account_storage_name (const McpAccountStorage *storage);

const gchar *mcp_account_storage_description (const McpAccountStorage *storage);
const gchar *mcp_account_storage_provider (const McpAccountStorage *storage);

void mcp_account_storage_emit_created (McpAccountStorage *storage,
    const gchar *account);
void mcp_account_storage_emit_altered (McpAccountStorage *storage,
    const gchar *account);
void mcp_account_storage_emit_altered_one (McpAccountStorage *storage,
    const gchar *account,
    const gchar *key);
void mcp_account_storage_emit_deleted (McpAccountStorage *storage,
    const gchar *account);
void mcp_account_storage_emit_toggled (McpAccountStorage *storage,
    const gchar *account,
    gboolean enabled);
void mcp_account_storage_emit_reconnect (McpAccountStorage *storage,
    const gchar *account);

G_END_DECLS

#endif
