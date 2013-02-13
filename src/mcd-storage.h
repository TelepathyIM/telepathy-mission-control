/* Mission Control storage API - interface which provides access to account
 * parameter/attribute storage
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

#include <glib-object.h>
#include <mission-control-plugins/mission-control-plugins.h>

#ifndef MCD_STORAGE_H
#define MCD_STORAGE_H

G_BEGIN_DECLS

typedef struct {
  GObject parent;
  TpDBusDaemon *dbusd;
  /* owned string => owned McdStorageAccount */
  GHashTable *accounts;
} McdStorage;

typedef struct _McdStorageClass McdStorageClass;
typedef struct _McdStoragePrivate McdStoragePrivate;

#define MCD_TYPE_STORAGE (mcd_storage_get_type ())

#define MCD_STORAGE(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_STORAGE, McdStorage))

#define MCD_STORAGE_CLASS(cls) \
  (G_TYPE_CHECK_CLASS_CAST ((cls), MCD_TYPE_STORAGE, McdStorageClass))

#define MCD_IS_STORAGE(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_STORAGE))

#define MCD_IS_STORAGE_CLASS(cls) \
  (G_TYPE_CHECK_CLASS_TYPE ((cls), MCD_TYPE_STORAGE))

#define MCD_STORAGE_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_STORAGE, McdStorageClass))

GType mcd_storage_get_type (void);

McdStorage *mcd_storage_new (TpDBusDaemon *dbus_daemon);
void mcd_storage_ready (McdStorage *self);
void mcd_storage_connect_signal (const gchar *signal,
    GCallback func,
    gpointer user_data);

void mcd_storage_load (McdStorage *storage);

GStrv mcd_storage_dup_accounts (McdStorage *storage, gsize *n);

GStrv mcd_storage_dup_attributes (McdStorage *storage,
    const gchar *account,
    gsize *n);

gboolean mcd_storage_set_string (McdStorage *storage,
    const gchar *account,
    const gchar *attribute,
    const gchar *value);

gboolean mcd_storage_set_strv (McdStorage *storage,
    const gchar *account,
    const gchar *attribute,
    const gchar * const *strv);

gboolean mcd_storage_set_attribute (McdStorage *storage,
    const gchar *account,
    const gchar *attribute,
    const GValue *value);

gboolean mcd_storage_set_parameter (McdStorage *storage,
    const gchar *account,
    const gchar *parameter,
    const GValue *value,
    gboolean secret);

gchar *mcd_storage_create_account (McdStorage *storage,
    const gchar *provider,
    const gchar *manager,
    const gchar *protocol,
    GHashTable *params,
    GError **error);

void mcd_storage_delete_account (McdStorage *storage, const gchar *account);

void mcd_storage_commit (McdStorage *storage, const gchar *account);

gchar *mcd_storage_dup_string (McdStorage *storage,
    const gchar *account,
    const gchar *attribute);

gboolean mcd_storage_get_attribute (McdStorage *storage,
    const gchar *account,
    const gchar *attribute,
    GValue *value,
    GError **error);

gboolean mcd_storage_get_parameter (McdStorage *storage,
    const gchar *account,
    const gchar *parameter,
    GValue *value,
    GError **error);

gboolean mcd_storage_get_boolean (McdStorage *storage,
    const gchar *account,
    const gchar *attribute);

gint mcd_storage_get_integer (McdStorage *storage,
    const gchar *account,
    const gchar *attribute);

McpAccountStorage * mcd_storage_get_plugin (McdStorage *storage,
    const gchar *account);

G_GNUC_INTERNAL void _mcd_storage_store_connections (McdStorage *storage);

gboolean mcd_storage_add_account_from_plugin (McdStorage *storage,
    McpAccountStorage *plugin,
    const gchar *account);

gboolean mcd_keyfile_get_value (GKeyFile *keyfile,
    const gchar *group,
    const gchar *key,
    GValue *value,
    GError **error);
gboolean mcd_keyfile_set_value (GKeyFile *keyfile,
    const gchar *name,
    const gchar *key,
    const GValue *value);

gchar *mcd_keyfile_escape_value (const GValue *value);
gboolean mcd_keyfile_unescape_value (const gchar *escaped,
    GValue *value,
    GError **error);

const gchar *mcd_storage_get_attribute_type (const gchar *attribute);
gboolean mcd_storage_init_value_for_attribute (GValue *value,
    const gchar *attribute);

G_END_DECLS

#endif /* MCD_STORAGE_H */
