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
#include "mcd-handler-map-priv.h"

#include <telepathy-glib/telepathy-glib.h>

#include "channel-utils.h"
#include "mcd-channel-priv.h"

G_DEFINE_TYPE (McdHandlerMap, _mcd_handler_map, G_TYPE_OBJECT);

struct _McdHandlerMapPrivate
{
    TpDBusDaemon *dbus_daemon;
    /* The handler for each channel currently being handled
     * owned gchar *object_path => owned gchar *unique_name */
    GHashTable *channel_processes;
    /* The well-known bus name we invoked in channel_processes[path]
     * owned gchar *object_path => owned gchar *well_known_name */
    GHashTable *channel_clients;
    /* owned gchar *unique_name => malloc'd gsize, number of channels */
    GHashTable *handler_processes;
    /* owned gchar *object_path => ref'd TpChannel */
    GHashTable *handled_channels;
    /* owned gchar *object_path =>  owned gchar *account_path */
    GHashTable *channel_accounts;
};

enum {
    PROP_0,
    PROP_DBUS_DAEMON
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

    self->priv->channel_clients = g_hash_table_new_full (g_str_hash,
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

    self->priv->channel_accounts = g_hash_table_new_full (g_str_hash,
                                                          g_str_equal,
                                                          g_free,
                                                          g_free);
}

static void
_mcd_handler_map_get_property (GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
    McdHandlerMap *self = MCD_HANDLER_MAP (object);

    switch (prop_id)
    {
        case PROP_DBUS_DAEMON:
            g_value_set_object (value, self->priv->dbus_daemon);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
_mcd_handler_map_set_property (GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
    McdHandlerMap *self = MCD_HANDLER_MAP (object);

    switch (prop_id)
    {
        case PROP_DBUS_DAEMON:
            g_assert (self->priv->dbus_daemon == NULL); /* construct-only */
            self->priv->dbus_daemon =
                TP_DBUS_DAEMON (g_value_dup_object (value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void mcd_handler_map_name_owner_cb (TpDBusDaemon *dbus_daemon,
                                           const gchar *name,
                                           const gchar *new_owner,
                                           gpointer user_data);

static void
_mcd_handler_map_dispose (GObject *object)
{
    McdHandlerMap *self = MCD_HANDLER_MAP (object);

    tp_clear_pointer (&self->priv->handled_channels, g_hash_table_unref);

    if (self->priv->handler_processes != NULL)
    {
        GHashTableIter iter;
        gpointer k;

        g_assert (self->priv->dbus_daemon != NULL);

        g_hash_table_iter_init (&iter, self->priv->handler_processes);

        while (g_hash_table_iter_next (&iter, &k, NULL))
        {
            tp_dbus_daemon_cancel_name_owner_watch (self->priv->dbus_daemon,
                k, mcd_handler_map_name_owner_cb, object);
        }

    }

    tp_clear_pointer (&self->priv->handler_processes, g_hash_table_unref);
    tp_clear_object (&self->priv->dbus_daemon);

    G_OBJECT_CLASS (_mcd_handler_map_parent_class)->dispose (object);
}

static void
_mcd_handler_map_finalize (GObject *object)
{
    McdHandlerMap *self = MCD_HANDLER_MAP (object);

    tp_clear_pointer (&self->priv->channel_processes, g_hash_table_unref);
    tp_clear_pointer (&self->priv->channel_clients, g_hash_table_unref);
    tp_clear_pointer (&self->priv->channel_accounts, g_hash_table_unref);

    G_OBJECT_CLASS (_mcd_handler_map_parent_class)->finalize (object);
}

static void
_mcd_handler_map_class_init (McdHandlerMapClass *klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;

    g_type_class_add_private (object_class, sizeof (McdHandlerMapPrivate));
    object_class->dispose = _mcd_handler_map_dispose;
    object_class->get_property = _mcd_handler_map_get_property;
    object_class->set_property = _mcd_handler_map_set_property;
    object_class->finalize = _mcd_handler_map_finalize;

    g_object_class_install_property (object_class, PROP_DBUS_DAEMON,
        g_param_spec_object ("dbus-daemon", "D-Bus daemon", "D-Bus daemon",
            TP_TYPE_DBUS_DAEMON,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
            G_PARAM_STATIC_STRINGS));
}

McdHandlerMap *
_mcd_handler_map_new (TpDBusDaemon *dbus_daemon)
{
    return g_object_new (MCD_TYPE_HANDLER_MAP,
                         "dbus-daemon", dbus_daemon,
                         NULL);
}

/*
 * @well_known_name: (out): the well-known Client name of the handler,
 *  or %NULL if not known (or if it's Mission Control itself)
 *
 * Returns: (transfer none): the unique name of the handler
 */
const gchar *
_mcd_handler_map_get_handler (McdHandlerMap *self,
                              const gchar *channel_path,
                              const gchar **well_known_name)
{
    if (well_known_name != NULL)
        *well_known_name = g_hash_table_lookup (self->priv->channel_clients,
                                                channel_path);

    return g_hash_table_lookup (self->priv->channel_processes, channel_path);
}

/*
 * @channel_path: a channel
 * @unique_name: the unique name of the handler
 * @well_known_name: the well-known name of the handler, or %NULL if not known
 *  (or if it's Mission Control itself)
 *
 * Record that @channel_path is being handled by the Client
 * @well_known_name, whose unique name is @unique_name.
 *
 * Returns: (transfer none): the unique name of the handler
 */
void
_mcd_handler_map_set_path_handled (McdHandlerMap *self,
                                   const gchar *channel_path,
                                   const gchar *unique_name,
                                   const gchar *well_known_name)
{
    const gchar *old;
    gsize *counter;

    /* In case we want to re-invoke the same client later, remember its
     * well-known name, if we know it. (In edge cases where we're recovering
     * from an MC crash, we can only guess, so we get NULL.) */
    if (well_known_name == NULL)
        g_hash_table_remove (self->priv->channel_clients, channel_path);
    else
        g_hash_table_insert (self->priv->channel_clients,
                             g_strdup (channel_path),
                             g_strdup (well_known_name));

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
            tp_dbus_daemon_cancel_name_owner_watch (self->priv->dbus_daemon,
                old, mcd_handler_map_name_owner_cb, self);
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
        tp_dbus_daemon_watch_name_owner (self->priv->dbus_daemon, unique_name,
                                         mcd_handler_map_name_owner_cb, self,
                                         NULL);
    }
    else
    {
        ++*counter;
    }
}

static void
handled_channel_invalidated_cb (TpChannel *channel,
                                guint domain,
                                gint code,
                                const gchar *message,
                                gpointer user_data)
{
    McdHandlerMap *self = MCD_HANDLER_MAP (user_data);
    const gchar *path = tp_proxy_get_object_path (channel);
    gchar *handler;

    g_signal_handlers_disconnect_by_func (channel,
                                          handled_channel_invalidated_cb,
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
    g_hash_table_remove (self->priv->channel_accounts, path);

    g_object_unref (self);
}

/*
 * @channel: a channel
 * @unique_name: the unique name of the handler
 * @well_known_name: the well-known name of the handler, or %NULL if not known
 *  (or if it's Mission Control itself)
 * @account_path: the account that @channel came from, or %NULL if not known
 *
 * Record that @channel_path is being handled by the Client
 * @well_known_name, whose unique name is @unique_name.
 * The record will be removed if the channel closes or is invalidated.
 *
 * Returns: (transfer none): the unique name of the handler
 */
void
_mcd_handler_map_set_channel_handled (McdHandlerMap *self,
                                      TpChannel *channel,
                                      const gchar *unique_name,
                                      const gchar *well_known_name,
                                      const gchar *account_path)
{
    const gchar *path = tp_proxy_get_object_path (channel);

    g_hash_table_insert (self->priv->handled_channels,
                         g_strdup (path),
                         g_object_ref (channel));

    g_hash_table_insert (self->priv->channel_accounts,
                         g_strdup (path),
                         g_strdup (account_path));

    g_signal_connect (channel, "invalidated",
                      G_CALLBACK (handled_channel_invalidated_cb),
                      g_object_ref (self));

    _mcd_handler_map_set_path_handled (self, path, unique_name,
                                       well_known_name);
}

static void
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

        tp_dbus_daemon_cancel_name_owner_watch (self->priv->dbus_daemon,
                                                unique_name,
                                                mcd_handler_map_name_owner_cb,
                                                self);
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
            TpChannel *channel = g_hash_table_lookup (
                self->priv->handled_channels, path);

            /* this is NULL-safe */
            if (_mcd_tp_channel_should_close (channel, "closing"))
            {
                DEBUG ("Closing channel %s", path);
                /* the corresponding McdChannel will get aborted when the
                 * Channel actually closes */
                tp_cli_channel_call_close (channel, -1,
                                           NULL, NULL, NULL, NULL);
            }

            paths = g_list_delete_link (paths, paths);
            g_free (path);
        }
    }
}

static void
mcd_handler_map_name_owner_cb (TpDBusDaemon *dbus_daemon,
                               const gchar *name,
                               const gchar *new_owner,
                               gpointer user_data)
{
    if (new_owner == NULL || new_owner[0] == '\0')
    {
        _mcd_handler_map_set_handler_crashed (user_data, name);
    }
}

/*
 * Returns: (transfer container): all channels that are being handled
 */
GList *
_mcd_handler_map_get_handled_channels (McdHandlerMap *self)
{
    return g_hash_table_get_values (self->priv->handled_channels);
}

/*
 * Returns: (transfer none): the account that @channel_path belongs to,
 *  or %NULL if not known
 */
const gchar *
_mcd_handler_map_get_channel_account (McdHandlerMap *self,
    const gchar *channel_path)
{
    return g_hash_table_lookup (self->priv->channel_accounts,
        channel_path);
}

/*
 * Record that MC itself is handling this channel, internally.
 */
void
_mcd_handler_map_set_channel_handled_internally (McdHandlerMap *self,
                                                 TpChannel *channel,
                                                 const gchar *account_path)
{
    _mcd_handler_map_set_channel_handled (self, channel,
        tp_dbus_daemon_get_unique_name (self->priv->dbus_daemon),
        NULL, account_path);
}
