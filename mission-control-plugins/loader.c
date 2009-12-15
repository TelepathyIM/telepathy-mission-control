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

#include <gmodule.h>
#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/debug-internal.h>

static gboolean debugging = FALSE;

void
mcp_set_debug (gboolean debug)
{
  debugging = debug;
}

gboolean
_mcp_is_debugging (void)
{
  return debugging;
}

void
_mcp_debug (const gchar *format, ...)
{
  if (debugging)
    {
      va_list args;

      va_start (args, format);
      g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
      va_end (args);
    }
}

static GList *plugins = NULL;

/**
 * mcp_add_object:
 * @object: an object implementing one or more plugin interfaces
 *
 * As currently implemented, these objects are never unreferenced.
 *
 * Add an object to the list of plugin objects.
 */
void
mcp_add_object (gpointer object)
{
  g_return_if_fail (G_IS_OBJECT (object));

  plugins = g_list_prepend (plugins, g_object_ref (object));
}

/**
 * mcp_plugin_ref_nth_object:
 * @n: object number, starting from 0
 *
 * Implemented by each plugin (not implemented in this library!) as a hook
 * point; it will be called repeatedly with an increasing argument, and must
 * return a GObject reference each time, until it returns NULL.
 *
 * As currently implemented, these objects are never unreferenced.
 *
 * Returns: a new reference to a #GObject, or NULL if @n is at least the number
 *  of objects supported by this plugin
 */

/**
 * mcp_read_dir:
 * @path: full path to a plugins directory
 *
 * Read plugins from the given path. Any file with suffix G_MODULE_SUFFIX is
 * considered as a potential plugin, and loaded; if it contains the symbol
 * mcp_plugin_ref_nth_object(), it's made resident, then that symbol is called
 * as a function.
 */
void
mcp_read_dir (const gchar *path)
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

      if (!g_str_has_prefix (entry, "mcp-"))
        {
          DEBUG ("%s isn't a plugin (doesn't start with mcp-)", entry);
          continue;
        }

      if (!g_str_has_suffix (entry, "." G_MODULE_SUFFIX))
        {
          DEBUG ("%s is not a loadable module", entry);
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
                  mcp_add_object (object);
                  g_object_unref (object);
                }

              DEBUG ("%u plugin object(s) found in %s", n, entry);
            }
          else
            {
              DEBUG ("%s does not have symbol %s", entry,
                  MCP_PLUGIN_REF_NTH_OBJECT_SYMBOL);
              g_module_close (module);
            }
        }

      g_free (full_path);
    }

  g_dir_close (dir);
}

/**
 * mcp_list_objects:
 *
 * Return a list of objects that might implement plugin interfaces.
 *
 * Returns: a constant list of plugin objects
 */
const GList *
mcp_list_objects (void)
{
  return plugins;
}
