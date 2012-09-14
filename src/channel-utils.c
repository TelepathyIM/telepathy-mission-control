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
    GQuark channel_type;

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

    channel_type = tp_channel_get_channel_type_id (channel);

    if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_CONTACT_LIST)
    {
        DEBUG ("Not %s %p:%s, it's a ContactList", verb, channel, object_path);
        return FALSE;
    }

    if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_TUBES)
    {
        DEBUG ("Not %s %p:%s, it's an old Tubes channel", verb, channel,
               object_path);
        return FALSE;
    }

    return TRUE;
}

static void
_channel_details_array_append (GPtrArray *channel_array, TpChannel *channel)
{
    GType type = TP_STRUCT_TYPE_CHANNEL_DETAILS;
    GValue channel_val = G_VALUE_INIT;
    GVariant *pair[2];
    GVariant *tuple;

    pair[0] = g_variant_new_object_path (tp_proxy_get_object_path (channel));
    pair[1] = tp_channel_dup_immutable_properties (channel);
    /* takes ownership of floating pair[0] */
    tuple = g_variant_new_tuple (pair, 2);
    dbus_g_value_parse_g_variant (tuple, &channel_val);
    g_variant_unref (pair[1]);
    g_variant_unref (tuple);
    g_assert (G_VALUE_HOLDS (&channel_val, type));

    g_ptr_array_add (channel_array, g_value_get_boxed (&channel_val));
}

/*
 * _mcd_tp_channel_details_build_from_list:
 * @channels: a #GList of #McdChannel elements.
 *
 * Returns: a #GPtrArray of Channel_Details, ready to be sent over D-Bus. Free
 * with _mcd_tp_channel_details_free().
 */
GPtrArray *
_mcd_tp_channel_details_build_from_list (const GList *channels)
{
    GPtrArray *channel_array;
    const GList *list;

    channel_array = g_ptr_array_sized_new (g_list_length ((GList *) channels));

    for (list = channels; list != NULL; list = list->next)
    {
        _channel_details_array_append (channel_array,
            mcd_channel_get_tp_channel (MCD_CHANNEL (list->data)));
    }

    return channel_array;
}

/*
 * _mcd_tp_channel_details_build_from_tp_chan:
 * @channel: a #TpChannel
 *
 * Returns: a #GPtrArray of Channel_Details, ready to be sent over D-Bus. Free
 * with _mcd_tp_channel_details_free().
 */
GPtrArray *
_mcd_tp_channel_details_build_from_tp_chan (TpChannel *channel)
{
    GPtrArray *channel_array = g_ptr_array_sized_new (1);

    _channel_details_array_append (channel_array, channel);
    return channel_array;
}

/*
 * _mcd_tp_channel_details_free:
 * @channels: a #GPtrArray of Channel_Details.
 *
 * Frees the memory used by @channels.
 */
void
_mcd_tp_channel_details_free (GPtrArray *channels)
{
    g_boxed_free (TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST, channels);
}


