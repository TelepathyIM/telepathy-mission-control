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

#include "mcd-debug.h"

#if ENABLE_AEGIS
#include "plugins/mcp-dbus-aegis-acl.h"
#endif

static gsize ready = 0;

void
_mcd_plugin_loader_init (void)
{
  if (g_once_init_enter (&ready))
    {
#if ENABLE_AEGIS
      GObject *pseudo_plugin;
#endif
      const gchar *dir = g_getenv ("MC_FILTER_PLUGIN_DIR");

      if (dir == NULL)
        dir = MCD_PLUGIN_LOADER_DIR;

      mcp_read_dir (dir);

#if ENABLE_AEGIS
      /* The last object added by mcp_add_object() will be treated as highest
       * priority, at least for the interfaces used here */
      DEBUG ("Initialising built-in Aegis ACL plugin");
      pseudo_plugin = G_OBJECT (aegis_acl_new ());
      mcp_add_object (pseudo_plugin);
      g_object_unref (pseudo_plugin);
#endif

      g_once_init_leave (&ready, 1);
    }
}
