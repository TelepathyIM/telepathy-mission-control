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

#include "client-registry.h"

#include <telepathy-glib/handle-repo-dynamic.h>

G_DEFINE_TYPE (McdClientRegistry, _mcd_client_registry, G_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_DBUS_DAEMON
};

enum
{
    S_CLIENT_ADDED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

struct _McdClientRegistryPrivate
{
  /* hash table containing clients
   * owned gchar * well_known_name -> owned McdClientProxy */
  GHashTable *clients;

  TpDBusDaemon *dbus_daemon;

  /* Not really handles as such, but TpHandleRepoIface gives us a convenient
   * reference-counted string pool */
  TpHandleRepoIface *string_pool;
};

static void mcd_client_registry_gone_cb (McdClientProxy *client,
    McdClientRegistry *self);

McdClientProxy *
_mcd_client_registry_add_new (McdClientRegistry *self,
    const gchar *well_known_name,
    const gchar *unique_name_if_known,
    gboolean activatable)
{
  McdClientProxy *client;

  g_return_val_if_fail (MCD_IS_CLIENT_REGISTRY (self), NULL);
  g_return_val_if_fail (g_hash_table_lookup (self->priv->clients,
        well_known_name) == NULL, NULL);

    client = _mcd_client_proxy_new (self->priv->dbus_daemon,
        self->priv->string_pool, well_known_name, unique_name_if_known,
        activatable);
    g_hash_table_insert (self->priv->clients, g_strdup (well_known_name),
        client);

    g_signal_connect (client, "gone",
                      G_CALLBACK (mcd_client_registry_gone_cb),
                      self);

    g_signal_emit (self, signals[S_CLIENT_ADDED], 0, client);

    return client;
}

McdClientProxy *
_mcd_client_registry_lookup (McdClientRegistry *self,
    const gchar *well_known_name)
{
  g_return_val_if_fail (MCD_IS_CLIENT_REGISTRY (self), NULL);
  return g_hash_table_lookup (self->priv->clients, well_known_name);
}

static void
mcd_client_registry_disconnect_client_signals (gpointer k G_GNUC_UNUSED,
    gpointer v,
    gpointer data)
{
  g_signal_handlers_disconnect_by_func (v, mcd_client_registry_gone_cb, data);
}

static void
_mcd_client_registry_remove (McdClientRegistry *self,
    const gchar *well_known_name)
{
  McdClientProxy *client;

  client = g_hash_table_lookup (self->priv->clients, well_known_name);

  if (client != NULL)
    {
      mcd_client_registry_disconnect_client_signals (NULL,
          client, self);
    }

  g_hash_table_remove (self->priv->clients, well_known_name);
}

void _mcd_client_registry_init_hash_iter (McdClientRegistry *self,
    GHashTableIter *iter)
{
  g_return_if_fail (MCD_IS_CLIENT_REGISTRY (self));
  g_hash_table_iter_init (iter, self->priv->clients);
}

static void
_mcd_client_registry_init (McdClientRegistry *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MCD_TYPE_CLIENT_REGISTRY,
      McdClientRegistryPrivate);

  self->priv->clients = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      g_object_unref);
}

static void
mcd_client_registry_constructed (GObject *object)
{
  McdClientRegistry *self = MCD_CLIENT_REGISTRY (object);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (_mcd_client_registry_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

  g_return_if_fail (self->priv->dbus_daemon != NULL);

  /* Dummy handle type, we're just using this as a string pool */
  self->priv->string_pool = tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_CONTACT,
      NULL, NULL);
}

static void
mcd_client_registry_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  McdClientRegistry *self = MCD_CLIENT_REGISTRY (object);

  switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
      g_assert (self->priv->dbus_daemon == NULL); /* it's construct-only */
      self->priv->dbus_daemon = TP_DBUS_DAEMON (g_value_dup_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mcd_client_registry_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  McdClientRegistry *self = MCD_CLIENT_REGISTRY (object);

  switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
      g_value_set_object (value, self->priv->dbus_daemon);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mcd_client_registry_dispose (GObject *object)
{
  McdClientRegistry *self = MCD_CLIENT_REGISTRY (object);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (_mcd_client_registry_parent_class)->dispose;

  if (self->priv->dbus_daemon != NULL)
    {
      g_object_unref (self->priv->dbus_daemon);
      self->priv->dbus_daemon = NULL;
    }

  if (self->priv->string_pool != NULL)
    {
      g_object_unref (self->priv->string_pool);
      self->priv->string_pool = NULL;
    }

  if (self->priv->clients != NULL)
    {
      g_hash_table_foreach (self->priv->clients,
          mcd_client_registry_disconnect_client_signals, self);

      g_hash_table_destroy (self->priv->clients);
      self->priv->clients = NULL;
    }

  if (chain_up != NULL)
    chain_up (object);
}

static void
_mcd_client_registry_class_init (McdClientRegistryClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  g_type_class_add_private (cls, sizeof (McdClientRegistryPrivate));

  object_class->constructed = mcd_client_registry_constructed;
  object_class->get_property = mcd_client_registry_get_property;
  object_class->set_property = mcd_client_registry_set_property;
  object_class->dispose = mcd_client_registry_dispose;

  g_object_class_install_property (object_class, PROP_DBUS_DAEMON,
      g_param_spec_object ("dbus-daemon", "D-Bus daemon", "D-Bus daemon",
        TP_TYPE_DBUS_DAEMON,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  signals[S_CLIENT_ADDED] = g_signal_new ("client-added",
      G_OBJECT_CLASS_TYPE (cls),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, MCD_TYPE_CLIENT_PROXY);
}

McdClientRegistry *
_mcd_client_registry_new (TpDBusDaemon *dbus_daemon)
{
  return g_object_new (MCD_TYPE_CLIENT_REGISTRY,
      "dbus-daemon", dbus_daemon,
      NULL);
}

static void
mcd_client_registry_gone_cb (McdClientProxy *client,
    McdClientRegistry *self)
{
  _mcd_client_registry_remove (self, tp_proxy_get_bus_name (client));
}

GPtrArray *
_mcd_client_registry_dup_client_caps (McdClientRegistry *self)
{
  GPtrArray *vas;
  GHashTableIter iter;
  gpointer p;

  g_return_val_if_fail (MCD_IS_CLIENT_REGISTRY (self), NULL);

  vas = g_ptr_array_sized_new (g_hash_table_size (self->priv->clients));

  g_hash_table_iter_init (&iter, self->priv->clients);

  while (g_hash_table_iter_next (&iter, NULL, &p))
    {
      g_ptr_array_add (vas,
          _mcd_client_proxy_dup_handler_capabilities (p));
    }

  return vas;
}
