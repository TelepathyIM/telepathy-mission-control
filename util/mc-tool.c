/*
 * This file is part of telepathy-mission-control
 *
 * Copyright (C) 2008 Nokia Corporation.
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>

#include <glib-object.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/defs.h>
#include <libmcclient/mc-account-manager.h>
#include <libmcclient/mc-account.h>
#include <libmcclient/mc-profile.h>

static gchar *app_name;
static GMainLoop *main_loop;

static void
show_help (gchar * err)
{
    if (err)
	printf ("Error: %s\n", err);

    printf ("Usage:\n"
	    "    %1$s list\n"
	    "    %1$s add <profile> <display name> [(int|bool|string):<key>=<value> ...]\n"
	    "    %1$s add <manager>/<protocol> <display name> [(int|bool|string):<key>=<value> ...]\n"
	    "    %1$s update <account name> [(int|uint|bool|string):<key>=<value>|clear:key] ...\n"
	    "    %1$s display <account name> <display name>\n"
	    "    %1$s nick <account name> <nick name>\n"
	    "    %1$s icon <account name> <icon name>\n"
	    "    %1$s show <account name>\n"
	    "    %1$s get <account name> [key...]\n"
	    "    %1$s enable <account name>\n"
	    "    %1$s disable <account name>\n"
	    "    %1$s auto-presence <account name> <presence status> [<message>]\n"
	    "    %1$s request <account name> <presence status> [<message>]\n"
	    "    %1$s auto-connect <account name> [(on|off)]\n"
	    "    %1$s remove <account name>\n",
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
	gboolean (*manager) (McAccountManager *manager);
	gboolean (*account) (McAccount *account);
    } ready;

    struct {
	struct common common;
	gchar const *manager, *protocol, *profile, *display;
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
    } display, nick, icon;

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
    gchar const *status;
    gchar const *message;
};

static char *startswith (char const *string, char const *prefix)
{
    while (*prefix && *string)
	if (*prefix++ != *string++)
	    return 0;
    return *prefix == '\0' ? (char *)string : NULL;
}

char const account_prefix[] =  "/org/freedesktop/Telepathy/Account/";

static char *prefix (char const *string)
{
    if (startswith (string, account_prefix))
	return g_strdup (string);
    return g_strdup_printf ("%s%s", account_prefix, string);
}

static char *strip (char const *string)
{
    char *prefixed = startswith (string, account_prefix);
    if (prefixed)
	return (char *)prefixed;
    return (char *)string;
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
    if (strv_type_key[0] == NULL ||
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
	    return TRUE;
	}
    }

    strv_param_value = g_strsplit (param, "=", 2);
    if (strv_param_value[0] == NULL ||
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
	g_value_set_boolean (gvalue, atoi (value));
	ret = TRUE;
    }
    else if (strcmp (type, "string") == 0)
    {
	g_value_init (gvalue, G_TYPE_STRING);
	g_value_set_string (gvalue, value);
	ret = TRUE;
    }

    if (ret)
	g_hash_table_replace (parameters, g_strdup (key), gvalue);
    else
	g_free (gvalue);

CLEANUP:
    if (strv_param_value)
	g_strfreev (strv_param_value);
    if (strv_type_key)
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

    getter_list_add("DisplayName", GET_STRING, mc_account_get_display_name);
    getter_list_add("Icon", GET_STRING, mc_account_get_icon);
    getter_list_add("Valid", GET_BOOLEAN, mc_account_is_valid);
    getter_list_add("Enabled", GET_BOOLEAN, mc_account_is_enabled);
    getter_list_add("Nickname", GET_STRING, mc_account_get_nickname);
    getter_list_add("ConnectAutomatically", GET_BOOLEAN,
		    mc_account_connects_automatically);
    getter_list_add("NormalizedName", GET_STRING,
		    mc_account_get_normalized_name);

    getter_list_add("AutomaticPresenceType",
		    GET_PRESENCE_TYPE, mc_account_get_automatic_presence);
    getter_list_add("AutomaticPresenceStatus",
		    GET_PRESENCE_STATUS, mc_account_get_automatic_presence);
    getter_list_add("AutomaticPresenceMessage",
		    GET_PRESENCE_MESSAGE, mc_account_get_automatic_presence);

    getter_list_add("RequestedPresenceType",
		    GET_PRESENCE_TYPE, mc_account_get_requested_presence);
    getter_list_add("RequestedPresenceStatus",
		    GET_PRESENCE_STATUS, mc_account_get_requested_presence);
    getter_list_add("RequestedPresenceMessage",
		    GET_PRESENCE_MESSAGE, mc_account_get_requested_presence);

    getter_list_add("CurrentPresenceType",
		    GET_PRESENCE_TYPE, mc_account_get_current_presence);
    getter_list_add("CurrentPresenceStatus",
		    GET_PRESENCE_STATUS, mc_account_get_current_presence);
    getter_list_add("CurrentPresenceMessage",
		    GET_PRESENCE_MESSAGE, mc_account_get_current_presence);
}

static Getter *
getter_by_name(char *name)
{
    getter_list_init();

    return g_datalist_get_data(&getter_list, name);
}

/* ====================================================================== */

static gboolean
command_list (McAccountManager *manager)
{
    const gchar * const *accounts;

    accounts = mc_account_manager_get_valid_accounts (manager);

    if (accounts) {
	int i;

	command.common.ret = 0;

	for (i = 0; accounts[i]; i++) {
	    puts (strip (accounts[i]));
	}
    }

    return FALSE;                 /* stop mainloop */
}


static void
callback_for_create_account (TpProxy *proxy,
			     gchar const *account,
			     GError const *error,
			     gpointer user_data,
			     GObject *weak)
{
    if (error == NULL) {
	command.common.ret = 0;
	puts (strip (account));
    }
    else {
	fprintf (stderr, "%s: %s\n", app_name, error->message);
    }
    g_main_loop_quit (main_loop);
}

static gboolean
command_add (McAccountManager *manager)
{
    GHashTable *properties;
    GValue v_profile = { 0 };

    properties = g_hash_table_new (g_str_hash, g_str_equal);

    if (command.add.profile)
    {
	g_value_init (&v_profile, G_TYPE_STRING);
	g_value_set_static_string (&v_profile, command.add.profile);
	g_hash_table_insert (properties,
			     MC_IFACE_ACCOUNT_INTERFACE_COMPAT ".Profile",
			     &v_profile);
    }

    return NULL !=
	mc_cli_account_manager_call_create_account
	    (manager, 25000,
	     command.add.manager,
	     command.add.protocol,
	     command.add.display,
	     command.add.parameters,
	     properties,
	     callback_for_create_account,
	     NULL, NULL, NULL);
}

static void
callback_for_update_parameters (TpProxy *proxy,
                                const gchar **unchanged G_GNUC_UNUSED,
                                const GError *error,
                                gpointer user_data,
                                GObject *weak_object)
{
    if (error == NULL) {
        command.common.ret = 0;
    }
    else {
        fprintf (stderr, "%s %s: %s\n", app_name, command.common.name,
                 error->message);
    }
    g_main_loop_quit (main_loop);
}

static void
callback_for_void (TpProxy *proxy,
		   const GError *error,
		   gpointer user_data,
		   GObject *weak_object)
{
    if (error == NULL) {
	command.common.ret = 0;
    }
    else {
	fprintf (stderr, "%s %s: %s\n", app_name, command.common.name,
		 error->message);
    }
    g_main_loop_quit (main_loop);
}

static gboolean
command_remove (McAccount *account)
{
    return NULL !=
	mc_cli_account_call_remove (account, 25000,
				    callback_for_void,
				    NULL, NULL, NULL);
}

static gboolean
command_show (McAccount *account)
{
    gchar const *name;
    GHashTable *parameters;
    GHashTableIter i[1];
    gpointer keyp, valuep;
    struct presence automatic, current, requested;

    name = strip (command.common.account);

    show ("Account", name);
    show ("Display Name", mc_account_get_display_name (account));
    show ("Normalized", mc_account_get_normalized_name (account));
    show ("Enabled", mc_account_is_enabled (account) ? "enabled" : "disabled");
    show ("Valid", mc_account_is_valid (account) ? "" : "false");
    show ("Icon", mc_account_get_icon (account));
    show ("Connects",
	  mc_account_connects_automatically (account) ? "automatically" : NULL);
    show ("Nickname", mc_account_get_nickname (account));

    mc_account_get_automatic_presence (account, &automatic.type,
				       &automatic.status, &automatic.message);
    show_presence ("Automatic", &automatic);
    mc_account_get_current_presence (account, &current.type,
				     &current.status, &current.message);
    show_presence ("Current", &current);
    mc_account_get_requested_presence (account, &requested.type,
				       &requested.status, &requested.message);
    show_presence ("Requested", &requested);
    puts ("");
    parameters = mc_account_get_parameters (account);

    for (g_hash_table_iter_init (i, parameters);
	 g_hash_table_iter_next (i, &keyp, &valuep);) {
	show_param (keyp, valuep);
    }

    command.common.ret = 0;

    return FALSE;
}

static gboolean
command_connection (McAccount *account)
{
    gchar const *name;
    TpConnectionStatus status;
    TpConnectionStatusReason reason;

    name = mc_account_get_connection_path (account);
    status = mc_account_get_connection_status (account);
    reason = mc_account_get_connection_status_reason (account);

    if (name && name[0]) {
	printf("%s %s %s\n", name,
	       connection_status_as_string(status),
	       connection_status_reason_as_string(reason));
	command.common.ret = 0;
    }
    else {
	fprintf(stderr, "%s: no connection\n",
		strip (command.common.account));
    }

    return FALSE;
}

static gboolean
command_get (McAccount *account)
{
    GPtrArray *args = command.get.args;
    GHashTable *parameters = NULL;
    guint i;

    command.common.ret = 0;

    for (i = 0; i < args->len; i++) {
	Getter *getter = g_ptr_array_index(args, i);

	if (getter->function) {
	    gchar const *(*getstring)(McAccount *) = getter->function;
	    gboolean (*getboolean)(McAccount *) = getter->function;
	    void (*getpresence)(McAccount *, TpConnectionPresenceType *type,
				gchar const **status, gchar const **message) =
		getter->function;

	    if (getter->type == GET_STRING) {
		printf("\"%s\"\n", getstring(account));
	    }
	    else if (getter->type == GET_BOOLEAN) {
		puts(getboolean(account) ? "true" : "false");
	    }
	    else if (getter->type == GET_PRESENCE_TYPE ||
		     getter->type == GET_PRESENCE_STATUS ||
		     getter->type == GET_PRESENCE_MESSAGE) {
		struct presence presence;
		getpresence(account, &presence.type, &presence.status,
			    &presence.message);
		if (getter->type == GET_PRESENCE_TYPE)
		    printf("%u\n", presence.type);
		else if (getter->type == GET_PRESENCE_STATUS)
		    printf("\"%s\"\n", presence.status);
		else
		    printf("\"%s\"\n", presence.message);
	    }
	    else {
	    }
	}
	else {
	    GValue *gvalue;
	    gchar *value;

	    if (parameters == NULL)
		parameters = mc_account_get_parameters(account);

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
command_enable (McAccount *account)
{
    return NULL !=
	mc_account_set_enabled (account, TRUE,
				callback_for_void, NULL, NULL, NULL);
}

static gboolean
command_disable (McAccount *account)
{
    return NULL !=
	mc_account_set_enabled (account, FALSE,
				callback_for_void, NULL, NULL, NULL);
}

static gboolean
command_display (McAccount *account)
{
    return NULL !=
	mc_account_set_display_name (account, command.display.name,
				     callback_for_void, NULL, NULL, NULL);
}

static gboolean
command_nick (McAccount *account)
{
    return NULL !=
	mc_account_set_nickname (account, command.nick.name,
				 callback_for_void, NULL, NULL, NULL);
}

static gboolean
command_icon (McAccount *account)
{
    return NULL !=
	mc_account_set_icon (account, command.icon.name,
			     callback_for_void, NULL, NULL, NULL);
}

static gboolean
command_auto_connect (McAccount *account)
{
    return NULL !=
	mc_account_set_connect_automatically (account, command.boolean.value,
					      callback_for_void,
					      NULL, NULL, NULL);
}

static gboolean
command_update (McAccount *account)
{
    return NULL !=
	mc_cli_account_call_update_parameters (account, 25000,
					       command.update.set,
					       (const gchar  **)
					       command.update.unset->pdata,
					       callback_for_update_parameters,
					       NULL, NULL, NULL);
}

static gboolean
command_auto_presence (McAccount *account)
{
    return NULL !=
	mc_account_set_automatic_presence (account,
					   command.presence.type,
					   command.presence.status,
					   command.presence.message,
					   callback_for_void,
					   NULL, NULL, NULL);
}

static gboolean
command_request (McAccount *account)
{
    return NULL !=
	mc_account_set_requested_presence (account,
					   command.presence.type,
					   command.presence.status,
					   command.presence.message,
					   callback_for_void,
					   NULL, NULL, NULL);
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
	    McProfile *profile;

	    profile = mc_profile_lookup (argv[2]);
	    if (!profile)
	    {
		g_warning ("%s: profile %s not found", argv[1], argv[2]);
		exit (1);
	    }

	    command.add.profile = argv[2];
	    command.add.manager = mc_profile_get_manager_name (profile);
	    command.add.protocol = mc_profile_get_protocol_name (profile);
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
	    char *param = startswith(name, "param=");

	    if (param) {
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
void manager_ready (McAccountManager *manager,
		    const GError *error,
		    gpointer user_data)
{
    if (error) {
	fprintf (stderr, "%s: %s\n", app_name, error->message);
    }
    else {
	if (command.ready.manager (manager))
	    return;
    }

    g_main_loop_quit (main_loop);
}

static
void account_ready (McAccount *account,
		    const GError *error,
		    gpointer user_data)
{
    if (error) {
	fprintf (stderr, "%s: %s: %s\n",
		 app_name, strip (command.common.account), error->message);
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
    McAccountManager *am = NULL;
    McAccount *a = NULL;
    DBusGConnection *dbus_conn;
    TpDBusDaemon *daemon;

    g_type_init ();

    app_name = basename (argv[0]);

    parse (argc, argv);

    command.common.ret = 1;

    dbus_conn = tp_get_bus ();
    daemon = tp_dbus_daemon_new (dbus_conn);
    dbus_g_connection_unref (dbus_conn);

    if (command.common.account == NULL) {
	am = mc_account_manager_new (daemon);
	mc_account_manager_call_when_ready (am, manager_ready, NULL);
    }
    else {
	command.common.account = prefix (command.common.account);
	a = mc_account_new (daemon, command.common.account);

	if (a == NULL)
	{
	    fprintf (stderr, "%s %s: '%s' is not a valid account name\n",
		     app_name, command.common.name,
		     strip (command.common.account));
	    goto out;
	}

	mc_account_call_when_ready (a, account_ready, NULL);
    }
    g_object_unref (daemon);

    main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (main_loop);

out:
    if (am) g_object_unref (am);
    if (a) g_object_unref (a);

    return command.common.ret;
}
