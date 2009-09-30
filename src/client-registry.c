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

struct _McdClientRegistryPrivate
{
  TpDBusDaemon *dbus_daemon;
  /* Not really handles as such, but TpHandleRepoIface gives us a convenient
   * reference-counted string pool */
  TpHandleRepoIface *string_pool;
};

static void
_mcd_client_registry_init (McdClientRegistry *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MCD_TYPE_CLIENT_REGISTRY,
      McdClientRegistryPrivate);
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
}

McdClientRegistry *
_mcd_client_registry_new (TpDBusDaemon *dbus_daemon)
{
  return g_object_new (MCD_TYPE_CLIENT_REGISTRY,
      "dbus-daemon", dbus_daemon,
      NULL);
}
