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
#include <telepathy-glib/telepathy-glib.h>

typedef enum {
    MCP_PARAMETER_FLAG_NONE = 0,
    MCP_PARAMETER_FLAG_SECRET = TP_CONN_MGR_PARAM_FLAG_SECRET
} McpParameterFlags;

typedef enum {
    MCP_ATTRIBUTE_FLAG_NONE = 0
} McpAttributeFlags;

#define _MCP_IN_MISSION_CONTROL_PLUGINS_H
#include <mission-control-plugins/account.h>
#include <mission-control-plugins/account-storage.h>
#include <mission-control-plugins/dbus-acl.h>
#include <mission-control-plugins/dispatch-operation.h>
#include <mission-control-plugins/dispatch-operation-policy.h>
#include <mission-control-plugins/loader.h>
#include <mission-control-plugins/request.h>
#include <mission-control-plugins/request-policy.h>
#include <mission-control-plugins/debug.h>
#undef  _MCP_IN_MISSION_CONTROL_PLUGINS_H

#endif
