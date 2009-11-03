/* Mission Control plugin API (merged header).
 * #include <mission-control-plugins/mission-control-plugins.h>.
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

#ifndef MCP_MISSION_CONTROL_PLUGINS_H
#define MCP_MISSION_CONTROL_PLUGINS_H

#include <glib-object.h>

#define MCP_IN_MISSION_CONTROL_PLUGINS_H
#include <mission-control-plugins/dispatch-operation.h>
#undef  MCP_IN_MISSION_CONTROL_PLUGINS_H

#define MCP_PLUGIN_REF_NTH_OBJECT_SYMBOL "mcp_plugin_ref_nth_object"

G_BEGIN_DECLS

/* Implemented by each plugin (not implemented in this library!) as a hook
 * point; it will be called repeatedly with an increasing argument, and must
 * return a GObject reference each time, until it returns NULL. */
GObject *mcp_plugin_ref_nth_object (guint n);

G_END_DECLS

#endif
