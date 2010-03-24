/* Loader for plugins that use mission-control-plugins
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include "config.h"
#include "plugin-loader.h"

#include <mission-control-plugins/mission-control-plugins.h>

static gsize ready = 0;

void
_mcd_plugin_loader_init (void)
{
  if (g_once_init_enter (&ready))
    {
      const gchar *dir = g_getenv ("MC_FILTER_PLUGIN_DIR");

      if (dir == NULL)
        dir = MCD_PLUGIN_LOADER_DIR;

      mcp_read_dir (dir);
      g_once_init_leave (&ready, 1);
    }
}
