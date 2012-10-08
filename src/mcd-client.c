/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * Mission Control client proxy.
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 * Copyright (C) 2009-2010 Collabora Ltd.
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
#include "mcd-client-priv.h"

#include <errno.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <telepathy-glib/proxy-subclass.h>

#include "channel-utils.h"
#include "mcd-channel-priv.h"
#include "mcd-debug.h"

G_DEFINE_TYPE (McdClientProxy, _mcd_client_proxy, TP_TYPE_CLIENT);

enum
{
    PROP_0,
    PROP_ACTIVATABLE,
    PROP_STRING_POOL,
    PROP_UNIQUE_NAME,
};

enum
{
    S_READY,
    S_IS_HANDLING_CHANNEL,
    S_HANDLER_CAPABILITIES_CHANGED,
    S_GONE,
    S_NEED_RECOVERY,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

struct _McdClientProxyPrivate
{
    GStrv capability_tokens;

    gchar *unique_name;
    guint ready_lock;
    gboolean introspect_started;
    gboolean ready;
    gboolean bypass_approval;
    gboolean bypass_observers;
    gboolean delay_approvers;
    gboolean recover;

    /* If a client was in the ListActivatableNames list, it must not be
     * removed when it disappear from the bus.
     */
    gboolean activatable;

    /* Channel filters
     * A channel filter is a GHashTable of
     * - key: gchar *property_name
     * - value: GValue of one of the allowed types on the ObserverChannelFilter
     *          spec. The following matching is observed:
     *           * G_TYPE_STRING: 's'
     *           * G_TYPE_BOOLEAN: 'b'
     *           * DBUS_TYPE_G_OBJECT_PATH: 'o'
     *           * G_TYPE_UINT64: 'y' (8b), 'q' (16b), 'u' (32b), 't' (64b)
     *           * G_TYPE_INT64:            'n' (16b), 'i' (32b), 'x' (64b)
     *
     * The list can be NULL if there is no filter, or the filters are not yet
     * retrieven from the D-Bus *ChannelFitler properties. In the last case,
     * the dispatcher just don't dispatch to this client.
     */
    GList *approver_filters;
    GList *handler_filters;
    GList *observer_filters;

    gboolean disposed;
};

typedef enum
{
    MCD_CLIENT_APPROVER,
    MCD_CLIENT_HANDLER,
    MCD_CLIENT_OBSERVER
} McdClientInterface;

void
_mcd_client_proxy_inc_ready_lock (McdClientProxy *self)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));

    if (self->priv->ready)
        return;

    g_return_if_fail (self->priv->ready_lock > 0);

    self->priv->ready_lock++;
}

void
_mcd_client_proxy_dec_ready_lock (McdClientProxy *self)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));

    if (self->priv->ready)
        return;

    g_return_if_fail (self->priv->ready_lock > 0);

    if (--self->priv->ready_lock == 0)
    {
        self->priv->ready = TRUE;
        g_signal_emit (self, signals[S_READY], 0);

        /* Activatable Observers needing recovery have already
         * been called (in order to reactivate them). */
        if (self->priv->recover && !self->priv->activatable)
            g_signal_emit (self, signals[S_NEED_RECOVERY], 0);
    }
}

static void _mcd_client_proxy_take_approver_filters
    (McdClientProxy *self, GList *filters);
static void _mcd_client_proxy_take_observer_filters
    (McdClientProxy *self, GList *filters);
static void _mcd_client_proxy_take_handler_filters
    (McdClientProxy *self, GList *filters);

static gchar *
_mcd_client_proxy_find_client_file (const gchar *client_name)
{
    const gchar * const *dirs;
    const gchar *dirname;
    const gchar *env_dirname;
    gchar *filename, *absolute_filepath;

    /*
     * The full path is $XDG_DATA_DIRS/telepathy/clients/clientname.client
     * or $XDG_DATA_HOME/telepathy/clients/clientname.client
     * For testing purposes, we also look for $MC_CLIENTS_DIR/clientname.client
     * if $MC_CLIENTS_DIR is set.
     */
    filename = g_strdup_printf ("%s.client", client_name);
    env_dirname = g_getenv ("MC_CLIENTS_DIR");
    if (env_dirname)
    {
        absolute_filepath = g_build_filename (env_dirname, filename, NULL);
        if (g_file_test (absolute_filepath, G_FILE_TEST_IS_REGULAR))
            goto finish;
        g_free (absolute_filepath);
    }

    dirname = g_get_user_data_dir ();
    if (G_LIKELY (dirname))
    {
        absolute_filepath = g_build_filename (dirname, "telepathy/clients",
                                              filename, NULL);
        if (g_file_test (absolute_filepath, G_FILE_TEST_IS_REGULAR))
            goto finish;
        g_free (absolute_filepath);
    }

    dirs = g_get_system_data_dirs ();
    for (dirname = *dirs; dirname != NULL; dirs++, dirname = *dirs)
    {
        absolute_filepath = g_build_filename (dirname, "telepathy/clients",
                                              filename, NULL);
        if (g_file_test (absolute_filepath, G_FILE_TEST_IS_REGULAR))
            goto finish;
        g_free (absolute_filepath);
    }

    absolute_filepath = NULL;
finish:
    g_free (filename);
    return absolute_filepath;
}

static GHashTable *
parse_client_filter (GKeyFile *file, const gchar *group)
{
    GHashTable *filter;
    gchar **keys;
    gsize len;
    guint i;

    filter = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                    (GDestroyNotify) tp_g_value_slice_free);

    keys = g_key_file_get_keys (file, group, &len, NULL);

    if (keys == NULL)
        len = 0;

    for (i = 0; i < len; i++)
    {
        const gchar *key;
        const gchar *space;
        gchar *file_property;
        gchar file_property_type;

        key = keys[i];
        space = g_strrstr (key, " ");

        if (space == NULL || space[1] == '\0' || space[2] != '\0')
        {
            g_warning ("Invalid key %s in client file", key);
            continue;
        }
        file_property_type = space[1];
        file_property = g_strndup (key, space - key);

        switch (file_property_type)
        {
        case 'q':
        case 'u':
        case 't': /* unsigned integer */
            {
                /* g_key_file_get_integer cannot be used because we need
                 * to support 64 bits */
                guint x;
                GValue *value = tp_g_value_slice_new (G_TYPE_UINT64);
                gchar *str = g_key_file_get_string (file, group, key,
                                                    NULL);
                errno = 0;
                x = g_ascii_strtoull (str, NULL, 0);
                if (errno != 0)
                {
                    g_warning ("Invalid unsigned integer '%s' in client"
                               " file", str);
                }
                else
                {
                    g_value_set_uint64 (value, x);
                    g_hash_table_insert (filter, file_property, value);
                }
                g_free (str);
                break;
            }

        case 'y':
        case 'n':
        case 'i':
        case 'x': /* signed integer */
            {
                gint x;
                GValue *value = tp_g_value_slice_new (G_TYPE_INT64);
                gchar *str = g_key_file_get_string (file, group, key, NULL);
                errno = 0;
                x = g_ascii_strtoll (str, NULL, 0);
                if (errno != 0)
                {
                    g_warning ("Invalid signed integer '%s' in client"
                               " file", str);
                }
                else
                {
                    g_value_set_int64 (value, x);
                    g_hash_table_insert (filter, file_property, value);
                }
                g_free (str);
                break;
            }

        case 'b':
            {
                GValue *value = tp_g_value_slice_new (G_TYPE_BOOLEAN);
                gboolean b = g_key_file_get_boolean (file, group, key, NULL);
                g_value_set_boolean (value, b);
                g_hash_table_insert (filter, file_property, value);
                break;
            }

        case 's':
            {
                GValue *value = tp_g_value_slice_new (G_TYPE_STRING);
                gchar *str = g_key_file_get_string (file, group, key, NULL);

                g_value_take_string (value, str);
                g_hash_table_insert (filter, file_property, value);
                break;
            }

        case 'o':
            {
                GValue *value = tp_g_value_slice_new
                    (DBUS_TYPE_G_OBJECT_PATH);
                gchar *str = g_key_file_get_string (file, group, key, NULL);

                g_value_take_boxed (value, str);
                g_hash_table_insert (filter, file_property, value);
                break;
            }

        default:
            g_warning ("Invalid key %s in client file", key);
            continue;
        }
    }
    g_strfreev (keys);

    return filter;
}

static void _mcd_client_proxy_set_cap_tokens (McdClientProxy *self,
                                              GStrv cap_tokens);
static void _mcd_client_proxy_add_interfaces (McdClientProxy *self,
                                              const gchar * const *interfaces);

static void
parse_client_file (McdClientProxy *client,
                   GKeyFile *file)
{
    gchar **iface_names, **groups, **cap_tokens;
    guint i;
    gsize len = 0;
    gboolean is_approver, is_handler, is_observer;
    GList *approver_filters = NULL;
    GList *observer_filters = NULL;
    GList *handler_filters = NULL;

    iface_names = g_key_file_get_string_list (file, TP_IFACE_CLIENT,
                                              "Interfaces", 0, NULL);
    if (!iface_names)
        return;

    _mcd_client_proxy_add_interfaces (client,
                                      (const gchar * const *) iface_names);
    g_strfreev (iface_names);

    is_approver = tp_proxy_has_interface_by_id (client,
                                                TP_IFACE_QUARK_CLIENT_APPROVER);
    is_observer = tp_proxy_has_interface_by_id (client,
                                                TP_IFACE_QUARK_CLIENT_OBSERVER);
    is_handler = tp_proxy_has_interface_by_id (client,
                                               TP_IFACE_QUARK_CLIENT_HANDLER);

    /* parse filtering rules */
    groups = g_key_file_get_groups (file, &len);
    for (i = 0; i < len; i++)
    {
        if (is_approver &&
            g_str_has_prefix (groups[i], TP_IFACE_CLIENT_APPROVER
                              ".ApproverChannelFilter "))
        {
            approver_filters =
                g_list_prepend (approver_filters,
                                parse_client_filter (file, groups[i]));
        }
        else if (is_handler &&
            g_str_has_prefix (groups[i], TP_IFACE_CLIENT_HANDLER
                              ".HandlerChannelFilter "))
        {
            handler_filters =
                g_list_prepend (handler_filters,
                                parse_client_filter (file, groups[i]));
        }
        else if (is_observer &&
            g_str_has_prefix (groups[i], TP_IFACE_CLIENT_OBSERVER
                              ".ObserverChannelFilter "))
        {
            observer_filters =
                g_list_prepend (observer_filters,
                                parse_client_filter (file, groups[i]));
        }
    }
    g_strfreev (groups);

    _mcd_client_proxy_take_approver_filters (client,
                                             approver_filters);
    _mcd_client_proxy_take_observer_filters (client,
                                             observer_filters);
    _mcd_client_proxy_take_handler_filters (client,
                                            handler_filters);

    /* Other client options */
    client->priv->bypass_approval =
        g_key_file_get_boolean (file, TP_IFACE_CLIENT_HANDLER,
                                "BypassApproval", NULL);

    client->priv->bypass_observers =
        g_key_file_get_boolean (file, TP_IFACE_CLIENT_HANDLER,
                                "BypassObservers", NULL);

    client->priv->delay_approvers =
        g_key_file_get_boolean (file, TP_IFACE_CLIENT_OBSERVER,
                                "DelayApprovers", NULL);

    client->priv->recover =
        g_key_file_get_boolean (file, TP_IFACE_CLIENT_OBSERVER,
                                "Recover", NULL);

    cap_tokens = g_key_file_get_keys (file,
                                      TP_IFACE_CLIENT_HANDLER ".Capabilities",
                                      NULL,
                                      NULL);
    _mcd_client_proxy_set_cap_tokens (client, cap_tokens);
    g_strfreev (cap_tokens);
}

static void
_mcd_client_proxy_set_filters (McdClientProxy *client,
                               McdClientInterface interface,
                               GPtrArray *filters)
{
    GList *client_filters = NULL;
    guint i;

    for (i = 0 ; i < filters->len ; i++)
    {
        GHashTable *channel_class = g_ptr_array_index (filters, i);
        GHashTable *new_channel_class;
        GHashTableIter iter;
        gchar *property_name;
        GValue *property_value;
        gboolean valid_filter = TRUE;

        new_channel_class = g_hash_table_new_full
            (g_str_hash, g_str_equal, g_free,
             (GDestroyNotify) tp_g_value_slice_free);

        g_hash_table_iter_init (&iter, channel_class);
        while (g_hash_table_iter_next (&iter, (gpointer *) &property_name,
                                       (gpointer *) &property_value)) 
        {
            GValue *filter_value;
            GType property_type = G_VALUE_TYPE (property_value);

            if (property_type == G_TYPE_BOOLEAN ||
                property_type == G_TYPE_STRING ||
                property_type == DBUS_TYPE_G_OBJECT_PATH)
            {
                filter_value = tp_g_value_slice_new
                    (G_VALUE_TYPE (property_value));
                g_value_copy (property_value, filter_value);
            }
            else if (property_type == G_TYPE_UCHAR ||
                     property_type == G_TYPE_UINT ||
                     property_type == G_TYPE_UINT64)
            {
                filter_value = tp_g_value_slice_new (G_TYPE_UINT64);
                g_value_transform (property_value, filter_value);
            }
            else if (property_type == G_TYPE_INT ||
                     property_type == G_TYPE_INT64)
            {
                filter_value = tp_g_value_slice_new (G_TYPE_INT64);
                g_value_transform (property_value, filter_value);
            }
            else
            {
                /* invalid type, do not add this filter */
                g_warning ("%s: Property %s has an invalid type (%s)",
                           G_STRFUNC, property_name,
                           g_type_name (G_VALUE_TYPE (property_value)));
                valid_filter = FALSE;
                break;
            }

            g_hash_table_insert (new_channel_class, g_strdup (property_name),
                                 filter_value);
        }

        if (valid_filter)
            client_filters = g_list_prepend (client_filters,
                                             new_channel_class);
        else
            g_hash_table_unref (new_channel_class);
    }

    switch (interface)
    {
        case MCD_CLIENT_OBSERVER:
            _mcd_client_proxy_take_observer_filters (client,
                                                     client_filters);
            break;

        case MCD_CLIENT_APPROVER:
            _mcd_client_proxy_take_approver_filters (client,
                                                     client_filters);
            break;

        case MCD_CLIENT_HANDLER:
            _mcd_client_proxy_take_handler_filters (client,
                                                    client_filters);
            break;

        default:
            g_assert_not_reached ();
    }
}

/* This is NULL-safe for the last argument, for ease of use with
 * tp_asv_get_boxed */
static void
_mcd_client_proxy_set_cap_tokens (McdClientProxy *self,
                                  GStrv cap_tokens)
{
    g_strfreev (self->priv->capability_tokens);
    self->priv->capability_tokens = g_strdupv (cap_tokens);
}

static void
_mcd_client_proxy_add_interfaces (McdClientProxy *self,
                                  const gchar * const *interfaces)
{
    guint i;

    if (interfaces == NULL)
        return;

    for (i = 0; interfaces[i] != NULL; i++)
    {
        if (tp_dbus_check_valid_interface_name (interfaces[i], NULL))
        {
            GQuark q = g_quark_from_string (interfaces[i]);

            DEBUG ("%s: %s", tp_proxy_get_bus_name (self), interfaces[i]);
            tp_proxy_add_interface_by_id ((TpProxy *) self, q);
        }
    }
}

static void
_mcd_client_proxy_init (McdClientProxy *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MCD_TYPE_CLIENT_PROXY,
                                              McdClientProxyPrivate);
    /* paired with first call to mcd_client_proxy_introspect */
    self->priv->ready_lock = 1;
}

gboolean
_mcd_client_proxy_is_ready (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), FALSE);

    return self->priv->ready;
}

gboolean
_mcd_client_proxy_is_active (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), FALSE);

    return self->priv->unique_name != NULL &&
        self->priv->unique_name[0] != '\0';
}

gboolean
_mcd_client_proxy_is_activatable (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), FALSE);

    return self->priv->activatable;
}

const gchar *
_mcd_client_proxy_get_unique_name (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), NULL);

    return self->priv->unique_name;
}

void
_mcd_client_recover_observer (McdClientProxy *self, TpChannel *channel,
    const gchar *account_path)
{
    GPtrArray *satisfied_requests;
    GHashTable *observer_info;
    TpConnection *conn;
    const gchar *connection_path;
    GPtrArray *channels_array;

    satisfied_requests = g_ptr_array_new ();
    observer_info = g_hash_table_new (g_str_hash, g_str_equal);
    tp_asv_set_boolean (observer_info, "recovering", TRUE);
    tp_asv_set_boxed (observer_info, "request-properties",
        TP_HASH_TYPE_OBJECT_IMMUTABLE_PROPERTIES_MAP,
        g_hash_table_new (NULL, NULL));

    channels_array = _mcd_tp_channel_details_build_from_tp_chan (channel);
    conn = tp_channel_get_connection (channel);
    connection_path = tp_proxy_get_object_path (conn);

    DEBUG ("calling ObserveChannels on %s for channel %p",
           tp_proxy_get_bus_name (self), channel);

    tp_cli_client_observer_call_observe_channels (
        (TpClient *) self, -1, account_path,
        connection_path, channels_array,
        "/", satisfied_requests, observer_info,
        NULL, NULL, NULL, NULL);

    _mcd_tp_channel_details_free (channels_array);
    g_ptr_array_unref (satisfied_requests);
    g_hash_table_unref (observer_info);
}

static void
_mcd_client_proxy_handler_get_all_cb (TpProxy *proxy,
                                      GHashTable *properties,
                                      const GError *error,
                                      gpointer p G_GNUC_UNUSED,
                                      GObject *o G_GNUC_UNUSED)
{
    McdClientProxy *self = MCD_CLIENT_PROXY (proxy);
    const gchar *bus_name = tp_proxy_get_bus_name (self);
    GPtrArray *filters;
    GPtrArray *handled_channels;
    gboolean bypass;

    if (error != NULL)
    {
        DEBUG ("GetAll(Handler) for client %s failed: %s #%d: %s",
               bus_name, g_quark_to_string (error->domain), error->code,
               error->message);
        goto finally;
    }

    /* by now, we at least know whether the client is running or not */
    g_assert (self->priv->unique_name != NULL);

    filters = tp_asv_get_boxed (properties, "HandlerChannelFilter",
                                TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST);

    if (filters != NULL)
    {
        DEBUG ("%s has %u HandlerChannelFilter entries", bus_name,
               filters->len);
        _mcd_client_proxy_set_filters (self, MCD_CLIENT_HANDLER, filters);
    }
    else
    {
        DEBUG ("%s HandlerChannelFilter absent or wrong type, assuming "
               "no channels can match", bus_name);
    }

    /* if wrong type or absent, assuming False is reasonable */
    bypass = tp_asv_get_boolean (properties, "BypassApproval", NULL);
    self->priv->bypass_approval = bypass;
    DEBUG ("%s has BypassApproval=%c", bus_name, bypass ? 'T' : 'F');

    bypass = tp_asv_get_boolean (properties, "BypassObservers", NULL);
    self->priv->bypass_observers = bypass;
    DEBUG ("%s has BypassObservers=%c", bus_name, bypass ? 'T' : 'F');

    /* don't emit handler-capabilities-changed if we're not actually available
     * any more - if that's the case, then we already signalled our loss of
     * any capabilities */
    if (self->priv->unique_name[0] != '\0' || self->priv->activatable)
    {
        _mcd_client_proxy_set_cap_tokens (self,
            tp_asv_get_boxed (properties, "Capabilities", G_TYPE_STRV));
        g_signal_emit (self, signals[S_HANDLER_CAPABILITIES_CHANGED], 0);
    }

    /* If our unique name is "", then we're not *really* handling these
     * channels - they're the last known information from before the
     * client exited - so don't claim them.
     *
     * At the moment, McdDispatcher deals with the transition from active
     * to inactive in a centralized way, so we don't need to signal that. */
    if (self->priv->unique_name[0] != '\0')
    {
        guint i;

        handled_channels = tp_asv_get_boxed (properties, "HandledChannels",
                                             TP_ARRAY_TYPE_OBJECT_PATH_LIST);

        if (handled_channels != NULL)
        {
            for (i = 0; i < handled_channels->len; i++)
            {
                const gchar *path = g_ptr_array_index (handled_channels, i);

                g_signal_emit (self, signals[S_IS_HANDLING_CHANNEL], 0, path);
            }
        }
    }

finally:
    _mcd_client_proxy_dec_ready_lock (self);
}

static void
_mcd_client_proxy_get_channel_filter_cb (TpProxy *proxy,
                                         const GValue *value,
                                         const GError *error,
                                         gpointer user_data,
                                         GObject *o G_GNUC_UNUSED)
{
    McdClientProxy *self = MCD_CLIENT_PROXY (proxy);
    McdClientInterface iface = GPOINTER_TO_UINT (user_data);

    if (error != NULL)
    {
        DEBUG ("error getting a filter list for client %s: %s #%d: %s",
               tp_proxy_get_object_path (self),
               g_quark_to_string (error->domain), error->code, error->message);
        goto finally;
    }

    if (!G_VALUE_HOLDS (value, TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST))
    {
        DEBUG ("wrong type for filter property on client %s: %s",
               tp_proxy_get_object_path (self), G_VALUE_TYPE_NAME (value));
        goto finally;
    }

    _mcd_client_proxy_set_filters (self, iface, g_value_get_boxed (value));

finally:
    _mcd_client_proxy_dec_ready_lock (self);
}

static void
_mcd_client_proxy_observer_get_all_cb (TpProxy *proxy,
                                       GHashTable *properties,
                                       const GError *error,
                                       gpointer p G_GNUC_UNUSED,
                                       GObject *o G_GNUC_UNUSED)
{
    McdClientProxy *self = MCD_CLIENT_PROXY (proxy);
    const gchar *bus_name = tp_proxy_get_bus_name (self);
    gboolean recover;
    GPtrArray *filters;

    if (error != NULL)
    {
        DEBUG ("GetAll(Observer) for client %s failed: %s #%d: %s",
               bus_name, g_quark_to_string (error->domain), error->code,
               error->message);
        goto finally;
    }

    /* by now, we at least know whether the client is running or not */
    g_assert (self->priv->unique_name != NULL);

    /* FALSE if DelayApprovers is invalid or missing is a good fallback */
    self->priv->delay_approvers = tp_asv_get_boolean (
        properties, "DelayApprovers", NULL);
    DEBUG ("%s has DelayApprovers=%c", bus_name,
        self->priv->delay_approvers ? 'T' : 'F');

    filters = tp_asv_get_boxed (properties, "ObserverChannelFilter",
                                TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST);

    if (filters != NULL)
    {
        DEBUG ("%s has %u ObserverChannelFilter entries", bus_name,
               filters->len);
        _mcd_client_proxy_set_filters (self, MCD_CLIENT_OBSERVER, filters);
    }
    else
    {
        DEBUG ("%s ObserverChannelFilter absent or wrong type, assuming "
               "no channels can match", bus_name);
    }

    /* if wrong type or absent, assuming False is reasonable */
    recover = tp_asv_get_boolean (properties, "Recover", NULL);
    self->priv->recover = recover;
    DEBUG ("%s has Recover=%c", bus_name, recover ? 'T' : 'F');

finally:
    _mcd_client_proxy_dec_ready_lock (self);
}

static void
_mcd_client_proxy_get_interfaces_cb (TpProxy *proxy,
                                     const GValue *out_Value,
                                     const GError *error,
                                     gpointer user_data G_GNUC_UNUSED,
                                     GObject *weak_object G_GNUC_UNUSED)
{
    McdClientProxy *self = MCD_CLIENT_PROXY (proxy);
    const gchar *bus_name = tp_proxy_get_bus_name (proxy);

    if (error != NULL)
    {
        DEBUG ("Error getting Interfaces for Client %s, assuming none: "
               "%s %d %s", bus_name,
               g_quark_to_string (error->domain), error->code, error->message);
        goto finally;
    }

    if (!G_VALUE_HOLDS (out_Value, G_TYPE_STRV))
    {
        DEBUG ("Wrong type getting Interfaces for Client %s, assuming none: "
               "%s", bus_name, G_VALUE_TYPE_NAME (out_Value));
        goto finally;
    }

    _mcd_client_proxy_add_interfaces (self, g_value_get_boxed (out_Value));

    DEBUG ("Client %s", bus_name);

    if (tp_proxy_has_interface_by_id (proxy, TP_IFACE_QUARK_CLIENT_APPROVER))
    {
        _mcd_client_proxy_inc_ready_lock (self);

        DEBUG ("%s is an Approver", bus_name);

        tp_cli_dbus_properties_call_get
            (self, -1, TP_IFACE_CLIENT_APPROVER,
             "ApproverChannelFilter", _mcd_client_proxy_get_channel_filter_cb,
             GUINT_TO_POINTER (MCD_CLIENT_APPROVER), NULL, NULL);
    }

    if (tp_proxy_has_interface_by_id (proxy, TP_IFACE_QUARK_CLIENT_HANDLER))
    {
        _mcd_client_proxy_inc_ready_lock (self);

        DEBUG ("%s is a Handler", bus_name);

        tp_cli_dbus_properties_call_get_all
            (self, -1, TP_IFACE_CLIENT_HANDLER,
             _mcd_client_proxy_handler_get_all_cb, NULL, NULL, NULL);
    }

    if (tp_proxy_has_interface_by_id (proxy, TP_IFACE_QUARK_CLIENT_OBSERVER))
    {
        _mcd_client_proxy_inc_ready_lock (self);

        DEBUG ("%s is an Observer", bus_name);

        tp_cli_dbus_properties_call_get_all
            (self, -1, TP_IFACE_CLIENT_OBSERVER,
             _mcd_client_proxy_observer_get_all_cb, NULL, NULL, NULL);
    }

finally:
    _mcd_client_proxy_dec_ready_lock (self);
}

static gboolean
_mcd_client_proxy_parse_client_file (McdClientProxy *self)
{
    gboolean file_found = FALSE;
    gchar *filename;
    const gchar *bus_name = tp_proxy_get_bus_name (self);

    filename = _mcd_client_proxy_find_client_file (
        bus_name + MC_CLIENT_BUS_NAME_BASE_LEN);

    if (filename)
    {
        GKeyFile *file;
        GError *error = NULL;

        file = g_key_file_new ();
        g_key_file_load_from_file (file, filename, 0, &error);
        if (G_LIKELY (!error))
        {
            DEBUG ("File found for %s: %s", bus_name, filename);
            parse_client_file (self, file);
            file_found = TRUE;
        }
        else
        {
            g_warning ("Loading file %s failed: %s", filename, error->message);
            g_error_free (error);
        }
        g_key_file_free (file);
        g_free (filename);
    }

    return file_found;
}

static gboolean
mcd_client_proxy_introspect (gpointer data)
{
    McdClientProxy *self = data;
    const gchar *bus_name = tp_proxy_get_bus_name (self);

    if (self->priv->introspect_started)
    {
        return FALSE;
    }

    self->priv->introspect_started = TRUE;

    /* The .client file is not mandatory as per the spec. However if it
     * exists, it is better to read it than activating the service to read the
     * D-Bus properties.
     */
    if (!_mcd_client_proxy_parse_client_file (self))
    {
        DEBUG ("No .client file for %s. Ask on D-Bus.", bus_name);

        _mcd_client_proxy_inc_ready_lock (self);

        tp_cli_dbus_properties_call_get (self, -1,
            TP_IFACE_CLIENT, "Interfaces", _mcd_client_proxy_get_interfaces_cb,
            NULL, NULL, NULL);
    }
    else
    {
        if (tp_proxy_has_interface_by_id (self, TP_IFACE_QUARK_CLIENT_HANDLER))
        {
            if (_mcd_client_proxy_is_active (self))
            {
                DEBUG ("%s is an active, activatable Handler", bus_name);

                /* We need to investigate whether it is handling any channels */

                _mcd_client_proxy_inc_ready_lock (self);

                tp_cli_dbus_properties_call_get_all (self, -1,
                    TP_IFACE_CLIENT_HANDLER,
                    _mcd_client_proxy_handler_get_all_cb,
                    NULL, NULL, NULL);
            }
            else
            {
                /* for us to have ever started introspecting, it must be
                 * activatable */
                DEBUG ("%s is a Handler but not active", bus_name);

                /* FIXME: we emit this even if the capabilities we got from the
                 * .client file match those we already had, possibly causing
                 * redundant UpdateCapabilities calls - however, those are
                 * harmless */
                g_signal_emit (self,
                               signals[S_HANDLER_CAPABILITIES_CHANGED], 0);
            }
        }
    }

    _mcd_client_proxy_dec_ready_lock (self);
    return FALSE;
}

static void
mcd_client_proxy_unique_name_cb (TpDBusDaemon *dbus_daemon,
                                 const gchar *well_known_name G_GNUC_UNUSED,
                                 const gchar *unique_name,
                                 gpointer user_data)
{
    McdClientProxy *self = MCD_CLIENT_PROXY (user_data);
    gboolean should_recover = FALSE;

    g_object_ref (self);

    if (unique_name == NULL || unique_name[0] == '\0')
    {
        _mcd_client_proxy_set_inactive (self);

        /* To recover activatable Observers, we just need to call
         * ObserveChannels on them. */
        should_recover = self->priv->recover && self->priv->activatable;
    }
    else
    {
        _mcd_client_proxy_set_active (self, unique_name);
    }

    mcd_client_proxy_introspect (self);

    if (should_recover)
        g_signal_emit (self, signals[S_NEED_RECOVERY], 0);

    g_object_unref (self);
}

static void
mcd_client_proxy_dispose (GObject *object)
{
    McdClientProxy *self = MCD_CLIENT_PROXY (object);
    void (*chain_up) (GObject *) =
        ((GObjectClass *) _mcd_client_proxy_parent_class)->dispose;

    if (self->priv->disposed)
        return;

    self->priv->disposed = TRUE;

    tp_dbus_daemon_cancel_name_owner_watch (tp_proxy_get_dbus_daemon (self),
                                            tp_proxy_get_bus_name (self),
                                            mcd_client_proxy_unique_name_cb,
                                            self);

    tp_clear_pointer (&self->priv->capability_tokens, g_strfreev);

    if (chain_up != NULL)
    {
        chain_up (object);
    }
}

static void
mcd_client_proxy_finalize (GObject *object)
{
    McdClientProxy *self = MCD_CLIENT_PROXY (object);
    void (*chain_up) (GObject *) =
        ((GObjectClass *) _mcd_client_proxy_parent_class)->finalize;

    g_free (self->priv->unique_name);

    _mcd_client_proxy_take_approver_filters (self, NULL);
    _mcd_client_proxy_take_observer_filters (self, NULL);
    _mcd_client_proxy_take_handler_filters (self, NULL);

    if (chain_up != NULL)
    {
        chain_up (object);
    }
}

static void
mcd_client_proxy_constructed (GObject *object)
{
    McdClientProxy *self = MCD_CLIENT_PROXY (object);
    void (*chain_up) (GObject *) =
        ((GObjectClass *) _mcd_client_proxy_parent_class)->constructed;
    const gchar *bus_name;

    if (chain_up != NULL)
    {
        chain_up (object);
    }

    bus_name = tp_proxy_get_bus_name (self);

    self->priv->capability_tokens = NULL;

    DEBUG ("%s", bus_name);

    tp_dbus_daemon_watch_name_owner (tp_proxy_get_dbus_daemon (self),
                                     bus_name,
                                     mcd_client_proxy_unique_name_cb,
                                     self, NULL);

    if (self->priv->unique_name != NULL)
    {
        /* we already know who we are, so we can skip straight to the
         * introspection. It's safe to call mcd_client_proxy_introspect
         * any number of times, so we don't need to guard against
         * duplication */
        g_idle_add_full (G_PRIORITY_HIGH, mcd_client_proxy_introspect,
                         g_object_ref (self), g_object_unref);
    }
}

static void
mcd_client_proxy_set_property (GObject *object,
                               guint property,
                               const GValue *value,
                               GParamSpec *param_spec)
{
    McdClientProxy *self = MCD_CLIENT_PROXY (object);

    switch (property)
    {
        case PROP_ACTIVATABLE:
            self->priv->activatable = g_value_get_boolean (value);
            break;

        case PROP_UNIQUE_NAME:
            g_assert (self->priv->unique_name == NULL);
            self->priv->unique_name = g_value_dup_string (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property, param_spec);
    }
}

static void
_mcd_client_proxy_class_init (McdClientProxyClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (McdClientProxyPrivate));

    object_class->constructed = mcd_client_proxy_constructed;
    object_class->dispose = mcd_client_proxy_dispose;
    object_class->finalize = mcd_client_proxy_finalize;
    object_class->set_property = mcd_client_proxy_set_property;

    signals[S_READY] = g_signal_new ("ready",
        G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0, NULL, NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

    signals[S_GONE] = g_signal_new ("gone",
        G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0, NULL, NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

    /* Never emitted until after the unique name is known */
    signals[S_IS_HANDLING_CHANNEL] = g_signal_new ("is-handling-channel",
        G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0, NULL, NULL,
        g_cclosure_marshal_VOID__STRING,
        G_TYPE_NONE, 1, G_TYPE_STRING);

    /* Never emitted until after the unique name is known */
    signals[S_HANDLER_CAPABILITIES_CHANGED] = g_signal_new (
        "handler-capabilities-changed",
        G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0, NULL, NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

    signals[S_NEED_RECOVERY] = g_signal_new ("need-recovery",
        G_OBJECT_CLASS_TYPE (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0, NULL, NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

    g_object_class_install_property (object_class, PROP_ACTIVATABLE,
        g_param_spec_boolean ("activatable", "Activatable?",
            "TRUE if this client can be service-activated", FALSE,
            G_PARAM_WRITABLE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class, PROP_UNIQUE_NAME,
        g_param_spec_string ("unique-name", "Unique name",
            "The D-Bus unique name of this client, \"\" if not running or "
            "NULL if unknown",
            NULL,
            G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
            G_PARAM_STATIC_STRINGS));

}

gboolean
_mcd_client_check_valid_name (const gchar *name_suffix,
                              GError **error)
{
    guint i;

    if (!g_ascii_isalpha (*name_suffix))
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "Client names must start with a letter");
        return FALSE;
    }

    for (i = 1; name_suffix[i] != '\0'; i++)
    {
        if (i > (255 - MC_CLIENT_BUS_NAME_BASE_LEN))
        {
            g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                         "Client name too long");
        }

        if (name_suffix[i] == '_' || g_ascii_isalpha (name_suffix[i]))
        {
            continue;
        }

        if (name_suffix[i] == '.' || g_ascii_isdigit (name_suffix[i]))
        {
            if (name_suffix[i-1] == '.')
            {
                g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                             "Client names must not have a digit or dot "
                             "following a dot");
                return FALSE;
            }
        }
        else
        {
            g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                         "Client names must not contain '%c'", name_suffix[i]);
            return FALSE;
        }
    }

    if (name_suffix[i-1] == '.')
    {
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                     "Client names must not end with a dot");
        return FALSE;
    }

    return TRUE;
}

McdClientProxy *
_mcd_client_proxy_new (TpDBusDaemon *dbus_daemon,
                       const gchar *well_known_name,
                       const gchar *unique_name_if_known,
                       gboolean activatable)
{
    McdClientProxy *self;
    const gchar *name_suffix;
    gchar *object_path;

    g_return_val_if_fail (g_str_has_prefix (well_known_name,
                                            TP_CLIENT_BUS_NAME_BASE), NULL);
    name_suffix = well_known_name + MC_CLIENT_BUS_NAME_BASE_LEN;
    g_return_val_if_fail (_mcd_client_check_valid_name (name_suffix, NULL),
                          NULL);

    object_path = g_strconcat ("/", well_known_name, NULL);
    g_strdelimit (object_path, ".", '/');

    g_assert (tp_dbus_check_valid_bus_name (well_known_name,
                                            TP_DBUS_NAME_TYPE_WELL_KNOWN,
                                            NULL));
    g_assert (tp_dbus_check_valid_object_path (object_path, NULL));

    self = g_object_new (MCD_TYPE_CLIENT_PROXY,
                         "dbus-daemon", dbus_daemon,
                         "object-path", object_path,
                         "bus-name", well_known_name,
                         "unique-name", unique_name_if_known,
                         "activatable", activatable,
                         NULL);

    g_free (object_path);

    return self;
}

static void _mcd_client_proxy_become_incapable (McdClientProxy *self);

void
_mcd_client_proxy_set_inactive (McdClientProxy *self)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));

    /* if unique name is already "" (i.e. known to be inactive), do nothing */
    if (self->priv->unique_name != NULL && self->priv->unique_name[0] == '\0')
    {
        return;
    }

    g_free (self->priv->unique_name);
    self->priv->unique_name = g_strdup ("");

    if (!self->priv->activatable)
    {
        /* in ContactCapabilities we indicate the disappearance
         * of a client by giving it an empty set of capabilities and
         * filters */
        _mcd_client_proxy_become_incapable (self);

        g_signal_emit (self, signals[S_GONE], 0);
    }
}

void
_mcd_client_proxy_set_active (McdClientProxy *self,
                              const gchar *unique_name)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));
    g_return_if_fail (unique_name != NULL);

    g_free (self->priv->unique_name);
    self->priv->unique_name = g_strdup (unique_name);
}

void
_mcd_client_proxy_set_activatable (McdClientProxy *self)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));

    self->priv->activatable = TRUE;
}

const GList *
_mcd_client_proxy_get_approver_filters (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), NULL);

    return self->priv->approver_filters;
}

const GList *
_mcd_client_proxy_get_observer_filters (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), NULL);

    return self->priv->observer_filters;
}

const GList *
_mcd_client_proxy_get_handler_filters (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), NULL);

    return self->priv->handler_filters;
}

static void
mcd_client_proxy_free_client_filters (GList **client_filters)
{
    g_assert (client_filters != NULL);

    if (*client_filters != NULL)
    {
        g_list_foreach (*client_filters, (GFunc) g_hash_table_unref, NULL);
        g_list_free (*client_filters);
        *client_filters = NULL;
    }
}

void
_mcd_client_proxy_take_approver_filters (McdClientProxy *self,
                                         GList *filters)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));

    mcd_client_proxy_free_client_filters (&(self->priv->approver_filters));
    self->priv->approver_filters = filters;
}

void
_mcd_client_proxy_take_observer_filters (McdClientProxy *self,
                                         GList *filters)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));

    mcd_client_proxy_free_client_filters (&(self->priv->observer_filters));
    self->priv->observer_filters = filters;
}

void
_mcd_client_proxy_take_handler_filters (McdClientProxy *self,
                                        GList *filters)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));

    mcd_client_proxy_free_client_filters (&(self->priv->handler_filters));
    self->priv->handler_filters = filters;
}

gboolean
_mcd_client_proxy_get_bypass_approval (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), FALSE);

    return self->priv->bypass_approval;
}

gboolean
_mcd_client_proxy_get_bypass_observers (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), FALSE);

    return self->priv->bypass_observers;
}

gboolean
_mcd_client_proxy_get_delay_approvers (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), FALSE);

    return self->priv->delay_approvers;
}

static void
_mcd_client_proxy_become_incapable (McdClientProxy *self)
{
    gboolean handler_was_capable = (self->priv->handler_filters != NULL);

    if (self->priv->capability_tokens != NULL &&
        self->priv->capability_tokens[0] != NULL)
    {
        handler_was_capable = TRUE;
    }

    _mcd_client_proxy_take_approver_filters (self, NULL);
    _mcd_client_proxy_take_observer_filters (self, NULL);
    _mcd_client_proxy_take_handler_filters (self, NULL);
    tp_clear_pointer (&self->priv->capability_tokens, g_strfreev);

    if (handler_was_capable)
    {
        g_signal_emit (self, signals[S_HANDLER_CAPABILITIES_CHANGED], 0);
    }
}

GValueArray *
_mcd_client_proxy_dup_handler_capabilities (McdClientProxy *self)
{
    GPtrArray *filters;
    GStrv cap_tokens;
    GValueArray *va;
    const GList *list;
    gchar *empty_strv[] = { NULL };

    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), NULL);

    filters = g_ptr_array_sized_new (
        g_list_length (self->priv->handler_filters));

    for (list = self->priv->handler_filters; list != NULL; list = list->next)
    {
        GHashTable *copy = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) tp_g_value_slice_free);

        tp_g_hash_table_update (copy, list->data,
                                (GBoxedCopyFunc) g_strdup,
                                (GBoxedCopyFunc) tp_g_value_slice_dup);
        g_ptr_array_add (filters, copy);
    }

    cap_tokens = self->priv->capability_tokens;

    if (cap_tokens == NULL)
        cap_tokens = empty_strv;

    if (DEBUGGING)
    {
        guint i;

        DEBUG ("%s:", tp_proxy_get_bus_name (self));

        DEBUG ("- %u channel filters", filters->len);
        DEBUG ("- %u capability tokens:", g_strv_length (cap_tokens));

        for (i = 0; cap_tokens[i] != NULL; i++)
        {
            DEBUG ("    %s", cap_tokens[i]);
        }

        DEBUG ("-end-");
    }

    va = g_value_array_new (3);
    g_value_array_append (va, NULL);
    g_value_array_append (va, NULL);
    g_value_array_append (va, NULL);

    g_value_init (va->values + 0, G_TYPE_STRING);
    g_value_init (va->values + 1, TP_ARRAY_TYPE_CHANNEL_CLASS_LIST);
    g_value_init (va->values + 2, G_TYPE_STRV);

    g_value_set_string (va->values + 0, tp_proxy_get_bus_name (self));
    g_value_take_boxed (va->values + 1, filters);
    g_value_set_boxed (va->values + 2, cap_tokens);

    return va;
}

/* returns TRUE if the channel matches one property criteria
 */
static gboolean
_mcd_client_match_property (GVariant *channel_properties,
                            gchar *property_name,
                            GValue *filter_value)
{
    GType filter_type = G_VALUE_TYPE (filter_value);

    g_return_val_if_fail (g_variant_is_of_type (channel_properties,
            G_VARIANT_TYPE_VARDICT), FALSE);

    g_assert (G_IS_VALUE (filter_value));

    if (filter_type == G_TYPE_STRING)
    {
        const gchar *string;

        string = tp_vardict_get_string (channel_properties, property_name);
        if (!string)
            return FALSE;

        return !tp_strdiff (string, g_value_get_string (filter_value));
    }

    if (filter_type == DBUS_TYPE_G_OBJECT_PATH)
    {
        const gchar *path;

        path = tp_vardict_get_object_path (channel_properties, property_name);
        if (!path)
            return FALSE;

        return !tp_strdiff (path, g_value_get_boxed (filter_value));
    }

    if (filter_type == G_TYPE_BOOLEAN)
    {
        gboolean valid;
        gboolean b;

        b = tp_vardict_get_boolean (channel_properties, property_name, &valid);
        if (!valid)
            return FALSE;

        return !!b == !!g_value_get_boolean (filter_value);
    }

    if (filter_type == G_TYPE_UCHAR || filter_type == G_TYPE_UINT ||
        filter_type == G_TYPE_UINT64)
    {
        gboolean valid;
        guint64 i;

        i = tp_vardict_get_uint64 (channel_properties, property_name, &valid);
        if (!valid)
            return FALSE;

        if (filter_type == G_TYPE_UCHAR)
            return i == g_value_get_uchar (filter_value);
        else if (filter_type == G_TYPE_UINT)
            return i == g_value_get_uint (filter_value);
        else
            return i == g_value_get_uint64 (filter_value);
    }

    if (filter_type == G_TYPE_INT || filter_type == G_TYPE_INT64)
    {
        gboolean valid;
        gint64 i;

        i = tp_vardict_get_int64 (channel_properties, property_name, &valid);
        if (!valid)
            return FALSE;

        if (filter_type == G_TYPE_INT)
            return i == g_value_get_int (filter_value);
        else
            return i == g_value_get_int64 (filter_value);
    }

    g_warning ("%s: Invalid type: %s",
               G_STRFUNC, g_type_name (filter_type));
    return FALSE;
}

/* if the channel matches one of the channel filters, returns a positive
 * number that increases with more specific matches; otherwise, returns 0
 *
 * (implementation detail: the positive number is 1 + the number of keys in the
 * largest filter that matched)
 */
guint
_mcd_client_match_filters (GVariant *channel_properties,
                           const GList *filters,
                           gboolean assume_requested)
{
    const GList *list;
    guint best_quality = 0;

    g_return_val_if_fail (g_variant_is_of_type (channel_properties,
            G_VARIANT_TYPE_VARDICT), 0);

    for (list = filters; list != NULL; list = list->next)
    {
        GHashTable *filter = list->data;
        GHashTableIter filter_iter;
        gboolean filter_matched = TRUE;
        gchar *property_name;
        GValue *filter_value;
        guint quality;

        /* +1 because the empty hash table matches everything :-) */
        quality = g_hash_table_size (filter) + 1;

        if (quality <= best_quality)
        {
            /* even if this filter matches, there's no way it can be a
             * better-quality match than the best one we saw so far */
            continue;
        }

        g_hash_table_iter_init (&filter_iter, filter);
        while (g_hash_table_iter_next (&filter_iter,
                                       (gpointer *) &property_name,
                                       (gpointer *) &filter_value))
        {
            if (assume_requested &&
                ! tp_strdiff (property_name, TP_IFACE_CHANNEL ".Requested"))
            {
                if (! G_VALUE_HOLDS_BOOLEAN (filter_value) ||
                    ! g_value_get_boolean (filter_value))
                {
                    filter_matched = FALSE;
                    break;
                }
            }
            else if (! _mcd_client_match_property (channel_properties,
                                                   property_name,
                                                   filter_value))
            {
                filter_matched = FALSE;
                break;
            }
        }

        if (filter_matched)
        {
            best_quality = quality;
        }
    }

    return best_quality;
}

static const gchar *
borrow_channel_account_path (McdChannel *channel)
{
    McdAccount *account;
    const gchar *account_path;

    account = mcd_channel_get_account (channel);
    account_path = account == NULL ? "/"
        : mcd_account_get_object_path (account);

    if (G_UNLIKELY (account_path == NULL))    /* can't happen? */
        account_path = "/";

    return account_path;
}

static const gchar *
borrow_channel_connection_path (McdChannel *channel)
{
    TpChannel *tp_channel;
    TpConnection *tp_connection;
    const gchar *connection_path;

    tp_channel = mcd_channel_get_tp_channel (channel);
    g_return_val_if_fail (tp_channel != NULL, "/");
    tp_connection = tp_channel_get_connection (tp_channel);
    g_return_val_if_fail (tp_connection != NULL, "/");
    connection_path = tp_proxy_get_object_path (tp_connection);
    g_return_val_if_fail (connection_path != NULL, "/");
    return connection_path;
}

void
_mcd_client_proxy_handle_channels (McdClientProxy *self,
    gint timeout_ms,
    const GList *channels,
    gint64 user_action_time,
    GHashTable *handler_info,
    tp_cli_client_handler_callback_for_handle_channels callback,
    gpointer user_data,
    GDestroyNotify destroy,
    GObject *weak_object)
{
    GPtrArray *channel_details;
    GPtrArray *requests_satisfied;
    const GList *iter;

    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));
    g_return_if_fail (channels != NULL);

    DEBUG ("calling HandleChannels on %s", tp_proxy_get_bus_name (self));

    channel_details = _mcd_tp_channel_details_build_from_list (channels);
    requests_satisfied = g_ptr_array_new_with_free_func (g_free);

    if (handler_info == NULL)
    {
        handler_info = g_hash_table_new (g_str_hash, g_str_equal);
    }
    else
    {
        g_hash_table_ref (handler_info);
    }

    for (iter = channels; iter != NULL; iter = iter->next)
    {
        gint64 req_time = 0;
        GHashTable *requests;
        GHashTableIter it;
        gpointer path;

        requests = _mcd_channel_get_satisfied_requests (iter->data,
                                                             &req_time);

        g_hash_table_iter_init (&it, requests);
        while (g_hash_table_iter_next (&it, &path, NULL))
        {
            g_ptr_array_add (requests_satisfied, g_strdup (path));
        }

        g_hash_table_unref (requests);

        /* Numerical order is correct for all currently supported values:
         *
         * (TP_USER_ACTION_TIME_NOT_USER_ACTION == 0) is less than
         * (normal X11 timestamps, which are 1 to G_MAXUINT32) are less than
         * (TP_USER_ACTION_TIME_CURRENT_TIME == G_MAXINT64) */
        if (req_time > user_action_time)
            user_action_time = req_time;

        _mcd_channel_set_status (iter->data,
                                 MCD_CHANNEL_STATUS_HANDLER_INVOKED);
    }

    tp_cli_client_handler_call_handle_channels ((TpClient *) self,
        timeout_ms, borrow_channel_account_path (channels->data),
        borrow_channel_connection_path (channels->data), channel_details,
        requests_satisfied, user_action_time, handler_info,
        callback, user_data, destroy, weak_object);

    _mcd_tp_channel_details_free (channel_details);
    g_ptr_array_unref (requests_satisfied);
    g_hash_table_unref (handler_info);
}
