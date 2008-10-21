/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008 Nokia Corporation. 
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

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <config.h>

#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>
#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-compat.h"
#include "mcd-account-manager.h"
#include "mcd-misc.h"
#include "mcd-service.h"
#include "_gen/interfaces.h"

static guint last_operation_id = 1;

static void
set_profile (TpSvcDBusProperties *self, const gchar *name,
	     const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    const gchar *string, *unique_name;
    GKeyFile *keyfile;

    keyfile = mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    string = g_value_get_string (value);
    if (string && string[0] != 0)
	g_key_file_set_string (keyfile, unique_name,
			       name, string);
    else
    {
	g_key_file_remove_key (keyfile, unique_name,
			       name, NULL);
    }
    mcd_account_manager_write_conf (keyfile);
}

static void
get_profile (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    const gchar *unique_name;
    GKeyFile *keyfile;
    gchar *string;

    keyfile = mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    string = g_key_file_get_string (keyfile, unique_name,
				    name, NULL);
    g_value_init (value, G_TYPE_STRING);
    g_value_take_string (value, string);
}

static void
get_avatar_file (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    gchar *string;

    string = mcd_account_get_avatar_filename (account);
    g_value_init (value, G_TYPE_STRING);
    g_value_take_string (value, string);
}

static void
set_secondary_vcard_fields (TpSvcDBusProperties *self, const gchar *name,
			    const GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    const gchar *unique_name, **fields, **field;
    GKeyFile *keyfile;

    keyfile = mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    fields = g_value_get_boxed (value);
    if (fields)
    {
	gsize len;

	for (field = fields, len = 0; *field; field++, len++);
	g_key_file_set_string_list (keyfile, unique_name,
				    name, fields, len);
    }
    else
    {
	g_key_file_remove_key (keyfile, unique_name,
			       name, NULL);
    }
    mcd_account_manager_write_conf (keyfile);
}

static void
get_secondary_vcard_fields (TpSvcDBusProperties *self, const gchar *name,
			    GValue *value)
{
    McdAccount *account = MCD_ACCOUNT (self);
    GKeyFile *keyfile;
    const gchar *unique_name;
    gchar **fields;

    keyfile = mcd_account_get_keyfile (account);
    unique_name = mcd_account_get_unique_name (account);
    fields = g_key_file_get_string_list (keyfile, unique_name,
					 name, NULL, NULL);
    g_value_init (value, G_TYPE_STRV);
    g_value_take_boxed (value, fields);
}


const McdDBusProp account_compat_properties[] = {
    { "Profile", set_profile, get_profile },
    { "AvatarFile", NULL, get_avatar_file },
    { "SecondaryVCardFields", set_secondary_vcard_fields, get_secondary_vcard_fields },
    { 0 },
};

static void
account_request_channel (McSvcAccountInterfaceCompat *self,
			 const gchar *type,
			 guint handle,
			 gint handle_type,
			 DBusGMethodInvocation *context)
{
    struct mcd_channel_request req;
    GError *error = NULL;

    memset (&req, 0, sizeof (req));
    req.channel_type = type;
    req.channel_handle = handle;
    req.channel_handle_type = handle_type;
    req.requestor_serial = last_operation_id++;
    req.requestor_client_id = dbus_g_method_get_sender (context);
    _mcd_account_compat_request_channel_nmc4 (MCD_ACCOUNT (self),
                                              &req, &error);
    if (error)
    {
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }
    mc_svc_account_interface_compat_return_from_request_channel (context, req.requestor_serial);
}

static void
account_request_channel_with_string_handle (McSvcAccountInterfaceCompat *self,
					    const gchar *type,
					    const gchar *handle,
					    gint handle_type,
					    DBusGMethodInvocation *context)
{
    struct mcd_channel_request req;
    GError *error = NULL;

    memset (&req, 0, sizeof (req));
    req.channel_type = type;
    req.channel_handle_string = handle;
    req.channel_handle_type = handle_type;
    req.requestor_serial = last_operation_id++;
    req.requestor_client_id = dbus_g_method_get_sender (context);
    _mcd_account_compat_request_channel_nmc4 (MCD_ACCOUNT (self),
                                              &req, &error);
    if (error)
    {
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	return;
    }
    mc_svc_account_interface_compat_return_from_request_channel_with_string_handle (context, req.requestor_serial);
}

static void
account_cancel_channel_request (McSvcAccountInterfaceCompat *self,
				guint in_operation_id,
				DBusGMethodInvocation *context)
{
    GError *error;

    error = g_error_new (TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
			 "%s is currently just a stub", G_STRFUNC);
    dbus_g_method_return_error (context, error);
    g_error_free (error);
}

void
account_compat_iface_init (McSvcAccountInterfaceCompatClass *iface,
			   gpointer iface_data)
{
#define IMPLEMENT(x) mc_svc_account_interface_compat_implement_##x (\
    iface, account_##x)
    IMPLEMENT(request_channel);
    IMPLEMENT(request_channel_with_string_handle);
    IMPLEMENT(cancel_channel_request);
#undef IMPLEMENT
}

static void
process_channel_request (McdAccount *account, gpointer userdata,
			 const GError *error)
{
    McdChannel *channel = MCD_CHANNEL (userdata);
    McdConnection *connection;
    GError *err = NULL;

    if (error)
    {
        g_warning ("%s: got error: %s", G_STRFUNC, error->message);
        /* TODO: report the error to the requestor process */
        g_object_unref (channel);
        return;
    }
    g_debug ("%s called", G_STRFUNC);
    connection = mcd_account_get_connection (account);
    g_return_if_fail (connection != NULL);
    g_return_if_fail (mcd_connection_get_connection_status (connection)
                      == TP_CONNECTION_STATUS_CONNECTED);

    mcd_connection_request_channel (connection, channel, &err);
}

static void
on_channel_status_changed (McdChannel *channel, McdChannelStatus status,
                           McdAccount *account)
{
    g_debug ("%s (%u)", G_STRFUNC, status);
    g_return_if_fail (MCD_IS_ACCOUNT (account));

    if (status == MCD_CHANNEL_FAILED)
    {
        guint requestor_serial;
        gchar *requestor_client_id;
        const GError *error;
        McdMaster *master;

        master = mcd_master_get_default ();
        g_return_if_fail (MCD_IS_SERVICE (master));

        error = _mcd_channel_get_error (channel);
        g_object_get (channel,
                      "requestor-serial", &requestor_serial,
                      "requestor-client-id", &requestor_client_id,
                      NULL);
        g_signal_emit_by_name (master, "mcd-error", requestor_serial,
                               requestor_client_id, error->code);
        g_free (requestor_client_id);
    }
}

gboolean
_mcd_account_compat_request_channel_nmc4 (McdAccount *account,
                                          const struct mcd_channel_request *req,
                                          GError **error)
{
    McdChannel *channel;
    GHashTable *properties;
    GValue *value;

    properties = g_hash_table_new_full (g_str_hash, g_str_equal,
                                        NULL, _mcd_prop_value_free);

    value = g_slice_new0 (GValue);
    g_value_init (value, G_TYPE_STRING);
    g_value_set_string (value, req->channel_type);
    g_hash_table_insert (properties, TP_IFACE_CHANNEL ".ChannelType", value);

    if (req->channel_handle_string)
    {
        value = g_slice_new0 (GValue);
        g_value_init (value, G_TYPE_STRING);
        g_value_set_string (value, req->channel_handle_string);
        g_hash_table_insert (properties, TP_IFACE_CHANNEL ".TargetID", value);
    }

    if (req->channel_handle)
    {
        value = g_slice_new0 (GValue);
        g_value_init (value, G_TYPE_UINT);
        g_value_set_uint (value, req->channel_handle);
        g_hash_table_insert (properties, TP_IFACE_CHANNEL ".TargetHandle",
                             value);
    }

    value = g_slice_new0 (GValue);
    g_value_init (value, G_TYPE_UINT);
    g_value_set_uint (value, req->channel_handle_type);
    g_hash_table_insert (properties, TP_IFACE_CHANNEL ".TargetHandleType",
                         value);

    channel = mcd_channel_new_request (properties);
    g_object_set ((GObject *)channel,
                  "requestor-serial", req->requestor_serial,
                  "requestor-client-id", req->requestor_client_id,
                  NULL);
    g_signal_connect (channel, "status-changed",
                      G_CALLBACK (on_channel_status_changed), account);

    return _mcd_account_online_request (account,
                                        process_channel_request,
                                        channel,
                                        error);
}

