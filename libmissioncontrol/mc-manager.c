/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
 *
 * Contact: Naba Kumar  <naba.kumar@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
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

#include <string.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "mc-protocol.h"
#include "mc-protocol-priv.h"

#include "mc-manager.h"
#include "mc-manager-priv.h"

#define MANAGER_PATH "/usr/share/telepathy/managers"
#define MANAGER_SUFFIX ".manager"
#define MANAGER_SUFFIX_LEN 8

#define MC_MANAGER_PRIV(manager) ((McManagerPrivate *)manager->priv)

G_DEFINE_TYPE (McManager, mc_manager, G_TYPE_OBJECT);

static GHashTable *manager_cache = NULL;

typedef struct {
  gchar *unique_name;
  gchar *bus_name;
  gchar *object_path;
  time_t mtime;
  GSList *protocols;
} McManagerPrivate;

static void
mc_manager_finalize (GObject *object)
{
  McManager *manager = MC_MANAGER(object);
  McManagerPrivate *priv = MC_MANAGER_PRIV (manager);
  GSList *i;

  g_free (priv->unique_name);
  g_free (priv->bus_name);
  g_free (priv->object_path);

  for (i = priv->protocols; NULL != i; i = i->next)
    g_object_unref (G_OBJECT (i->data));

  g_slist_free (priv->protocols);
}

static void
mc_manager_class_init (McManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (object_class, sizeof (McManagerPrivate));
  object_class->finalize = mc_manager_finalize;
}

static void
mc_manager_init (McManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
      MC_TYPE_MANAGER, McManagerPrivate);
}

static McManager *
mc_manager_new (gchar *unique_name, gchar *bus_name, gchar *object_path,
                   GSList *protocols)
{
  McManager *new = g_object_new (MC_TYPE_MANAGER, NULL);
  McManagerPrivate *priv = MC_MANAGER_PRIV (new);

  priv->unique_name = unique_name;
  priv->bus_name = bus_name;
  priv->object_path = object_path;
  priv->protocols = protocols;

  return new;
}

static const gchar *
_mc_manager_path (void)
{
  const gchar *ret = NULL;

  if (ret == NULL)
    {
      ret = g_getenv ("MC_MANAGER_DIR");
      if (ret == NULL)
        ret = MANAGER_PATH;
    }

  return ret;
}

static gchar *
_mc_manager_filename (const gchar *unique_name)
{
  return g_strconcat (_mc_manager_path (), G_DIR_SEPARATOR_S,
                      unique_name, MANAGER_SUFFIX, NULL);
}

#define PREFIX_PROTOCOL "Protocol "
#define PREFIX_PROTOCOL_LEN 9
#define PREFIX_PROTOCOL_OLD "Proto "
#define PREFIX_PROTOCOL_OLD_LEN 6

static GSList *
_keyfile_get_protocols (GKeyFile *keyfile, const gchar *manager)
{
  GSList *protocols = NULL;
  gchar **groups = NULL;
  gchar **i;

  groups = g_key_file_get_groups (keyfile, NULL);

  for (i = groups; NULL != *i; i++)
    {
      const gchar *name = NULL;

      if (0 == strncmp (*i, PREFIX_PROTOCOL, PREFIX_PROTOCOL_LEN))
        name = *i + PREFIX_PROTOCOL_LEN;
      else if (0 == strncmp (*i, PREFIX_PROTOCOL_OLD, PREFIX_PROTOCOL_OLD_LEN))
        name = *i + PREFIX_PROTOCOL_OLD_LEN;

      if (NULL != name)
        {
          McProtocol *protocol = _mc_protocol_from_keyfile (keyfile,
            manager, *i, name);

          if (protocol)
            protocols = g_slist_prepend (protocols, protocol);
        }
    }

  g_strfreev (groups);
  return protocols;
}

static McManager *
_mc_manager_from_file (const gchar *unique_name, const gchar *filename)
{
  GError *error;
  GKeyFile *keyfile;
  gchar *bus_name = NULL;
  gchar *object_path = NULL;
  GSList *protocols = NULL;

  keyfile = g_key_file_new ();

  if (!g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, &error))
    {
      g_debug ("%s: loading %s failed: %s", G_STRFUNC, filename, error->message);
      g_error_free (error);
      return NULL;
    }

  bus_name = g_key_file_get_string (
    keyfile, "ConnectionManager", "BusName", NULL);
  object_path = g_key_file_get_string (
    keyfile, "ConnectionManager", "ObjectPath", NULL);

  if (!bus_name || !object_path)
    {
      g_debug ("%s: failed to get name, bus name and object path from file",
        G_STRFUNC);
      g_free (bus_name);
      g_free (object_path);
      return NULL;
    }

  protocols = _keyfile_get_protocols (keyfile, unique_name);
  g_key_file_free (keyfile);

  return mc_manager_new (g_strdup (unique_name), bus_name, object_path,
    protocols);
}

static time_t
_mc_manager_get_mtime (McManager *manager)
{
  McManagerPrivate *priv = MC_MANAGER_PRIV (manager);
  return priv->mtime;
}

/**
 * mc_manager_lookup:
 * @unique_name: the unique name.
 *
 * Looks up for the #McManager having the given unique name.
 * The returned object's reference count is incremented.
 *
 * Returns: the #McManager, or NULL if not found.
 */
McManager *
mc_manager_lookup (const gchar *unique_name)
{
  McManager *manager = NULL;
  gchar *filename;
  struct stat buf;

  g_return_val_if_fail (unique_name != NULL, NULL);
  g_return_val_if_fail (*unique_name != '\0', NULL);

  filename = _mc_manager_filename (unique_name);

  if (0 != g_stat (filename, &buf))
    goto OUT;

  if (NULL == manager_cache)
    manager_cache = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, g_object_unref);

  manager = g_hash_table_lookup (manager_cache, unique_name);

  if (NULL != manager && _mc_manager_get_mtime (manager) >= buf.st_mtime)
    {
      g_object_ref (manager);
      goto OUT;
    }

  manager = _mc_manager_from_file (unique_name, filename);

  if (NULL != manager)
    {
      McManagerPrivate *priv;
      priv = MC_MANAGER_PRIV (manager);
      priv->mtime = buf.st_mtime;
      g_hash_table_replace (manager_cache, g_strdup (unique_name), manager);
      g_object_ref (manager);
    }

OUT:
  g_free (filename);
  return manager;
}

/**
 * mc_manager_free:
 * @id: the #McManager.
 *
 * Frees (unrefs) the manager.
 */
void
mc_manager_free (McManager *id)
{
  g_return_if_fail (id != NULL);

  g_object_unref (id);
}

/**
 * mc_manager_clear_cache:
 *
 * Clears the managers cache.
 */
void
mc_manager_clear_cache(void)
{
  if (NULL == manager_cache)
    return;

  g_hash_table_destroy(manager_cache);
  manager_cache = NULL;
}

/**
 * mc_managers_list:
 *
 * Lists all configured managers. <emphasis>Currently this function returns
 * only the "gabble" manager</emphasis>.
 *
 * Returns: a #GList of the managers, to be freed with #mc_managers_free_list.
 */
GList *
mc_managers_list (void)
{
  return g_list_prepend (NULL, mc_manager_lookup ("gabble"));
}

/**
 * mc_managers_free_list:
 * @list: a #GList of #McManager.
 *
 * Frees a list of managers.
 */
void
mc_managers_free_list (GList *list)
{
  GList *tmp;

  for (tmp = list; tmp != NULL; tmp = tmp->next)
    mc_manager_free ((McManager *) tmp->data);

  g_list_free (list);
}

/**
 * mc_manager_get_unique_name:
 * @id: the #McManager.
 *
 * Gets the unique name of the manager.
 *
 * Returns: the unique name, as a string (not to be freed).
 */
const gchar *
mc_manager_get_unique_name (McManager *id)
{
  g_return_val_if_fail (id != NULL, NULL);

  return MC_MANAGER_PRIV (id)->unique_name;
}

/**
 * mc_manager_get_bus_name:
 * @id: the #McManager.
 *
 * Gets the D-Bus bus name of the manager.
 *
 * Returns: the bus name, as a string (not to be freed).
 */
const gchar *
mc_manager_get_bus_name (McManager *id)
{
  g_return_val_if_fail (id != NULL, NULL);

  return MC_MANAGER_PRIV (id)->bus_name;
}

/**
 * mc_manager_get_object_path:
 * @id: the #McManager.
 *
 * Gets the D-Bus object path of the manager.
 *
 * Returns: the object path, as a string (not to be freed).
 */
const gchar *
mc_manager_get_object_path (McManager *id)
{
  g_return_val_if_fail (id != NULL, NULL);

  return MC_MANAGER_PRIV (id)->object_path;
}

McProtocol *
_mc_manager_protocol_lookup (McManager *manager, const gchar *name)
{
  GSList *i;

  g_return_val_if_fail (manager != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (*name != '\0', NULL);

  for (i = MC_MANAGER_PRIV (manager)->protocols; NULL != i; i++)
    {
      McProtocol *protocol = (McProtocol *) i->data;

      if (0 == strcmp (name, mc_protocol_get_name (protocol)))
        {
          g_object_ref (protocol);
          return protocol;
        }
    }

  return NULL;
}

