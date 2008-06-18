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

#include <string.h>
#include "mc-account-manager.h"
#include "dbus-api.h"
#include "mc-signals-marshal.h"

#include <telepathy-glib/proxy-subclass.h>

#include "_gen/cli-account-manager-body.h"
#include "_gen/signals-marshal.h"
#include "_gen/register-dbus-glib-marshallers-body.h"

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

typedef struct _McAccountManagerProps {
    gchar **valid_accounts;
    gchar **invalid_accounts;
} McAccountManagerProps;

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
    McAccountManagerProps *props;
};

G_DEFINE_TYPE (McAccountManager, mc_account_manager, TP_TYPE_PROXY);

static void
mc_account_manager_init (McAccountManager *self)
{
    tp_proxy_add_interface_by_id ((TpProxy *)self,
	MC_IFACE_QUARK_ACCOUNT_MANAGER_INTERFACE_QUERY);
}

static void
mc_account_manager_class_init (McAccountManagerClass *klass)
{
    GType type = MC_TYPE_ACCOUNT_MANAGER;
    TpProxyClass *proxy_class = (TpProxyClass *) klass;

    /* the API is stateless, so we can keep the same proxy across restarts */
    proxy_class->must_have_unique_name = FALSE;

    _mc_ext_register_dbus_glib_marshallers ();

    proxy_class->interface = MC_IFACE_QUARK_ACCOUNT_MANAGER;
    tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_account_manager_add_signals);

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

static void
update_property (gpointer key, gpointer ht_value, gpointer user_data)
{
    McAccountManager *manager = user_data;
    McAccountManagerProps *props = manager->props;
    GValue *value = ht_value;
    const gchar *name = key;

    if (strcmp (name, "ValidAccounts") == 0)
    {
	g_strfreev (props->valid_accounts);
	props->valid_accounts = g_value_get_boxed (value);
	_mc_gvalue_stolen (value);
    }
    else if (strcmp (name, "InvalidAccounts") == 0)
    {
	g_strfreev (props->invalid_accounts);
	props->invalid_accounts = g_value_get_boxed (value);
	_mc_gvalue_stolen (value);
    }
}

static void
create_props (TpProxy *proxy, GHashTable *props)
{
    McAccountManager *manager = MC_ACCOUNT_MANAGER (proxy);

    manager->props = g_malloc0 (sizeof (McAccountManagerProps));
    g_hash_table_foreach (props, update_property, manager);
}

static void
account_remove (const gchar *account_path, gchar ***src)
{
    gchar **ap, **new_list, **ap2;
    gsize len;

    /* remove the account from the src list (it could be not there) */
    if (*src)
    {
	gchar **to_remove = NULL;

	for (ap = *src, len = 0; *ap != NULL; ap++, len++)
	{
	    if (strcmp (*ap, account_path) == 0)
		to_remove = ap;
	}
	if (to_remove)
	{ /* found, let's delete it */
	    ap2 = new_list = g_new (gchar *, len);
	    for (ap = *src; *ap != NULL; ap++)
	    {
		if (strcmp (*ap, account_path) != 0)
		{
		    *ap2 = *ap;
		    ap2++;
		}
		else
		    g_free (*ap);
	    }
	    *ap2 = NULL;
	    g_free (*src);
	    *src = new_list;
	}
    }
}

static void
account_add (const gchar *account_path, gchar ***dst)
{
    gchar **ap, **new_list;
    gchar *account_path_dup;
    gsize len;

    account_path_dup = g_strdup (account_path);
    if (!*dst)
    {
	*dst = g_new (gchar *, 2);
	(*dst)[0] = account_path_dup;
	(*dst)[1] = NULL;
    }
    else
    {
	for (ap = *dst, len = 0; *ap != NULL; ap++, len++);
	new_list = g_new (gchar *, len + 2);
	memcpy (new_list, *dst, sizeof (gchar *) * len);
	new_list[len] = account_path_dup;
	new_list[len + 1] = NULL;
	g_free (*dst);
	*dst = new_list;
    }
}

static void
on_account_validity_changed (TpProxy *proxy, const gchar *account_path,
			     gboolean valid, gpointer user_data,
			     GObject *weak_object)
{
    McAccountManager *manager = MC_ACCOUNT_MANAGER (proxy);
    McAccountManagerProps *props = manager->props;

    if (G_UNLIKELY (!props)) return;
    /* update the lists */
    if (valid)
    {
	account_remove (account_path, &props->invalid_accounts);
	account_add (account_path, &props->valid_accounts);
    }
    else
    {
	account_remove (account_path, &props->valid_accounts);
	account_add (account_path, &props->invalid_accounts);
    }
}

static void
on_account_removed (TpProxy *proxy, const gchar *account_path,
		    gpointer user_data, GObject *weak_object)
{
    McAccountManager *manager = MC_ACCOUNT_MANAGER (proxy);
    McAccountManagerProps *props = manager->props;

    if (G_UNLIKELY (!props)) return;

    account_remove (account_path, &props->valid_accounts);
    account_remove (account_path, &props->invalid_accounts);
}

void
mc_account_manager_call_when_ready (McAccountManager *manager,
				    McAccountManagerWhenReadyCb callback,
				    gpointer user_data)
{
    McIfaceData iface_data;

    iface_data.id = MC_IFACE_QUARK_ACCOUNT_MANAGER;
    iface_data.props_data_ptr = (gpointer)&manager->props;
    iface_data.create_props = create_props;

    if (_mc_iface_call_when_ready_int ((TpProxy *)manager,
				       (McIfaceWhenReadyCb)callback, user_data,
				       &iface_data))
    {
	mc_cli_account_manager_connect_to_account_validity_changed (manager,
						on_account_validity_changed,
						NULL, NULL, NULL, NULL);
	mc_cli_account_manager_connect_to_account_removed (manager,
							   on_account_removed,
							   NULL, NULL,
							   NULL, NULL);
    }
}

const gchar * const *
mc_account_manager_get_valid_accounts (McAccountManager *manager)
{
    g_return_val_if_fail (MC_IS_ACCOUNT_MANAGER (manager), NULL);
    if (G_UNLIKELY (!manager->props)) return NULL;
    return (const gchar * const *)manager->props->valid_accounts;
}

const gchar * const *
mc_account_manager_get_invalid_accounts (McAccountManager *manager)
{
    g_return_val_if_fail (MC_IS_ACCOUNT_MANAGER (manager), NULL);
    if (G_UNLIKELY (!manager->props)) return NULL;
    return (const gchar * const *)manager->props->invalid_accounts;
}

