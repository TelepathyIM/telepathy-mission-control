/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
 *
 * Contact: Alberto Mardegan  <alberto.mardegan@nokia.com>
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
#include <gconf/gconf-client.h>
#include <stdio.h>
#include <string.h>
#include <config.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <telepathy-glib/dbus.h>
#include <libmcclient/mc-account-manager.h>
#include <libmcclient/mc-account.h>

#define MC_ACCOUNTS_GCONF_BASE "/apps/telepathy/mc/accounts"
#define MC_ACCOUNTS_GCONF_KEY_DELETED "deleted"
#define MC_ACCOUNTS_GCONF_KEY_PROFILE "profile"

#define MC_ACCOUNTS_KEY_MANAGER "manager"
#define MC_ACCOUNTS_KEY_PROTOCOL "protocol"
#define MC_ACCOUNTS_KEY_PRESETS "presets"

#define PROFILE_GROUP "Profile"

#define PROFILE_SUFFIX ".profile"
#define MANAGER_SUFFIX ".manager"

static GMainLoop *main_loop;
GConfClient *client = NULL;
static gint num_processing_accounts = 0;

typedef struct
{
    gchar *manager;
    gchar *protocol;
    gchar *profile;
    GHashTable *parameters;
    GKeyFile *manager_cfg;
    gchar *protocol_grp;

    gchar *alias;
    GArray avatar;
    gchar *avatar_mime;
    gchar *display_name;
    gchar *normalized_name;
    gboolean enabled;

    gint active_calls;
} AccountInfo;
    
typedef union
{
    gchar *v_string;
    gint v_int;
    gboolean v_bool;
} ParamValue;

static void
account_info_free (AccountInfo *ai)
{
    g_free (ai->manager);
    g_free (ai->protocol);
    g_free (ai->profile);
    g_free (ai->protocol_grp);
    g_key_file_free (ai->manager_cfg);
    g_hash_table_destroy (ai->parameters);
    g_free (ai->alias);
    g_free (ai->avatar.data);
    g_free (ai->avatar_mime);
    g_free (ai->display_name);
    g_free (ai->normalized_name);

    g_free (ai);
}

static inline void
account_creation_ended (AccountInfo *ai)
{
    account_info_free (ai);
    num_processing_accounts--;
    if (num_processing_accounts == 0)
	g_main_loop_quit (main_loop);
}

static gchar *
gc_dup_string (const GConfValue *value)
{
    return g_strdup (gconf_value_get_string (value));
}

static const gchar**
_mc_manager_get_dirs (void)
{
    GSList *dir_list = NULL, *slist;
    const gchar *dirname;
    static gchar **manager_dirs = NULL;
    guint n;

    if (manager_dirs) return (const gchar **)manager_dirs;

    dirname = g_getenv ("MC_MANAGER_DIR");
    if (dirname && g_file_test (dirname, G_FILE_TEST_IS_DIR))
	dir_list = g_slist_prepend (dir_list, (gchar *)dirname);

    if (MANAGERS_DIR[0] == '/')
    {
	if (g_file_test (MANAGERS_DIR, G_FILE_TEST_IS_DIR))
	    dir_list = g_slist_prepend (dir_list, MANAGERS_DIR);
    }
    else
    {
	const gchar * const *dirs;
	gchar *dir;

	dir = g_build_filename (g_get_user_data_dir(), MANAGERS_DIR, NULL);
	if (g_file_test (dir, G_FILE_TEST_IS_DIR))
	    dir_list = g_slist_prepend (dir_list, dir);
	else g_free (dir);

	dirs = g_get_system_data_dirs();
	for (dirname = *dirs; dirname; dirs++, dirname = *dirs)
	{
	    dir = g_build_filename (dirname, MANAGERS_DIR, NULL);
	    if (g_file_test (dir, G_FILE_TEST_IS_DIR))
		dir_list = g_slist_prepend (dir_list, dir);
	    else g_free (dir);
	}
    }

    /* build the string array */
    n = g_slist_length (dir_list);
    manager_dirs = g_new (gchar *, n + 1);
    manager_dirs[n--] = NULL;
    for (slist = dir_list; slist; slist = slist->next)
	manager_dirs[n--] = slist->data;
    g_slist_free (dir_list);
    return (const gchar **)manager_dirs;
}

static gchar *
_mc_manager_filename (const gchar *unique_name)
{
    const gchar **manager_dirs;
    const gchar *dirname;
    gchar *filename, *filepath = NULL;

    manager_dirs = _mc_manager_get_dirs ();
    if (!manager_dirs) return NULL;

    filename = g_strconcat (unique_name, MANAGER_SUFFIX, NULL);
    for (dirname = *manager_dirs; dirname; manager_dirs++, dirname = *manager_dirs)
    {
	filepath = g_build_filename (dirname, filename, NULL);
	if (g_file_test (filepath, G_FILE_TEST_EXISTS)) break;
	g_free (filepath);
	filepath = NULL;
    }
    g_free (filename);
    return filepath;
}

static const gchar**
_mc_profile_get_dirs (void)
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
get_profile_path (const gchar *name)
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

static const gchar *
account_key (const gchar *account, const gchar *key)
{
    static gchar buffer[2048];

    g_snprintf (buffer, sizeof (buffer), "%s/%s/%s", MC_ACCOUNTS_GCONF_BASE, account, key);
    return buffer;
}

static void
add_parameter (AccountInfo *ai, const gchar *name, ParamValue pv, gchar signature)
{
    GValue *value;

    value = g_new0(GValue, 1);

    switch (signature)
    {
    case DBUS_TYPE_STRING:
	g_value_init (value, G_TYPE_STRING);
	g_value_take_string (value, pv.v_string);
	break;
    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
	g_value_init (value, G_TYPE_INT);
	g_value_set_int (value, pv.v_int);
	break;
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
	g_value_init (value, G_TYPE_UINT);
	g_value_set_uint (value, (guint)pv.v_int);
	break;
    case DBUS_TYPE_BOOLEAN:
	g_value_init (value, G_TYPE_BOOLEAN);
	g_value_set_boolean (value, pv.v_bool);
	break;
    }

    g_hash_table_replace (ai->parameters, g_strdup (name), value);
}

static gboolean
read_gconf_data (AccountInfo *ai, const gchar *unique_name)
{
    GSList *entries, *list;
    gchar dir[200], signature, *avatar_filename = NULL;
    ParamValue pv;
    gint len;

    len = g_snprintf (dir, sizeof (dir), "%s/%s", MC_ACCOUNTS_GCONF_BASE, unique_name);
    len++;
    entries = gconf_client_all_entries (client, dir, NULL);
    for (list = entries; list != NULL; list = list->next)
    {
	GConfEntry *entry = list->data;
	const gchar *key = entry->key + len;
	GConfValue *value = entry->value;

	if (strncmp (key, "param-", 6) == 0)
	{
	    switch (value->type)
	    {
	    case GCONF_VALUE_STRING:
		pv.v_string = gc_dup_string (value);
		signature = DBUS_TYPE_STRING;
		break;
	    case GCONF_VALUE_INT:
		pv.v_int = gconf_value_get_int (value);
		signature = DBUS_TYPE_INT32;
		break;
	    case GCONF_VALUE_BOOL:
		pv.v_bool = gconf_value_get_bool (value);
		signature = DBUS_TYPE_BOOLEAN;
		break;
	    default:
		g_warning ("Parameter %s has unrecognized type: %u", key, value->type);
		return FALSE;
	    }
	    add_parameter (ai, key + 6, pv, signature);
	}
	else if (strcmp (key, "alias") == 0)
	{
	    ai->alias = gc_dup_string (value);
	}
	else if (strcmp (key, "avatar_mime") == 0)
	{
	    ai->avatar_mime = gc_dup_string (value);
	}
	else if (strcmp (key, "display_name") == 0)
	{
	    ai->display_name = gc_dup_string (value);
	}
	else if (strcmp (key, "normalized_name") == 0)
	{
	    ai->normalized_name = gc_dup_string (value);
	}
	else if (strcmp (key, "enabled") == 0)
	{
	    ai->enabled = gconf_value_get_bool (value);
	}
	else if (strcmp (key, "data_dir") == 0)
	{
	    const gchar *data_dir = gconf_value_get_string (value);
	    avatar_filename = g_build_filename (data_dir, "avatar.bin", NULL);
	}
    }
    g_slist_foreach (entries, (GFunc)gconf_entry_free, NULL);
    g_slist_free (entries);

    /* read the avatar */
    if (avatar_filename && g_file_test (avatar_filename, G_FILE_TEST_EXISTS))
    {
	GError *error = NULL;
	gchar *data;
	gsize avatar_len;
	if (g_file_get_contents (avatar_filename, &data, &avatar_len, &error))
	{
	    ai->avatar.data = (gchar *)data;
	    ai->avatar.len = avatar_len;
	}
	else
	{
	    g_warning ("%s: reading file %s failed (%s)", G_STRLOC,
		       avatar_filename, error->message);
	    g_error_free (error);
	}
    }
    g_free (avatar_filename);
    return TRUE;
}

static gboolean
parse_profile_param (AccountInfo *ai, GKeyFile *profile, const gchar *key)
{
    gchar *param_info, signature, param_str[200];
    GError *error = NULL;
    ParamValue pv;

    /* read the parameter signature from the manager file */
    /* key + 8, to skip the "Default-" */
    g_snprintf (param_str, sizeof (param_str), "param-%s", key + 8);
    param_info = g_key_file_get_string (ai->manager_cfg, ai->protocol_grp, param_str, NULL);
    if (!param_info) return FALSE;
    signature = param_info[0];
    g_free (param_info);

    switch (signature)
    {
    case DBUS_TYPE_STRING:
	pv.v_string = g_key_file_get_string (profile, PROFILE_GROUP,
					     key, &error);
	break;
    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
	pv.v_int = g_key_file_get_integer (profile, PROFILE_GROUP,
					   key, &error);
	break;
    case DBUS_TYPE_BOOLEAN:
	pv.v_bool = g_key_file_get_boolean (profile, PROFILE_GROUP,
					    key, &error);
	break;
    default:
	g_warning ("%s: skipping parameter %s, unknown type %c", G_STRFUNC, key + 8, signature);
	return FALSE;
    }
    if (error)
    {
	g_error_free (error);
	return FALSE;
    }

    add_parameter (ai, key + 8, pv, signature);
    return FALSE;
}

static gboolean
read_manager (AccountInfo *ai)
{
    GError *error = NULL;
    gchar *filename;
    gboolean ret = FALSE;

    if (!ai->manager) return FALSE;
    filename = _mc_manager_filename (ai->manager);

    ai->manager_cfg = g_key_file_new ();
    if (!ai->manager_cfg)
	goto free_filename;

    if (!g_key_file_load_from_file (ai->manager_cfg, filename, G_KEY_FILE_NONE, &error))
    {
	g_warning ("%s: loading %s failed: %s", G_STRFUNC,
		   filename, error->message);
	g_error_free (error);
	g_key_file_free (ai->manager_cfg);
	goto free_filename;
    }

    ai->protocol_grp = g_strdup_printf ("Protocol %s", ai->protocol);
    ret = TRUE;
free_filename:
    g_free (filename);
    return ret;
}

static void
_g_value_free (gpointer data)
{
    GValue *value = (GValue *) data;
    g_value_unset (value);
    g_free (value);
}

static void
set_prop_cb (TpProxy *proxy, const GError *error,
	     gpointer user_data, GObject *weak_object)
{
    McAccount *account = MC_ACCOUNT (proxy);
    AccountInfo *ai = user_data;

    ai->active_calls--;
    g_debug ("%s (%s): active calls left: %d",
	     G_STRFUNC, account->name, ai->active_calls);
    if (error)
    {
	g_warning ("%s failed: %s", G_STRFUNC, error->message);
	g_object_unref (proxy);
	account_creation_ended (ai);
    }

    if (ai->active_calls == 0)
    {
	g_debug ("No more active calls");
	account_creation_ended (ai);
    }
}

static gboolean
set_account_prop (McAccount *account, const gchar *iface, const gchar *name,
		  GValue *value, AccountInfo *ai)
{
    TpProxyPendingCall *call;

    call = tp_cli_dbus_properties_call_set (account, -1,
					    iface, name, value,
					    set_prop_cb, ai, NULL,
					    NULL);
    g_value_unset (value);
    if (!call)
    {
	g_object_unref (account);
	account_creation_ended (ai);
	return FALSE;
    }
    ai->active_calls++;
    return TRUE;
}

static void
create_account_cb (TpProxy *proxy, const gchar *obj_path, const GError *error,
		   gpointer user_data, GObject *weak_object)
{
    AccountInfo *ai = user_data;
    McAccount *account;
    GValue value = { 0 };
    gboolean ok;
    GType type;

    g_debug ("%s called", G_STRFUNC);
    if (error)
    {
	g_warning ("got error %s", error->message);
	account_creation_ended (ai);
	return;
    }

    account = mc_account_new (proxy->dbus_daemon, obj_path);
    g_debug ("Created account %s", account->name);

    /* Set all account properties */
    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, ai->profile);
    ok = set_account_prop (account,
			   MC_IFACE_ACCOUNT_INTERFACE_COMPAT, "Profile",
			   &value, ai);
    if (!ok) return;

    g_value_init (&value, G_TYPE_STRING);
    g_value_set_static_string (&value, ai->alias);
    ok = set_account_prop (account,
			   MC_IFACE_ACCOUNT, "Nickname",
			   &value, ai);
    if (!ok) return;

    g_value_init (&value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&value, ai->enabled);
    ok = set_account_prop (account,
			   MC_IFACE_ACCOUNT, "Enabled",
			   &value, ai);
    if (!ok) return;

    if (ai->avatar.data && ai->avatar_mime)
    {
	GValueArray *va;

	type = dbus_g_type_get_struct ("GValueArray",
				       dbus_g_type_get_collection ("GArray",
								   G_TYPE_UCHAR),
				       G_TYPE_STRING,
				       G_TYPE_INVALID);
	g_value_init (&value, type);
	g_value_take_boxed (&value, dbus_g_type_specialized_construct (type));
	va = (GValueArray *) g_value_get_boxed (&value);
	g_value_set_static_boxed (va->values, &ai->avatar);
	g_value_set_static_string (va->values + 1, ai->avatar_mime);
	ok = set_account_prop (account,
			       MC_IFACE_ACCOUNT_INTERFACE_AVATAR, "Avatar",
			       &value, ai);
	if (!ok) return;
    }
}

static gboolean
write_account (McAccountManager *am, AccountInfo *ai)
{
    TpProxyPendingCall *call;
    GHashTable *empty;

    empty = g_hash_table_new (g_str_hash, g_str_equal);
    call = mc_cli_account_manager_call_create_account (am, -1,
						       ai->manager,
						       ai->protocol,
						       ai->display_name,
						       ai->parameters,
                                                       empty,
						       create_account_cb,
						       ai,
						       NULL,
						       NULL);
    g_hash_table_unref (empty);
    if (!call) return FALSE;

    return TRUE;
}

static gboolean
convert_account (const gchar *unique_name, McAccountManager *am)
{
    AccountInfo *ai;
    const gchar *key;
    GKeyFile *profile;
    GError *error = NULL;
    gchar *profile_name, *profile_path;
    gchar **keys, **i_key;
    gboolean ret;

    key = account_key (unique_name, MC_ACCOUNTS_GCONF_KEY_DELETED);
    if (gconf_client_get_bool (client, key, NULL)) return TRUE;

    g_debug ("Converting account %s", unique_name);

    key = account_key (unique_name, MC_ACCOUNTS_GCONF_KEY_PROFILE);
    profile_name = gconf_client_get_string (client, key, NULL);
    if (!profile_name) return FALSE;

    profile_path = get_profile_path (profile_name);
    if (!profile_path)
    {
	g_warning ("Profile `%s' not found", profile_name);
	g_free (profile_name);
	return FALSE;
    }

    profile = g_key_file_new ();
    g_key_file_load_from_file (profile, profile_path, 0, &error);
    g_free (profile_path);
    if (error)
    {
	g_warning ("Couldn't load profile `%s': %s", profile_name, error->message);
	g_error_free (error);
	g_key_file_free (profile);
	g_free (profile_name);
	return FALSE;
    }

    ai = g_new0 (AccountInfo, 1);
    ai->profile = profile_name;
    ai->parameters = g_hash_table_new_full (g_str_hash, g_str_equal,
					    g_free, _g_value_free);
    ai->manager = g_key_file_get_string (profile, PROFILE_GROUP, "Manager", NULL);
    ai->protocol = g_key_file_get_string (profile, PROFILE_GROUP, "Protocol", NULL);
    read_manager (ai);

    keys = g_key_file_get_keys (profile, PROFILE_GROUP, NULL, NULL);
    for (i_key = keys; *i_key != NULL; i_key++)
    {
	if (strncmp (*i_key, "Default-", 8) == 0)
	{
	    parse_profile_param (ai, profile, *i_key);
	    g_debug ("Copying param %s from profile", *i_key + 8);
	}
    }
    g_strfreev (keys);
    g_key_file_free (profile);

    if (!read_gconf_data (ai, unique_name))
    {
	account_info_free (ai);
	return FALSE;
    }

    num_processing_accounts++;
    ret = write_account (am, ai);
    if (!ret)
	account_info_free (ai);

    return ret;
}

static gchar *
_account_name_from_key (const gchar *key)
{
    guint base_len = strlen (MC_ACCOUNTS_GCONF_BASE);
    const gchar *base, *slash;

    g_assert (key == strstr (key, MC_ACCOUNTS_GCONF_BASE));
    g_assert (strlen (key) > base_len + 1);

    base = key + base_len + 1;
    slash = strchr (base, '/');

    if (slash == NULL)
	return g_strdup (base);
    else
	return g_strndup (base, slash - base);
}

static gboolean
convert_accounts (McAccountManager *am)
{
    GError *error = NULL;
    GSList *dirs, *list;

    dirs = gconf_client_all_dirs (client, MC_ACCOUNTS_GCONF_BASE, &error);
    if (error)
    {
	g_warning ("gconf_client_all_dirs failed: %s", error->message);
	g_error_free (error);
	return FALSE;
    }

    for (list = dirs; list; list = list->next)
    {
	gchar *unique_name = _account_name_from_key (list->data);
	if (!convert_account (unique_name, am))
	{
	    num_processing_accounts--;
	    g_debug ("...FAILED!");
	}
	g_free (unique_name);
    }
    g_slist_foreach (dirs, (GFunc)g_free, NULL);
    g_slist_free (dirs);

    g_debug ("processing accounts: %d", num_processing_accounts);
    if (num_processing_accounts == 0)
	g_main_loop_quit (main_loop);
    return FALSE;
}

int
main (int argc, char **argv)
{
    McAccountManager *am;
    DBusGConnection *dbus_conn;
    TpDBusDaemon *daemon;
    gint ret = 0;

    g_type_init ();

    dbus_conn = tp_get_bus ();
    daemon = tp_dbus_daemon_new (dbus_conn);
    dbus_g_connection_unref (dbus_conn);

    am = mc_account_manager_new (daemon);
    g_object_unref (daemon);

    client = gconf_client_get_default ();

    g_idle_add ((GSourceFunc)convert_accounts, am);

    main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (main_loop);

    g_object_unref (am);
    g_object_unref (client);

    return ret;
}


