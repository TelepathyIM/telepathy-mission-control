/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

#ifndef __MC_PROFILE_H__
#define __MC_PROFILE_H__

#include <glib.h>
#include <glib-object.h>

#include <telepathy-glib/enums.h>

G_BEGIN_DECLS

typedef enum
{
    MC_PROFILE_CAPABILITY_NONE = 0,
    MC_PROFILE_CAPABILITY_CHAT_P2P = 1 << 0,
    MC_PROFILE_CAPABILITY_CHAT_ROOM = 1 << 1,
    MC_PROFILE_CAPABILITY_CHAT_ROOM_LIST = 1 << 2,
    MC_PROFILE_CAPABILITY_VOICE_P2P = 1 << 3,
    MC_PROFILE_CAPABILITY_CONTACT_SEARCH = 1 << 4,
    MC_PROFILE_CAPABILITY_SPLIT_ACCOUNT = 1 << 5,
    MC_PROFILE_CAPABILITY_REGISTRATION_UI = 1 << 6,
    MC_PROFILE_CAPABILITY_SUPPORTS_AVATARS = 1 << 7,
    MC_PROFILE_CAPABILITY_SUPPORTS_ALIAS = 1 << 8,
    MC_PROFILE_CAPABILITY_SUPPORTS_ROSTER = 1 << 9,
    MC_PROFILE_CAPABILITY_VIDEO_P2P = 1 << 10,
} McProfileCapabilityFlags;

typedef struct {
    GObject parent;
    gpointer priv;
} McProfile;

#define MC_TYPE_PROFILE mc_profile_get_type()

#define MC_PROFILE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  MC_TYPE_PROFILE, McProfile))

#define MC_PROFILE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  MC_TYPE_PROFILE, McProfilewClass))

#define MC_IS_PROFILE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  MC_TYPE_PROFILE))

#define MC_IS_PROFILE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  MC_TYPE_PROFILE))

#define MC_PROFILE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  MC_TYPE_PROFILE, McProfilewClass))

typedef struct {
        GObjectClass parent_class;
} McProfileClass;

GType mc_profile_get_type (void);

/* to find one profile */
McProfile *mc_profile_lookup (const gchar *unique_name);
gchar *mc_profile_get_filename (const gchar *unique_name);
McProfile *mc_profile_lookup_default_for_vcard_field (const gchar *vcard_field);
void mc_profile_clear_cache (void);

/* to find many profiles */
GList *mc_profiles_list (void);
GList *mc_profiles_list_by_vcard_field (const gchar *vcard_field);
GList *mc_profiles_list_by_protocol (const gchar *protocol);
void mc_profiles_free_list (GList *list);

const gchar *mc_profile_get_unique_name (McProfile *id);
const gchar *mc_profile_get_configuration_ui (McProfile *id);
const gchar *mc_profile_get_display_name (McProfile *id);
const gchar *mc_profile_get_icon_name (McProfile *id);
const gchar *mc_profile_get_branding_icon_name (McProfile *id);
const gchar *mc_profile_get_vcard_field (McProfile *id);
const gchar *mc_profile_get_default_account_domain (McProfile *id);
const gchar *mc_profile_get_avatar_mime_type (McProfile *id);
const TpConnectionPresenceType *mc_profile_get_supported_presences (McProfile *id);
gboolean mc_profile_supports_presence (McProfile *id, TpConnectionPresenceType presence);
gint mc_profile_get_priority (McProfile *id);
const gchar *mc_profile_get_default_account_name (McProfile *id);

const gchar *mc_profile_get_protocol_name (McProfile *id);
const gchar *mc_profile_get_manager_name (McProfile *id);

gboolean mc_profile_is_default_for_vcard_field (McProfile *id);
gboolean mc_profile_get_single_enable (McProfile *id);
McProfileCapabilityFlags mc_profile_get_capabilities (McProfile *id);
const gchar *mc_profile_get_default_setting (McProfile *id,
					     const gchar *setting);
const gchar *mc_profile_get_vcard_mangle (McProfile *id, const gchar *vcard_field);

GKeyFile *mc_profile_get_keyfile (McProfile *profile);

/* presences */
const gchar * const *mc_profile_presences_list (McProfile *id);
gchar *mc_profile_presence_get_name (McProfile *id,
                                     const gchar *presence);
TpConnectionPresenceType mc_profile_presence_get_type (McProfile *id,
                                                       const gchar *presence);
gchar *mc_profile_presence_get_icon_name (McProfile *id,
                                          const gchar *presence);

/* actions */
GList *mc_profile_actions_list (McProfile *profile);
GList *mc_profile_actions_list_by_vcard_field (McProfile *profile,
                                               const gchar *vcard_field);
GList *mc_profile_actions_list_by_vcard_fields (McProfile *profile,
                                                const gchar **vcard_fields);

gchar *mc_profile_action_get_name (McProfile *profile, const gchar *action);
gchar *mc_profile_action_get_icon_name (McProfile *profile,
                                        const gchar *action);
gchar **mc_profile_action_get_vcard_fields (McProfile *profile,
                                            const gchar *action);
GHashTable *mc_profile_action_get_properties (McProfile *profile,
                                              const gchar *action);
void mc_profile_actions_list_free (GList *actions);

G_END_DECLS

#endif /* __MC_PROFILE_H__ */
