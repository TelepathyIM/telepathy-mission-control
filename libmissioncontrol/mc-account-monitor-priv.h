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

#ifndef __MC_ACCOUNT_MONITOR_PRIV_H__
#define __MC_ACCOUNT_MONITOR_PRIV_H__

McAccount * _mc_account_monitor_lookup (McAccountMonitor *monitor, const gchar *unique_name);
GList * _mc_account_monitor_list (McAccountMonitor *monitor);
McAccount *_mc_account_monitor_create_account (McAccountMonitor *monitor,
					       const gchar *manager,
					       const gchar *protocol,
					       const gchar *display_name,
					       GHashTable *parameters);

#endif /* __MC_ACCOUNT_MONITOR_PRIV_H__ */

