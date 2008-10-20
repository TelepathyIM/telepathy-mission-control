/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4 -*- */
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
#include <telepathy-glib/util.h>
#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-requests.h"
#include "mcd-account-manager.h"
#include "_gen/interfaces.h"


const McdDBusProp account_channelrequests_properties[] = {
    { 0 },
};

static void
account_request_create (McSvcAccountInterfaceChannelRequests *self,
                        GHashTable *properties, guint64 user_time,
                        const gchar *preferred_handler,
                        DBusGMethodInvocation *context)
{
    GError *error = NULL;
    const gchar *request_id;

    if (error)
    {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }
    request_id = "/com/nokia/chavo/request/r3";
    mc_svc_account_interface_channelrequests_return_from_create (context,
                                                                 request_id);
}

static void
account_request_ensure_channel (McSvcAccountInterfaceChannelRequests *self,
                                GHashTable *properties, guint64 user_time,
                                const gchar *preferred_handler,
                                DBusGMethodInvocation *context)
{
    GError *error = NULL;
    const gchar *request_id;

    if (error)
    {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }
    request_id = "/com/nokia/chavo/request/r4";
    mc_svc_account_interface_channelrequests_return_from_ensure_channel
        (context, request_id);
}

static void
account_request_cancel (McSvcAccountInterfaceChannelRequests *self,
                        const gchar *request_id,
                        DBusGMethodInvocation *context)
{
    GError *error;

    error = g_error_new (TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
                         "%s is currently just a stub", G_STRFUNC);
    dbus_g_method_return_error (context, error);
    g_error_free (error);
}

void
account_channelrequests_iface_init (McSvcAccountInterfaceChannelRequestsClass *iface,
                                    gpointer iface_data)
{
#define IMPLEMENT(x) mc_svc_account_interface_channelrequests_implement_##x (\
    iface, account_request_##x)
    IMPLEMENT(create);
    IMPLEMENT(ensure_channel);
    IMPLEMENT(cancel);
#undef IMPLEMENT
}

