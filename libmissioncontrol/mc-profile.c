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

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/types.h>
#include <libintl.h>

#include "mc-profile.h"
#include "mc-enum-types.h"
#include <config.h>

#define PROFILE_SUFFIX ".profile"
#define PROFILE_SUFFIX_LEN 8
#define PROFILE_GROUP "Profile"

#define MC_PROFILE_PRIV(profile) ((McProfilePrivate *)profile->priv)

G_DEFINE_TYPE (McProfile, mc_profile, G_TYPE_OBJECT);

static GHashTable *profile_cache = NULL;

const GDebugKey capabilities[] = {
  { "chat-p2p", MC_PROFILE_CAPABILITY_CHAT_P2P },
  { "chat-room", MC_PROFILE_CAPABILITY_CHAT_ROOM },
  { "chat-room-list", MC_PROFILE_CAPABILITY_CHAT_ROOM_LIST },
  { "voice-p2p", MC_PROFILE_CAPABILITY_VOICE_P2P },
  { "contact-search", MC_PROFILE_CAPABILITY_CONTACT_SEARCH },
  { "split-account", MC_PROFILE_CAPABILITY_SPLIT_ACCOUNT },
  { "registration-ui", MC_PROFILE_CAPABILITY_REGISTRATION_UI },
  { "supports-avatars", MC_PROFILE_CAPABILITY_SUPPORTS_AVATARS },
};

typedef struct {
    gchar *unique_name;
    gboolean loaded;
    gchar *configuration_ui;
    gchar *display_name;
    gchar *icon_name;
    gchar *branding_icon_name;
    gchar *manager;
    gchar *protocol;
    gchar *vcard_field;
    gchar *default_account_domain;
    gboolean vcard_default;
    McProfileCapabilityFlags capabilities;
    GHashTable *default_settings;
    GArray *supported_presences;
    time_t mtime;
} McProfilePrivate;

static void
mc_profile_init (McProfile *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
      MC_TYPE_PROFILE, McProfilePrivate);
}

static void
mc_profile_finalize (GObject *object)
{
  McProfile *self = MC_PROFILE(object);
  McProfilePrivate *priv;
  g_return_if_fail (self != NULL);

  priv = MC_PROFILE_PRIV (self);
  g_free (priv->unique_name);
  g_free (priv->configuration_ui);
  g_free (priv->display_name);
  g_free (priv->icon_name);
  g_free (priv->branding_icon_name);
  g_free (priv->manager);
  g_free (priv->protocol);
  g_free (priv->vcard_field);
  g_free (priv->default_account_domain);
  g_hash_table_destroy (priv->default_settings);
  g_array_free (priv->supported_presences, TRUE);
}

static void
mc_profile_class_init (McProfileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (object_class, sizeof (McProfilePrivate));
  object_class->finalize = mc_profile_finalize;
}

/**
 * mc_profile_clear_cache:
 *
 * Clears the profiles cache.
 */
void
mc_profile_clear_cache(void)
{
  if (NULL == profile_cache)
    return;

  g_hash_table_destroy(profile_cache);
  profile_cache = NULL;
}

static gchar * _mc_profile_filename (const gchar *name);

static time_t
_mc_profile_mtime (McProfile *profile)
{  
  McProfilePrivate *priv = MC_PROFILE_PRIV (profile);
  return priv->mtime;
}

static McProfile *
_mc_profile_new (const gchar *unique_name)
{
  McProfile *profile = NULL;
  McProfilePrivate *priv;
  struct stat buf;
  gchar *filename = _mc_profile_filename (unique_name);

  if (0 != g_stat (filename, &buf))
     goto OUT;

  if (NULL == profile_cache)
     profile_cache = g_hash_table_new_full (
       g_str_hash, g_str_equal, g_free, g_object_unref);

  profile = g_hash_table_lookup (profile_cache, unique_name);

  if (NULL != profile && _mc_profile_mtime (profile) >= buf.st_mtime)
    {
      g_object_ref (profile);
      goto OUT;
    }

  profile = (McProfile *) g_object_new (MC_TYPE_PROFILE, NULL);
  priv = MC_PROFILE_PRIV (profile);
  priv->unique_name = g_strdup (unique_name);
  priv->mtime = buf.st_mtime;
  g_hash_table_replace (profile_cache, g_strdup (unique_name), profile);
  g_object_ref (profile);

OUT:
  g_free (filename);
  return profile;
}

static const gchar**
_mc_profile_get_dirs ()
{
    GSList *dir_list = NULL, *slist;
    const gchar *dirname;
    static gchar **profile_dirs = NULL;
    guint n;

    if (profile_dirs) return (const gchar **)profile_dirs;

    dirname = g_getenv ("MC_PROFILE_DIR");
    if (dirname && g_file_test (dirname, G_FILE_TEST_IS_DIR))
	dir_list = g_slist_prepend (dir_list, (gchar *)dirname);

    if (PROFILES_DIR[0] == '/')
    {
	if (g_file_test (PROFILES_DIR, G_FILE_TEST_IS_DIR))
	    dir_list = g_slist_prepend (dir_list, PROFILES_DIR);
    }
    else
    {
	const gchar * const *dirs;
	gchar *dir;
	
	dir = g_build_filename (g_get_user_data_dir(), PROFILES_DIR, NULL);
	if (g_file_test (dir, G_FILE_TEST_IS_DIR))
	    dir_list = g_slist_prepend (dir_list, dir);
	else g_free (dir);

	dirs = g_get_system_data_dirs();
	for (dirname = *dirs; dirname; dirs++, dirname = *dirs)
	{
	    dir = g_build_filename (dirname, PROFILES_DIR, NULL);
	    if (g_file_test (dir, G_FILE_TEST_IS_DIR))
		dir_list = g_slist_prepend (dir_list, dir);
	    else g_free (dir);
	}
    }

    /* build the string array */
    n = g_slist_length (dir_list);
    profile_dirs = g_new (gchar *, n + 1);
    profile_dirs[n--] = NULL;
    for (slist = dir_list; slist; slist = slist->next)
	profile_dirs[n--] = slist->data;
    g_slist_free (dir_list);
    return (const gchar **)profile_dirs;
}

static gchar *
_mc_profile_filename (const gchar *name)
{
    const gchar **profile_dirs;
    const gchar *dirname;
    gchar *filename, *filepath = NULL;

    profile_dirs = _mc_profile_get_dirs ();
    if (!profile_dirs) return NULL;

    filename = g_strconcat (name, PROFILE_SUFFIX, NULL);
    for (dirname = *profile_dirs; dirname; profile_dirs++, dirname = *profile_dirs)
    {
	filepath = g_build_filename (dirname, filename);
	if (g_file_test (dirname, G_FILE_TEST_EXISTS)) break;
	g_free (filepath);
	filepath = NULL;
    }
    g_free (filename);
    return filepath;
}

static gboolean
_mc_profile_load (McProfile *profile)
{
  gchar *filename;
  GKeyFile *keyfile;
  GError *error = NULL;
  gchar *caps, *localization_domain;
  gchar **keys, **tmp;
  McProfilePrivate *priv;
  gchar **presences_str;
  GEnumClass *presences_class;
  gsize length;
  gint i;

  priv = MC_PROFILE_PRIV (profile);

  if (priv->loaded)
    return TRUE;

  filename = _mc_profile_filename (priv->unique_name);

  keyfile = g_key_file_new ();
  if (!g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, &error))
    {
      g_debug ("%s: loading %s failed: %s", G_STRFUNC, filename, error->message);
      g_error_free (error);
      return FALSE;
    }

  priv->configuration_ui = g_key_file_get_string (keyfile, PROFILE_GROUP, "ConfigurationUI", NULL);
  priv->display_name = g_key_file_get_string (keyfile, PROFILE_GROUP, "DisplayName", NULL);
  priv->icon_name = g_key_file_get_string (keyfile, PROFILE_GROUP, "IconName", NULL);
  priv->branding_icon_name = g_key_file_get_string (keyfile, PROFILE_GROUP, "BrandingIconName", NULL);
  priv->manager = g_key_file_get_string (keyfile, PROFILE_GROUP, "Manager", NULL);
  priv->protocol = g_key_file_get_string (keyfile, PROFILE_GROUP, "Protocol", NULL);
  priv->vcard_field = g_key_file_get_string (keyfile, PROFILE_GROUP, "VCardField", NULL);
  priv->vcard_default = g_key_file_get_boolean (keyfile, PROFILE_GROUP, "VCardDefault", NULL);
  priv->default_account_domain = g_key_file_get_string (keyfile, PROFILE_GROUP, "DefaultAccountDomain", NULL);
  localization_domain = g_key_file_get_string (keyfile, PROFILE_GROUP, "LocalizationDomain", NULL);
  if (localization_domain)
  {
    gchar *display_name;

    display_name = g_strdup (dgettext (localization_domain,
				       priv->display_name));
    g_free (priv->display_name);
    priv->display_name = display_name;
    g_free (localization_domain);
  }

  g_key_file_set_list_separator (keyfile, ',');
  presences_str = g_key_file_get_string_list (keyfile, PROFILE_GROUP,
					      "SupportedPresences", &length,
					      NULL);
  if (!presences_str) length = 0;
  presences_class = g_type_class_ref (MC_TYPE_PRESENCE);
  priv->supported_presences = g_array_sized_new (TRUE, FALSE,
						 sizeof (McPresence), length);
  for (i = 0; i < length; i++)
  {
      McPresence presence;
      GEnumValue *value;

      value = g_enum_get_value_by_nick (presences_class,
				       	presences_str[i]);
      if (!value)
      {
	  g_warning ("Unrecognized presence `%s'", presences_str[i]);
	  continue;
      }
      presence = value->value;
      g_array_append_val (priv->supported_presences, presence);
  }
  g_type_class_unref (presences_class);
  g_strfreev (presences_str);

  /* :D */
  caps = g_key_file_get_string (keyfile, PROFILE_GROUP, "Capabilities", NULL);
  if (caps)
  {
      g_strdelimit (caps, " ,;", ':');
      priv->capabilities =
	  g_parse_debug_string (caps, capabilities,
			       	sizeof (capabilities) / sizeof (GDebugKey));
      g_free (caps);
  }

  priv->default_settings = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  (GDestroyNotify) g_free,
                                                  (GDestroyNotify) g_free);
  keys = g_key_file_get_keys (keyfile, PROFILE_GROUP, 0, NULL);
  for (tmp = keys; *tmp != NULL; tmp++)
    {
      gchar *key = *tmp;
      if (0 == g_ascii_strncasecmp ("Default-", key, 8))
        {
          gchar *k, *v;
          k = g_strdup (key+8);
          v = g_key_file_get_string (keyfile, PROFILE_GROUP, key, NULL);
          g_hash_table_insert (MC_PROFILE_PRIV (profile)->default_settings, k, v);
        }
    }
  g_strfreev (keys);

  g_key_file_free (keyfile);
  g_free (filename);

  priv->loaded = TRUE;
  return TRUE;
}

/**
 * mc_profile_lookup:
 * @unique_name: The unique name of the profile.
 *
 * Get the profile whose unique name is the one specified. If no profile with
 * that name exists, a new one is created. The returned object's reference
 * count is incremented.
 *
 * Return value: The #McProfile.
 */
McProfile *
mc_profile_lookup (const gchar *unique_name)
{
  g_return_val_if_fail (unique_name != NULL, NULL);
  g_return_val_if_fail (*unique_name != '\0', NULL);

  return _mc_profile_new (unique_name);
}

/**
 * mc_profile_lookup_default_for_vcard_field:
 * @vcard_field: The vcard field.
 *
 * Get the profile whose vcard field is the one specified. 
 * The returned object's reference count is incremented.
 *
 * Return value: The #McProfile.
 */
McProfile *
mc_profile_lookup_default_for_vcard_field (const gchar *vcard_field)
{
  McProfile *ret = NULL;
  GList *list, *tmp;

  g_return_val_if_fail (vcard_field != NULL, NULL);
  g_return_val_if_fail (*vcard_field != '\0', NULL);

  list = mc_profiles_list ();

  for (tmp = list;
       tmp != NULL;
       tmp = tmp->next)
    {
      McProfile *cur = (McProfile *) tmp->data;
      McProfilePrivate *priv = MC_PROFILE_PRIV (cur);

      /* free the profile if we've found the desired value,
       * or we're unable to load this one */
      if (ret != NULL ||
          !_mc_profile_load (cur))
        {
          g_object_unref (cur);
          continue;
        }

      /* store the profile if it's the desired one,
       * free it otherwise */
      if (priv->vcard_default &&
          0 == strcmp (priv->vcard_field, vcard_field))
        {
          ret = cur;
          break;
        }
      else
        {
          g_object_unref (cur);
        }
    }

  g_list_free (list);

  return ret;
}

/**
 * mc_profile_free:
 * @id: The #McProfile.
 *
 * Frees (unref) the given profile.
 * DEPRECATED, use g_object_unref() instead.
 */
void
mc_profile_free (McProfile *id)
{
  g_object_unref (id);
}

/**
 * mc_profiles_list:
 * 
 * Lists all configured profiles.
 *
 * Returns: a #GList of the profiles (must be freed with #mc_profiles_free_list).
 */
GList *
mc_profiles_list (void)
{
    GDir *dir;
    GError *error = NULL;
    GList *ret = NULL;
    const gchar *filename, *dirname, **profile_dirs;

    profile_dirs = _mc_profile_get_dirs ();
    if (!profile_dirs) return NULL;

    for (dirname = *profile_dirs; dirname; profile_dirs++, dirname = *profile_dirs)
    {
	dir = g_dir_open(dirname, 0, &error);
	if (!dir)
	{
	    g_warning ("%s: unable to open directory %s: %s",
		       G_STRFUNC, dirname, error->message);
	    g_error_free (error);
	    continue;
	}

	while ((filename = g_dir_read_name(dir)) != NULL)
	{
	    gchar *unique_name;
	    McProfile *profile;

	    if (!g_str_has_suffix (filename, PROFILE_SUFFIX))
		continue;

	    unique_name = g_strndup (filename,
				     strlen(filename) - PROFILE_SUFFIX_LEN);
	    profile = _mc_profile_new (unique_name);
	    g_free (unique_name);

	    ret = g_list_prepend (ret, profile);
	}

	g_dir_close (dir);
    }

    return ret;
}

/**
 * mc_profiles_list_by_vcard_field:
 * @vcard_field: The vcard field.
 * 
 * Lists all configured profiles with the given vcard field..
 *
 * Returns: a #GList of the profiles (must be freed with #mc_profiles_free_list).
 */
GList *
mc_profiles_list_by_vcard_field (const gchar *vcard_field)
{
  GList *all, *tmp, *ret;

  g_return_val_if_fail (vcard_field != NULL, NULL);
  g_return_val_if_fail (*vcard_field != '\0', NULL);

  all = mc_profiles_list ();
  ret = NULL;

  for (tmp = all;
       tmp != NULL;
       tmp = tmp->next)
    {
      McProfile *cur = (McProfile *) tmp->data;
      McProfilePrivate *priv = MC_PROFILE_PRIV (cur);

      if (!_mc_profile_load (cur))
        {
          g_object_unref (cur);
          continue;
        }

      if (priv->vcard_field && 0 == strcmp (vcard_field, priv->vcard_field))
        {
          ret = g_list_prepend (ret, cur);
        }
      else
        {
          g_object_unref (cur);
        }
    }

  g_list_free (all);

  return ret;
}

/**
 * mc_profiles_free_list:
 * @list: The #GList of #McProfile.
 * 
 * Frees a list of profiles.
 */
void
mc_profiles_free_list (GList *list)
{
  GList *tmp;

  for (tmp = list; tmp != NULL; tmp = tmp->next)
    {
      McProfile *p = tmp->data;
      g_object_unref (p);
    }

  g_list_free (list);
}

/**
 * mc_profile_get_unique_name:
 * @id: The #McProfile.
 * 
 * Get the unique name of the profile.
 *
 * Returns: a string representing the unique name (must not be freed).
 */
const gchar *
mc_profile_get_unique_name (McProfile *id)
{
  gboolean profile_loaded;

  g_return_val_if_fail (id != NULL, NULL);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, NULL);

  return MC_PROFILE_PRIV(id)->unique_name;
}

/**
 * mc_profile_get_configuration_ui:
 * @id: The #McProfile.
 * 
 * Get the configuration ui of the profile.
 *
 * Returns: a string representing the configuration ui (must not be freed).
 */
const gchar *
mc_profile_get_configuration_ui (McProfile *id)
{
  gboolean profile_loaded;
    
  g_return_val_if_fail (id != NULL, NULL);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, NULL);

  return MC_PROFILE_PRIV (id)->configuration_ui;
}

/**
 * mc_profile_get_display_name:
 * @id: The #McProfile.
 * 
 * Get the display name of the profile.
 *
 * Returns: a string representing the display name (must not be freed).
 */
const gchar *
mc_profile_get_display_name (McProfile *id)
{
  gboolean profile_loaded;
    
  g_return_val_if_fail (id != NULL, NULL);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, NULL);

  return MC_PROFILE_PRIV (id)->display_name;
}

/**
 * mc_profile_get_icon_name:
 * @id: The #McProfile.
 * 
 * Get the icon name of the profile.
 *
 * Returns: a string representing the icon name (must not be freed).
 */
const gchar *
mc_profile_get_icon_name (McProfile *id)
{
  gboolean profile_loaded;
    
  g_return_val_if_fail (id != NULL, NULL);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, NULL);

  return MC_PROFILE_PRIV (id)->icon_name;
}

/**
 * mc_profile_get_branding_icon_name:
 * @id: The #McProfile.
 * 
 * Get the branding icon name of the profile.
 *
 * Returns: a string representing the branding icon name (must not be freed).
 */
const gchar *
mc_profile_get_branding_icon_name (McProfile *id)
{
  gboolean profile_loaded;
    
  g_return_val_if_fail (id != NULL, NULL);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, NULL);

  return MC_PROFILE_PRIV (id)->branding_icon_name;
}

/**
 * mc_profile_get_supported_presences:
 * @id: The #McProfile.
 *
 * Checks what presence states are supported by this profile.
 *
 * Returns: a zero-terminated array listing all the supported #McPresence.
 * It must not be freed.
 */
const McPresence *
mc_profile_get_supported_presences (McProfile *id)
{
  gboolean profile_loaded;
    
  g_return_val_if_fail (id != NULL, NULL);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, NULL);

  return (McPresence *)(MC_PROFILE_PRIV (id)->supported_presences->data);
}

/*
 * mc_profile_supports_presence:
 * @id: The #McProfile.
 * @presence: The #McPresence.
 *
 * Tests whether the profile supports the presence @presence.
 *
 * Returns: a #gboolean.
 */
gboolean
mc_profile_supports_presence (McProfile *id, McPresence presence)
{
  const McPresence *presences;

  presences = mc_profile_get_supported_presences (id);
  if (!presences) return FALSE;

  while (*presences)
  {
      if (*presences == presence)
	  return TRUE;
      presences++;
  }
  return FALSE;
}

/**
 * mc_profile_get_protocol:
 * @id: The #McProfile.
 * 
 * gets the protocol in use for this profile.
 *
 * Returns: a #McProtocol, or NULL if some error occurs.
 */
McProtocol *
mc_profile_get_protocol (McProfile *id)
{
  McManager *manager;
  McProtocol *protocol;
  McProfilePrivate *priv = MC_PROFILE_PRIV (id);
  gboolean profile_loaded;

  g_return_val_if_fail (id != NULL, NULL);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, NULL);

  manager = mc_manager_lookup (priv->manager);
  g_return_val_if_fail (manager != NULL, NULL);

  protocol = mc_protocol_lookup (manager, priv->protocol);
  g_object_unref (manager);

  g_return_val_if_fail (protocol != NULL, NULL);
  return protocol;
}

/**
 * mc_profile_get_protocol_name:
 * @id: The #McProfile.
 * 
 * Get the protocol name of the profile.
 *
 * Returns: a string representing the protocol name (must not be freed).
 */
const gchar *
mc_profile_get_protocol_name (McProfile *id)
{
  gboolean profile_loaded;

  g_return_val_if_fail (id != NULL, NULL);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, NULL);

  return MC_PROFILE_PRIV (id)->protocol;
}

/**
 * mc_profile_get_vcard_field:
 * @id: The #McProfile.
 * 
 * Get the vcard field of the profile.
 *
 * Returns: a string representing the vcard field (must not be freed).
 */
const gchar *
mc_profile_get_vcard_field (McProfile *id)
{
  gboolean profile_loaded;

  g_return_val_if_fail (id != NULL, NULL);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, NULL);

  return MC_PROFILE_PRIV (id)->vcard_field;
}

/**
 * mc_profile_get_default_account_domain:
 * @id: The #McProfile.
 * 
 * Get the default account domain of the profile.
 *
 * Returns: a string representing the default account domain (must not be
 * freed).
 */
const gchar *
mc_profile_get_default_account_domain (McProfile *id)
{
  McProfilePrivate *priv = MC_PROFILE_PRIV (id);
  gboolean profile_loaded;

  g_return_val_if_fail (id != NULL, NULL);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, NULL);
  g_return_val_if_fail (
    priv->capabilities & MC_PROFILE_CAPABILITY_SPLIT_ACCOUNT, NULL);

  return priv->default_account_domain;
}

/**
 * mc_profile_is_default_for_vcard_field:
 * @id: The #McProfile.
 * 
 * Checks if this is the default profile for the given vcard field.
 *
 * Returns: a gboolean.
 */
gboolean
mc_profile_is_default_for_vcard_field (McProfile *id)
{
  gboolean profile_loaded;

  g_return_val_if_fail (id != NULL, FALSE);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, FALSE);

  return MC_PROFILE_PRIV (id)->vcard_default;
}

/**
 * mc_profile_get_capabilities:
 * @id: The #McProfile.
 * 
 * Gets the capabilities of this profile.
 *
 * Returns: a combination of #McProfileCapabilityFlags.
 */
McProfileCapabilityFlags
mc_profile_get_capabilities (McProfile *id)
{
  gboolean profile_loaded;

  g_return_val_if_fail (id != NULL, 0);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, 0);

  return MC_PROFILE_PRIV (id)->capabilities;
}

/**
 * mc_profile_get_default_setting:
 * @id: The #McProfile.
 * @setting: The setting for which default value has to be retrieved.
 * 
 * Get the default value of a setting of the profile.
 *
 * Returns: a string representing the default setting (must not be freed).
 */
const gchar *
mc_profile_get_default_setting (McProfile *id, const gchar *setting)
{
  McProtocol *proto;
  GSList *params, *tmp;
  McProtocolParam *param;
  const gchar *def;
  gboolean profile_loaded;

  g_return_val_if_fail (id != NULL, NULL);
  g_return_val_if_fail (setting != NULL, NULL);
  g_return_val_if_fail (*setting != '\0', NULL);

  profile_loaded = _mc_profile_load (id);
  g_return_val_if_fail (profile_loaded, NULL);

  def = (const gchar *) g_hash_table_lookup (MC_PROFILE_PRIV (id)->default_settings, setting);

  if (def)
    {
      return def;
    }

  proto = mc_profile_get_protocol (id);
  params = mc_protocol_get_params (proto);

  for (tmp = params; tmp != NULL; tmp = tmp->next)
    {
      param = (McProtocolParam *) tmp->data;      

      if ((NULL != param) && (NULL != param->name)
            && (0 == strcmp(param->name, setting)))
        {
          def = param->def;
        }
    }

  mc_protocol_free_params_list (params);
  return def;
}
