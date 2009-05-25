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
#include <telepathy-glib/errors.h>
#include <telepathy-glib/proxy-subclass.h>

#include "mcd-debug.h"
#include "_gen/interfaces.h"

G_DEFINE_TYPE (McdClientProxy, _mcd_client_proxy, TP_TYPE_PROXY);

enum
{
    S_READY,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

struct _McdClientProxyPrivate
{
    gchar *unique_name;
    gboolean ready;
};

static void
_mcd_client_proxy_init (McdClientProxy *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MCD_TYPE_CLIENT_PROXY,
                                              McdClientProxyPrivate);
}

static gboolean
mcd_client_proxy_emit_ready (gpointer data)
{
    McdClientProxy *self = data;

    if (self->priv->ready)
        return FALSE;

    self->priv->ready = TRUE;

    g_signal_emit (self, signals[S_READY], 0);

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
        unique_name = "";
    }

    self->priv->unique_name = g_strdup (unique_name);

    mcd_client_proxy_emit_ready (self);
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
        g_idle_add_full (G_PRIORITY_HIGH, mcd_client_proxy_emit_ready,
                         g_object_ref (self), g_object_unref);
    }
}

static void
_mcd_client_proxy_class_init (McdClientProxyClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (McdClientProxyPrivate));

    object_class->constructed = mcd_client_proxy_constructed;

    signals[S_READY] = g_signal_new ("ready", G_OBJECT_CLASS_TYPE (klass),
                                     G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                     0, NULL, NULL,
                                     g_cclosure_marshal_VOID__VOID,
                                     G_TYPE_NONE, 0);
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
                       const gchar *name_suffix)
{
    McdClientProxy *self;
    gchar *bus_name, *object_path;

    g_return_val_if_fail (_mcd_client_check_valid_name (name_suffix, NULL),
                          NULL);

    bus_name = g_strconcat (MC_CLIENT_BUS_NAME_BASE, name_suffix, NULL);
    object_path = g_strconcat (MC_CLIENT_OBJECT_PATH_BASE, name_suffix, NULL);
    g_strdelimit (object_path, ".", '/');

    g_assert (tp_dbus_check_valid_bus_name (bus_name,
                                            TP_DBUS_NAME_TYPE_WELL_KNOWN,
                                            NULL));
    g_assert (tp_dbus_check_valid_object_path (object_path, NULL));

    self = g_object_new (MCD_TYPE_CLIENT_PROXY,
                         "dbus-daemon", dbus_daemon,
                         "object-path", object_path,
                         "bus-name", bus_name,
                         NULL);

    g_free (object_path);
    g_free (bus_name);

    return self;
}
