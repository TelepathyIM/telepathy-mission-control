/*
 * This file is part of telepathy-mission-control
 *
 * Copyright (C) 2008 Nokia Corporation.
 * Copyright (C) 2010 Collabora Ltd.
 *
 * Contact: Pekka Pessi  <first.last@nokia.com>
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

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

static gchar *app_name;
static GMainLoop *main_loop;

static void
show_help (gchar * err)
{
    if (err)
	printf ("Error: %s\n", err);

    printf ("Usage:\n"
	    "    %1$s list\n"
	    "    %1$s summary\n"
	    "    %1$s dump\n"
	    "    %1$s add <manager>/<protocol> <display name> [<param> ...]\n"
	    "    %1$s update <account name> [<param>|clear:key] ...\n"
	    "    %1$s display <account name> <display name>\n"
	    "    %1$s nick <account name> <nick name>\n"
	    "    %1$s service <account name> <service name>\n"
	    "    %1$s icon <account name> <icon name>\n"
	    "    %1$s show <account name>\n"
	    "    %1$s get <account name> [key...]\n"
	    "    %1$s enable <account name>\n"
	    "    %1$s disable <account name>\n"
	    "    %1$s auto-presence <account name> <presence status> [<message>]\n"
	    "    %1$s request <account name> <presence status> [<message>]\n"
	    "    %1$s auto-connect <account name> [(on|off)]\n"
	    "    %1$s reconnect <account name>\n"
	    "    %1$s remove <account name>\n"
	    "  where <param> matches (int|uint|bool|string|path):<key>=<value>\n",
	    app_name);

    if (err)
	exit (-1);
    else
	exit (0);
}

static
union command {
    struct common {
	void *callback;
	gchar const *name;
	gchar const *account;
	int ret;
    } common;

    union {
	gboolean (*manager) (TpAccountManager *manager);
	gboolean (*account) (TpAccount *account);
    } ready;

    struct {
	struct common common;
	gchar const *manager, *protocol, *display;
	GHashTable *parameters;
    } add;

    struct {
	struct common common;
	GHashTable *set;
	GPtrArray *unset;
    } update;

    struct {
	struct common common;
	GPtrArray *args;
    } get;

    struct {
	struct common common;
	gchar const *name;
    } display, nick, icon, service;

    struct {
	struct common common;
	TpConnectionPresenceType type;
	gchar const *status, *message;
    } presence;

    struct {
	struct common common;
	gboolean value;
    } boolean;
} command;

struct presence {
    TpConnectionPresenceType type;
    gchar *status;
    gchar *message;
};

static const char *strip_prefix (char const *string, char const *prefix)
{
    while (*prefix && *string)
	if (*prefix++ != *string++)
	    return 0;
    return *prefix == '\0' ? (char *)string : NULL;
}

static char *ensure_prefix (char const *string)
{
    if (g_str_has_prefix (string, TP_ACCOUNT_OBJECT_PATH_BASE))
	return g_strdup (string);
    return g_strdup_printf ("%s%s", TP_ACCOUNT_OBJECT_PATH_BASE, string);
}

static void
_g_value_free (gpointer data)
{
    GValue *value = (GValue *) data;
    g_value_unset (value);
    g_free (value);
}

static GHashTable *
new_params (void)
{
    return g_hash_table_new_full (g_str_hash, g_str_equal,
				  g_free, _g_value_free);
}

static gboolean
set_param (GHashTable *parameters,
	   GPtrArray *clear,
	   gchar *param_value)
{
    gchar **strv_param_value = NULL;
    gchar **strv_type_key = NULL;
    const gchar *param, *type, *key, *value;
    gboolean ret = 0;
    GValue *gvalue;

    if (!param_value)
	return FALSE;

    strv_type_key = g_strsplit (param_value, ":", 2);

    if (strv_type_key == NULL ||
        strv_type_key[0] == NULL ||
        strv_type_key[1] == NULL ||
        strv_type_key[2] != NULL)
        goto CLEANUP;

    type = strv_type_key[0];
    param = strv_type_key[1];

    if (clear)
    {
        if (strcmp (type, "clear") == 0 || strcmp (type, "unset") == 0)
        {
            g_ptr_array_add (clear, g_strdup (param));
            ret = TRUE;
            goto CLEANUP;
        }
    }

    strv_param_value = g_strsplit (param, "=", 2);

    if (strv_param_value == NULL ||
        strv_param_value[0] == NULL ||
        strv_param_value[1] == NULL ||
        strv_param_value[2] != NULL)
        goto CLEANUP;

    key = strv_param_value[0];
    value = strv_param_value[1];

    gvalue = g_new0 (GValue, 1);

    /* Set the key */
    if (strcmp (type, "int") == 0)
    {
	g_value_init (gvalue, G_TYPE_INT);
	g_value_set_int (gvalue, strtol (value, NULL, 10));
	ret = TRUE;
    }
    else if (strcmp (type, "uint") == 0)
    {
	g_value_init (gvalue, G_TYPE_UINT);
	g_value_set_uint (gvalue, strtoul (value, NULL, 10));
	ret = TRUE;
    }
    else if (strcmp (type, "bool") == 0 || strcmp (type, "boolean") == 0)
    {
	g_value_init (gvalue, G_TYPE_BOOLEAN);
	if (g_ascii_strcasecmp (value, "1") == 0 ||
	    g_ascii_strcasecmp (value, "true") == 0 ||
	    /* "yes please!" / "yes sir, captain tightpants" */
	    g_ascii_strncasecmp (value, "yes", 3) == 0 ||
	    g_ascii_strcasecmp (value, "mos def") == 0 ||
	    g_ascii_strcasecmp (value, "oui") == 0 ||
	    strcmp (value, "ou là là!") == 0)
	{
	    g_value_set_boolean (gvalue, TRUE);
	    ret = TRUE;
	}
	else if (g_ascii_strcasecmp (value, "0") == 0 ||
	         g_ascii_strcasecmp (value, "false") == 0 ||
	         g_ascii_strcasecmp (value, "no") == 0 ||
	         g_ascii_strcasecmp (value, "nope") == 0 ||
	         g_ascii_strcasecmp (value, "non") == 0)
	{
	    g_value_set_boolean (gvalue, FALSE);
	    ret = TRUE;
	}
    }
    else if (strcmp (type, "string") == 0)
    {
	g_value_init (gvalue, G_TYPE_STRING);
	g_value_set_string (gvalue, value);
	ret = TRUE;
    }
    else if (strcmp (type, "path") == 0)
    {
	g_value_init (gvalue, DBUS_TYPE_G_OBJECT_PATH);
	g_value_set_boxed (gvalue, value);
	ret = TRUE;
    }

    if (ret)
	g_hash_table_replace (parameters, g_strdup (key), gvalue);
    else
	g_free (gvalue);

CLEANUP:
	g_strfreev (strv_param_value);
	g_strfreev (strv_type_key);
    return ret;
}

static void
show_param (gchar const *key, GValue *value)
{
    gchar const *type;
    gchar *decoded = NULL;
    int width;

    if (G_VALUE_HOLDS_STRING (value)) {
	type = "string";
	decoded = g_value_dup_string (value);
    }
    else if (G_VALUE_HOLDS_UINT (value)) {
	type = "uint";
	decoded = g_strdup_printf ("%u", g_value_get_uint (value));
    }
    else if (G_VALUE_HOLDS_INT (value)) {
	type = "int";
	decoded = g_strdup_printf ("%i", g_value_get_int (value));
    }
    else if (G_VALUE_HOLDS_BOOLEAN (value)) {
	type = "bool";
	decoded = g_strdup (g_value_get_boolean (value) ? "true" : "false");
    }
    else if (G_VALUE_HOLDS (value, DBUS_TYPE_G_OBJECT_PATH)) {
	type = "path";
	decoded = g_value_dup_boxed (value);
    }
    else {
	type = G_VALUE_TYPE_NAME (value);
	decoded = g_strdup_value_contents (value);
    }

    width = 11 - strlen (type); if (width < 0) width = 0;

    printf ("%*s (%s) %s = %s\n", width, "", type, key, decoded);

    g_free (decoded);
}

static int
show (gchar const *what, gchar const *value)
{
    if (value == NULL || value[0] == '\0')
	return 0;
    return printf ("%12s: %s\n", what, value);
}

static int
show_presence (gchar const *what, struct presence *presence)
{
  return printf ("%12s: %s (%d) \"%s\"\n", what, presence->status,
    presence->type, presence->message);
}

static int
show_uri_schemes (const gchar * const *schemes)
{
  int result;
  gchar *tmp;

  tmp = g_strjoinv (", ", (gchar **) schemes);

  result = printf ("%12s: %s\n", "URIScheme", tmp);

  g_free (tmp);
  return result;
}

static void
free_presence (struct presence *presence)
{
  g_free (presence->status);
  g_free (presence->message);
}

static TpConnectionPresenceType
get_presence_type_for_status(char const *status)
{
    if (g_ascii_strcasecmp(status, "unset") == 0)
	return TP_CONNECTION_PRESENCE_TYPE_UNSET;
    if (g_ascii_strcasecmp(status, "unknown") == 0)
	return TP_CONNECTION_PRESENCE_TYPE_UNKNOWN;
    if (g_ascii_strcasecmp(status, "offline") == 0)
	return TP_CONNECTION_PRESENCE_TYPE_OFFLINE;
    if (g_ascii_strcasecmp(status, "available") == 0 ||
	g_ascii_strcasecmp(status, "online") == 0)
	return TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;
    if (g_ascii_strcasecmp(status, "away") == 0 ||
	g_ascii_strcasecmp(status, "brb") == 0)
	return TP_CONNECTION_PRESENCE_TYPE_AWAY;
    if (g_ascii_strcasecmp(status, "xa") == 0 ||
	g_ascii_strcasecmp(status, "extended-away") == 0 ||
	g_ascii_strcasecmp(status, "extendedaway") == 0)
	return TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY;
    if (g_ascii_strcasecmp(status, "hidden") == 0)
	return TP_CONNECTION_PRESENCE_TYPE_HIDDEN;
    if (g_ascii_strcasecmp(status, "busy") == 0 ||
	g_ascii_strcasecmp(status, "dnd") == 0 ||
	g_ascii_strcasecmp(status, "do_not_disturb") == 0 ||
	g_ascii_strcasecmp(status, "donotdisturb") == 0)
	return TP_CONNECTION_PRESENCE_TYPE_BUSY;
    if (g_ascii_strcasecmp(status, "error") == 0)
	return TP_CONNECTION_PRESENCE_TYPE_ERROR;

    return TP_CONNECTION_PRESENCE_TYPE_UNKNOWN;
}

static gchar const *
connection_status_as_string(TpConnectionStatus status)
{
    switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTED: return "CONNECTED";
    case TP_CONNECTION_STATUS_CONNECTING: return "CONNECTING";
    case TP_CONNECTION_STATUS_DISCONNECTED: return "DISCONNECTED";
    default: return "<unknown>";
    }
}

static gchar const *
connection_status_reason_as_string(TpConnectionStatusReason reason)
{
    switch (reason)
    {
    case TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED:
	return "NONE";
    case TP_CONNECTION_STATUS_REASON_REQUESTED:
	return "REQUESTED";
    case TP_CONNECTION_STATUS_REASON_NETWORK_ERROR:
	return "NETWORK_ERROR";
    case TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED:
	return "AUTHENTICATION_FAILED";
    case TP_CONNECTION_STATUS_REASON_ENCRYPTION_ERROR:
	return "ENCRYPTION_ERROR";
    case TP_CONNECTION_STATUS_REASON_NAME_IN_USE:
	return "NAME_IN_USE";
    case TP_CONNECTION_STATUS_REASON_CERT_NOT_PROVIDED:
	return "CERT_NOT_PROVIDED";
    case TP_CONNECTION_STATUS_REASON_CERT_UNTRUSTED:
	return "CERT_UNTRUSTED";
    case TP_CONNECTION_STATUS_REASON_CERT_EXPIRED:
	return "CERT_EXPIRED";
    case TP_CONNECTION_STATUS_REASON_CERT_NOT_ACTIVATED:
	return "CERT_NOT_ACTIVATED";
    case TP_CONNECTION_STATUS_REASON_CERT_HOSTNAME_MISMATCH:
	return "CERT_HOSTNAME_MISMATCH";
    case TP_CONNECTION_STATUS_REASON_CERT_FINGERPRINT_MISMATCH:
	return "CERT_FINGERPRINT_MISMATCH";
    case TP_CONNECTION_STATUS_REASON_CERT_SELF_SIGNED:
	return "CERT_SELF_SIGNED";
    case TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR:
	return "CERT_OTHER_ERROR";
    default:
	return "<unknown>";
    }
}

typedef enum {
    GET_PARAM,
    GET_STRING,
    GET_BOOLEAN,
    GET_PRESENCE,
    GET_PRESENCE_TYPE,
    GET_PRESENCE_STATUS,
    GET_PRESENCE_MESSAGE
} GetterType;

typedef struct _Getter {
    gchar const *name;
    GetterType type;
    gpointer function;
} Getter;

static GData *getter_list;

static void
getter_list_add(gchar const *name,
		GetterType type,
		gpointer function)
{
    GQuark id;
    Getter *getter;

    id = g_quark_from_static_string(name);

    getter = g_new0(Getter, 1);
    getter->name = name;
    getter->type = type;
    getter->function = function;

    g_datalist_id_set_data(&getter_list, id, getter);
}

static void
getter_list_init(void)
{
    static int init;

    if (init) return;

    init = 1;

    getter_list_add("DisplayName", GET_STRING, tp_account_get_display_name);
    getter_list_add("Icon", GET_STRING, tp_account_get_icon_name);
    getter_list_add("Valid", GET_BOOLEAN, tp_account_is_valid);
    getter_list_add("Enabled", GET_BOOLEAN, tp_account_is_enabled);
    getter_list_add("Nickname", GET_STRING, tp_account_get_nickname);
    getter_list_add("ConnectAutomatically", GET_BOOLEAN,
		    tp_account_get_connect_automatically);
    getter_list_add("NormalizedName", GET_STRING, tp_account_get_normalized_name);

    getter_list_add("AutomaticPresence",
                    GET_PRESENCE, tp_account_get_automatic_presence);
    getter_list_add("AutomaticPresenceType",
		    GET_PRESENCE_TYPE, tp_account_get_automatic_presence);
    getter_list_add("AutomaticPresenceStatus",
		    GET_PRESENCE_STATUS, tp_account_get_automatic_presence);
    getter_list_add("AutomaticPresenceMessage",
		    GET_PRESENCE_MESSAGE, tp_account_get_automatic_presence);

    getter_list_add("RequestedPresenceType",
		    GET_PRESENCE_TYPE, tp_account_get_requested_presence);
    getter_list_add("RequestedPresenceStatus",
		    GET_PRESENCE_STATUS, tp_account_get_requested_presence);
    getter_list_add("RequestedPresenceMessage",
		    GET_PRESENCE_MESSAGE, tp_account_get_requested_presence);

    getter_list_add("CurrentPresenceType",
		    GET_PRESENCE_TYPE, tp_account_get_current_presence);
    getter_list_add("CurrentPresenceStatus",
		    GET_PRESENCE_STATUS, tp_account_get_current_presence);
    getter_list_add("CurrentPresenceMessage",
		    GET_PRESENCE_MESSAGE, tp_account_get_current_presence);
}

static Getter *
getter_by_name(char *name)
{
    getter_list_init();

    return g_datalist_get_data(&getter_list, name);
}

/* ====================================================================== */

static gint
compare_accounts (gconstpointer a,
                  gconstpointer b)
{
    return strcmp (tp_account_get_path_suffix (TP_ACCOUNT (a)),
                   tp_account_get_path_suffix (TP_ACCOUNT (b)));
}

static GList *
dup_valid_accounts_sorted (TpAccountManager *manager)
{
    return g_list_sort (tp_account_manager_dup_valid_accounts (manager),
                        compare_accounts);
}

static gboolean
command_list (TpAccountManager *manager)
{
    GList *accounts = dup_valid_accounts_sorted (manager);

    if (accounts != NULL) {
	GList *ptr;

	command.common.ret = 0;

	for (ptr = accounts; ptr != NULL; ptr = ptr->next) {
	    puts (tp_account_get_path_suffix (ptr->data));
	}

	g_list_free_full (accounts, g_object_unref);
    }

    return FALSE;                 /* stop mainloop */
}

static gboolean
command_summary (TpAccountManager *manager)
{
    GList *accounts, *l;
    guint longest_account = 0;

    accounts = tp_account_manager_dup_valid_accounts (manager);
    if (accounts == NULL) {
        return FALSE;
    }
    command.common.ret = 0;

    for (l = accounts; l != NULL; l = l->next) {
        TpAccount *account = TP_ACCOUNT (l->data);

        longest_account = MAX (longest_account,
            strlen (tp_account_get_path_suffix (account)));
    }

    /* The -6 is so we can line up the "Enabled" header to have the ticks and
     * crosses below the 7th and final character. We're only guaranteed
     * longest_account ≥ 5 in theory (a/b/c is the shortest legal suffix) but
     * in practice it's always going to be ≥ 7.
     */
    g_return_val_if_fail (longest_account >= 7, FALSE);
    printf ("%-*s %s %s\n", longest_account - 6, "Account", "Enabled", "Requested");
    printf ("%-*s %s %s\n", longest_account - 6, "=======", "=======", "=========");

    for (l = accounts; l != NULL; l = l->next) {
        TpAccount *account = TP_ACCOUNT (l->data);
        gchar *status;

        tp_account_get_requested_presence (account, &status, NULL);
        printf ("%-*s %s %s\n",
            longest_account, tp_account_get_path_suffix (account),
            tp_account_is_enabled (account) ? "✓" : "☐",
            status);
    }

    g_list_free_full (accounts, g_object_unref);
    return FALSE; /* stop mainloop */
}

static void
callback_for_create_account (GObject *source,
			     GAsyncResult *result,
			     gpointer user_data)
{
    TpAccountManager *am = TP_ACCOUNT_MANAGER (source);
    TpAccount *account;
    GError *error = NULL;

    account = tp_account_manager_create_account_finish (am, result, &error);

    if (account != NULL) {
	command.common.ret = 0;
	puts (tp_account_get_path_suffix (account));
    }
    else {
	fprintf (stderr, "%s %s: %s\n", app_name, command.common.name,
                 error->message);
    }
    g_main_loop_quit (main_loop);
}

static gboolean
command_add (TpAccountManager *manager)
{
    GHashTable *properties = tp_asv_new (NULL, NULL);

    tp_account_manager_create_account_async (manager,
	     command.add.manager,
	     command.add.protocol,
	     command.add.display,
	     command.add.parameters,
	     properties,
	     callback_for_create_account,
	     NULL);
    return TRUE;
}

static void
callback_for_update_parameters (GObject *source,
                                GAsyncResult *result,
                                gpointer user_data G_GNUC_UNUSED)
{
    TpAccount *account = TP_ACCOUNT (source);
    gchar **reconnect_required = NULL;
    GError *error = NULL;

    if (tp_account_update_parameters_finish (account, result,
                                             &reconnect_required, &error)) {
        if (reconnect_required[0] != NULL) {
            gchar *r = g_strjoinv (", ", reconnect_required);

            printf ("To apply changes to these parameters:\n");
            printf ("  %s\n", r);
            printf ("run:\n");
            printf ("  %s reconnect %s\n", app_name,
                    tp_account_get_path_suffix (account));
            g_free (r);
            g_strfreev (reconnect_required);
        }

        command.common.ret = 0;
    }
    else {
        fprintf (stderr, "%s %s: %s\n", app_name, command.common.name,
                 error->message);
    }
    g_main_loop_quit (main_loop);
}

static void
callback_for_async (GObject *account,
		    GAsyncResult *res,
		    gpointer user_data)
{
    gboolean (* finish_func) (TpAccount *, GAsyncResult *, GError **);
    GError *error = NULL;

    finish_func = user_data;

    if (!finish_func (TP_ACCOUNT (account), res, &error)) {
        fprintf (stderr, "%s %s: %s\n", app_name, command.common.name,
                 error->message);
        g_error_free (error);
    }
    else {
        command.common.ret = 0;
    }

    g_main_loop_quit (main_loop);
}

static gboolean
command_remove (TpAccount *account)
{
    tp_account_remove_async (account, callback_for_async, tp_account_remove_finish);
    return TRUE;
}

static gchar *
dup_storage_restrictions (TpAccount *account)
{
  TpStorageRestrictionFlags flags;
  GPtrArray *tmp;
  gchar *result;

  flags = tp_account_get_storage_restrictions (account);
  if (flags == 0)
    return g_strdup ("(none)");

  tmp = g_ptr_array_new ();

  if (flags & TP_STORAGE_RESTRICTION_FLAG_CANNOT_SET_PARAMETERS)
    g_ptr_array_add (tmp, "Cannot_Set_Parameters");
  if (flags & TP_STORAGE_RESTRICTION_FLAG_CANNOT_SET_ENABLED)
    g_ptr_array_add (tmp, "Cannot_Set_Enabled");
  if (flags & TP_STORAGE_RESTRICTION_FLAG_CANNOT_SET_PRESENCE)
    g_ptr_array_add (tmp, "Cannot_Set_Presence");
  if (flags & TP_STORAGE_RESTRICTION_FLAG_CANNOT_SET_SERVICE)
    g_ptr_array_add (tmp, "Cannot_Set_Service");

  g_ptr_array_add (tmp, NULL);

  result = g_strjoinv (", ", (gchar **) tmp->pdata);

  g_ptr_array_unref (tmp);
  return result;
}

static gboolean
command_show (TpAccount *account)
{
    const GHashTable *parameters;
    GHashTableIter i[1];
    gpointer keyp, valuep;
    struct presence automatic, current, requested;
    const gchar * const *schemes;
    const gchar *storage_provider;
    const gchar * const *supersedes;

    show ("Account", tp_account_get_path_suffix (account));
    show ("Display Name", tp_account_get_display_name (account));
    show ("Normalized", tp_account_get_normalized_name (account));
    show ("Enabled", tp_account_is_enabled (account) ? "enabled" : "disabled");
    show ("Valid", tp_account_is_valid (account) ? "" : "false");
    show ("Icon", tp_account_get_icon_name (account));
    show ("Connects",
        tp_account_get_connect_automatically (account)
            ? "automatically" : "only when requested");
    show ("Nickname", tp_account_get_nickname (account));
    show ("Service", tp_account_get_service (account));

    puts ("");
    puts ("Presences:");
    automatic.type = tp_account_get_automatic_presence (account,
                                                     &automatic.status,
                                                     &automatic.message);
    show_presence ("Automatic", &automatic);
    free_presence (&automatic);

    current.type = tp_account_get_current_presence (account,
                                                    &current.status,
                                                    &current.message);
    show_presence ("Current", &current);
    free_presence (&current);

    requested.type = tp_account_get_requested_presence (account,
                                                        &requested.status,
                                                        &requested.message);
    show_presence ("Requested", &requested);
    free_presence (&requested);

    show ("Changing",
        tp_account_get_changing_presence (account) ? "yes" : "no");

    schemes = tp_account_get_uri_schemes (account);
    if (schemes != NULL && schemes[0] != NULL)
      {
        puts ("");
        puts ("Addressing:");

        show_uri_schemes (schemes);
      }

    storage_provider = tp_account_get_storage_provider (account);
    if (!tp_str_empty (storage_provider))
      {
        GVariant *storage_identifier;
        gchar *storage_restrictions;

        puts ("");
        puts ("Storage:");
        show ("Provider", storage_provider);

        storage_identifier = tp_account_dup_storage_identifier_variant (
            account);
        if (storage_identifier != NULL)
          {
            gchar *tmp = g_variant_print (storage_identifier, TRUE);

            show ("Identifier", tmp);

            g_free (tmp);
            g_variant_unref (storage_identifier);
          }

        storage_restrictions = dup_storage_restrictions (account);
        show ("Restrictions", storage_restrictions);
        g_free (storage_restrictions);
      }

    supersedes = tp_account_get_supersedes (account);
    if (supersedes != NULL && supersedes[0] != NULL)
      {
        puts ("");
        puts ("Supersedes:");
        for (; *supersedes != NULL; supersedes++)
          printf ("  %s\n", *supersedes + strlen (TP_ACCOUNT_OBJECT_PATH_BASE));
      }

    puts ("");
    parameters = tp_account_get_parameters (account);

    for (g_hash_table_iter_init (i, (GHashTable *) parameters);
	 g_hash_table_iter_next (i, &keyp, &valuep);) {
	show_param (keyp, valuep);
    }

    command.common.ret = 0;

    return FALSE;
}

static gboolean
command_dump (TpAccountManager *manager)
{
    GList *accounts, *l;

    accounts = tp_account_manager_dup_valid_accounts (manager);
    if (accounts == NULL) {
        return FALSE;
    }
    command.common.ret = 0;

    for (l = accounts; l != NULL; l = l->next) {
        TpAccount *account = TP_ACCOUNT (l->data);

        command_show (account);

        if (l->next != NULL)
          printf ("\n------------------------------------------------------------\n\n");
    }

    g_list_free_full (accounts, g_object_unref);
    return FALSE; /* stop mainloop */
}

static gboolean
command_connection (TpAccount *account)
{
    TpConnection *conn;

    conn = tp_account_get_connection (account);

    if (conn != NULL) {
        TpConnectionStatus status;
        TpConnectionStatusReason reason;
	const gchar *name;

	name = tp_proxy_get_object_path (conn);
	status = tp_account_get_connection_status (account, &reason);

	printf("%s %s %s\n", name,
	       connection_status_as_string(status),
	       connection_status_reason_as_string(reason));
	command.common.ret = 0;
    }
    else {
	fprintf(stderr, "%s: no connection\n",
		tp_account_get_path_suffix (account));
    }

    return FALSE;
}

static gboolean
command_get (TpAccount *account)
{
    GPtrArray *args = command.get.args;
    GHashTable *parameters = NULL;
    guint i;

    command.common.ret = 0;

    for (i = 0; i < args->len; i++) {
	Getter *getter = g_ptr_array_index(args, i);

	if (getter->function) {
	    gchar const *(*getstring)(TpAccount *) = getter->function;
	    gboolean (*getboolean)(TpAccount *) = getter->function;
	    TpConnectionPresenceType (*getpresence)(TpAccount *,
				gchar **status, gchar **message) =
		getter->function;

	    if (getter->type == GET_STRING) {
		printf("\"%s\"\n", getstring(account));
	    }
	    else if (getter->type == GET_BOOLEAN) {
		puts(getboolean(account) ? "true" : "false");
	    }
            else if (getter->type == GET_PRESENCE)
            {
                struct presence presence;

                presence.type = getpresence(account, &presence.status,
                    &presence.message);
                printf ("(%u, \"%s\", \"%s\")\n", presence.type,
                    presence.status, presence.message);
                free_presence (&presence);
            }
	    else if (getter->type == GET_PRESENCE_TYPE ||
		     getter->type == GET_PRESENCE_STATUS ||
		     getter->type == GET_PRESENCE_MESSAGE) {
		struct presence presence;

		presence.type = getpresence(account, &presence.status,
			    &presence.message);

		if (getter->type == GET_PRESENCE_TYPE)
		    printf("%u\n", presence.type);
		else if (getter->type == GET_PRESENCE_STATUS)
		    printf("\"%s\"\n", presence.status);
		else
		    printf("\"%s\"\n", presence.message);

                free_presence (&presence);
	    }
	    else {
	    }
	}
	else {
	    GValue *gvalue;
	    gchar *value;

	    if (parameters == NULL)
		parameters = (GHashTable *) tp_account_get_parameters(account);

	    gvalue = g_hash_table_lookup(parameters, getter->name);

	    if (gvalue == NULL) {
		command.common.ret = 1;
		fprintf(stderr, "%s %s: param=%s: %s\n",
			app_name, command.common.name,
			getter->name, "not found");
		continue;
	    }

	    value = g_strdup_value_contents (gvalue);
	    puts(value);
	    g_free(value);
	}
    }

    return FALSE;
}

static gboolean
command_enable (TpAccount *account)
{
    tp_account_set_enabled_async (account, TRUE,
                                  callback_for_async,
                                  tp_account_set_enabled_finish);

    return TRUE;
}

static gboolean
command_disable (TpAccount *account)
{
    tp_account_set_enabled_async (account, FALSE,
                                  callback_for_async,
                                  tp_account_set_enabled_finish);

    return TRUE;
}

static gboolean
command_display (TpAccount *account)
{
    tp_account_set_display_name_async (account, command.display.name,
                                       callback_for_async,
                                       tp_account_set_display_name_finish);

    return TRUE;
}

static gboolean
command_nick (TpAccount *account)
{
    tp_account_set_nickname_async (account, command.nick.name,
				   callback_for_async,
                                   tp_account_set_nickname_finish);

    return TRUE;
}

static gboolean
command_service (TpAccount *account)
{
    tp_account_set_service_async (account, command.service.name,
                                  callback_for_async,
                                  tp_account_set_service_finish);

    return TRUE;
}

static gboolean
command_icon (TpAccount *account)
{
    tp_account_set_icon_name_async (account, command.icon.name,
                                    callback_for_async,
                                    tp_account_set_icon_name_finish);

    return TRUE;
}

static gboolean
command_auto_connect (TpAccount *account)
{
    tp_account_set_connect_automatically_async (account, command.boolean.value,
                                                callback_for_async,
                                                tp_account_set_connect_automatically_finish);

    return TRUE;
}

static gboolean
command_reconnect (TpAccount *account)
{
    tp_account_reconnect_async (account, callback_for_async,
                                tp_account_reconnect_finish);

    return TRUE;
}

static gboolean
command_update (TpAccount *account)
{
    tp_account_update_parameters_async (account,
                                        command.update.set,
                                        (const gchar **) command.update.unset->pdata,
                                        callback_for_update_parameters,
                                        NULL);
    return TRUE;
}

static gboolean
command_auto_presence (TpAccount *account)
{
    tp_account_set_automatic_presence_async (account,
                                             command.presence.type,
                                             command.presence.status,
                                             command.presence.message,
                                             callback_for_async,
                                             tp_account_set_automatic_presence_finish);

    return TRUE;
}

static gboolean
command_request (TpAccount *account)
{
    tp_account_request_presence_async (account,
                                       command.presence.type,
                                       command.presence.status,
                                       command.presence.message,
                                       callback_for_async,
                                       tp_account_request_presence_finish);

    return TRUE;
}

static void
parse (int argc, char **argv)
{
    int i;
    gboolean status;

    app_name = basename (argv[0]);

    if (argc < 2)
	show_help ("No command specified");

    g_type_init ();
    /* Command processing */

    command.common.name = argv[1];

    if (strcmp (argv[1], "add") == 0)
    {
	gchar **strv;

	/* Add account */
	if (argc < 4)
	    show_help ("Invalid add command.");

	if (strchr (argv[2], '/') != NULL)
	{
	    strv = g_strsplit (argv[2], "/", 2);

	    if (strv[0] == NULL || strv[1] == NULL || strv[2] != NULL)
		show_help ("Invalid add command.");

	    command.add.manager = strv[0];
	    command.add.protocol = strv[1];
	}
	else
	{
	    show_help ("Invalid add command.");
	}

	command.ready.manager = command_add;
	command.add.display = argv[3];

	command.add.parameters = new_params ();

	for (i = 4; i < argc; i++)
	{
	    status = set_param (command.add.parameters, NULL, argv[i]);
	    if (!status) {
		g_warning ("%s: bad parameter: %s", argv[1], argv[i]);
		exit (1);
	    }
	}
    }
    else if (strcmp (argv[1], "list") == 0)
    {
	/* List accounts */
	if (argc != 2)
	    show_help ("Invalid list command.");

	command.ready.manager = command_list;
    }
    else if (strcmp (argv[1], "summary") == 0)
    {
        /* List accounts */
        if (argc != 2)
            show_help ("Invalid summary command.");

        command.ready.manager = command_summary;
    }
    else if (strcmp (argv[1], "dump") == 0)
    {
        /* Dump all accounts */
        if (argc != 2)
            show_help ("Invalid dump command.");

        command.ready.manager = command_dump;
    }
    else if (strcmp  (argv[1], "remove") == 0
	     || strcmp (argv[1], "delete") == 0)
    {
	/* Remove account */
	if (argc != 3)
	    show_help ("Invalid remove command.");

	command.ready.account = command_remove;
	command.common.account = argv[2];
    }
    else if (strcmp (argv[1], "show") == 0)
    {
	/* Show account details */

	if (argc != 3)
	    show_help ("Invalid show command.");

	command.ready.account = command_show;
	command.common.account = argv[2];
    }
    else if (strcmp (argv[1], "get") == 0)
    {
	/* Get account details */

	if (argc < 3)
	    show_help ("Invalid get command.");

	command.ready.account = command_get;
	command.common.account = argv[2];
	command.get.args = g_ptr_array_new();

	for (i = 3; argv[i]; i++) {
	    char *name = argv[i];
	    Getter *getter;
	    const char *param = strip_prefix (name, "param=");

	    if (param != NULL) {
		getter = g_new0(Getter, 1);
		getter->name = param;
		getter->type = GET_PARAM;
		getter->function = NULL;
	    }
	    else {
		getter = getter_by_name(name);
		if (getter == NULL) {
		    fprintf(stderr, "%s %s: %s: unknown\n", app_name,
			    "get", name);
		    exit(1);
		}
	    }

	    g_ptr_array_add(command.get.args, getter);
	}
    }
    else if (strcmp (argv[1], "connection") == 0)
    {
	/* Show connection status  */

	if (argc != 3)
	    show_help ("Invalid connection command.");

	command.ready.account = command_connection;
	command.common.account = argv[2];
    }
    else if (strcmp (argv[1], "enable") == 0)
    {
	/* Enable account */
	if (argc != 3)
	    show_help ("Invalid enable command.");

	command.ready.account = command_enable;
	command.common.account = argv[2];
    }
    else if (strcmp (argv[1], "disable") == 0)
    {
	/* Disable account */
	if (argc != 3)
	    show_help ("Invalid disable command.");

	command.ready.account = command_disable;
	command.common.account = argv[2];
    }
    else if (strcmp (argv[1], "display") == 0)
    {
	/* Set display name */
	if (argc != 4)
	    show_help ("Invalid display command.");

	command.ready.account = command_display;
	command.common.account = argv[2];
	command.display.name = argv[3];
    }
    else if (strcmp (argv[1], "nick") == 0)
    {
	/* Set nickname */
	if (argc != 4)
	    show_help ("Invalid nick command.");

	command.ready.account = command_nick;
	command.common.account = argv[2];
	command.nick.name = argv[3];
    }
    else if (strcmp (argv[1], "service") == 0)
    {
        /* Set service */
	if (argc != 4)
	    show_help ("Invalid service command.");

	command.ready.account = command_service;
	command.common.account = argv[2];
	command.service.name = argv[3];
    }
    else if (strcmp (argv[1], "icon") == 0)
    {
	/* Set icon */
	if (argc != 4)
	    show_help ("Invalid icon command.");

	command.ready.account = command_icon;
	command.common.account = argv[2];
	command.icon.name = argv[3];
    }
    else if (strcmp (argv[1], "update") == 0
	     || strcmp (argv[1], "set") == 0)
    {
	/* Set account parameter (s) */
	if (argc < 4)
	    show_help ("Invalid update command.");

	command.ready.account = command_update;
	command.common.account = argv[2];
	command.update.set = new_params ();
	command.update.unset = g_ptr_array_new ();

	for (i = 3; i < argc; i++)
	{
	    status = set_param (command.update.set, command.update.unset,
				argv[i]);
	    if (!status) {
		g_warning ("%s: bad parameter: %s", argv[1], argv[i]);
		exit (1);
	    }
	}

	g_ptr_array_add (command.update.unset, NULL);
    }
    else if (strcmp (argv[1], "auto-presence") == 0)
    {
	/* Set automatic presence */
	if (argc != 4 && argc != 5)
	    show_help ("Invalid auto-presence command.");

	command.ready.account = command_auto_presence;
	command.common.account = argv[2];
	command.presence.type = get_presence_type_for_status(argv[3]);
	command.presence.status = argv[3];
	command.presence.message = argv[4] ? argv[4] : "";

	switch (command.presence.type) {
	case TP_CONNECTION_PRESENCE_TYPE_UNKNOWN:
	case TP_CONNECTION_PRESENCE_TYPE_ERROR:
	    fprintf(stderr, "%s: %s: unknown presence %s\n",
		    app_name, argv[1], argv[3]);
	    exit(1);
	    break;
	default:
	    break;
	}
    }
    else if (strcmp (argv[1], "request") == 0)
    {
	/* Set presence  */
	if (argc != 4 && argc != 5)
	    show_help ("Invalid request command.");

	command.ready.account = command_request;
	command.common.account = argv[2];
	command.presence.type = get_presence_type_for_status(argv[3]);
	command.presence.status = argv[3];
	command.presence.message = argv[4] ? argv[4] : "";

	switch (command.presence.type) {
	case TP_CONNECTION_PRESENCE_TYPE_UNKNOWN:
	case TP_CONNECTION_PRESENCE_TYPE_ERROR:
	    fprintf(stderr, "%s: %s: unknown presence %s\n",
		    app_name, argv[1], argv[3]);
	    exit(1);
	    break;
	default:
	    break;
	}
    }
    else if (strcmp (argv[1], "auto-connect") == 0) {
	/* Turn on (or off) auto-connect  */
	if (argc != 3 && argc != 4)
	    show_help ("Invalid auto-connect command.");

	command.ready.account = command_auto_connect;
	command.common.account = argv[2];
	if (argv[3] == NULL ||
	    g_ascii_strcasecmp (argv[3], "on") == 0 ||
	    g_ascii_strcasecmp (argv[3], "true") == 0 ||
	    g_ascii_strcasecmp (argv[3], "1") == 0)
	    command.boolean.value = TRUE;
	else if (g_ascii_strcasecmp (argv[3], "off") == 0 ||
		 g_ascii_strcasecmp (argv[3], "false") == 0 ||
		 g_ascii_strcasecmp (argv[3], "0") == 0)
	    command.boolean.value = FALSE;
	else
	    show_help ("Invalid auto-connect command.");
    }
    else if (strcmp (argv[1], "reconnect") == 0) {
        if (argc != 3)
            show_help ("Invalid reconnect command.");

        command.ready.account = command_reconnect;
        command.common.account = argv[2];
    }
    else if (strcmp (argv[1], "help") == 0
	     || strcmp (argv[1], "-h") == 0 || strcmp (argv[1], "--help") == 0)
    {
	show_help (NULL);

    }
    else
    {
	show_help ("Unknown command.");
    }
}

static
void manager_ready (GObject *manager,
		    GAsyncResult *res,
		    gpointer user_data)
{
    GError *error = NULL;

    if (!tp_proxy_prepare_finish (manager, res, &error)) {
	fprintf (stderr, "%s: %s\n", app_name, error->message);
	g_error_free (error);
    }
    else {
	if (command.ready.manager (TP_ACCOUNT_MANAGER (manager)))
	    return;
    }

    g_main_loop_quit (main_loop);
}

static
void account_ready (GObject *source,
		    GAsyncResult *res,
		    gpointer user_data)
{
    TpAccount *account = TP_ACCOUNT (source);
    GError *error = NULL;

    if (!tp_proxy_prepare_finish (account, res, &error)) {
        fprintf (stderr, "%s: couldn't load account '%s': %s\n", app_name,
            tp_account_get_path_suffix (account), error->message);
        fprintf (stderr, "Try '%s list' to list known accounts.\n", app_name);
        g_error_free (error);
    }
    else {
        if (command.ready.account (account))
            return;
    }

    g_main_loop_quit (main_loop);
}

int
main (int argc, char **argv)
{
    TpAccountManager *am = NULL;
    TpAccount *a = NULL;
    TpDBusDaemon *dbus = NULL;
    TpSimpleClientFactory *client_factory = NULL;
    GError *error = NULL;
    const GQuark features[] = { TP_ACCOUNT_FEATURE_CORE,
        TP_ACCOUNT_FEATURE_ADDRESSING, TP_ACCOUNT_FEATURE_STORAGE, 0 };

    g_type_init ();

    app_name = basename (argv[0]);

    parse (argc, argv);

    command.common.ret = 1;

    dbus = tp_dbus_daemon_dup (&error);
    if (error != NULL) {
        fprintf (stderr, "%s %s: Failed to connect to D-Bus: %s\n",
            app_name, command.common.name, error->message);
        goto out;
    }
    client_factory = tp_simple_client_factory_new (dbus);

    if (command.common.account == NULL) {
        TpSimpleClientFactory *factory;

        am = tp_account_manager_new (dbus);
        factory = tp_proxy_get_factory (am);

        tp_simple_client_factory_add_account_features (factory, features);

        tp_proxy_prepare_async (am, NULL, manager_ready, NULL);
    }
    else {

	command.common.account = ensure_prefix (command.common.account);
        a = tp_simple_client_factory_ensure_account (client_factory,
            command.common.account, NULL, &error);

	if (error != NULL)
	{
	    fprintf (stderr, "%s %s: %s\n",
		     app_name, command.common.name,
		     error->message);
	    goto out;
	}

	tp_proxy_prepare_async (a, features, account_ready, NULL);
    }

    main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (main_loop);

out:
    g_clear_error (&error);
    tp_clear_object (&client_factory);
    tp_clear_object (&dbus);
    tp_clear_object (&am);
    tp_clear_object (&a);
    tp_clear_pointer (&main_loop, g_main_loop_unref);

    return command.common.ret;
}
