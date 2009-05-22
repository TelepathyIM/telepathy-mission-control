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

#include "_gen/interfaces.h"

G_DEFINE_TYPE (McdClientProxy, _mcd_client_proxy, TP_TYPE_PROXY);

struct _McdClientProxyPrivate
{
  guint dummy:1;
};

static void
_mcd_client_proxy_init (McdClientProxy *self)
{
}

static void
_mcd_client_proxy_class_init (McdClientProxyClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (McdClientProxyPrivate));
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
