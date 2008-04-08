/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
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

#ifndef __MC_ACCOUNT_PRIV_H__
#define __MC_ACCOUNT_PRIV_H__

#define MC_ACCOUNT_MANAGER_DBUS_SERVICE "org.freedesktop.Telepathy.AccountManager"
#define MC_ACCOUNT_MANAGER_DBUS_OBJECT "/org/freedesktop/Telepathy/AccountManager"
#define MC_ACCOUNT_DBUS_OBJECT_BASE "/org/freedesktop/Telepathy/Account/"

#define MC_ACCOUNTS_GCONF_KEY_DISPLAY_NAME "DisplayName"
#define MC_ACCOUNTS_GCONF_KEY_NORMALIZED_NAME "NormalizedName"
#define MC_ACCOUNTS_GCONF_KEY_VALID "Valid"
#define MC_ACCOUNTS_GCONF_KEY_ENABLED "Enabled"
#define MC_ACCOUNTS_GCONF_KEY_PARAMETERS "Parameters"
#define MC_ACCOUNTS_GCONF_KEY_DELETED "deleted"
#define MC_ACCOUNTS_GCONF_KEY_PROFILE "Profile"
#define MC_ACCOUNTS_GCONF_KEY_PARAM_ACCOUNT "account"
#define MC_ACCOUNTS_GCONF_KEY_AVATAR "Avatar"
#define MC_ACCOUNTS_GCONF_KEY_AVATAR_FILE "AvatarFile"
#define MC_ACCOUNTS_GCONF_KEY_AVATAR_TOKEN "avatar_token"
#define MC_ACCOUNTS_GCONF_KEY_AVATAR_MIME "avatar_mime"
#define MC_ACCOUNTS_GCONF_KEY_AVATAR_ID "avatar_id"
#define MC_ACCOUNTS_GCONF_KEY_DATA_DIR "data_dir"
#define MC_ACCOUNTS_GCONF_KEY_ALIAS "Nickname"
#define MC_ACCOUNTS_GCONF_KEY_SECONDARY_VCARD_FIELDS "SecondaryVCardFields"

McAccount *_mc_account_new (TpDBusDaemon *dbus_daemon,
			    const gchar *object_path);
void _mc_account_set_enabled_priv (McAccount *account, gboolean enabled);
void _mc_account_set_normalized_name_priv (McAccount *account, const gchar *name);
void _mc_account_set_display_name_priv (McAccount *account, const gchar *name);

#define MC_ACCOUNT_UNIQUE_NAME_FROM_PATH(object_path) \
    (object_path + (sizeof (MC_ACCOUNT_DBUS_OBJECT_BASE) - 1))
#endif /* __MC_ACCOUNT_PRIV_H__ */

