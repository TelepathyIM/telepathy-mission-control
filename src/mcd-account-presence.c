/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2010 Nokia Corporation.
 * Copyright (C) 2010 Collabora Ltd.
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

#include "mcd-account-presence.h"

#include <config.h>

#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/svc-generic.h>

#include <libmcclient/mc-interfaces.h>

#include "mcd-account.h"
#include "mcd-account-priv.h"

struct _McdAccountPresencePrivate
{
    TpDBusDaemon *dbus_daemon;

    /* gchar *unique_name → GValueArray *simple_presence */
    GHashTable *minimum_presence_requests;
};

/* higher is better */
static TpConnectionPresenceType presence_type_priorities[] = {
    TP_CONNECTION_PRESENCE_TYPE_UNKNOWN,
    TP_CONNECTION_PRESENCE_TYPE_UNSET,
    TP_CONNECTION_PRESENCE_TYPE_OFFLINE,
    TP_CONNECTION_PRESENCE_TYPE_HIDDEN,
    TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY,
    TP_CONNECTION_PRESENCE_TYPE_AWAY,
    TP_CONNECTION_PRESENCE_TYPE_BUSY,
    TP_CONNECTION_PRESENCE_TYPE_AVAILABLE,
    TP_CONNECTION_PRESENCE_TYPE_ERROR
};

gint
_mcd_account_presence_type_priority (TpConnectionPresenceType type)
{
    gint i;

    for (i = 0; presence_type_priorities[i] !=
            TP_CONNECTION_PRESENCE_TYPE_ERROR; i++)
    {
        if (presence_type_priorities[i] == type)
            return i;
    }

    return -1;
}

static void
get_most_available_presence (McdAccount *self,
                             TpConnectionPresenceType *type,
                             const gchar **status,
                             const gchar **message)
{
    McdAccountPresencePrivate *priv = self->presence_priv;
    GHashTableIter iter;
    gpointer k, v;
    gint prio;

    *type = TP_CONNECTION_PRESENCE_TYPE_UNSET;
    *status = NULL;
    *message = NULL;
    prio = _mcd_account_presence_type_priority (*type);

    g_hash_table_iter_init (&iter, priv->minimum_presence_requests);
    while (g_hash_table_iter_next (&iter, &k, &v))
    {
        GValue *val = g_value_array_get_nth (v, 0);
        TpConnectionPresenceType t = g_value_get_uint (val);
        gint p = _mcd_account_presence_type_priority (t);

        if (p > prio)
        {
            prio = p;
            tp_value_array_unpack (v, 3, type, status, message);
        }
    }
}

static void
name_owner_changed_cb (TpDBusDaemon *bus_daemon,
                       const gchar *name,
                       const gchar *new_owner,
                       gpointer user_data)
{
    McdAccount *self = MCD_ACCOUNT (user_data);
    McdAccountPresencePrivate *priv = self->presence_priv;

    /* if they fell of the bus, cancel their request for them */
    if (new_owner == NULL || new_owner[0] == '\0')
    {
        TpConnectionPresenceType type;
        const gchar *status;
        const gchar *message;

        g_hash_table_remove (priv->minimum_presence_requests, name);

        get_most_available_presence (self, &type, &status, &message);
        _mcd_account_set_minimum_presence (self, type, status, message);
    }
}

static void
minimum_presence_request (McSvcAccountInterfaceMinimumPresence *iface,
                          const GValueArray *simple_presence,
                          DBusGMethodInvocation *context)
{
    McdAccount *self = MCD_ACCOUNT (iface);
    McdAccountPresencePrivate *priv = self->presence_priv;
    TpConnectionPresenceType type;
    const gchar *status;
    const gchar *message;
    gchar *client = dbus_g_method_get_sender (context);

    tp_value_array_unpack ((GValueArray *) simple_presence,
        3, &type, &status, &message);

    if (!_mcd_account_presence_type_is_settable (type))
    {
        GError *error = NULL;

        g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
            "MinimumPresence %d cannot be set on yourself", type);
        dbus_g_method_return_error (context, error);
        return;
    }

    DEBUG ("Client %s requests MinimumPresence %s: %s", client, status,
         message);

    if (G_LIKELY (priv->dbus_daemon) &&
        !g_hash_table_lookup (priv->minimum_presence_requests, client))
    {
        tp_dbus_daemon_watch_name_owner (priv->dbus_daemon,
            client, name_owner_changed_cb, self, NULL);
    }

    g_hash_table_replace (priv->minimum_presence_requests, client,
        g_value_array_copy (simple_presence));

    get_most_available_presence (self, &type, &status, &message);

    _mcd_account_set_minimum_presence (self, type, status, message);

    mc_svc_account_interface_minimum_presence_return_from_request (context);
}

static void
minimum_presence_release (McSvcAccountInterfaceMinimumPresence *iface,
                          DBusGMethodInvocation *context)
{
    McdAccount *self = MCD_ACCOUNT (iface);
    McdAccountPresencePrivate *priv = self->presence_priv;
    TpConnectionPresenceType type;
    const gchar *status;
    const gchar *message;
    gchar *client = dbus_g_method_get_sender (context);

    get_most_available_presence (self, &type, &status, &message);
    _mcd_account_set_minimum_presence (self, type, status, message);

    if (G_LIKELY (priv->dbus_daemon))
    {
        tp_dbus_daemon_cancel_name_owner_watch (priv->dbus_daemon,
            client, name_owner_changed_cb, self);
    }

    g_hash_table_remove (priv->minimum_presence_requests, client);
    g_free (client);

    mc_svc_account_interface_minimum_presence_return_from_release (context);
}

static void
get_requests (TpSvcDBusProperties *iface, const gchar *name, GValue *value)
{
    McdAccount *self = MCD_ACCOUNT (iface);
    McdAccountPresencePrivate *priv = self->presence_priv;
    GType type = dbus_g_type_get_map ("GHashTable", G_TYPE_STRING,
        TP_STRUCT_TYPE_SIMPLE_PRESENCE);

    g_value_init (value, type);
    g_value_take_boxed (value,
        g_hash_table_ref (priv->minimum_presence_requests));
}

const McdDBusProp minimum_presence_properties[] = {
    { "Requests", NULL, get_requests },
    { 0 },
};

void
minimum_presence_iface_init (McSvcAccountInterfaceMinimumPresenceClass *iface,
                               gpointer iface_data)
{
#define IMPLEMENT(x) mc_svc_account_interface_minimum_presence_implement_##x (\
    iface, minimum_presence_##x)
    IMPLEMENT(request);
    IMPLEMENT(release);
#undef IMPLEMENT
}

void
minimum_presence_instance_init (TpSvcDBusProperties *self)
{
    McdAccount *account = MCD_ACCOUNT (self);
    McdAccountPresencePrivate *priv;
    GError *error = NULL;

    priv = g_new0 (McdAccountPresencePrivate, 1);
    account->presence_priv = priv;

    priv->minimum_presence_requests = g_hash_table_new_full (
        g_str_hash, g_str_equal, g_free,
        (GDestroyNotify) g_value_array_free);

    priv->dbus_daemon = tp_dbus_daemon_dup (&error);
    if (!priv->dbus_daemon)
    {
      DEBUG ("Can't get Tp DBus daemon wrapper: %s", error->message);
      g_error_free (error);
    }
}

void
minimum_presence_finalize (McdAccount *account)
{
    McdAccountPresencePrivate *priv = account->presence_priv;

    g_hash_table_destroy (priv->minimum_presence_requests);

    if (priv->dbus_daemon)
        g_object_unref (priv->dbus_daemon);

    g_free (priv);
}

