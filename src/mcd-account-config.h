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

#ifndef __MCD_ACCOUNT_CONFIG_H__
#define __MCD_ACCOUNT_CONFIG_H__

#include <telepathy-glib/telepathy-glib-dbus.h>

/* If you add new storable attributes you must also update
 * known_attributes in mcd-storage.c. */

/* string, 's' */
#define MC_ACCOUNTS_KEY_MANAGER "manager"
#define MC_ACCOUNTS_KEY_PROTOCOL "protocol"
#define MC_ACCOUNTS_KEY_DISPLAY_NAME "DisplayName"
#define MC_ACCOUNTS_KEY_NORMALIZED_NAME "NormalizedName"
#define MC_ACCOUNTS_KEY_AVATAR_TOKEN "avatar_token"
#define MC_ACCOUNTS_KEY_AVATAR_MIME "AvatarMime"
#define MC_ACCOUNTS_KEY_AUTOMATIC_PRESENCE "AutomaticPresence"
/* next two are obsoleted by MC_ACCOUNTS_KEY_AUTOMATIC_PRESENCE */
#define MC_ACCOUNTS_KEY_AUTO_PRESENCE_STATUS "AutomaticPresenceStatus"
#define MC_ACCOUNTS_KEY_AUTO_PRESENCE_MESSAGE "AutomaticPresenceMessage"
#define MC_ACCOUNTS_KEY_ICON "Icon"
#define MC_ACCOUNTS_KEY_NICKNAME "Nickname"
#define MC_ACCOUNTS_KEY_SERVICE "Service"
/* ... also "condition-*" reserved by mcd-account-conditions.c */

/* unsigned 32-bit integer, 'u' */
/* obsoleted by MC_ACCOUNTS_KEY_AUTOMATIC_PRESENCE */
#define MC_ACCOUNTS_KEY_AUTO_PRESENCE_TYPE "AutomaticPresenceType"

/* boolean, 'b' */
#define MC_ACCOUNTS_KEY_ALWAYS_DISPATCH "always_dispatch"
#define MC_ACCOUNTS_KEY_CONNECT_AUTOMATICALLY "ConnectAutomatically"
#define MC_ACCOUNTS_KEY_ENABLED "Enabled"
#define MC_ACCOUNTS_KEY_HAS_BEEN_ONLINE "HasBeenOnline"
#define MC_ACCOUNTS_KEY_HIDDEN "Hidden"

/* string array, 'as' */
#define MC_ACCOUNTS_KEY_URI_SCHEMES \
    TP_IFACE_ACCOUNT_INTERFACE_ADDRESSING ".URISchemes"

/* object path array, 'ao' */
#define MC_ACCOUNTS_KEY_SUPERSEDES "Supersedes"

/* things that previously existed, so they should now be considered
 * to be reserved */
#define PRESETS_GROUP "Presets"
#define PRESETS_GROUP_DEFAULTS "Defaults"
#define MC_OLD_ACCOUNTS_KEY_AVATAR_ID "avatar_id"
#define MC_OLD_ACCOUNTS_KEY_DATA_DIR "data_dir"
#define MC_OLD_ACCOUNTS_KEY_DELETED "deleted"
#define MC_OLD_ACCOUNTS_KEY_GROUPS "groups"
#define MC_OLD_ACCOUNTS_KEY_ICON_NAME "icon_name"
#define MC_OLD_ACCOUNTS_KEY_PRESETS "presets"
#define MC_OLD_ACCOUNTS_KEY_PROFILE "profile"
#define MC_OLD_ACCOUNTS_KEY_SECONDARY_VCARD_FIELDS "secondary_vcard_fields"

#endif /* __MCD_ACCOUNT_CONFIG_H__ */
