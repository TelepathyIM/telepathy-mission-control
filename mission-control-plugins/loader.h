/* Mission Control plugin API - loader
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
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

#ifndef MCP_LOADER_H
#define MCP_LOADER_H

#ifndef _MCP_IN_MISSION_CONTROL_PLUGINS_H
#error Use <mission-control-plugins/mission-control-plugins.h> instead
#endif

G_BEGIN_DECLS

/* not actually implemented in this library, see loader.c */
GObject *mcp_plugin_ref_nth_object (guint n);

#define MCP_PLUGIN_REF_NTH_OBJECT_SYMBOL "mcp_plugin_ref_nth_object"

void mcp_set_debug (gboolean debug);

void mcp_add_object (gpointer object);

void mcp_read_dir (const gchar *path);

const GList *mcp_list_objects (void);

G_END_DECLS

#endif
