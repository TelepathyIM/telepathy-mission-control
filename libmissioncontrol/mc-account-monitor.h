/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
 *
 * Contact: Naba Kumar  <naba.kumar@nokia.com>
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

#ifndef __MC_ACCOUNT_MONITOR_H__
#define __MC_ACCOUNT_MONITOR_H__

#include <glib-object.h>
#include <libmissioncontrol/mission-control.h>

G_BEGIN_DECLS

#define MC_TYPE_ACCOUNT_MONITOR mc_account_monitor_get_type()

#define MC_ACCOUNT_MONITOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  MC_TYPE_ACCOUNT_MONITOR, McAccountMonitor))

#define MC_ACCOUNT_MONITOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  MC_TYPE_ACCOUNT_MONITOR, McAccountMonitorClass))

#define MC_IS_ACCOUNT_MONITOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  MC_TYPE_ACCOUNT_MONITOR))

#define MC_IS_ACCOUNT_MONITOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  MC_TYPE_ACCOUNT_MONITOR))

#define MC_ACCOUNT_MONITOR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  MC_TYPE_ACCOUNT_MONITOR, McAccountMonitorClass))

typedef struct {
    GObject parent;
    gpointer priv;
} McAccountMonitor;

typedef struct {
    GObjectClass parent_class;
} McAccountMonitorClass;

GType mc_account_monitor_get_type (void);

McAccountMonitor* mc_account_monitor_new (void);

McPresence * mc_account_monitor_get_supported_presences (McAccountMonitor *
							 monitor);

G_END_DECLS

#endif /* __MC_ACCOUNT_MONITOR_H__ */
