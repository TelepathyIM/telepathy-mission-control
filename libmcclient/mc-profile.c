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

#define MC_INTERNAL

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/types.h>
#include <libintl.h>

#include "mc-profile.h"
#include <config.h>
#include <telepathy-glib/util.h>
#include "dbus-api.h"

#define PROFILE_SUFFIX ".profile"
#define PROFILE_SUFFIX_LEN 8
#define PROFILE_GROUP "Profile"
#define PRESENCE_PREFIX "Presence "
#define PRESENCE_PREFIX_LEN (sizeof (PRESENCE_PREFIX) - 1)
#define ACTION_PREFIX "Action "
#define ACTION_PREFIX_LEN (sizeof (ACTION_PREFIX) - 1)
#define ACTION_PROP_PREFIX "prop-"
#define ACTION_PROP_PREFIX_LEN (sizeof (ACTION_PROP_PREFIX) - 1)

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
    { "supports-alias", MC_PROFILE_CAPABILITY_SUPPORTS_ALIAS },
    { "supports-roster", MC_PROFILE_CAPABILITY_SUPPORTS_ROSTER },
    { "video-p2p", MC_PROFILE_CAPABILITY_VIDEO_P2P },
};

const static gchar *presence_map[] = {
    "unset",
    "offline",
    "available",
    "away",
    "extended-away",
    "hidden",
    "do-not-disturb",
    NULL
};

typedef struct {
    GKeyFile *keyfile;
    gchar *unique_name;
    gchar *configuration_ui;
    gchar *display_name;
    gchar *icon_name;
    gchar *branding_icon_name;
    gchar *manager;
    gchar *protocol;
    gchar *vcard_field;
    gchar *default_account_domain;
    gchar *avatar_mime_type;
    gchar *default_account_name;
    gchar *localization_domain;
    gchar **presences;
    gint priority;
    guint vcard_default : 1;
    guint single_enable : 1;
    McProfileCapabilityFlags capabilities;
    GHashTable *default_settings;
    GHashTable *vcard_mangle_hash;
    GArray *supported_presences;
    time_t mtime;
} McProfilePrivate;

#define get_private_and_load_or_return_val(profile, val) \
{ \
    g_return_val_if_fail (MC_IS_PROFILE (profile), val); \
    priv = profile->priv; \
    if (G_UNLIKELY (!priv->keyfile)) _mc_profile_load (profile); \
    g_return_val_if_fail (priv->keyfile != NULL, val); \
}

static gchar *
get_localized_group_field (McProfilePrivate *priv, const gchar *group,
                           const gchar *field)
{
    gchar *name, *string;

    if (priv->localization_domain)
    {
        string = g_key_file_get_string (priv->keyfile, group, field, NULL);
        if (string)
        {
            name = g_strdup (dgettext (priv->localization_domain, string));
            g_free (string);
        }
        else
            name = NULL;
    }
    else
        name = g_key_file_get_locale_string (priv->keyfile, group, field,
                                             NULL, NULL);
    return name;
}

static gboolean
set_value_from_key (GKeyFile *keyfile, const gchar *group, const gchar *key,
                    GValue *value)
{
    gboolean ok = FALSE;
    switch (G_VALUE_TYPE (value))
    {
    case G_TYPE_STRING:
        {
            gchar *string;
            string = g_key_file_get_string (keyfile, group, key, NULL);
            if (string)
            {
                g_value_take_string (value, string);
                ok = TRUE;
            }
        }
        break;
    case G_TYPE_UINT:
        {
            guint i;
            i = (guint)g_key_file_get_integer (keyfile, group, key, NULL);
            g_value_set_uint (value, i);
            ok = TRUE;
        }
        break;
    case G_TYPE_INT:
        {
            gint i;
            i = g_key_file_get_integer (keyfile, group, key, NULL);
            g_value_set_int (value, i);
            ok = TRUE;
        }
        break;
    case G_TYPE_BOOLEAN:
        {
            gboolean b;
            b = g_key_file_get_boolean (keyfile, group, key, NULL);
            g_value_set_boolean (value, b);
            ok = TRUE;
        }
        break;
    default:
        g_warning ("%s: don't know how to parse type %s", G_STRFUNC,
                   G_VALUE_TYPE_NAME (value));
        break;
    }
    return ok;
}

static TpConnectionPresenceType
map_presence (const gchar *status)
{
    TpConnectionPresenceType type;

    for (type = 0; presence_map[type] != NULL; type++)
	if (strcmp (status, presence_map[type]) == 0)
	    return type;
    return TP_CONNECTION_PRESENCE_TYPE_UNSET;
}

static void
mc_profile_init (McProfile *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, MC_TYPE_PROFILE,
					     McProfilePrivate);
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
    g_free (priv->avatar_mime_type);
    g_free (priv->default_account_name);
    g_free (priv->localization_domain);
    g_strfreev (priv->presences);
    g_hash_table_destroy (priv->default_settings);
    g_hash_table_destroy (priv->vcard_mangle_hash);
    g_array_free (priv->supported_presences, TRUE);
    g_key_file_free (priv->keyfile);
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
	profile_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, g_object_unref);

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
	filepath = g_build_filename (dirname, filename, NULL);
	if (g_file_test (filepath, G_FILE_TEST_EXISTS)) break;
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
    gchar *caps;
    gchar **keys, **tmp;
    McProfilePrivate *priv;
    gchar **presences_str;
    gsize length;
    gint i;

    priv = MC_PROFILE_PRIV (profile);

    if (priv->keyfile)
	return TRUE;

    filename = _mc_profile_filename (priv->unique_name);

    priv->keyfile = keyfile = g_key_file_new ();
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
    priv->single_enable = g_key_file_get_boolean (keyfile, PROFILE_GROUP, "SingleEnable", NULL);
    priv->default_account_domain = g_key_file_get_string (keyfile, PROFILE_GROUP, "DefaultAccountDomain", NULL);
    priv->avatar_mime_type = g_key_file_get_string (keyfile, PROFILE_GROUP, "AvatarMimeType", NULL);
    priv->default_account_name = g_key_file_get_string (keyfile, PROFILE_GROUP, "DefaultAccountName", NULL);
    priv->priority = g_key_file_get_integer (keyfile, PROFILE_GROUP, "Priority", NULL);
    priv->localization_domain = g_key_file_get_string (keyfile, PROFILE_GROUP,
                                                       "LocalizationDomain",
                                                       NULL);
    if (priv->localization_domain)
    {
	gchar *display_name;

	display_name = g_strdup (dgettext (priv->localization_domain,
					   priv->display_name));
	g_free (priv->display_name);
	priv->display_name = display_name;
    }

    g_key_file_set_list_separator (keyfile, ',');
    presences_str = g_key_file_get_string_list (keyfile, PROFILE_GROUP,
						"SupportedPresences", &length,
						NULL);
    if (!presences_str) length = 0;
    priv->supported_presences =
	g_array_sized_new (TRUE, FALSE, sizeof (TpConnectionPresenceType),
			   length);
    for (i = 0; i < length; i++)
    {
	TpConnectionPresenceType presence;
	gchar *presence_str;

	presence_str = g_strstrip (presences_str[i]);
	presence = map_presence (presence_str);
	if (presence == TP_CONNECTION_PRESENCE_TYPE_UNSET)
	{
	    g_warning ("Unrecognized presence `%s'", presence_str);
	    continue;
	}
	g_array_append_val (priv->supported_presences, presence);
    }
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

    /* fill in the defaul settings hash */
    priv->default_settings = g_hash_table_new_full (g_str_hash, g_str_equal,
						    (GDestroyNotify) g_free,
						    (GDestroyNotify) g_free);
    keys = g_key_file_get_keys (keyfile, PROFILE_GROUP, 0, NULL);
    for (tmp = keys; tmp != NULL && *tmp != NULL; tmp++)
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

    /* fill in the vcard mangling hashtable */
    priv->vcard_mangle_hash = g_hash_table_new_full (
						     g_str_hash, g_str_equal, (GDestroyNotify) g_free, (GDestroyNotify) g_free);

    keys = g_key_file_get_keys (keyfile, PROFILE_GROUP, 0, NULL);
    for (tmp = keys; tmp != NULL && *tmp != NULL; tmp++)
    {
	gchar *key = *tmp;
	if (0 == g_ascii_strncasecmp ("Mangle-", key, 7))
	{
	    gchar *k, *v;
	    k = g_strdup (key + 7);
	    v = g_key_file_get_string (keyfile, PROFILE_GROUP, key, NULL);
	    g_hash_table_insert (MC_PROFILE_PRIV (profile)->vcard_mangle_hash, k, v);
	}
    }
    g_strfreev (keys);

    g_free (filename);

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
 * mc_profile_get_filename:
 * @unique_name: The unique name of the profile.
 *
 * Get the .profile file path of the profile @unique_name. This can be useful
 * for applications which wants to parse the .profile file themselves, for the
 * cases when the profile contains application specific data.
 * Note that this function is not meant to be used for creating new profiles:
 * if the .profile file does not exist, this functions fails.
 *
 * Return value: The path of the .profile file, or %NULL.
 */
gchar *
mc_profile_get_filename (const gchar *unique_name)
{
    return _mc_profile_filename (unique_name);
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

	    if (profile)
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
 * mc_profiles_list_by_protocol:
 * @protocol: string id of the protocol.
 * 
 * Lists all configured profiles for the given protocol.
 *
 * Returns: a #GList of the profiles (must be freed with #mc_profiles_free_list).
 */
GList *
mc_profiles_list_by_protocol (const gchar *protocol)
{
    GList *all, *tmp, *ret;

    g_return_val_if_fail (protocol != NULL, NULL);
    g_return_val_if_fail (*protocol != '\0', NULL);

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

	if (priv->protocol && 0 == strcmp (protocol, priv->protocol))
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
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    return priv->unique_name;
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
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    return priv->configuration_ui;
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
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    return priv->display_name;
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
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    return priv->icon_name;
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
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    return priv->branding_icon_name;
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
const TpConnectionPresenceType *
mc_profile_get_supported_presences (McProfile *id)
{
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    return (TpConnectionPresenceType *)(priv->supported_presences->data);
}

/*
 * mc_profile_supports_presence:
 * @id: The #McProfile.
 * @presence: The #TpConnectionPresenceType.
 *
 * Tests whether the profile supports the presence @presence.
 *
 * Returns: a #gboolean.
 */
gboolean
mc_profile_supports_presence (McProfile *id, TpConnectionPresenceType presence)
{
    const TpConnectionPresenceType *presences;

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
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    return priv->protocol;
}

/**
 * mc_profile_get_manager_name:
 * @id: The #McProfile.
 * 
 * Get the manager name of the profile.
 *
 * Returns: a string representing the manager name (must not be freed).
 */
const gchar *
mc_profile_get_manager_name (McProfile *id)
{
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    return priv->manager;
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
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    return priv->vcard_field;
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
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    g_return_val_if_fail (
			  priv->capabilities & MC_PROFILE_CAPABILITY_SPLIT_ACCOUNT, NULL);

    return priv->default_account_domain;
}

/**
 * mc_profile_get_avatar_mime_type:
 * @id: The #McProfile.
 * 
 * Get the preferred MIME type for the avatar.
 *
 * Returns: a string representing the MIME type (must not be freed).
 */
const gchar *
mc_profile_get_avatar_mime_type (McProfile *id)
{
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    return priv->avatar_mime_type;
}

/**
 * mc_profile_get_default_account_name:
 * @id: The #McProfile.
 * 
 * Get the default account display name.
 *
 * Returns: a string representing the default account display name (must not be
 * freed).
 */
const gchar *
mc_profile_get_default_account_name (McProfile *id)
{
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    return priv->default_account_name;
}

/**
 * mc_profile_get_priority:
 * @id: The #McProfile.
 * 
 * Get the priority of the profile, as an integer number.
 *
 * Returns: the profile priority (0 meaning normal).
 */
gint
mc_profile_get_priority (McProfile *id)
{
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, 0);
    return priv->priority;
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
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, FALSE);
    return priv->vcard_default;
}

/**
 * mc_profile_get_single_enable:
 * @id: The #McProfile.
 *
 * Returns: #TRUE if no more than one account should be enabled for this
 * service at the same time.
 */
gboolean
mc_profile_get_single_enable (McProfile *id)
{
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, FALSE);
    return priv->single_enable;
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
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, 0);
    return priv->capabilities;
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
    McProfilePrivate *priv;
    const gchar *def;

    get_private_and_load_or_return_val (id, NULL);
    g_return_val_if_fail (setting != NULL, NULL);
    g_return_val_if_fail (*setting != '\0', NULL);

    def = g_hash_table_lookup (priv->default_settings,
			       setting);

    return def;
}

/**
 * mc_profile_get_vcard_mangle:
 * @id: The #McProfile.
 * @vcard_field: The vcard field for which to get the mangle
 *
 * Get a mangle to transform a foreign address to a handle this profile understands
 *
 * Returns: a string representing the mangle from the profile (must not be freed).
 */
const gchar *
mc_profile_get_vcard_mangle (McProfile *id, const gchar *vcard_field)
{
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (id, NULL);
    g_return_val_if_fail (vcard_field != NULL, NULL);
    g_return_val_if_fail (*vcard_field != '\0', NULL);

    return (const gchar *) g_hash_table_lookup (priv->vcard_mangle_hash,
                                                vcard_field);
}

/**
 * mc_profile_get_keyfile:
 * @profile: the #McProfile.
 *
 * Gets the #GKeyFile which holds the profile data. This function should be
 * used only when there is not a specific function to access the desired
 * information, and it may be that in future version it will just return %NULL
 * if the McProfile implementation changes and is not based on #GKeyfile
 * anymore.
 *
 * Returns: the #GKeyfile associated with @profile, or %NULL.
 */
GKeyFile *
mc_profile_get_keyfile (McProfile *profile)
{
    McProfilePrivate *priv;

    get_private_and_load_or_return_val (profile, NULL);
    return priv->keyfile;
}

/**
 * mc_profile_presences_list:
 * @id: The #McProfile.
 *
 * List the presences supported by this profile.
 *
 * Returns: an array of strings representing the presence statuses supported by
 * this profile (do not free).
 */
const gchar * const *
mc_profile_presences_list (McProfile *id)
{
    McProfilePrivate *priv;
    gchar **groups;
    GPtrArray *presences;
    gsize len = 0;
    guint i;

    g_return_val_if_fail (MC_IS_PROFILE (id), NULL);
    priv = id->priv;
    if (!priv->presences)
    {
        if (G_UNLIKELY (!priv->keyfile)) _mc_profile_load (id);
        g_return_val_if_fail (priv->keyfile != NULL, NULL);

        presences = g_ptr_array_new ();
        groups = g_key_file_get_groups (priv->keyfile, &len);
        for (i = 0; i < len; i++)
        {
            gchar *presence;

            if (strncmp (groups[i], PRESENCE_PREFIX, PRESENCE_PREFIX_LEN) != 0)
                continue;
            presence = g_strdup (groups[i] + PRESENCE_PREFIX_LEN);
            g_ptr_array_add (presences, presence);
        }
        g_strfreev (groups);

        /* the list is NULL-terminated */
        g_ptr_array_add (presences, NULL);
        priv->presences = (gchar **)g_ptr_array_free (presences, FALSE);
    }
    return (const gchar * const *)priv->presences;
}

/**
 * mc_profile_presence_get_name:
 * @id: The #McProfile.
 * @presence: status name of the presence.
 *
 * Returns: the localized name of the presence status.
 */
gchar *
mc_profile_presence_get_name (McProfile *id, const gchar *presence)
{
    McProfilePrivate *priv;
    gchar group[128];

    get_private_and_load_or_return_val (id, NULL);

    g_snprintf (group, sizeof (group), PRESENCE_PREFIX "%s", presence);
    return get_localized_group_field (priv, group, "Name");
}

/**
 * mc_profile_presence_get_type:
 * @id: The #McProfile.
 * @presence: status name of the presence.
 *
 * Returns: the #TpConnectionPresenceType of @presence.
 */
TpConnectionPresenceType
mc_profile_presence_get_type (McProfile *id, const gchar *presence)
{
    McProfilePrivate *priv;
    gchar group[128];

    get_private_and_load_or_return_val (id, TP_CONNECTION_PRESENCE_TYPE_UNSET);

    g_snprintf (group, sizeof (group), PRESENCE_PREFIX "%s", presence);
    return (TpConnectionPresenceType)
        g_key_file_get_integer (priv->keyfile, group, "Type", NULL);
}

/**
 * mc_profile_presence_get_icon_name:
 * @id: The #McProfile.
 * @presence: status name of the presence.
 *
 * Returns: the branding icon name for @presence.
 */
gchar *
mc_profile_presence_get_icon_name (McProfile *id, const gchar *presence)
{
    McProfilePrivate *priv;
    gchar group[128];

    get_private_and_load_or_return_val (id, NULL);
    g_snprintf (group, sizeof (group), PRESENCE_PREFIX "%s", presence);
    return g_key_file_get_string (priv->keyfile, group, "IconName", NULL);
}

/**
 * mc_profile_actions_list:
 * @profile: The #McProfile.
 *
 * List the action IDs supported by this profile.
 *
 * Returns: a #GList of strings. To be free'd with
 * mc_profile_actions_list_free().
 */
GList *
mc_profile_actions_list (McProfile *profile)
{
    return mc_profile_actions_list_by_vcard_fields (profile, NULL);
}

/**
 * mc_profile_actions_list_by_vcard_field:
 * @profile: The #McProfile.
 * @vcard_field: a VCard field.
 *
 * List the action IDs supported by this profile for the given VCard field.
 *
 * Returns: a #GList of strings. To be free'd with
 * mc_profile_actions_list_free().
 */
GList *
mc_profile_actions_list_by_vcard_field (McProfile *profile,
                                        const gchar *vcard_field)
{
    const gchar *fields[2];

    fields[0] = vcard_field;
    fields[1] = NULL;
    return mc_profile_actions_list_by_vcard_fields (profile, fields);
}

/**
 * mc_profile_actions_list_by_vcard_fields:
 * @profile: The #McProfile.
 * @vcard_fields: an array of VCard fields.
 *
 * List the action IDs supported by this profile for the given VCard fields.
 *
 * Returns: a #GList of strings. To be free'd with
 * mc_profile_actions_list_free().
 */
GList *
mc_profile_actions_list_by_vcard_fields (McProfile *profile,
                                         const gchar **vcard_fields)
{
    McProfilePrivate *priv;
    gchar **groups;
    GList *actions = NULL;
    gsize len = 0;
    guint i;

    get_private_and_load_or_return_val (profile, NULL);

    groups = g_key_file_get_groups (priv->keyfile, &len);
    for (i = 0; i < len; i++)
    {
        const gchar *p_action;
        gchar *action;

        if (strncmp (groups[i], ACTION_PREFIX, ACTION_PREFIX_LEN) != 0)
            continue;
        p_action = groups[i] + ACTION_PREFIX_LEN;
        if (vcard_fields)
        {
            const gchar **field_r;
            gchar **action_fields, **field_a;
            gboolean found = FALSE;

            /* check if any of the action VCard fields match those requested by
             * the caller */
            action_fields = mc_profile_action_get_vcard_fields (profile,
                                                                p_action);
            for (field_r = vcard_fields; *field_r != NULL; field_r++)
                for (field_a = action_fields; *field_a != NULL; field_a++)
                    if (strcmp (*field_a, *field_r) == 0)
                    {
                        found = TRUE;
                        break;
                    }

            g_strfreev (action_fields);
            if (!found) continue;
        }
        action = g_strdup (p_action);
        actions = g_list_prepend (actions, action);
    }
    g_strfreev (groups);

    return g_list_reverse (actions);
}

/**
 * mc_profile_action_get_name:
 * @profile: The #McProfile.
 * @action: the action ID.
 *
 * Returns: the localized name of the action.
 */
gchar *
mc_profile_action_get_name (McProfile *profile, const gchar *action)
{
    McProfilePrivate *priv;
    gchar group[128];

    get_private_and_load_or_return_val (profile, NULL);
    g_snprintf (group, sizeof (group), ACTION_PREFIX "%s", action);
    return get_localized_group_field (priv, group, "Name");
}

/**
 * mc_profile_action_get_icon_name:
 * @profile: The #McProfile.
 * @action: the action ID.
 *
 * Returns: the name of the action icon.
 */
gchar *
mc_profile_action_get_icon_name (McProfile *profile, const gchar *action)
{
    McProfilePrivate *priv;
    gchar group[128];

    get_private_and_load_or_return_val (profile, NULL);
    g_snprintf (group, sizeof (group), ACTION_PREFIX "%s", action);
    return g_key_file_get_string (priv->keyfile, group, "IconName", NULL);
}

/**
 * mc_profile_action_get_vcard_fields:
 * @profile: The #McProfile.
 * @action: the action ID.
 *
 * Returns: the VCard fields of the action. Free with g_strfreev().
 */
gchar **
mc_profile_action_get_vcard_fields (McProfile *profile, const gchar *action)
{
    McProfilePrivate *priv;
    gchar group[128];

    get_private_and_load_or_return_val (profile, NULL);
    g_snprintf (group, sizeof (group), ACTION_PREFIX "%s", action);
    return g_key_file_get_string_list (priv->keyfile, group, "VCardFields",
                                       NULL, NULL);
}

/**
 * mc_profile_action_get_properties:
 * @profile: The #McProfile.
 * @action: the action ID.
 *
 * Get the #GHashTable of qualified channel properties, to be used when
 * requesting a Telepathy channel for this action. Properties can be added to
 * the hash table: keys will be free'd with g_free() and values with
 * g_value_unset() + g_slice_free().
 *
 * Returns: a #GHashTable of properties.
 */
GHashTable *
mc_profile_action_get_properties (McProfile *profile, const gchar *action)
{
    McProfilePrivate *priv;
    gchar group[128];
    GHashTable *properties;
    gchar **keys;
    gsize len;
    guint i;

    get_private_and_load_or_return_val (profile, NULL);
    g_snprintf (group, sizeof (group), ACTION_PREFIX "%s", action);

    properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                        (GDestroyNotify)tp_g_value_slice_free);
    keys = g_key_file_get_keys (priv->keyfile, group, &len, NULL);

    if (keys == NULL)
        len = 0;

    for (i = 0; i < len; i++)
    {
        gchar *p_name, *p_type, *name;
        GValue *value;
        GType type;

        if (strncmp (keys[i], ACTION_PROP_PREFIX, ACTION_PROP_PREFIX_LEN) != 0)
            continue;
        p_name = keys[i] + ACTION_PROP_PREFIX_LEN;
        p_type = strchr (p_name, '-');
        if (p_type) p_type++;
        type = _mc_gtype_from_dbus_signature (p_type);
        if (G_UNLIKELY (type == G_TYPE_INVALID))
        {
            g_warning ("%s: invalid type %s for action %s in profile %s",
                       G_STRFUNC, p_type, action, priv->unique_name);
            continue;
        }

        value = tp_g_value_slice_new (type);

        if (set_value_from_key (priv->keyfile, group, keys[i], value))
        {
            name = g_strndup (p_name, p_type - p_name - 1);
            g_hash_table_insert (properties, name, value);
        }
        else
            tp_g_value_slice_free (value);
    }
    g_strfreev (keys);

    return properties;
}

/**
 * mc_profile_actions_list_free:
 * @profile: The #McProfile.
 *
 * Free a list of profile actions
 */
void
mc_profile_actions_list_free (GList *actions)
{
    GList *list;

    for (list = actions; list != NULL; list = list->next)
        g_free (list->data);
    g_list_free (actions);
}

