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
#include <glib/gi18n.h>
#include <config.h>

#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-manager.h"

#define MCD_ACCOUNT_PRIV(account) (MCD_ACCOUNT (account)->priv)

static void account_iface_init (McSvcAccountClass *iface,
			       	gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (McdAccount, mcd_account, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (MC_TYPE_SVC_ACCOUNT,
						account_iface_init);
			)

struct _McdAccountPrivate
{
};

enum
{
    PROP_0,
};


static void
account_iface_init (McSvcAccountClass *iface, gpointer iface_data)
{
}

static void
_mcd_account_finalize (GObject *object)
{
    G_OBJECT_CLASS (mcd_account_parent_class)->finalize (object);
}

static void
_mcd_account_dispose (GObject *object)
{
    G_OBJECT_CLASS (mcd_account_parent_class)->dispose (object);
}

static void
mcd_account_class_init (McdAccountClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdAccountPrivate));

    object_class->dispose = _mcd_account_dispose;
    object_class->finalize = _mcd_account_finalize;
}

static void
mcd_account_init (McdAccount *account)
{
    McdAccountPrivate *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE ((account),
					MCD_TYPE_ACCOUNT,
					McdAccountPrivate);
    account->priv = priv;
}

McdAccount *
mcd_account_new (GKeyFile *keyfile, const gchar *name)
{
    gpointer *obj;
    obj = g_object_new (MCD_TYPE_ACCOUNT,
			"keyfile", keyfile,
			"name", name,
			NULL);
    return MCD_ACCOUNT (obj);
}

