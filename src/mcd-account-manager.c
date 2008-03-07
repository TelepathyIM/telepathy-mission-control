/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
 *
 * Contact: Naba Kumar  <naba.kumar@nokia.com>
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

#include <glib/gi18n.h>
#include <config.h>

#include "mcd-account-manager.h"
#include "mcd-account.h"
#include "mcd-account-priv.h"

#define MCD_ACCOUNT_MANAGER_PRIV(account_manager) \
    (MCD_ACCOUNT_MANAGER (account_manager)->priv)

static void account_manager_iface_init (McSvcAccountManagerClass *iface,
					gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (McdAccountManager, mcd_account_manager, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (MC_TYPE_SVC_ACCOUNT_MANAGER,
						account_manager_iface_init);
			)

struct _McdAccountManagerPrivate
{
};

enum
{
    PROP_0,
};


static void
account_manager_iface_init (McSvcAccountManagerClass *iface,
			    gpointer iface_data)
{
}

static void
_mcd_account_manager_finalize (GObject *object)
{
    G_OBJECT_CLASS (mcd_account_manager_parent_class)->finalize (object);
}

static void
_mcd_account_manager_dispose (GObject *object)
{
    G_OBJECT_CLASS (mcd_account_manager_parent_class)->dispose (object);
}

static void
mcd_account_manager_class_init (McdAccountManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdAccountManagerPrivate));

    object_class->dispose = _mcd_account_manager_dispose;
    object_class->finalize = _mcd_account_manager_finalize;
}

static void
mcd_account_manager_init (McdAccountManager *account_manager)
{
    McdAccountManagerPrivate *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE ((account_manager),
					MCD_TYPE_ACCOUNT_MANAGER,
					McdAccountManagerPrivate);
    account_manager->priv = priv;
}

McdAccountManager *
mcd_account_manager_new (void)
{
    gpointer *obj;

    obj = g_object_new (MCD_TYPE_ACCOUNT_MANAGER, NULL);
    return MCD_ACCOUNT_MANAGER (obj);
}

