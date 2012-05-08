/*
 * kludge-transport.h - header for the shortest path to NM integration
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

#ifndef MCD_KLUDGE_TRANSPORT_H
#define MCD_KLUDGE_TRANSPORT_H

#include <glib-object.h>
#include "mcd-master.h"
#include "mcd-transport.h"

typedef struct _McdKludgeTransport McdKludgeTransport;
typedef struct _McdKludgeTransportClass McdKludgeTransportClass;
typedef struct _McdKludgeTransportPrivate McdKludgeTransportPrivate;

struct _McdKludgeTransportClass {
    GObjectClass parent_class;
};

struct _McdKludgeTransport {
    GObject parent;

    McdKludgeTransportPrivate *priv;
};

GType mcd_kludge_transport_get_type (void);

void mcd_kludge_transport_install (McdMaster *master);

/* TYPE MACROS */
#define MCD_TYPE_KLUDGE_TRANSPORT \
  (mcd_kludge_transport_get_type ())
#define MCD_KLUDGE_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), MCD_TYPE_KLUDGE_TRANSPORT, McdKludgeTransport))
#define MCD_KLUDGE_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), MCD_TYPE_KLUDGE_TRANSPORT,\
                           McdKludgeTransportClass))
#define MCD_IS_KLUDGE_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), MCD_TYPE_KLUDGE_TRANSPORT))
#define MCD_IS_KLUDGE_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), MCD_TYPE_KLUDGE_TRANSPORT))
#define MCD_KLUDGE_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_KLUDGE_TRANSPORT, \
                              McdKludgeTransportClass))

#endif /* MCD_KLUDGE_TRANSPORT_H */
