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

typedef enum {
    MCP_ACCOUNT_STORAGE_SET_RESULT_FAILED = 0,
    MCP_ACCOUNT_STORAGE_SET_RESULT_CHANGED,
    MCP_ACCOUNT_STORAGE_SET_RESULT_UNCHANGED
} McpAccountStorageSetResult;

/* API for plugins to implement */
typedef struct _McpAccountStorage McpAccountStorage;
typedef struct _McpAccountStorageIface McpAccountStorageIface;

#ifdef _i_used_gtk_doc_but_all_i_got_was_this_heap_of_workarounds
/* Without this, gtk-doc doesn't generate documentation for
 * #McpAccountStorage. There is actually no such struct: do not attempt
 * to use it. */
struct _McpAccountStorage { };
#endif

#define MCP_TYPE_ACCOUNT_STORAGE (mcp_account_storage_get_type ())

#define MCP_ACCOUNT_STORAGE(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCP_TYPE_ACCOUNT_STORAGE, \
      McpAccountStorage))

#define MCP_IS_ACCOUNT_STORAGE(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCP_TYPE_ACCOUNT_STORAGE))

#define MCP_ACCOUNT_STORAGE_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), MCP_TYPE_ACCOUNT_STORAGE, \
                                  McpAccountStorageIface))

GType mcp_account_storage_get_type (void);

/* Virtual method implementation signatures */
typedef gchar * (*McpAccountStorageCreate) (
    const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *manager,
    const gchar *protocol,
    const gchar *identification,
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
typedef TpStorageRestrictionFlags (*McpAccountStorageGetRestrictionsFunc) (
    const McpAccountStorage *storage,
    const gchar *account);

struct _McpAccountStorageIface
{
  GTypeInterface parent;

  gint priority;
  const gchar *name;
  const gchar *desc;
  const gchar *provider;

  void (*delete_async) (McpAccountStorage *storage,
      McpAccountManager *am,
      const gchar *account,
      GCancellable *cancellable,
      GAsyncReadyCallback callback,
      gpointer user_data);
  gboolean (*delete_finish) (McpAccountStorage *storage,
      GAsyncResult *res,
      GError **error);
  McpAccountStorageCommitFunc commit;
  McpAccountStorageListFunc list;
  McpAccountStorageReadyFunc ready;
  McpAccountStorageGetIdentifierFunc get_identifier;
  McpAccountStorageGetAdditionalInfoFunc get_additional_info;
  McpAccountStorageGetRestrictionsFunc get_restrictions;
  McpAccountStorageCreate create;

  /* Since 5.15.0 */
  GVariant *(*get_attribute) (McpAccountStorage *storage,
      McpAccountManager *am,
      const gchar *account,
      const gchar *attribute,
      const GVariantType *type,
      McpAttributeFlags *flags);
  GVariant *(*get_parameter) (McpAccountStorage *storage,
      McpAccountManager *am,
      const gchar *account,
      const gchar *parameter,
      const GVariantType *type,
      McpParameterFlags *flags);
  McpAccountStorageSetResult (*set_attribute) (McpAccountStorage *storage,
      McpAccountManager *am,
      const gchar *account,
      const gchar *attribute,
      GVariant *val,
      McpAttributeFlags flags);
  McpAccountStorageSetResult (*set_parameter) (McpAccountStorage *storage,
      McpAccountManager *am,
      const gchar *account,
      const gchar *parameter,
      GVariant *val,
      McpParameterFlags flags);
};

/* virtual methods */
gint mcp_account_storage_priority (const McpAccountStorage *storage);

gchar * mcp_account_storage_create (const McpAccountStorage *storage,
    const McpAccountManager *am,
    const gchar *manager,
    const gchar *protocol,
    const gchar *identification,
    GError **error);

void mcp_account_storage_delete_async (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);
gboolean mcp_account_storage_delete_finish (McpAccountStorage *storage,
    GAsyncResult *result,
    GError **error);

void mcp_account_storage_ready (const McpAccountStorage *storage,
    const McpAccountManager *am);

gboolean
mcp_account_storage_commit (const McpAccountStorage *storage,
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

TpStorageRestrictionFlags mcp_account_storage_get_restrictions (
    const McpAccountStorage *storage,
    const gchar *account);

const gchar *mcp_account_storage_name (const McpAccountStorage *storage);

const gchar *mcp_account_storage_description (const McpAccountStorage *storage);
const gchar *mcp_account_storage_provider (const McpAccountStorage *storage);

GVariant *mcp_account_storage_get_attribute (McpAccountStorage *storage,
      McpAccountManager *am,
      const gchar *account,
      const gchar *attribute,
      const GVariantType *type,
      McpAttributeFlags *flags);
GVariant *mcp_account_storage_get_parameter (McpAccountStorage *storage,
      McpAccountManager *am,
      const gchar *account,
      const gchar *parameter,
      const GVariantType *type,
      McpParameterFlags *flags);

McpAccountStorageSetResult mcp_account_storage_set_attribute (
    McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account,
    const gchar *attribute,
    GVariant *value,
    McpAttributeFlags flags);
McpAccountStorageSetResult mcp_account_storage_set_parameter (
    McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account,
    const gchar *parameter,
    GVariant *value,
    McpParameterFlags flags);

void mcp_account_storage_emit_created (McpAccountStorage *storage,
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
