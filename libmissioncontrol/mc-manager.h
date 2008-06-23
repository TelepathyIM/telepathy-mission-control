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

#ifndef __MC_MANAGER_H__
#define __MC_MANAGER_H__

#include <glib.h>
#include <glib-object.h>

#include <libmissioncontrol/mc-remap.h>

G_BEGIN_DECLS

#define MC_TYPE_MANAGER mc_manager_get_type()

#define MC_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  MC_TYPE_MANAGER, McManager))

#define MC_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  MC_TYPE_MANAGER, McManagerClass))

#define MC_IS_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  MC_TYPE_MANAGER))

#define MC_IS_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  MC_TYPE_MANAGER))

#define MC_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  MC_TYPE_MANAGER, McManagerClass))

typedef struct {
    GObject parent;
    gpointer priv;
} McManager;

typedef struct {
    GObjectClass parent_class;
} McManagerClass;

GType mc_manager_get_type (void);

McManager *mc_manager_lookup (const gchar *unique_name);
#ifndef MC_DISABLE_DEPRECATED
void mc_manager_free (McManager *id);
#endif
void mc_manager_clear_cache (void);

/* get all managers; returns a list of McManager *s */
GList *mc_managers_list (void);
void mc_managers_free_list (GList *list);

const gchar *mc_manager_get_unique_name (McManager *id);
const gchar *mc_manager_get_bus_name (McManager *id);
const gchar *mc_manager_get_object_path (McManager *id);
const gchar *mc_manager_get_filename (McManager *id);

G_END_DECLS

#endif /* __MC_MANAGER_H__ */
