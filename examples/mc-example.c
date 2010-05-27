/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008 Nokia Corporation. 
 *
 * Contact: Alberto Mardegan <alberto.mardegan@nokia.com>
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
#include <stdio.h>
#include <string.h>
#include <telepathy-glib/dbus.h>
#include <libmcclient/mc-account-manager.h>
#include <libmcclient/mc-account.h>
#include <libmcclient/mc-profile.h>

typedef struct _TestObjectClass {
    GObjectClass parent_class;
} TestObjectClass;
typedef struct _TestObject {
    GObject parent;
    gchar *string;
} TestObject;
GType test_object_get_type (void);
#define TEST_TYPE_OBJECT (test_object_get_type ())
G_DEFINE_TYPE (TestObject, test_object, G_TYPE_OBJECT);

static void
test_object_init (TestObject *to)
{
    to->string = g_strdup ("a test string");
}

static void
dispose (GObject *object)
{
    g_debug ("%s called for %p", G_STRFUNC, object);
    G_OBJECT_CLASS (test_object_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    TestObject *to = (TestObject *)object;

    g_debug ("%s called for %p", G_STRFUNC, object);
    g_free (to->string);
    G_OBJECT_CLASS (test_object_parent_class)->finalize (object);
}

static void
test_object_class_init (TestObjectClass *klass)
{
    GObjectClass *object_class = (GObjectClass *)klass;

    object_class->dispose = dispose;
    object_class->finalize = finalize;
}


static GMainLoop *main_loop;
static gint n_avatar;

static void
set_conditions (McAccount *account)
{
    GHashTable *conditions;

    conditions = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert (conditions, "ip-route", g_strdup ("true"));
    mc_account_conditions_set (account, conditions,
			       NULL, NULL, NULL, NULL);
    g_hash_table_destroy (conditions);
}

static void
set_fields (McAccount *account)
{
    const gchar *const fields[] = { "X-TEL", "X-MSN", NULL };


    mc_account_compat_set_secondary_vcard_fields (account, fields,
			       NULL, NULL, NULL, NULL);
}

static void
set_display_name_cb (TpProxy *proxy, const GError *error, gpointer user_data,
		     GObject *weak_object)
{
    g_debug ("%s called (%s)", G_STRFUNC, (gchar *)user_data);
}

static void
ready_cb (McAccount *account, const GError *error, gpointer userdata,
	  GObject *weak_object)
{
    const gchar *ciao = userdata;
    TpConnectionPresenceType type;
    const gchar *status, *message, *name;
    
    g_debug ("%s called with userdata %s", G_STRFUNC, ciao);
    if (error)
    {
	g_warning ("%s: got error: %s", G_STRFUNC, error->message);
	return;
    }
    g_debug ("Displayname: %s", mc_account_get_display_name (account));
    name = mc_account_get_display_name (account);
    if (name && strcmp (name, "Pippo") == 0)
	mc_account_set_display_name (account, "Pluto",
				     set_display_name_cb, "beo", NULL, NULL);
    mc_account_get_automatic_presence (account, &type, &status, &message);
    type = (type == TP_CONNECTION_PRESENCE_TYPE_AWAY) ?
       	TP_CONNECTION_PRESENCE_TYPE_AVAILABLE : TP_CONNECTION_PRESENCE_TYPE_AWAY;
    if (status && strcmp (status, "away") == 0)
	status = "available";
    else
	status = "away";
    mc_account_set_automatic_presence (account, type, status, "ciao",
				       NULL, NULL, NULL, NULL);


    g_debug ("normalizedname: %s", mc_account_get_normalized_name (account));
    mc_account_get_requested_presence (account, &type, &status, &message);
    g_debug ("requestedpresence: %u, %s, %s", type, status, message);

    set_conditions (account);
    set_fields (account);
}

static void
set_avatar_cb (TpProxy *proxy, const GError *error, gpointer user_data,
		     GObject *weak_object)
{
    g_debug ("%s called (%s)", G_STRFUNC, (gchar *)user_data);
    if (error)
	g_warning ("%s: %s", G_STRFUNC, error->message);
}

static void
avatar_ready_cb (McAccount *account, const GError *error, gpointer userdata,
		 GObject *weak_object)
{
    gchar filename[200], *data_old;
    const gchar *ciao = userdata;
    const gchar *data, *mime_type;
    gsize len;
    
    g_debug ("%s called with userdata %s", G_STRFUNC, ciao);
    if (error)
    {
	g_warning ("%s: got error: %s", G_STRFUNC, error->message);
	return;
    }
    sprintf (filename, "avatar%d.bin", n_avatar++);
    if (g_file_get_contents (filename, &data_old, &len, NULL))
    {
	g_debug ("setting avatar %s", filename);
	mc_account_avatar_set (account, data_old, len, "image/png",
			       set_avatar_cb, "boh", NULL, NULL);
	g_free (data_old);
    }
    mc_account_avatar_get (account, &data, &len, &mime_type);
    g_debug ("Mime type: %s", mime_type);
    g_file_set_contents (filename, data, len, NULL);
}

static void
on_string_changed (McAccount *account, GQuark string, const gchar *text,
		   gpointer userdata)
{
    g_debug ("%s changed for account %s:\n  new string: %s",
	     g_quark_to_string (string), account->name,
	     text);
}

static void
on_presence_changed (McAccount *account, GQuark presence, TpConnectionPresenceType type,
		     const gchar *status, const gchar *message, gpointer userdata)
{
    g_debug ("%s Presence changed for account %s:\ntype %d, status %s, message %s",
	     g_quark_to_string (presence), account->name,
	     type, status, message);
}

static void
on_connection_status_changed (McAccount *account, TpConnectionStatus status,
			      TpConnectionStatusReason reason)
{
    g_debug ("Connection status changed for account %s:\n %d, reason %d",
	     account->name, status, reason);
}

static void
on_flag_changed (McAccount *account, GQuark flag, gboolean value, gpointer userdata)
{
    g_debug ("%s flag changed for account %s: %d",
	     g_quark_to_string (flag), account->name, value);

    if (flag == MC_QUARK_VALID && !value)
	g_object_unref (account);
}

static void
print_param (gpointer key, gpointer ht_value, gpointer userdata)
{
    GValue *value = ht_value;
    gchar *name = key;

    if (G_VALUE_TYPE (value) == G_TYPE_BOOLEAN)
	g_debug ("name: %s, value: %d", name, g_value_get_boolean (value));
    else if (G_VALUE_TYPE (value) == G_TYPE_STRING)
	g_debug ("name: %s, value: %s", name, g_value_get_string (value));
    else if (G_VALUE_TYPE (value) == G_TYPE_UINT)
	g_debug ("name: %s, value: %u", name, g_value_get_uint (value));
    else if (G_VALUE_TYPE (value) == G_TYPE_INT)
	g_debug ("name: %s, value: %d", name, g_value_get_int (value));
}

static void
on_parameters_changed (McAccount *account, GHashTable *old, GHashTable *new)
{
    g_debug ("parameters changed for account %s:",
	     account->name);
    g_debug ("old:");
    g_hash_table_foreach (old, print_param, NULL);
    g_debug ("new:");
    g_hash_table_foreach (new, print_param, NULL);
}

static void
on_avatar_changed (McAccount *account, GArray *avatar, const gchar *mime_type)
{
    g_debug ("avatar changed for account %s:",
	     account->name);
    g_debug ("len %d, mime type: %s", avatar->len, mime_type);
}

static void
on_account_removed (TpProxy *proxy, gpointer user_data, GObject *weak_object)
{
    McAccount *account = MC_ACCOUNT (proxy);
    g_debug ("Account %s removed", account->name);
    g_object_unref (account);
}

static void
free_string (gpointer ptr)
{
    g_debug ("%s: %s", G_STRFUNC, (gchar *)ptr);
    g_free (ptr);
}

static gboolean
unref_test_object (gpointer obj)
{
    g_object_unref (obj);
    return FALSE;
}

static void
all_ready_cb (McAccount *account, const GError *error, gpointer user_data,
	      GObject *weak_object)
{
    TestObject *to = (TestObject *)weak_object;
    g_debug ("%s called, account %p, user_data = %s, weak = %p",
	     G_STRFUNC, account, (gchar *)user_data, weak_object);
    g_debug ("Test string: %s", to->string);

    ready_cb (account, error, user_data, weak_object);
    avatar_ready_cb (account, error, user_data, weak_object);
}

static void
watch_account (McAccount *account)
{
    GObject *to;

    g_debug ("watching account %s, manager %s, protocol %s",
	     account->name, account->manager_name, account->protocol_name);

    mc_cli_account_connect_to_removed (account, on_account_removed,
				       NULL, NULL, NULL, NULL);
    g_signal_connect (account, "string-changed",
		      G_CALLBACK (on_string_changed), NULL);
    g_signal_connect (account, "presence-changed",
		      G_CALLBACK (on_presence_changed), NULL);
    g_signal_connect (account, "connection-status-changed",
		      G_CALLBACK (on_connection_status_changed), NULL);
    g_signal_connect (account, "flag-changed",
		      G_CALLBACK (on_flag_changed), NULL);
    g_signal_connect (account, "parameters-changed",
		      G_CALLBACK (on_parameters_changed), NULL);
    g_signal_connect (account, "avatar-changed",
		      G_CALLBACK (on_avatar_changed), NULL);

    to = g_object_new (TEST_TYPE_OBJECT, NULL);
    mc_account_call_when_all_ready (account,
				    all_ready_cb,
				    g_strdup ("Userdata string"), free_string,
				    to,
				    MC_IFACE_QUARK_ACCOUNT,
				    MC_IFACE_QUARK_ACCOUNT_INTERFACE_AVATAR,
				    0);
    unref_test_object (to);
}

static void
am_ready (McAccountManager *am, const GError *error, gpointer user_data)
{
    const gchar * const *accounts, * const *name;

    g_debug ("%s called", G_STRFUNC);
    if (error)
    {
	g_warning ("%s: got error: %s", G_STRFUNC, error->message);
	return;
    }

    accounts = mc_account_manager_get_valid_accounts (am);
    for (name = accounts; *name != NULL; name++)
    {
	McAccount *account;

	account = mc_account_new (((TpProxy *)am)->dbus_daemon, *name);

	watch_account (account);
    }

}

static void
find_accounts_cb (TpProxy *proxy, const GPtrArray *accounts,
		  const GError *error, gpointer user_data,
		  GObject *weak_object)
{
    gchar *name;
    guint i;

    g_debug ("%s called", G_STRFUNC);
    if (error)
    {
	g_warning ("%s: got error: %s", G_STRFUNC, error->message);
	return;
    }

    for (i = 0; i < accounts->len; i++)
    {
	McAccount *account;

	name = g_ptr_array_index (accounts, i);
	account = mc_account_new (proxy->dbus_daemon, name);
	g_debug ("enabled account %s, manager %s, protocol %s",
		 account->name, account->manager_name, account->protocol_name);
    }
}

static void
on_validity_changed (TpProxy *proxy, const gchar *path, gboolean valid,
		     gpointer user_data, GObject *weak_object)
{
    McAccountManager *am = MC_ACCOUNT_MANAGER (proxy);
    const gchar * const *accounts, * const *name;

    g_debug ("Account %s is now %s", path, valid ? "valid" : "invalid");
    g_debug ("valid accounts:");
    accounts = mc_account_manager_get_valid_accounts (am);
    for (name = accounts; *name != NULL; name++)
    {
	g_debug ("  %s", *name);
    }
    g_debug ("invalid accounts:");
    accounts = mc_account_manager_get_invalid_accounts (am);
    for (name = accounts; *name != NULL; name++)
    {
	g_debug ("  %s", *name);
    }

    if (valid)
    {
	McAccount *account;

	account = mc_account_new (proxy->dbus_daemon, path);

	watch_account (account);
    }
}

static void
on_account_created (McAccountManager *am, const gchar *account_path,
		    gboolean valid, gpointer user_data)
{
    g_debug ("%s: %s (%d)", G_STRFUNC, account_path, valid);
}

int
main (int argc,
      char **argv)
{
    McAccountManager *am;
    DBusGConnection *dbus_conn;
    TpDBusDaemon *dbus;
    GHashTable *params;
    GValue v_true = { 0 }, v_profile = { 0 };

    g_type_init ();
    dbus_conn = tp_get_bus ();
    dbus = tp_dbus_daemon_new (dbus_conn);
    dbus_g_connection_unref (dbus_conn);

    am = mc_account_manager_new (dbus);
    g_object_unref (dbus);

    g_signal_connect (am, "account-created",
		      G_CALLBACK (on_account_created), NULL);
    mc_account_manager_call_when_ready (am, am_ready, NULL);
    mc_cli_account_manager_connect_to_account_validity_changed (am,
		      on_validity_changed, NULL, NULL, NULL, NULL);
 
    params = g_hash_table_new (g_str_hash, g_str_equal);
    g_value_init (&v_true, G_TYPE_BOOLEAN);
    g_value_set_boolean (&v_true, TRUE);
    g_hash_table_insert (params,
			 MC_IFACE_ACCOUNT ".Enabled",
			 &v_true);
    g_value_init (&v_profile, G_TYPE_STRING);
    g_value_set_static_string (&v_profile, "sip");
    g_hash_table_insert (params,
			 MC_IFACE_ACCOUNT_INTERFACE_COMPAT ".Profile",
			 &v_profile);
    mc_cli_account_manager_interface_query_call_find_accounts (am, -1, 
							       params,
							       find_accounts_cb,
							       NULL, NULL,
							       NULL);
    g_hash_table_destroy (params);
    main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (main_loop);

    g_object_unref (am);

    return 0;
}


