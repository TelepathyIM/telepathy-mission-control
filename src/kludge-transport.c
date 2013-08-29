/*
 * kludge-transport.c - the shortest path to NM integration
 * Copyright ©2011 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "kludge-transport.h"

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-debug.h"

#include "connectivity-monitor.h"

struct _McdKludgeTransportPrivate {
    /* Rawr! I'm a mythical creature. */
    McdConnectivityMonitor *minotaur;

    /* Pointers representing network connections, exposed to the application as
     * opaque McdTransport pointers.
     *
     * In this generate example of an McdTransportPlugin, we only have one
     * transport, representing "the internet". So in fact this list always
     * contains a single pointer, namely the McdKludgeTransport instance
     * itself.
     */
    GList *transports;
};

static void transport_iface_init (
    gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (McdKludgeTransport, mcd_kludge_transport,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCD_TYPE_TRANSPORT_PLUGIN, transport_iface_init);
    )

static void
mcd_kludge_transport_init (McdKludgeTransport *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      MCD_TYPE_KLUDGE_TRANSPORT, McdKludgeTransportPrivate);
}

static void
mcd_kludge_transport_constructed (GObject *object)
{
  McdKludgeTransport *self = MCD_KLUDGE_TRANSPORT (object);
  McdKludgeTransportPrivate *priv = self->priv;
  GObjectClass *parent_class = mcd_kludge_transport_parent_class;

  if (parent_class->constructed != NULL)
    parent_class->constructed (object);

  /* We just use ourself as the McdTransport pointer... */
  priv->transports = g_list_prepend (NULL, self);
}

static void
mcd_kludge_transport_dispose (GObject *object)
{
  McdKludgeTransport *self = MCD_KLUDGE_TRANSPORT (object);
  McdKludgeTransportPrivate *priv = self->priv;
  GObjectClass *parent_class = mcd_kludge_transport_parent_class;

  tp_clear_object (&priv->minotaur);
  g_list_free (priv->transports);
  priv->transports = NULL;

  if (parent_class->dispose != NULL)
    parent_class->dispose (object);
}

static void
mcd_kludge_transport_class_init (McdKludgeTransportClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = mcd_kludge_transport_constructed;
  object_class->dispose = mcd_kludge_transport_dispose;

  g_type_class_add_private (klass, sizeof (McdKludgeTransportPrivate));
}

static const GList *
mcd_kludge_transport_get_transports (
    McdTransportPlugin *plugin)
{
  McdKludgeTransport *self = MCD_KLUDGE_TRANSPORT (plugin);

  g_return_val_if_fail (MCD_IS_KLUDGE_TRANSPORT (plugin), NULL);

  return self->priv->transports;
}

static const gchar *
mcd_kludge_transport_get_transport_name (
    McdTransportPlugin *plugin,
    McdTransport *transport)
{
  g_return_val_if_fail (MCD_IS_KLUDGE_TRANSPORT (plugin), NULL);
  g_return_val_if_fail (plugin == (McdTransportPlugin *) transport, NULL);

  return "i love the internet";
}

static void
transport_iface_init (
    gpointer g_iface,
    gpointer iface_data)
{
  McdTransportPluginIface *klass = g_iface;

  klass->get_transports = mcd_kludge_transport_get_transports;
  klass->get_transport_name = mcd_kludge_transport_get_transport_name;
}

static McdTransportPlugin *
mcd_kludge_transport_new (McdConnectivityMonitor *connectivity_monitor)
{
  McdKludgeTransport *self = g_object_new (MCD_TYPE_KLUDGE_TRANSPORT, NULL);

  /* Strictly speaking this should be done with properties, but I'm
   * going to delete this class soon anyway. */
  self->priv->minotaur = connectivity_monitor;

  return MCD_TRANSPORT_PLUGIN (self);
}

void
mcd_kludge_transport_install (McdMaster *master,
    McdConnectivityMonitor *connectivity_monitor)
{
  McdTransportPlugin *self = mcd_kludge_transport_new (connectivity_monitor);

  mcd_master_register_transport (master, self);
}
