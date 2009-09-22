/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * Mission Control client proxy.
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
#include "mcd-client-priv.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/proxy-subclass.h>

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
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

struct _McdClientProxyPrivate
{
    TpHandleRepoIface *string_pool;
    /* Handler.Capabilities, represented as handles taken from
     * dispatcher->priv->string_pool */
    TpHandleSet *capability_tokens;

    gchar *unique_name;
    gboolean ready;
    gboolean bypass_approval;

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
};

gchar *
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

static void
_mcd_client_proxy_init (McdClientProxy *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MCD_TYPE_CLIENT_PROXY,
                                              McdClientProxyPrivate);
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
    g_return_val_if_fail (self->priv->ready, FALSE);

    return self->priv->unique_name != NULL &&
        self->priv->unique_name[0] != '\0';
}

gboolean
_mcd_client_proxy_is_activatable (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), FALSE);
    g_return_val_if_fail (self->priv->ready, FALSE);

    return self->priv->activatable;
}

const gchar *
_mcd_client_proxy_get_unique_name (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), NULL);
    g_return_val_if_fail (self->priv->ready, NULL);

    return self->priv->unique_name;
}

static void
mcd_client_proxy_emit_ready (McdClientProxy *self)
{
    if (self->priv->ready)
        return;

    self->priv->ready = TRUE;

    g_signal_emit (self, signals[S_READY], 0);
}

static gboolean
mcd_client_proxy_introspect (gpointer data)
{
    mcd_client_proxy_emit_ready (data);
    return FALSE;
}

static void
mcd_client_proxy_unique_name_cb (TpDBusDaemon *dbus_daemon,
                                 const gchar *unique_name,
                                 const GError *error,
                                 gpointer unused G_GNUC_UNUSED,
                                 GObject *weak_object)
{
    McdClientProxy *self = MCD_CLIENT_PROXY (weak_object);

    if (error != NULL)
    {
        DEBUG ("Error getting unique name, assuming not active: %s %d: %s",
               g_quark_to_string (error->domain), error->code, error->message);
        _mcd_client_proxy_set_inactive (self);
    }
    else
    {
        _mcd_client_proxy_set_active (self, unique_name);
    }

    mcd_client_proxy_introspect (self);
}

static void
mcd_client_proxy_dispose (GObject *object)
{
    McdClientProxy *self = MCD_CLIENT_PROXY (object);
    void (*chain_up) (GObject *) =
        ((GObjectClass *) _mcd_client_proxy_parent_class)->dispose;

    if (self->priv->string_pool != NULL)
    {
        if (self->priv->capability_tokens != NULL)
        {
            tp_handle_set_destroy (self->priv->capability_tokens);
            self->priv->capability_tokens = NULL;
        }

        g_object_unref (self->priv->string_pool);
        self->priv->string_pool = NULL;
    }

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

    if (chain_up != NULL)
    {
        chain_up (object);
    }

    self->priv->capability_tokens = tp_handle_set_new (
        self->priv->string_pool);

    if (self->priv->unique_name == NULL)
    {
        tp_cli_dbus_daemon_call_get_name_owner (tp_proxy_get_dbus_daemon (self),
                                                -1,
                                                tp_proxy_get_bus_name (self),
                                                mcd_client_proxy_unique_name_cb,
                                                NULL, NULL, (GObject *) self);
    }
    else
    {
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

        case PROP_STRING_POOL:
            g_assert (self->priv->string_pool == NULL);
            self->priv->string_pool = g_value_dup_object (value);
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

    signals[S_READY] = g_signal_new ("ready", G_OBJECT_CLASS_TYPE (klass),
                                     G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                     0, NULL, NULL,
                                     g_cclosure_marshal_VOID__VOID,
                                     G_TYPE_NONE, 0);

    g_object_class_install_property (object_class, PROP_ACTIVATABLE,
        g_param_spec_boolean ("activatable", "Activatable?",
            "TRUE if this client can be service-activated", FALSE,
            G_PARAM_WRITABLE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class, PROP_STRING_POOL,
        g_param_spec_object ("string-pool", "String pool",
            "TpHandleRepoIface used to intern strings representing capability "
            "tokens",
            G_TYPE_OBJECT,
            G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
            G_PARAM_STATIC_STRINGS));

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
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Client names must start with a letter");
        return FALSE;
    }

    for (i = 1; name_suffix[i] != '\0'; i++)
    {
        if (i > (255 - MC_CLIENT_BUS_NAME_BASE_LEN))
        {
            g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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
                g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                             "Client names must not have a digit or dot "
                             "following a dot");
                return FALSE;
            }
        }
        else
        {
            g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                         "Client names must not contain '%c'", name_suffix[i]);
            return FALSE;
        }
    }

    if (name_suffix[i-1] == '.')
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Client names must not end with a dot");
        return FALSE;
    }

    return TRUE;
}

McdClientProxy *
_mcd_client_proxy_new (TpDBusDaemon *dbus_daemon,
                       TpHandleRepoIface *string_pool,
                       const gchar *name_suffix,
                       const gchar *unique_name_if_known,
                       gboolean activatable)
{
    McdClientProxy *self;
    gchar *bus_name, *object_path;

    g_return_val_if_fail (_mcd_client_check_valid_name (name_suffix, NULL),
                          NULL);

    bus_name = g_strconcat (TP_CLIENT_BUS_NAME_BASE, name_suffix, NULL);
    object_path = g_strconcat (TP_CLIENT_OBJECT_PATH_BASE, name_suffix, NULL);
    g_strdelimit (object_path, ".", '/');

    g_assert (tp_dbus_check_valid_bus_name (bus_name,
                                            TP_DBUS_NAME_TYPE_WELL_KNOWN,
                                            NULL));
    g_assert (tp_dbus_check_valid_object_path (object_path, NULL));

    self = g_object_new (MCD_TYPE_CLIENT_PROXY,
                         "dbus-daemon", dbus_daemon,
                         "string-pool", string_pool,
                         "object-path", object_path,
                         "bus-name", bus_name,
                         "unique-name", unique_name_if_known,
                         "activatable", activatable,
                         NULL);

    g_free (object_path);
    g_free (bus_name);

    return self;
}

void
_mcd_client_proxy_set_inactive (McdClientProxy *self)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));

    g_free (self->priv->unique_name);
    self->priv->unique_name = g_strdup ("");
}

void
_mcd_client_proxy_set_active (McdClientProxy *self,
                              const gchar *unique_name)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));

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
        g_list_foreach (*client_filters, (GFunc) g_hash_table_destroy, NULL);
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

void
_mcd_client_proxy_set_bypass_approval (McdClientProxy *self,
                                       gboolean bypass)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));

    self->priv->bypass_approval = bypass;
}

void
_mcd_client_proxy_clear_capability_tokens (McdClientProxy *self)
{
    g_return_if_fail (MCD_IS_CLIENT_PROXY (self));

    tp_handle_set_destroy (self->priv->capability_tokens);
    self->priv->capability_tokens = tp_handle_set_new (
        self->priv->string_pool);
}

TpHandleSet *
_mcd_client_proxy_peek_capability_tokens (McdClientProxy *self)
{
    g_return_val_if_fail (MCD_IS_CLIENT_PROXY (self), NULL);
    return self->priv->capability_tokens;
}
