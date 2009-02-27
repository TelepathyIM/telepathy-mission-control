/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * mc-account.c - Telepathy Account D-Bus interface (client side)
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "mc-errors.h"
#include <dbus/dbus-glib.h>

GQuark mc_error_quark (void)
{
    static gsize quark = 0;

    if (g_once_init_enter (&quark))
    {
        GQuark domain = g_quark_from_static_string ("mc-errors");

        g_assert (sizeof (GQuark) <= sizeof (gsize));

        g_type_init ();
        dbus_g_error_domain_register (domain, MC_ERROR_PREFIX, MC_TYPE_ERROR);
        g_once_init_leave (&quark, domain);
    }
    return (GQuark) quark;
}

GType
mc_error_get_type (void)
{
    static GType etype = 0;
    if (G_UNLIKELY (etype == 0))
    {
        static const GEnumValue values[] = {
            { MC_DISCONNECTED_ERROR, "MC_DISCONNECTED_ERROR", "Disconnected" },
            { MC_INVALID_HANDLE_ERROR, "MC_INVALID_HANDLE_ERROR", "InvalidHandle" },
            { MC_NO_MATCHING_CONNECTION_ERROR, "MC_NO_MATCHING_CONNECTION_ERROR", "NoMatchingConnection" },
            { MC_INVALID_ACCOUNT_ERROR, "MC_INVALID_ACCOUNT_ERROR", "InvalidAccount" },
            { MC_PRESENCE_FAILURE_ERROR, "MC_PRESENCE_FAILURE_ERROR", "PresenceFailure" },
            { MC_NO_ACCOUNTS_ERROR, "MC_NO_ACCOUNTS_ERROR", "NoAccounts" },
            { MC_NETWORK_ERROR, "MC_NETWORK_ERROR", "NetworkError" },
            { MC_CONTACT_DOES_NOT_SUPPORT_VOICE_ERROR, "MC_CONTACT_DOES_NOT_SUPPORT_VOICE_ERROR", "ContactDoesNotSupportVoice" },
            { MC_LOWMEM_ERROR, "MC_LOWMEM_ERROR", "Lowmem" },
            { MC_CHANNEL_REQUEST_GENERIC_ERROR, "MC_CHANNEL_REQUEST_GENERIC_ERROR", "ChannelRequestGenericError" },
            { MC_CHANNEL_BANNED_ERROR, "MC_CHANNEL_BANNED_ERROR", "ChannelBanned" },
            { MC_CHANNEL_FULL_ERROR, "MC_CHANNEL_FULL_ERROR", "ChannelFull" },
            { MC_CHANNEL_INVITE_ONLY_ERROR, "MC_CHANNEL_INVITE_ONLY_ERROR", "ChannelInviteOnly" },
            { 0 }
        };

      etype = g_enum_register_static ("McError", values);
    }
  return etype;
}

