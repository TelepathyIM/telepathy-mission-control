/*
 * mc-account-manager.c - Telepathy Account Manager D-Bus interface
 * (client side)
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

#include "mc-account-manager.h"
#include "dbus-api.h"

#include <telepathy-glib/proxy-subclass.h>

#include "_gen/cli-Account_Manager-body.h"

/**
 * SECTION:mc-account-manager
 * @title: McAccountManager
 * @short_description: proxy object for the Telepathy AccountManager D-Bus API
 *
 * This module provides a client-side proxy object for the Telepathy
 * AccountManager D-Bus API.
 *
 * Since: FIXME
 */

/**
 * McAccountManagerClass:
 *
 * The class of a #McAccountManager.
 *
 * Since: FIXME
 */
struct _McAccountManagerClass {
    TpProxyClass parent_class;
    /*<private>*/
    gpointer priv;
};

/**
 * McAccountManager:
 *
 * A proxy object for the Telepathy AccountManager D-Bus API. This is a
 * subclass of #TpProxy.
 *
 * Since: FIXME
 */
struct _McAccountManager {
    TpProxy parent;
    /*<private>*/
    gpointer priv;
};

G_DEFINE_TYPE (McAccountManager, mc_account_manager, TP_TYPE_PROXY);

static void
mc_account_manager_init (McAccountManager *self)
{
}

static void
mc_account_manager_class_init (McAccountManagerClass *klass)
{
    GType type = MC_TYPE_ACCOUNT_MANAGER;
    TpProxyClass *proxy_class = (TpProxyClass *) klass;

    /* the API is stateless, so we can keep the same proxy across restarts */
    proxy_class->must_have_unique_name = FALSE;

    proxy_class->interface = MC_IFACE_QUARK_ACCOUNT_MANAGER;
    tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_Account_Manager_add_signals);

    tp_proxy_subclass_add_error_mapping (type, TP_ERROR_PREFIX, TP_ERRORS,
					 TP_TYPE_ERROR);
}

/**
 * mc_account_manager_new:
 * @dbus: a D-Bus daemon; may not be %NULL
 *
 * <!-- -->
 *
 * Returns: a new NMC 4.x proxy
 *
 * Since: FIXME
 */
McAccountManager *
mc_account_manager_new (TpDBusDaemon *dbus)
{
    return g_object_new (MC_TYPE_ACCOUNT_MANAGER,
			 "dbus-daemon", dbus,
			 "bus-name", MC_ACCOUNT_MANAGER_DBUS_SERVICE,
			 "object-path", MC_ACCOUNT_MANAGER_DBUS_OBJECT,
			 NULL);
}

