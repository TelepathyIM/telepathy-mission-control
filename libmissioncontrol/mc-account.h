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

#ifndef __MC_ACCOUNT_H__
#define __MC_ACCOUNT_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MC_TYPE_ACCOUNT mc_account_get_type()

#define MC_ACCOUNT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  MC_TYPE_ACCOUNT, McAccount))

#define MC_ACCOUNT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  MC_TYPE_ACCOUNT, McAccountwClass))

#define MC_IS_ACCOUNT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  MC_TYPE_ACCOUNT))

#define MC_IS_ACCOUNT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  MC_TYPE_ACCOUNT))

#define MC_ACCOUNT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  MC_TYPE_ACCOUNT, McAccountwClass))

typedef struct {
    GObject parent;
    gpointer priv;
} McAccount;

typedef struct {
    GObjectClass parent_class;
} McAccountClass;

GType mc_account_get_type (void);

typedef enum
{
    MC_ACCOUNT_SETTING_ABSENT = 0,
    MC_ACCOUNT_SETTING_FROM_ACCOUNT,
    MC_ACCOUNT_SETTING_FROM_PROFILE,
    MC_ACCOUNT_SETTING_FROM_PROXY,
} McAccountSettingState;

#include <libmissioncontrol/mc-profile.h>

/* returns NULL if unique name does not exist */
McAccount *mc_account_lookup (const gchar *unique_name);
McAccount *mc_account_lookup_with_profile (McProfile *profile,
                                                 const gchar *account);
McAccount *mc_account_lookup_with_vcard_field (const gchar *vcard_field,
                                                     const gchar *account);
#ifndef MC_DISABLE_DEPRECATED
void mc_account_free (McAccount* account);
#endif
void mc_account_clear_cache (void);

/* returns newly-created account, enabled by default */
McAccount *mc_account_create (McProfile *profile);

/* this function only deletes the account from database,
 * the account struct itself must be freed separately */
gboolean mc_account_delete (McAccount *account);

/* lists returned by these functions should be freed with
 * mc_accounts_list_free */
GList *mc_accounts_list (void);
GList *mc_accounts_list_by_enabled (gboolean enabled);
GList *mc_accounts_list_by_profile (McProfile *profile);
GList *mc_accounts_list_by_vcard_field (const gchar *vcard_field);
GList *mc_accounts_list_by_secondary_vcard_field (const gchar *vcard_field);
void mc_accounts_list_free (GList *list);

/* filter a list of accounts according to whether a function returns TRUE,
 * freeing the list and any accounts which are filtered out, and returning a
 * new list which must be freed with mc_accounts_list_free. */
typedef gboolean (*McAccountFilter) (McAccount *account, gpointer data);
GList *mc_accounts_filter (GList *accounts,
                              McAccountFilter filter,
                              gpointer data);

/* a unique identifier string for this account */
const gchar *mc_account_get_unique_name (McAccount *account);

/* get profile */
McProfile *mc_account_get_profile (McAccount *account);

/* human-readable name */
const gchar *mc_account_get_display_name (McAccount *account);
gboolean mc_account_set_display_name (McAccount *account,
                                         const gchar *name);

/* normalized name */
const gchar *mc_account_get_normalized_name (McAccount *account);
gboolean mc_account_set_normalized_name (McAccount *account,
                                         const gchar *name);

/* whether account is enabled or disabled */
gboolean mc_account_is_enabled (McAccount *account);
gboolean mc_account_set_enabled (McAccount *account,
                                    const gboolean enabled);

/* the following methods retrieve a parameter from the account or the
 * default from the profile if the account does not set the value */
McAccountSettingState mc_account_get_param_boolean (McAccount *account,
                                                          const gchar *name,
                                                          gboolean *value);
McAccountSettingState mc_account_get_param_int (McAccount *account,
                                                      const gchar *name,
                                                      gint *value);
McAccountSettingState mc_account_get_param_string (McAccount *account,
                                                         const gchar *name,
                                                         gchar **value);

/* for every parameter (both optional and mandatory) defined by the
 * protocol, get a hash table of the params from the account or
 * the default profile. each setting is stored in a GValue. */
GHashTable *mc_account_get_params (McAccount *account);

/* Set functions. Returns true if success, else false is returned */
gboolean mc_account_set_param_boolean (McAccount *account,
                                          const gchar *name,
                                          gboolean value);
gboolean mc_account_set_param_int (McAccount *account,
                                      const gchar *name,
                                      gint value);
gboolean mc_account_set_param_string (McAccount *account,
                                         const gchar *name,
                                         const gchar *value);

gboolean mc_account_unset_param (McAccount *account, const gchar *name);

/* returns TRUE if the account information, along with the profile defaults
 * has all mandatory fields (declared by the protocol) set */
gboolean mc_account_is_complete (McAccount *account);

const McPresence *mc_account_get_supported_presences (McAccount *account);
gboolean mc_account_supports_presence (McAccount *account,
				       McPresence presence);

gboolean mc_account_set_avatar (McAccount *account, const gchar *filename,
			       	const gchar *mime_type);
gboolean mc_account_get_avatar (McAccount *account, gchar **filename,
			       	gchar **mime_type, gchar **token);

gboolean mc_account_set_avatar_token (McAccount *account, const gchar *token);
gboolean mc_account_set_avatar_mime_type (McAccount *account,
					  const gchar *mime_type);

gint mc_account_get_avatar_id (McAccount *account);
gboolean mc_account_reset_avatar_id (McAccount *account);

gboolean mc_account_set_alias (McAccount *account, const gchar *alias);
gchar *mc_account_get_alias (McAccount *account);

gboolean mc_account_set_secondary_vcard_fields (McAccount *account,
					       	const GList *fields);
GList *mc_account_get_secondary_vcard_fields (McAccount * acct);
/*
void mc_account_add_secondary_vcard_field (McAccount * acct, const char * field);
void mc_account_remove_secondary_vcard_field (McAccount * acct, const char * field);
*/
G_END_DECLS

#endif /* __MC_ACCOUNT_H__ */
