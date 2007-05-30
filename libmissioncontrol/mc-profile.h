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

#include <libmissioncontrol/mc-protocol.h>

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

#include <libmissioncontrol/mission-control.h>

GType mc_profile_get_type (void);

McProfile* mc_profile_new (const gchar *unique_name);

/* to find one profile */
McProfile *mc_profile_lookup (const gchar *unique_name);
McProfile *mc_profile_lookup_default_for_vcard_field (const gchar *vcard_field);
#ifndef MC_DISABLE_DEPRECATED
void mc_profile_free (McProfile *id);
#endif
void mc_profile_clear_cache (void);

/* to find many profiles */
GList *mc_profiles_list (void);
GList *mc_profiles_list_by_vcard_field (const gchar *vcard_field);
void mc_profiles_free_list (GList *list);

const gchar *mc_profile_get_unique_name (McProfile *id);
const gchar *mc_profile_get_configuration_ui (McProfile *id);
const gchar *mc_profile_get_display_name (McProfile *id);
const gchar *mc_profile_get_icon_name (McProfile *id);
const gchar *mc_profile_get_branding_icon_name (McProfile *id);
const gchar *mc_profile_get_vcard_field (McProfile *id);
const gchar *mc_profile_get_default_account_domain (McProfile *id);
const McPresence *mc_profile_get_supported_presences (McProfile *id);
gboolean mc_profile_supports_presence (McProfile *id, McPresence presence);
McProtocol *mc_profile_get_protocol (McProfile *id);

/* only use this protocol name instead of the real McProfile if you do
 * not care about being able to discover the correct connection manager
 * and hence which options are valid when connecting a certain account.
 * without the manager name also, the protocol name is not sufficient
 * to look up an McProfile. this is intentional. */
const gchar *mc_profile_get_protocol_name (McProfile *id);

gboolean mc_profile_is_default_for_vcard_field (McProfile *id);
McProfileCapabilityFlags mc_profile_get_capabilities (McProfile *id);
const gchar *mc_profile_get_default_setting (McProfile *id, const gchar *setting);
const gchar *mc_profile_get_vcard_mangle (McProfile *id, const gchar *vcard_field);

G_END_DECLS

#endif /* __MC_PROFILE_H__ */
