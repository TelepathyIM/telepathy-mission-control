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

#include <stdio.h>
#include <string.h>
#include "mc-account.h"
#include "dbus-api.h"

#include <telepathy-glib/proxy-subclass.h>

#include "_gen/cli-account-body.h"

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

struct _McAccountPrivate {
    gint useless;
};

/**
 * McAccount:
 *
 * A proxy object for the Telepathy Account D-Bus API. This is a subclass of
 * #TpProxy.
 *
 * Since: FIXME
 */

G_DEFINE_TYPE (McAccount, mc_account, TP_TYPE_PROXY);

enum
{
    PROP_0,
    PROP_OBJECT_PATH,
};

static inline gboolean
parse_object_path (McAccount *account)
{
    gchar manager[64], protocol[64], name[256];
    gchar *object_path = account->parent.object_path;
    gint n;

    n = sscanf (object_path, MC_ACCOUNT_DBUS_OBJECT_BASE "%[^/]/%[^/]/%s",
		manager, protocol, name);
    if (n != 3) return FALSE;

    account->manager_name = g_strdup (manager);
    account->protocol_name = g_strdup (protocol);
    account->name = object_path +
       	(sizeof (MC_ACCOUNT_DBUS_OBJECT_BASE) - 1);
    return TRUE;
}

static void
mc_account_init (McAccount *account)
{
    McAccountPrivate *priv;

    priv = account->priv =
       	G_TYPE_INSTANCE_GET_PRIVATE(account, MC_TYPE_ACCOUNT,
				    McAccountPrivate);

    tp_proxy_add_interface_by_id ((TpProxy *)account,
				  MC_IFACE_QUARK_ACCOUNT_INTERFACE_AVATAR);
    tp_proxy_add_interface_by_id ((TpProxy *)account,
				  MC_IFACE_QUARK_ACCOUNT_INTERFACE_COMPAT);
    tp_proxy_add_interface_by_id ((TpProxy *)account,
				  MC_IFACE_QUARK_ACCOUNT_INTERFACE_CONDITIONS);
}

static GObject *
constructor (GType type,
	     guint n_params,
	     GObjectConstructParam *params)
{
    GObjectClass *object_class = (GObjectClass *) mc_account_parent_class;
    McAccount *account;
   
    account =  MC_ACCOUNT (object_class->constructor (type, n_params, params));

    g_return_val_if_fail (account != NULL, NULL);

    if (!parse_object_path (account))
	return NULL;

    return (GObject *) account;
}

static void
finalize (GObject *object)
{
    McAccount *account = MC_ACCOUNT (object);

    g_free (account->manager_name);
    g_free (account->protocol_name);

    G_OBJECT_CLASS (mc_account_parent_class)->finalize (object);
}

static void
mc_account_class_init (McAccountClass *klass)
{
    GType type = MC_TYPE_ACCOUNT;
    GObjectClass *object_class = (GObjectClass *)klass;
    TpProxyClass *proxy_class = (TpProxyClass *)klass;

    g_type_class_add_private (object_class, sizeof (McAccountPrivate));
    object_class->constructor = constructor;
    object_class->finalize = finalize;

    /* the API is stateless, so we can keep the same proxy across restarts */
    proxy_class->must_have_unique_name = FALSE;

    _mc_ext_register_dbus_glib_marshallers ();

    proxy_class->interface = MC_IFACE_QUARK_ACCOUNT;
    tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_account_add_signals);

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
mc_account_new (TpDBusDaemon *dbus, const gchar *object_path)
{
    McAccount *account;

    account = g_object_new (MC_TYPE_ACCOUNT,
			    "dbus-daemon", dbus,
			    "bus-name", MC_ACCOUNT_MANAGER_DBUS_SERVICE,
			    "object-path", object_path,
			    NULL);
    return account;
}

