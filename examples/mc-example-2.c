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
}

static gboolean
unref_test_object (gpointer obj)
{
    g_object_unref (obj);
    return FALSE;
}

static void
watch_account (McAccount *account)
{
    g_debug ("watching account %s (name %s, manager %s, protocol %s)",
	     mc_account_get_display_name (account),
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
}

static void
find_accounts_cb (TpProxy *proxy, const GPtrArray *accounts,
		  const GError *error, gpointer user_data,
		  GObject *weak_object)
{
    McAccountManager *am = MC_ACCOUNT_MANAGER (proxy);
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
	account = mc_account_manager_get_account (am, name);
	g_debug ("enabled account %s, manager %s, protocol %s",
		 account->name, account->manager_name, account->protocol_name);
	watch_account (account);
    }
}

static void
on_account_ready (McAccountManager *manager, McAccount *account)
{
    g_debug ("%s called", G_STRFUNC);
    g_debug ("Account %s is ready", account->name);
}

static void
find_accounts (McAccountManager *am)
{
    GHashTable *params;
    GValue v_true = { 0 }, v_profile = { 0 };

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
}

static gboolean
enabled_filter (McAccount *account, gpointer user_data)
{
    g_debug ("%s called, %s", G_STRFUNC, (gchar *)user_data);
    return mc_account_is_enabled (account);
}

static void
ready_with_accounts_cb (McAccountManager *manager, const GError *error,
			gpointer user_data, GObject *weak_object)
{
    GList *accounts, *list;
    g_debug ("%s called", G_STRFUNC);
    g_debug ("Userdata: %s, weak_object: %p", (gchar *)user_data, weak_object);

    if (error)
    {
	g_warning ("Got error: %s", error->message);
	return;
    }

    g_signal_connect (manager, "account-ready",
		      G_CALLBACK (on_account_ready), NULL);
    accounts = mc_account_manager_list_accounts (manager,
						 enabled_filter, "Hello!");
    for (list = accounts; list != NULL; list = list->next)
    {
	McAccount *account = list->data;

	g_debug ("Enabled account %s", account->name);
    }
    g_list_free (accounts);

    find_accounts (manager);
}

int
main (int argc,
      char **argv)
{
    McAccountManager *am;
    DBusGConnection *dbus_conn;
    TpDBusDaemon *daemon;
    GObject *to;

    g_type_init ();
    dbus_conn = tp_get_bus ();
    daemon = tp_dbus_daemon_new (dbus_conn);
    dbus_g_connection_unref (dbus_conn);

    am = mc_account_manager_new (daemon);
    g_object_unref (daemon);

    to = g_object_new (TEST_TYPE_OBJECT, NULL);
    mc_account_manager_call_when_ready_with_accounts (am,
	    ready_with_accounts_cb,
	    g_strdup ("Please free me"), g_free,
	    to,
	    MC_IFACE_QUARK_ACCOUNT,
	    MC_IFACE_QUARK_ACCOUNT_INTERFACE_AVATAR,
	    0);
    g_timeout_add (240000, unref_test_object, to);

    main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (main_loop);

    g_object_unref (am);

    return 0;
}


