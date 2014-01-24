/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
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

#include "channel-utils.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "mcd-channel.h"
#include "mcd-debug.h"

gboolean
_mcd_tp_channel_should_close (TpChannel *channel,
                              const gchar *verb)
{
    const GError *invalidated;
    const gchar *object_path;

    if (channel == NULL)
    {
        DEBUG ("Not %s NULL channel", verb);
        return FALSE;
    }

    invalidated = tp_proxy_get_invalidated (channel);
    object_path = tp_proxy_get_object_path (channel);

    if (invalidated != NULL)
    {
        DEBUG ("Not %s %p:%s, already invalidated: %s %d: %s",
               verb, channel, object_path,
               g_quark_to_string (invalidated->domain),
               invalidated->code, invalidated->message);
        return FALSE;
    }

    /* we used to special case ContactList and Tubes channels here and
     * never close them automatically, but no longer! */

    return TRUE;
}

GHashTable *
_mcd_tp_channel_dup_immutable_properties_asv (TpChannel *channel)
{
    GVariant *props;
    GHashTable *asv;
    GValue v = G_VALUE_INIT;

    props = tp_channel_dup_immutable_properties (channel);
    g_return_val_if_fail (props != NULL, NULL);

    dbus_g_value_parse_g_variant (props, &v);
    asv = g_value_dup_boxed (&v);

    g_variant_unref (props);
    g_value_unset (&v);

    return asv;
}

