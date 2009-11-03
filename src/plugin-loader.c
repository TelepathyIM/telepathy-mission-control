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

#include <gmodule.h>
#include <mission-control-plugins/mission-control-plugins.h>

#include "mcd-debug.h"

static GList *plugins = NULL;

static void
mcd_plugin_loader_read_dir (const gchar *path)
{
  GError *error = NULL;
  GDir *dir = g_dir_open (path, 0, &error);
  const gchar *entry;

  if (dir == NULL)
    {
      DEBUG ("could not load plugins from %s: %s", path, error->message);
      g_error_free (error);
      return;
    }

  for (entry = g_dir_read_name (dir);
      entry != NULL;
      entry = g_dir_read_name (dir))
    {
      gchar *full_path;
      GModule *module;

      if (!g_str_has_suffix (entry, "." G_MODULE_SUFFIX))
        {
          DEBUG ("%s is not a loadable module or a libtool library", entry);
          continue;
        }

      full_path = g_build_filename (path, entry, NULL);

      module = g_module_open (full_path, G_MODULE_BIND_LOCAL);

      if (module != NULL)
        {
          gpointer symbol;

          if (g_module_symbol (module, MCP_PLUGIN_REF_NTH_OBJECT_SYMBOL,
                &symbol))
            {
              GObject *(* ref_nth) (guint) = symbol;
              guint n = 0;
              GObject *object;

              /* In practice, approximately no GModules can safely be unloaded.
               * For those that can, if there's ever a need for it, we can add
               * an API for "please don't make me resident". */
              g_module_make_resident (module);

              for (object = ref_nth (n);
                  object != NULL;
                  object = ref_nth (++n))
                {
                  plugins = g_list_prepend (plugins, object);
                }

              DEBUG ("%u plugin object(s) found in %s", n, entry);
            }
          else
            {
              DEBUG ("%s does not have symbol %s", entry,
                  MCP_PLUGIN_REF_NTH_OBJECT_SYMBOL);
            }
        }

      g_free (full_path);
    }

  g_dir_close (dir);
}

void
_mcd_plugin_loader_init (void)
{
  const gchar *dir = g_getenv ("MC_FILTER_PLUGIN_DIR");

  if (dir == NULL)
    dir = MCD_PLUGIN_LOADER_DIR;

  mcd_plugin_loader_read_dir (dir);
}

const GList *
_mcd_plugin_loader_list_objects (void)
{
  return plugins;
}
