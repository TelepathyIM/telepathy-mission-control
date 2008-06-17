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
//#include <dbus/dbus-glib.h>
#include <telepathy-glib/dbus.h>
#include <libmcclient/dbus-api.h>
#include <libmcclient/mc-account-manager.h>
#include <libmcclient/mc-account.h>
#include <libmcclient/mc-profile.h>

static GMainLoop *main_loop;
static gint n_avatar;

static void
ready_cb (McAccount *account, const GError *error, gpointer userdata)
{
    const gchar *ciao = userdata;
    TpConnectionPresenceType type;
    const gchar *status, *message;
    
    g_debug ("%s called with userdata %s", G_STRFUNC, ciao);
    if (error)
    {
	g_warning ("%s: got error: %s", G_STRFUNC, error->message);
	return;
    }
    g_debug ("Displayname: %s", mc_account_get_display_name (account));
    g_debug ("normalizedname: %s", mc_account_get_normalized_name (account));
    mc_account_get_requested_presence (account, &type, &status, &message);
    g_debug ("requestedpresence: %u, %s, %s", type, status, message);
}

static void
avatar_ready_cb (McAccount *account, const GError *error, gpointer userdata)
{
    gchar filename[200];
    const gchar *ciao = userdata;
    const gchar *data, *mime_type;
    gsize len;
    
    g_debug ("%s called with userdata %s", G_STRFUNC, ciao);
    if (error)
    {
	g_warning ("%s: got error: %s", G_STRFUNC, error->message);
	return;
    }
    mc_account_avatar_get (account, &data, &len, &mime_type);
    g_debug ("Mime type: %s", mime_type);
    sprintf (filename, "avatar%d.bin", n_avatar++);
    g_file_set_contents (filename, data, len, NULL);
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
	g_debug ("account %s, manager %s, protocol %s",
		 account->name, account->manager_name, account->protocol_name);
	g_signal_connect (account, "presence-changed::current",
			  G_CALLBACK (on_presence_changed), NULL);
	g_signal_connect (account, "connection-status-changed",
			  G_CALLBACK (on_connection_status_changed), NULL);
	g_signal_connect (account, "flag-changed",
			  G_CALLBACK (on_flag_changed), NULL);
	g_signal_connect (account, "parameters-changed",
			  G_CALLBACK (on_parameters_changed), NULL);
	g_signal_connect (account, "avatar-changed",
			  G_CALLBACK (on_avatar_changed), NULL);

	mc_account_call_when_ready (account, ready_cb, NULL);
	mc_account_avatar_call_when_ready (account, avatar_ready_cb, NULL);
    }

}

void find_accounts_cb (TpProxy *proxy, const GPtrArray *accounts,
		       const GError *error, gpointer user_data,
		       GObject *weak_object)
{
    gchar *name;
    gint i;

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
}

int main ()
{
    McAccountManager *am;
    DBusGConnection *dbus_conn;
    TpDBusDaemon *daemon;
    GHashTable *params;
    GValue v_true = { 0 };

    g_type_init ();
    dbus_conn = tp_get_bus ();
    daemon = tp_dbus_daemon_new (dbus_conn);
    dbus_g_connection_unref (dbus_conn);

    am = mc_account_manager_new (daemon);
    g_object_unref (daemon);

    mc_account_manager_call_when_ready (am, am_ready, NULL);
    mc_cli_account_manager_connect_to_account_validity_changed (am,
		      on_validity_changed, NULL, NULL, NULL, NULL);
 
    params = g_hash_table_new (g_str_hash, g_str_equal);
    g_value_init (&v_true, G_TYPE_BOOLEAN);
    g_value_set_boolean (&v_true, TRUE);
    g_hash_table_insert (params,
			 "org.freedesktop.Telepathy.Account.Enabled",
			 &v_true);
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


