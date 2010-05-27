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
#include <glib/gprintf.h>
#include <stdio.h>
#include <string.h>
#include <telepathy-glib/dbus.h>
#include <libmcclient/mc-account-manager.h>
#include <libmcclient/mc-account.h>
#include <libmcclient/mc-profile.h>
#include <telepathy-glib/interfaces.h>

typedef struct
{
    McAccount *account;
    guint req_id;
} ReqData;

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

static gboolean
unref_test_object (gpointer obj)
{
    g_object_unref (obj);
    return FALSE;
}

static void
channel_request_cb (McAccount *account, guint request_id,
		    McAccountChannelrequestEvent event,
		    gpointer user_data,
		    GObject *weak_object)
{
    g_debug ("%s: id = %x, event = %u", G_STRFUNC, request_id, event);
    g_debug ("userdata = %s", (gchar *)user_data);
    g_debug ("request path = %s",
	     mc_account_channelrequest_get_path (account, request_id));
}

static gboolean
cancel_request (gpointer user_data)
{
    ReqData *rd = user_data;

    g_debug ("%s called, cancelling %u", G_STRFUNC, rd->req_id);
    mc_account_channelrequest_cancel (rd->account, rd->req_id);
    g_slice_free (ReqData, rd);
    return FALSE;
}

static void
request_channel (McAccount *account, GQuark type, const gchar *contact)
{
    McAccountChannelrequestData req;
    GObject *to;
    guint id;
    ReqData *rd;

    to = g_object_new (TEST_TYPE_OBJECT, NULL);

    MC_ACCOUNT_CRD_INIT (&req);
    MC_ACCOUNT_CRD_SET (&req, channel_type, type);
    MC_ACCOUNT_CRD_SET (&req, target_id, contact);
    MC_ACCOUNT_CRD_SET (&req, target_handle_type, TP_HANDLE_TYPE_CONTACT);
    id = mc_account_channelrequest (account, &req, time(0),
				    NULL, MC_ACCOUNT_CR_FLAG_USE_EXISTING,
				    channel_request_cb,
				    g_strdup ("ciao"), g_free,
				    to);
    g_debug ("Request id = %x", id);
    g_timeout_add (10000, unref_test_object, to);

    rd = g_slice_new (ReqData);
    rd->account = account;
    rd->req_id = id;
    g_timeout_add (500, cancel_request, rd);
}

static gboolean
enabled_filter (McAccount *account, gpointer user_data)
{
    return mc_account_is_enabled (account);
}

static void
ready_with_accounts_cb (McAccountManager *manager, const GError *error,
			gpointer user_data, GObject *weak_object)
{
    GList *accounts, *list;
    GQuark channel_type;
    char contact[256];
    McAccount *account;

    gint i;

    g_debug ("%s called", G_STRFUNC);

    if (error)
    {
	g_warning ("Got error: %s", error->message);
	return;
    }

    accounts = mc_account_manager_list_accounts (manager,
						 enabled_filter, NULL);
    i = 1;
    g_printf ("Choose account\n");
    for (list = accounts; list != NULL; list = list->next)
    {
	account = list->data;

	g_printf ("%d) %s\n", i++, account->name);
    }

    i = 0;
    while (scanf ("%u", &i) != 1);

    if (i < 1) g_main_loop_quit (main_loop);
    account = g_list_nth_data (accounts, i - 1);
    g_list_free (accounts);

    g_printf ("Choose channel type:\n"
	      "1) StreamedMedia\n"
	      "2) Text\n");
    i = 0;
    while (scanf ("%u", &i) != 1);

    if (i < 1 || i > 2) g_main_loop_quit (main_loop);
    channel_type = (i == 1) ?
	TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA :
	TP_IFACE_QUARK_CHANNEL_TYPE_TEXT;

    g_printf ("Contact:\n");
    while (fgets (contact, sizeof(contact), stdin))
    {
	g_strchomp (contact);
	if (strlen(contact) > 0) break;
    }
    request_channel (account, channel_type, contact);
}

int
main (int argc,
      char **argv)
{
    McAccountManager *am;
    DBusGConnection *dbus_conn;
    TpDBusDaemon *dbus;

    g_type_init ();
    dbus_conn = tp_get_bus ();
    dbus = tp_dbus_daemon_new (dbus_conn);
    dbus_g_connection_unref (dbus_conn);

    am = mc_account_manager_new (dbus);
    g_object_unref (dbus);

    mc_account_manager_call_when_ready_with_accounts (am,
	    ready_with_accounts_cb,
	    NULL, NULL, NULL,
	    MC_IFACE_QUARK_ACCOUNT,
	    MC_IFACE_QUARK_ACCOUNT_INTERFACE_AVATAR,
	    0);

    main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (main_loop);

    g_object_unref (am);

    return 0;
}


