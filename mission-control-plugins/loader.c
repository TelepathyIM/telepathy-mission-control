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

/**
 * SECTION:loader
 * @title: Plugin loader and global functions
 * @short_description: Writing a plugin, or loading plugins
 * @see_also:
 * @include: mission-control-plugins/mission-control-plugins.h
 *
 * To write plugins for Mission Control, build a GModule whose name starts
 * with "mcp-" and ends with %G_MODULE_SUFFIX, for instance mcp-testplugin.so
 * on Linux or mcp-testplugin.dll on Windows. It must be installed in the
 * directory given by the ${plugindir} variable in the mission-control-plugins
 * pkg-config file.
 *
 * Each plugin must contain an extern (public) function called
 * mcp_plugin_ref_nth_object() which behaves as documented here. Mission
 * Control will call that function to load the plugin.
 *
 * Mission Control also uses functions from this part of the library, to load
 * the plugins.
 */

#include "config.h"

#include <gmodule.h>
#include <mission-control-plugins/mission-control-plugins.h>
#include <mission-control-plugins/debug.h>

static gboolean debugging = FALSE;

#undef  DEBUG
#define DEBUG(format, ...) \
  G_STMT_START { if (debugging || mcp_is_debugging (MCP_DEBUG_LOADER))  \
      g_debug ("%s " format, G_STRLOC, ##__VA_ARGS__); } G_STMT_END

/* Android's build system prefixes the plugins with lib */
#ifndef __BIONIC__
#define PLUGIN_PREFIX "mcp-"
#else
#define PLUGIN_PREFIX "libmcp-"
#endif

/**
 * mcp_set_debug:
 * @debug: whether to log debug output
 *
 * Set whether debug output will be produced via g_debug() for the plugin
 * loader. Plugins shouldn't normally need to call this function.
 */
void
mcp_set_debug (gboolean debug)
{
  debugging = debug;
}

static GList *plugins = NULL;

/**
 * mcp_add_object:
 * @object: an object implementing one or more plugin interfaces
 *
 * Add an object to the list of "plugin objects". Mission Control does this
 * automatically for the objects returned by mcp_plugin_ref_nth_object(),
 * so you should only need to use this if you're embedding part of Mission
 * Control in a larger process.
 *
 * As currently implemented, these objects are never unreferenced.
 *
 * Mission Control uses this function to load its plugins; plugins shouldn't
 * call it.
 */
void
mcp_add_object (gpointer object)
{
  g_return_if_fail (G_IS_OBJECT (object));

  plugins = g_list_prepend (plugins, g_object_ref (object));
}

/**
 * MCP_PLUGIN_REF_NTH_OBJECT_SYMBOL:
 *
 * A string constant whose value is the name mcp_plugin_ref_nth_object().
 */

/**
 * mcp_plugin_ref_nth_object:
 * @n: object number, starting from 0
 *
 * Implemented by each plugin (not implemented in this library!) as a hook
 * point; it will be called repeatedly with an increasing argument, and must
 * return a #GObject reference each time, until it returns %NULL.
 *
 * Mission Control will query each object for the #GInterface<!-- -->s it
 * implements, and behave accordingly; for instance, the objects might
 * implement #McpRequestPolicy and/or #McpDispatchOperationPolicy.
 *
 * As currently implemented, these objects are never unreferenced.
 *
 * Returns: a new reference to a #GObject, or %NULL if @n is at least the
 *  number of objects supported by this plugin
 */

/**
 * mcp_read_dir:
 * @path: full path to a plugins directory
 *
 * Read plugins from the given path. Any file with prefix "mcp-" and suffix
 * %G_MODULE_SUFFIX is considered as a potential plugin, and loaded; if it
 * contains the symbol mcp_plugin_ref_nth_object(), the plugin is made
 * resident, then that symbol is called as a function until it returns %NULL.
 *
 * Mission Control uses this function to load its plugins; plugins shouldn't
 * call it.
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

      if (!g_str_has_prefix (entry, PLUGIN_PREFIX))
        {
          DEBUG ("%s isn't a plugin (doesn't start with " PLUGIN_PREFIX ")", entry);
          continue;
        }

      if (!g_str_has_suffix (entry, "." G_MODULE_SUFFIX))
        {
          DEBUG ("%s is not a loadable module", entry);
          continue;
        }

      full_path = g_build_filename (path, entry, NULL);

      module = g_module_open (full_path, G_MODULE_BIND_LOCAL);
      if (module)
        DEBUG ("g_module_open (%s, ...) = %p", full_path, module);
      else
        DEBUG ("g_module_open (%s, ...) = %s", full_path, g_module_error ());

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
 * Mission Control uses this function to iterate through the loaded plugin
 * objects; plugins shouldn't need to call it.
 *
 * Returns: a constant list of plugin objects
 */
const GList *
mcp_list_objects (void)
{
  return plugins;
}
