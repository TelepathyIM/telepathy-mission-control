/* Mission Control storage API - interface which provides access to account
 * parameter/setting storage
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
  GKeyFile *keyfile;
  GKeyFile *secrets;
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

GStrv mcd_storage_dup_settings (McdStorage *storage,
    const gchar *account,
    gsize *n);

gboolean mcd_storage_set_string (McdStorage *storage,
    const gchar *account,
    const gchar *key,
    const gchar *value,
    gboolean secret);

gboolean mcd_storage_set_strv (McdStorage *storage,
    const gchar *account,
    const gchar *key,
    const gchar * const *strv,
    gboolean secret);

gboolean mcd_storage_set_value (McdStorage *storage,
    const gchar *account,
    const gchar *key,
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
    const gchar *key);

gboolean mcd_storage_has_value (McdStorage *storage,
    const gchar *account,
    const gchar *key);

GValue *mcd_storage_dup_value (McdStorage *storage,
    const gchar *account,
    const gchar *key,
    GType type,
    GError **error);

gboolean mcd_storage_get_boolean (McdStorage *storage,
    const gchar *account,
    const gchar *key);

gint mcd_storage_get_integer (McdStorage *storage,
    const gchar *account,
    const gchar *key);

McpAccountStorage * mcd_storage_get_plugin (McdStorage *storage,
    const gchar *account);

G_GNUC_INTERNAL void _mcd_storage_store_connections (McdStorage *storage);

G_END_DECLS

#endif /* MCD_STORAGE_H */
