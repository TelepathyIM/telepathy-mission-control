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

#define MC_ACCOUNTS_GCONF_BASE "/apps/telepathy/mc/accounts"
#define MC_ACCOUNTS_GCONF_KEY_DISPLAY_NAME "display_name"
#define MC_ACCOUNTS_GCONF_KEY_NORMALIZED_NAME "normalized_name"
#define MC_ACCOUNTS_GCONF_KEY_ENABLED "enabled"
#define MC_ACCOUNTS_GCONF_KEY_DELETED "deleted"
#define MC_ACCOUNTS_GCONF_KEY_PROFILE "profile"
#define MC_ACCOUNTS_GCONF_KEY_PARAM_ACCOUNT "account"
#define MC_ACCOUNTS_GCONF_KEY_AVATAR_TOKEN "avatar_token"
#define MC_ACCOUNTS_GCONF_KEY_AVATAR_MIME "avatar_mime"
#define MC_ACCOUNTS_GCONF_KEY_AVATAR_ID "avatar_id"
#define MC_ACCOUNTS_GCONF_KEY_DATA_DIR "data_dir"
#define MC_ACCOUNTS_GCONF_KEY_ALIAS "alias"
#define MC_ACCOUNTS_GCONF_KEY_SECONDARY_VCARD_FIELDS "secondary_vcard_fields"

McAccount * _mc_account_new (const gchar *unique_name);
void _mc_account_set_enabled_priv (McAccount *account, gboolean enabled);

#endif /* __MC_ACCOUNT_PRIV_H__ */

