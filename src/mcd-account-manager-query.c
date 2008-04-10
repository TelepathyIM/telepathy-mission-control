/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
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

#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>
#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-manager.h"
#include "mcd-account-manager-query.h"
#include "_gen/interfaces.h"

static const gchar *supported_keywords[] = {
    "Manager", "Protocol",
    "RequestedPresence", "RequestedStatus", 
    "CurrentPresence", "CurrentStatus",
    NULL
}; 

static void
get_keywords (TpSvcDBusProperties *self, const gchar *name,
	      GValue *value)
{
    g_value_init (value, G_TYPE_STRV);
    g_value_set_static_boxed (value, supported_keywords);
}


McdDBusProp account_manager_query_properties[] = {
    { "Keywords", NULL, get_keywords },
    { 0 },
};

const McdDBusProp *
_mcd_account_manager_query_get_properties (void)
{
    return account_manager_query_properties;
}

void
_mcd_account_manager_query_iface_init (McSvcAccountManagerInterfaceQueryClass *iface,
				       gpointer iface_data)
{
}

