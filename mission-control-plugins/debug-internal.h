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

G_BEGIN_DECLS

gboolean _mcp_is_debugging (void);
void _mcp_debug (const gchar *format, ...) G_GNUC_PRINTF (1, 2);

G_END_DECLS

#ifdef ENABLE_DEBUG

#undef DEBUG
#define DEBUG(format, ...) \
  _mcp_debug ("%s: " format, G_STRFUNC, ##__VA_ARGS__)

#undef DEBUGGING
#define DEBUGGING _mcp_is_debugging ()

#else /* !defined (ENABLE_DEBUG) */

#undef DEBUG
#define DEBUG(format, ...) do {} while (0)

#undef DEBUGGING
#define DEBUGGING 0

#endif /* !defined (ENABLE_DEBUG) */

#endif
