/*
 * mc-account.c - Telepathy Account D-Bus interface (client side)
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "mc-account.h"
#include "dbus-api.h"

#include <telepathy-glib/proxy-subclass.h>

#include "_gen/cli-Account-body.h"

/**
 * SECTION:mc-account
 * @title: McAccount
 * @short_description: proxy object for the Telepathy Account D-Bus API
 *
 * This module provides a client-side proxy object for the Telepathy
 * Account D-Bus API.
 *
 * Since: FIXME
 */

/**
 * McAccountClass:
 *
 * The class of a #McAccount.
 *
 * Since: FIXME
 */
struct _McAccountClass {
    TpProxyClass parent_class;
    /*<private>*/
    gpointer priv;
};

/**
 * McAccount:
 *
 * A proxy object for the Telepathy Account D-Bus API. This is a subclass of
 * #TpProxy.
 *
 * Since: FIXME
 */
struct _McAccount {
    TpProxy parent;
    /*<private>*/
    gpointer priv;
};

G_DEFINE_TYPE (McAccount, mc_account, TP_TYPE_PROXY);

static void
mc_account_init (McAccount *self)
{
}

static void
mc_account_class_init (McAccountClass *klass)
{
    GType type = MC_TYPE_ACCOUNT;
    TpProxyClass *proxy_class = (TpProxyClass *) klass;

    /* the API is stateless, so we can keep the same proxy across restarts */
    proxy_class->must_have_unique_name = FALSE;

    proxy_class->interface = MC_IFACE_QUARK_ACCOUNT;
    tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_Account_add_signals);

    tp_proxy_subclass_add_error_mapping (type, TP_ERROR_PREFIX, TP_ERRORS,
					 TP_TYPE_ERROR);
}

/**
 * mc_account_new:
 * @dbus: a D-Bus daemon; may not be %NULL
 *
 * <!-- -->
 *
 * Returns: a new NMC 4.x proxy
 *
 * Since: FIXME
 */
McAccount *
mc_account_new (TpDBusDaemon *dbus)
{
    return MC_ACCOUNT (g_object_new (MC_TYPE_ACCOUNT,
					     "dbus-daemon", dbus,
					     "bus-name", MISSION_CONTROL_SERVICE,
					     "object-path", MISSION_CONTROL_PATH,
					     NULL));
}

