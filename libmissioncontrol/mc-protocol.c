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

#include "config.h"
#undef MC_DISABLE_DEPRECATED
#include "mc-protocol.h"
#define MC_DISABLE_DEPRECATED

#define DBUS_API_SUBJECT_TO_CHANGE 1

#include <dbus/dbus.h>
#include <glib.h>
#include <string.h>

#include "mc-manager.h"
#include "mc-manager-priv.h"

#include "mc-protocol-priv.h"

#define MC_PROTOCOL_PRIV(protocol) ((McProtocolPrivate *)protocol->priv)

G_DEFINE_TYPE (McProtocol, mc_protocol, G_TYPE_OBJECT);

typedef struct
{
  gchar *manager;
  gchar *name;
  GSList *params;
} McProtocolPrivate;

static void
mc_protocol_init (McProtocol *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
      MC_TYPE_PROTOCOL, McProtocolPrivate);
}

static McProtocol *
_mc_protocol_new (const gchar *manager, const gchar *name, GSList *params)
{
    McProtocol *new = g_object_new (MC_TYPE_PROTOCOL, NULL);
    McProtocolPrivate *priv = MC_PROTOCOL_PRIV (new);

    priv->manager = g_strdup (manager);
    priv->name = g_strdup (name);
    priv->params = params;
    return new;
}

static void
mc_protocol_finalize (GObject *object)
{
  McProtocol *protocol = MC_PROTOCOL (object);
  McProtocolPrivate *priv = MC_PROTOCOL_PRIV (protocol);
  McProtocolParam *param;
  GSList *i;

  g_free (priv->manager);
  g_free (priv->name);

  for (i = priv->params; NULL != i; i = i->next)
    {
      param = (McProtocolParam *) i->data;
      
      g_free (i->data);
    }

  g_slist_free (priv->params);
}

static void
mc_protocol_class_init (McProtocolClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (object_class, sizeof (McProtocolPrivate));
  object_class->finalize = mc_protocol_finalize;
}

/**
 * mc_protocol_lookup:
 * @id: The #McManager.
 * @protocol: The protocol name.
 *
 * Looks up the protocol having the given name in the manager's supported
 * protocols. The returned object's reference count is incremented.
 *
 * Returns: the #McProtocol, or NULL if not found.
 */
McProtocol *
mc_protocol_lookup (McManager *manager, const gchar *protocol)
{
  return _mc_manager_protocol_lookup (manager, protocol);
}

/**
 * mc_protocol_free:
 * @id: The #McProtocol.
 *
 * Frees (unrefs) the protocol.
 * DEPRECATED, use g_object_unref() instead.
 */
void
mc_protocol_free (McProtocol *id)
{
  g_return_if_fail (id != NULL);

  g_object_unref (id);
}

/**
 * mc_protocols_list:
 *
 * Lists all supported protocols. <emphasis>This currently lists all protocols
 * supported by the "gabble" manager</emphasis>.
 *
 * Returns: a #GList of #McProtocol, to be freed with #mc_protocols_free_list.
 */
GList *
mc_protocols_list (void)
{
  return mc_protocols_list_by_manager (mc_manager_lookup ("gabble"));
}

/**
 * mc_protocols_list_by_manager:
 * @id: a #McManager.
 *
 * Lists all protocols supported by the given manager.
 *
 * Returns: a #GList of #McProtocol, to be freed with #mc_protocols_free_list.
 */
GList *
mc_protocols_list_by_manager (McManager *id)
{
  return g_list_prepend (NULL, mc_protocol_lookup (id, "jabber"));
}

/**
 * mc_protocols_free_list:
 * @list: a #GList of #McProtocol.
 *
 * Frees a list of protocols.
 */
void
mc_protocols_free_list (GList *list)
{
  GList *tmp;

  for (tmp = list; tmp != NULL; tmp = tmp->next)
    g_object_unref ((McProtocol *) tmp->data);

  g_list_free (list);
}

/**
 * mc_protocol_get_manager:
 * @id: The #McProtocol.
 *
 * Gets the manager for this protocol.
 *
 * Returns: the #McManager, or NULL if some error occurred.
 */
McManager *
mc_protocol_get_manager (McProtocol *id)
{
  g_return_val_if_fail (id != NULL, NULL);

  return mc_manager_lookup (MC_PROTOCOL_PRIV (id)->manager);
}

/**
 * mc_protocol_get_name:
 * @id: The #McProtocol.
 *
 * Gets the name of this protocol.
 *
 * Returns: a string representing the name (not to be freed)
 */
const gchar *
mc_protocol_get_name (McProtocol *id)
{
  g_return_val_if_fail (id != NULL, NULL);

  return MC_PROTOCOL_PRIV (id)->name;
}

/**
 * mc_protocol_get_params:
 * @protocol: The #McProtocol.
 *
 * Gets the parameters for this protocol.
 *
 * Returns: a #GList of #McProtocolParam, to be freed with
 * #mc_protocol_free_params_list.
 */
GSList *
mc_protocol_get_params (McProtocol *protocol)
{
  McProtocolPrivate *priv = MC_PROTOCOL_PRIV (protocol);

  return g_slist_copy (priv->params);
}

/**
 * mc_protocol_free_params_list:
 * @list: The #GList.
 *
 * Frees a list of #McProtocolParam.
 */
void
mc_protocol_free_params_list (GSList *list)
{
  g_slist_free (list);
}

static McProtocolParam *
_parse_parameter (const gchar *name, const gchar *s)
{
  McProtocolParam *new;
  gchar **bits;
  gchar **i;

  bits = g_strsplit (s, " ", 0);

  if (NULL == *bits)
    {
      g_debug ("%s: param \"%s\" has no signature", G_STRFUNC, name);
      return NULL;
    }

  if (1 != strlen (*bits))
    {
      g_debug ("%s: param \"%s\" has invalid signature", G_STRFUNC, name);
      return NULL;
    }

  new = g_new0 (McProtocolParam, 1);
  new->name = g_strdup (name);
  new->signature = g_strdup (*bits);
  new->def = NULL;

  for (i = bits + 1; NULL != *i; i++)
    {
      if (0 == strcmp (*i, "required"))
        new->flags |= MC_PROTOCOL_PARAM_REQUIRED;
      else if (0 == strcmp (*i, "register"))
        new->flags |= MC_PROTOCOL_PARAM_REGISTER;
      else
          g_debug ("%s: unrecognised parameter flag \"%s\"", G_STRFUNC, *i);
    }

  g_strfreev (bits);
  return new;
}

#define PREFIX_PARAM "param-"
#define PREFIX_PARAM_LEN 6
#define PREFIX_DEFAULT "default-"
#define PREFIX_DEFAULT_LEN 8

static gint
find_param_by_name_func(gconstpointer a, gconstpointer b)
{
  McProtocolParam *param = (McProtocolParam *) a;
  const gchar *name = (const gchar *) b;

  return !((NULL != param->name) && (NULL != name)
             && (0 == strcmp(param->name, name)));
}

McProtocol *
_mc_protocol_from_keyfile (GKeyFile *keyfile, const gchar *manager_name,
                              const gchar *group_name, const gchar *name)
{
  GSList *params = NULL;
  gchar **keys;
  gchar **i;

  g_assert (name);
  keys = g_key_file_get_keys (keyfile, group_name, NULL, NULL);

  if (!keys)
    {
      g_debug ("%s: failed to get keys from file", G_STRFUNC);
      return NULL;
    }

  for (i = keys; NULL != *i; i++)
    {
      McProtocolParam *param;
      const gchar *key = *i;
      gchar *value = g_key_file_get_string (keyfile, group_name, key, NULL);

      if (0 == strncmp (key, PREFIX_PARAM, PREFIX_PARAM_LEN))
        {
          key += PREFIX_PARAM_LEN;
          param = _parse_parameter (key, value);

          if (param)
            params = g_slist_prepend (params, param);
        }
      else if (0 == strncmp (key, PREFIX_DEFAULT, PREFIX_DEFAULT_LEN))
        {
          GSList *node;
          
          key += PREFIX_DEFAULT_LEN;
          node = g_slist_find_custom (params, key, find_param_by_name_func);

          if (node)
            {
              param = (McProtocolParam *) node->data;

              if (!param->def)
                param->def = g_strdup(value);
              else
                g_warning("%s: encountered multiple default values for parameter \"%s\"", G_STRFUNC, name);
            }
        }
      else
        {
          g_debug ("%s: unrecognised protocol key \"%s\"", G_STRFUNC, key);
        }

      g_free (value);
    }

  g_strfreev (keys);

  return _mc_protocol_new (manager_name, name, params);
}

/**
 * mc_protocol_print:
 * @protocol: the #McProtocol.
 *
 * Prints the protocol name and all protocol parameters via #g_print.
 */
void
mc_protocol_print (McProtocol *protocol)
{
  GSList *i;

  g_print ("protocol: %s\n", mc_protocol_get_name (protocol));

  for (i = mc_protocol_get_params (protocol); NULL != i; i = i->next)
    {
      McProtocolParam *param = (McProtocolParam *) i->data;

      g_print ("  %s:%s:%s\n", param->signature, param->name, param->def);
    }
}

