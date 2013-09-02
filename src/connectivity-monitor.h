/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/*
 * Copyright © 2009–2011 Collabora Ltd.
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
 *
 * Authors:
 *   Jonny Lamb <jonny.lamb@collabora.co.uk>
 *   Will Thompson <will.thompson@collabora.co.uk>
 */

#ifndef MCD_CONNECTIVITY_MONITOR_H
#define MCD_CONNECTIVITY_MONITOR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define MCD_TYPE_CONNECTIVITY_MONITOR (mcd_connectivity_monitor_get_type ())
#define MCD_CONNECTIVITY_MONITOR(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_CONNECTIVITY_MONITOR, \
      McdConnectivityMonitor))
#define MCD_CONNECTIVITY_MONITOR_CLASS(k) \
  (G_TYPE_CHECK_CLASS_CAST ((k), MCD_TYPE_CONNECTIVITY_MONITOR, \
      McdConnectivityMonitorClass))
#define MCD_IS_CONNECTIVITY_MONITOR(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_CONNECTIVITY_MONITOR))
#define MCD_IS_CONNECTIVITY_MONITOR_CLASS(k) \
  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_CONNECTIVITY_MONITOR))
#define MCD_CONNECTIVITY_MONITOR_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_CONNECTIVITY_MONITOR, \
      McdConnectivityMonitorClass))

typedef struct _McdConnectivityMonitor McdConnectivityMonitor;
typedef struct _McdConnectivityMonitorClass McdConnectivityMonitorClass;
typedef struct _McdConnectivityMonitorPrivate McdConnectivityMonitorPrivate;

struct _McdConnectivityMonitor {
  GObject parent;
  McdConnectivityMonitorPrivate *priv;
};

struct _McdConnectivityMonitorClass {
  GObjectClass parent_class;
};

GType mcd_connectivity_monitor_get_type (void);

/* public methods */

McdConnectivityMonitor *mcd_connectivity_monitor_new (void);

gboolean mcd_connectivity_monitor_is_online (McdConnectivityMonitor *connectivity);

gboolean mcd_connectivity_monitor_get_use_conn (McdConnectivityMonitor *connectivity);
void mcd_connectivity_monitor_set_use_conn (McdConnectivityMonitor *connectivity,
    gboolean use_conn);

typedef struct _McdInhibit McdInhibit;
McdInhibit *mcd_inhibit_hold (McdInhibit *inhibit);
void mcd_inhibit_release (McdInhibit *inhibit);

G_END_DECLS

#endif /* MCD_CONNECTIVITY_MONITOR_H */

