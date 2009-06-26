/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * Keep track of which handlers own which channels.
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
#include "mcd-channel-priv.h"
#include "mcd-handler-map-priv.h"

#include <telepathy-glib/util.h>

G_DEFINE_TYPE (McdHandlerMap, _mcd_handler_map, G_TYPE_OBJECT);

struct _McdHandlerMapPrivate
{
    /* The handler for each channel currently being handled
     * owned gchar *object_path => owned gchar *unique_name */
    GHashTable *channel_processes;
    /* owned gchar *unique_name => malloc'd gsize, number of channels */
    GHashTable *handler_processes;
    /* owned gchar *object_path => ref'd McdChannel */
    GHashTable *handled_channels;
};

static void
slice_free_gsize (gpointer p)
{
    g_slice_free (gsize, p);
}

static void
_mcd_handler_map_init (McdHandlerMap *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MCD_TYPE_HANDLER_MAP,
                                              McdHandlerMapPrivate);

    self->priv->channel_processes = g_hash_table_new_full (g_str_hash,
                                                           g_str_equal,
                                                           g_free, g_free);

    self->priv->handler_processes = g_hash_table_new_full (g_str_hash,
                                                           g_str_equal,
                                                           g_free,
                                                           slice_free_gsize);

    self->priv->handled_channels = g_hash_table_new_full (g_str_hash,
                                                          g_str_equal,
                                                          g_free,
                                                          g_object_unref);
}

static void
_mcd_handler_map_dispose (GObject *object)
{
    McdHandlerMap *self = MCD_HANDLER_MAP (object);

    if (self->priv->handled_channels != NULL)
    {
        g_hash_table_destroy (self->priv->handled_channels);
        self->priv->handled_channels = NULL;
    }

    G_OBJECT_CLASS (_mcd_handler_map_parent_class)->dispose (object);
}

static void
_mcd_handler_map_finalize (GObject *object)
{
    McdHandlerMap *self = MCD_HANDLER_MAP (object);

    if (self->priv->channel_processes != NULL)
    {
        g_hash_table_destroy (self->priv->channel_processes);
        self->priv->channel_processes = NULL;
    }

    if (self->priv->handler_processes != NULL)
    {
        g_hash_table_destroy (self->priv->handler_processes);
        self->priv->handler_processes = NULL;
    }

    G_OBJECT_CLASS (_mcd_handler_map_parent_class)->finalize (object);
}

static void
_mcd_handler_map_class_init (McdHandlerMapClass *klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;

    g_type_class_add_private (object_class, sizeof (McdHandlerMapPrivate));
    object_class->dispose = _mcd_handler_map_dispose;
    object_class->finalize = _mcd_handler_map_finalize;
}

McdHandlerMap *
_mcd_handler_map_new (void)
{
    return g_object_new (MCD_TYPE_HANDLER_MAP,
                         NULL);
}

const gchar *
_mcd_handler_map_get_handler (McdHandlerMap *self,
                              const gchar *channel_path)
{
    return g_hash_table_lookup (self->priv->channel_processes, channel_path);
}

void
_mcd_handler_map_set_path_handled (McdHandlerMap *self,
                                   const gchar *channel_path,
                                   const gchar *unique_name)
{
    const gchar *old;
    gsize *counter;

    old = g_hash_table_lookup (self->priv->channel_processes, channel_path);

    if (!tp_strdiff (old, unique_name))
    {
        /* no-op - the new handler is the same as the old */
        return;
    }

    if (old != NULL)
    {
        counter = g_hash_table_lookup (self->priv->handler_processes,
                                       old);

        if (--*counter == 0)
        {
            g_hash_table_remove (self->priv->handler_processes, old);
        }
    }

    g_hash_table_insert (self->priv->channel_processes,
                         g_strdup (channel_path), g_strdup (unique_name));

    counter = g_hash_table_lookup (self->priv->handler_processes,
                                   unique_name);

    if (counter == NULL)
    {
        counter = g_slice_new (gsize);
        *counter = 1;
        g_hash_table_insert (self->priv->handler_processes,
                             g_strdup (unique_name), counter);
    }
    else
    {
        ++*counter;
    }
}

static void
handled_channel_aborted_cb (McdChannel *channel,
                            gpointer user_data)
{
    McdHandlerMap *self = MCD_HANDLER_MAP (user_data);
    const gchar *path = mcd_channel_get_object_path (channel);
    gchar *handler;

    g_signal_handlers_disconnect_by_func (channel,
                                          handled_channel_aborted_cb,
                                          user_data);

    handler = g_hash_table_lookup (self->priv->channel_processes, path);

    if (handler != NULL)
    {
        gsize *counter = g_hash_table_lookup (self->priv->handler_processes,
                                              handler);

        g_assert (counter != NULL);

        if (--*counter == 0)
        {
            g_hash_table_remove (self->priv->handler_processes, handler);
        }

        g_hash_table_remove (self->priv->channel_processes, path);
    }

    g_hash_table_remove (self->priv->handled_channels, path);

    g_object_unref (self);
}

void
_mcd_handler_map_set_channel_handled (McdHandlerMap *self,
                                      McdChannel *channel,
                                      const gchar *unique_name)
{
    const gchar *path = mcd_channel_get_object_path (channel);

    g_hash_table_insert (self->priv->handled_channels,
                         g_strdup (path),
                         g_object_ref (channel));

    g_signal_connect (channel, "abort",
                      G_CALLBACK (handled_channel_aborted_cb),
                      g_object_ref (self));

    _mcd_handler_map_set_path_handled (self, path, unique_name);
}

void
_mcd_handler_map_set_handler_crashed (McdHandlerMap *self,
                                      const gchar *unique_name)
{
    gsize *counter = g_hash_table_lookup (self->priv->handler_processes,
                                          unique_name);

    if (counter != NULL)
    {
        GHashTableIter iter;
        gpointer path_p, name_p;
        GList *paths = NULL;

        g_hash_table_remove (self->priv->handler_processes, unique_name);

        /* This is O(number of channels being handled) but then again
         * it only happens if a handler crashes */
        g_hash_table_iter_init (&iter, self->priv->channel_processes);

        while (g_hash_table_iter_next (&iter, &path_p, &name_p))
        {
            if (!tp_strdiff (name_p, unique_name))
            {
                DEBUG ("%s lost its handler %s", (const gchar *) path_p,
                       (const gchar *) name_p);
                paths = g_list_prepend (paths, g_strdup (path_p));
                g_hash_table_iter_remove (&iter);
            }
        }

        while (paths != NULL)
        {
            gchar *path = paths->data;
            McdChannel *channel = g_hash_table_lookup (
                self->priv->handled_channels, path);

            if (channel != NULL)
            {
                DEBUG ("Closing channel %s", path);
                /* channel will get aborted when it actually closes */
                _mcd_channel_close (channel);
            }
            else
            {
                DEBUG ("No McdChannel for %s, not aborting it", path);
            }

            paths = g_list_delete_link (paths, paths);
            g_free (path);
        }
    }
}
