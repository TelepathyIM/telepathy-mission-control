/* Mission Control plugin API - debug
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

#ifndef MCP_DEBUG_INTERNAL_H
#define MCP_DEBUG_INTERNAL_H

#include "config.h"

#include <glib.h>
#include <mission-control-plugins/debug.h>

#ifdef DEBUG
#undef DEBUG
#endif

#ifdef ENABLE_DEBUG

#define DEBUG(format, ...) \
  MCP_DEBUG (MCP_DEBUG_TYPE, format, ##__VA_ARGS__)

#else

#define DEBUG(format, ...) do {} while (0)

#endif

#endif
