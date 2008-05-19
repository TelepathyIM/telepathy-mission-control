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
//#include <dbus/dbus-glib.h>
#include <telepathy-glib/dbus.h>
#include <libmcclient/dbus-api.h>
#include <libmcclient/mc-account-manager.h>
#include <libmcclient/mc-account.h>

static GMainLoop *main_loop;


static void
valid_accounts_cb (TpProxy *proxy, const GValue *val_accounts,
		   const GError *error, gpointer user_data,
		   GObject *weak_object)
{
    const gchar **accounts, **name;

    g_debug ("%s called", G_STRFUNC);
    if (error)
    {
	g_warning ("%s: got error: %s", G_STRFUNC, error->message);
	return;
    }

    accounts = g_value_get_boxed (val_accounts);
    for (name = accounts; *name != NULL; name++)
    {
	McAccount *account;

	account = mc_account_new (proxy->dbus_daemon, *name);
	g_debug ("account %s, manager %s, protocol %s",
		 account->unique_name, account->manager_name, account->protocol_name);
    }

    g_timeout_add (2000, (GSourceFunc)g_main_loop_quit, main_loop);
}

int main ()
{
    McAccountManager *am;
    DBusGConnection *dbus_conn;
    TpDBusDaemon *daemon;

    g_type_init ();
    dbus_conn = tp_get_bus ();
    daemon = tp_dbus_daemon_new (dbus_conn);
    dbus_g_connection_unref (dbus_conn);

    am = mc_account_manager_new (daemon);
    g_object_unref (daemon);

    tp_cli_dbus_properties_call_get (am, -1,
				     MC_IFACE_ACCOUNT_MANAGER,
				     "ValidAccounts",
				     valid_accounts_cb,
				     NULL, NULL, NULL);
				     
    main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (main_loop);

    g_object_unref (am);

    return 0;
}


