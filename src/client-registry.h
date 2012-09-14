/* Map containing registered Telepathy clients
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef MCD_CLIENT_REGISTRY_H
#define MCD_CLIENT_REGISTRY_H

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-client-priv.h"

G_BEGIN_DECLS

typedef struct _McdClientRegistry McdClientRegistry;
typedef struct _McdClientRegistryClass McdClientRegistryClass;
typedef struct _McdClientRegistryPrivate McdClientRegistryPrivate;

struct _McdClientRegistry
{
  GObject parent;
  McdClientRegistryPrivate *priv;
};

struct _McdClientRegistryClass
{
  GObjectClass parent;
};

GType _mcd_client_registry_get_type (void);

#define MCD_TYPE_CLIENT_REGISTRY \
  (_mcd_client_registry_get_type ())
#define MCD_CLIENT_REGISTRY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MCD_TYPE_CLIENT_REGISTRY, \
                               McdClientRegistry))
#define MCD_CLIENT_REGISTRY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MCD_TYPE_CLIENT_REGISTRY, \
                            McdClientRegistryClass))
#define MCD_IS_CLIENT_REGISTRY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCD_TYPE_CLIENT_REGISTRY))
#define MCD_IS_CLIENT_REGISTRY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MCD_TYPE_CLIENT_REGISTRY))
#define MCD_CLIENT_REGISTRY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_CLIENT_REGISTRY, \
                              McdClientRegistryClass))

G_GNUC_INTERNAL McdClientRegistry *_mcd_client_registry_new (
    TpDBusDaemon *dbus_daemon);

TpDBusDaemon *_mcd_client_registry_get_dbus_daemon (McdClientRegistry *self);

G_GNUC_INTERNAL McdClientProxy *_mcd_client_registry_lookup (
    McdClientRegistry *self, const gchar *well_known_name);

G_GNUC_INTERNAL GPtrArray *_mcd_client_registry_dup_client_caps (
    McdClientRegistry *self);

G_GNUC_INTERNAL gboolean _mcd_client_registry_is_ready (
    McdClientRegistry *self);

G_GNUC_INTERNAL void _mcd_client_registry_init_hash_iter (
    McdClientRegistry *self, GHashTableIter *iter);

G_GNUC_INTERNAL GList *_mcd_client_registry_list_possible_handlers (
    McdClientRegistry *self, const gchar *preferred_handler,
    GVariant *request_props, TpChannel *channel,
    const gchar *must_have_unique_name);

G_END_DECLS

#endif
