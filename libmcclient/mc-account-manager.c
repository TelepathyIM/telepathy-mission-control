/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

#define MC_INTERNAL

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
 */

/**
 * McAccountManagerClass:
 *
 * The class of a #McAccountManager.
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
 */
struct _McAccountManager {
    TpProxy parent;
    /*<private>*/
    McAccountManagerPrivate *priv;
};

struct _McAccountManagerPrivate {
    McAccountManagerProps *props;
    GPtrArray *account_ifaces;
    GHashTable *accounts;
};

G_DEFINE_TYPE (McAccountManager, mc_account_manager, TP_TYPE_PROXY);

typedef struct {
    McAccountManagerWhenReadyObjectCb callback;
    gpointer user_data;
    GDestroyNotify destroy;
    GError *error;
    McAccountManager *manager;
    gint ref_count;
    gint cb_remaining;
} ReadyWithAccountsData;

enum
{
    ACCOUNT_CREATED,
    ACCOUNT_READY,
    LAST_SIGNAL
};

guint _mc_account_manager_signals[LAST_SIGNAL] = { 0 };

static McAccountManager *account_manager_singleton = NULL;

static void create_props (TpProxy *proxy, GHashTable *props);
static void setup_props_monitor (TpProxy *proxy, GQuark interface);

static McIfaceDescription iface_description = {
    G_STRUCT_OFFSET (McAccountManagerPrivate, props),
    create_props,
    setup_props_monitor,
};


static void
on_account_invalidated (McAccount *account,
			guint domain, gint code, gchar *message,
			McAccountManager *manager)
{
    g_hash_table_remove (manager->priv->accounts, account->name);
}

static void
account_cache_remove (gpointer ptr)
{
    McAccount *account = MC_ACCOUNT (ptr);

    g_signal_handlers_disconnect_matched (account, G_SIGNAL_MATCH_FUNC,
					  0, 0, NULL,
					  on_account_invalidated, NULL);
    g_object_unref (account);
}

static void
ready_with_accounts_data_unref (gpointer ptr)
{
    ReadyWithAccountsData *cb_data = ptr;

    cb_data->ref_count--;
    g_assert (cb_data->ref_count >= 0);
    if (cb_data->ref_count == 0)
    {
	if (cb_data->destroy)
	    cb_data->destroy (cb_data->user_data);
	if (cb_data->error)
	    g_error_free (cb_data->error);
	g_slice_free (ReadyWithAccountsData, cb_data);
    }
}

static void
account_ready_cb (TpProxy *proxy, const GError *error,
		  gpointer user_data, GObject *weak_object)
{
    ReadyWithAccountsData *cb_data = user_data;

    if (error)
    {
	if (cb_data->error == NULL)
	    cb_data->error = g_error_copy (error);
    }
    cb_data->cb_remaining--;
    if (cb_data->cb_remaining == 0)
    {
	if (cb_data->callback)
	    cb_data->callback (cb_data->manager, error, cb_data->user_data,
			       weak_object);
    }
}

static void
get_accounts_ready (McAccountManager *manager, gchar **accounts,
		    ReadyWithAccountsData *cb_data, GObject *weak_object)
{
    McAccountManagerPrivate *priv = manager->priv;
    GQuark *ifaces;
    guint i, n_ifaces;

    ifaces = (GQuark *)priv->account_ifaces->pdata;
    n_ifaces = priv->account_ifaces->len;

    for (i = 0; accounts[i] != NULL; i++)
    {
	McAccount *account;

	account = mc_account_manager_get_account (manager, accounts[i]);
	cb_data->ref_count++;
	cb_data->cb_remaining++;
	_mc_iface_call_when_all_readyv (TP_PROXY (account), MC_TYPE_ACCOUNT,
					account_ready_cb, cb_data,
					ready_with_accounts_data_unref,
					weak_object,
					n_ifaces, ifaces);
    }
}

static void
manager_ready_cb (McAccountManager *manager, const GError *error,
		  gpointer user_data, GObject *weak_object)
{
    McAccountManagerPrivate *priv = manager->priv;
    ReadyWithAccountsData *cb_data = user_data;

    if (error)
    {
	if (cb_data->callback)
	    cb_data->callback (manager, error, cb_data->user_data,
			       weak_object);
	return;
    }

    /* now we have the account names; create all accounts and get them ready */
    get_accounts_ready (manager, priv->props->valid_accounts,
			cb_data, weak_object);
    get_accounts_ready (manager, priv->props->invalid_accounts,
			cb_data, weak_object);
    cb_data->cb_remaining--;
    if (cb_data->cb_remaining == 0)
    {
	/* either we have no accounts, or they were all ready; in any case, we
	 * can invoke the callback now */
	if (cb_data->callback)
	    cb_data->callback (manager, error, cb_data->user_data,
			       weak_object);
    }
}

static void
mc_account_manager_init (McAccountManager *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, MC_TYPE_ACCOUNT_MANAGER,
					     McAccountManagerPrivate);

    tp_proxy_add_interface_by_id ((TpProxy *)self,
	MC_IFACE_QUARK_ACCOUNT_MANAGER_INTERFACE_QUERY);
}

static inline void
manager_props_free (McAccountManagerProps *props)
{
    g_strfreev (props->valid_accounts);
    g_strfreev (props->invalid_accounts);
    g_free (props);
}

static GObject *
constructor (GType type, guint n_params, GObjectConstructParam *params)
{
    GObject *object;

    if (!account_manager_singleton)
    {
	object = G_OBJECT_CLASS (mc_account_manager_parent_class)->constructor
	    (type, n_params, params);
	account_manager_singleton = MC_ACCOUNT_MANAGER (object);
        g_object_add_weak_pointer (object,
                                   (gpointer) &account_manager_singleton);
    }
    else
	object = g_object_ref (account_manager_singleton);

    return object;
}

static void
dispose (GObject *object)
{
    McAccountManagerPrivate *priv = MC_ACCOUNT_MANAGER (object)->priv;

    if (priv->accounts)
    {
	g_hash_table_destroy (priv->accounts);
	priv->accounts = NULL;
    }

    G_OBJECT_CLASS (mc_account_manager_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    McAccountManagerPrivate *priv = MC_ACCOUNT_MANAGER (object)->priv;

    if (priv->props)
	manager_props_free (priv->props);

    if (priv->account_ifaces)
	g_ptr_array_free (priv->account_ifaces, TRUE);

    G_OBJECT_CLASS (mc_account_manager_parent_class)->finalize (object);
}

static void
mc_account_manager_class_init (McAccountManagerClass *klass)
{
    GType type = MC_TYPE_ACCOUNT_MANAGER;
    GObjectClass *object_class = (GObjectClass *)klass;
    TpProxyClass *proxy_class = (TpProxyClass *) klass;

    g_type_class_add_private (object_class, sizeof (McAccountManagerPrivate));
    object_class->constructor = constructor;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    /* the API is stateless, so we can keep the same proxy across restarts */
    proxy_class->must_have_unique_name = FALSE;

    _mc_ext_register_dbus_glib_marshallers ();

    proxy_class->interface = MC_IFACE_QUARK_ACCOUNT_MANAGER;
    tp_proxy_init_known_interfaces ();
    tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_account_manager_add_signals);

    tp_proxy_subclass_add_error_mapping (type, TP_ERROR_PREFIX, TP_ERRORS,
					 TP_TYPE_ERROR);

    /**
     * McAccountManager::account-created:
     * @account_manager: the #McAccountManager.
     * @object_path: the path to the DBus Account object.
     * @valid: %TRUE if this is a valid account.
     *
     * Emitted when a new account is created.
     *
     * This signal will be emitted only once
     * mc_account_manager_call_when_ready() has been successfully invoked.
     */
    _mc_account_manager_signals[ACCOUNT_CREATED] =
	g_signal_new ("account-created",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST,
		      0,
		      NULL, NULL,
		      mc_signals_marshal_VOID__STRING_BOOLEAN,
		      G_TYPE_NONE,
		      2, G_TYPE_STRING, G_TYPE_BOOLEAN);

    /**
     * McAccountManager::account-ready:
     * @account_manager: the #McAccountManager.
     * @account: the #McAccount that became ready.
     *
     * Emitted when a new account has appeared on the D-Bus and all the
     * requested interfaces (see
     * mc_account_manager_call_when_ready_with_accounts()) have become ready.
     *
     * Clients should connect to this signal only after
     * mc_account_manager_call_when_ready_with_accounts() has called the
     * callback function.
     *
     * In the unlikely case that this signal is emitted several times for the
     * same account, clients should ignore all but the first emission.
     */
    _mc_account_manager_signals[ACCOUNT_READY] =
	g_signal_new ("account-ready",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST,
		      0,
		      NULL, NULL,
		      g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE,
		      1, MC_TYPE_ACCOUNT);

    _mc_iface_add (MC_TYPE_ACCOUNT_MANAGER, MC_IFACE_QUARK_ACCOUNT_MANAGER,
		   &iface_description);
}

/**
 * mc_account_manager_new:
 * @dbus: a D-Bus daemon; may not be %NULL
 *
 * Creates a proxy for the DBus AccountManager Telepathy object.
 *
 * Returns: a new #McAccountManager object.
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
    static GType ao_type = G_TYPE_INVALID;
    McAccountManager *manager = user_data;
    McAccountManagerProps *props = manager->priv->props;
    GValue *value = ht_value;
    const gchar *name = key;

    if (G_UNLIKELY (ao_type == G_TYPE_INVALID))
        ao_type = dbus_g_type_get_collection ("GPtrArray",
                                              DBUS_TYPE_G_OBJECT_PATH);

    if (strcmp (name, "ValidAccounts") == 0 &&
        G_VALUE_HOLDS (value, ao_type))
    {
        GPtrArray *contents = g_value_get_boxed (value);

        _mc_gvalue_stolen (value);
        g_strfreev (props->valid_accounts);
        g_ptr_array_add (contents, NULL);
        props->valid_accounts = (gchar **) g_ptr_array_free (contents, FALSE);
    }
    else if (strcmp (name, "InvalidAccounts") == 0 &&
             G_VALUE_HOLDS (value, ao_type))
    {
        GPtrArray *contents = g_value_get_boxed (value);

        _mc_gvalue_stolen (value);
        g_strfreev (props->invalid_accounts);
        g_ptr_array_add (contents, NULL);
        props->invalid_accounts = (gchar **) g_ptr_array_free (contents, FALSE);
    }
}

static void
create_props (TpProxy *proxy, GHashTable *props)
{
    McAccountManager *manager = MC_ACCOUNT_MANAGER (proxy);

    manager->priv->props = g_malloc0 (sizeof (McAccountManagerProps));
    g_hash_table_foreach (props, update_property, manager);
}

static gboolean
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
	    return TRUE;
	}
    }
    return FALSE;
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
new_account_ready_cb (TpProxy *proxy, const GError *error,
		      gpointer user_data, GObject *weak_object)
{
    McAccountManager *manager = MC_ACCOUNT_MANAGER (weak_object);
    McAccount *account = MC_ACCOUNT (proxy);

    if (error)
    {
	g_warning ("Error retrieving properties for %s: %s",
		   account->name, error->message);
	return;
    }

    g_signal_emit (manager,
		   _mc_account_manager_signals[ACCOUNT_READY], 0,
		   account);
}

static void
on_account_validity_changed (TpProxy *proxy, const gchar *account_path,
			     gboolean valid, gpointer user_data,
			     GObject *weak_object)
{
    McAccountManager *manager = MC_ACCOUNT_MANAGER (proxy);
    McAccountManagerPrivate *priv = manager->priv;
    McAccountManagerProps *props = priv->props;
    gboolean existed;

    if (G_UNLIKELY (!props)) return;
    /* update the lists */
    if (valid)
    {
	existed = account_remove (account_path, &props->invalid_accounts);
	account_add (account_path, &props->valid_accounts);
    }
    else
    {
	existed = account_remove (account_path, &props->valid_accounts);
	account_add (account_path, &props->invalid_accounts);
    }

    if (!existed)
    {
	g_signal_emit (manager,
		       _mc_account_manager_signals[ACCOUNT_CREATED], 0,
		       account_path, valid);
	if (priv->account_ifaces)
	{
	    McAccount *account;
	    GQuark *ifaces;
	    guint n_ifaces;

	    ifaces = (GQuark *)priv->account_ifaces->pdata;
	    n_ifaces = priv->account_ifaces->len;

	    account = mc_account_manager_get_account (manager, account_path);
	    _mc_iface_call_when_all_readyv (TP_PROXY (account), MC_TYPE_ACCOUNT,
					    new_account_ready_cb, NULL, NULL,
					    (GObject *)manager,
					    n_ifaces, ifaces);
	}
    }
}

static void
on_account_removed (TpProxy *proxy, const gchar *account_path,
		    gpointer user_data, GObject *weak_object)
{
    McAccountManager *manager = MC_ACCOUNT_MANAGER (proxy);
    McAccountManagerProps *props = manager->priv->props;

    if (G_UNLIKELY (!props)) return;

    account_remove (account_path, &props->valid_accounts);
    account_remove (account_path, &props->invalid_accounts);
}

static void
setup_props_monitor (TpProxy *proxy, GQuark interface)
{
    McAccountManager *manager = MC_ACCOUNT_MANAGER (proxy);

    mc_cli_account_manager_connect_to_account_validity_changed (manager,
					    on_account_validity_changed,
					    NULL, NULL, NULL, NULL);
    mc_cli_account_manager_connect_to_account_removed (manager,
						       on_account_removed,
						       NULL, NULL,
						       NULL, NULL);
}

/**
 * McAccountManagerWhenReadyCb:
 * @manager: the #McAccountManager.
 * @error: %NULL if the interface is ready for use, or the error with which it
 * was invalidated if it is now invalid.
 * @user_data: the user data that was passed to
 * mc_account_manager_call_when_ready().
 */

/**
 * mc_account_manager_call_when_ready:
 * @manager: the #McAccountManager.
 * @callback: called when the interface becomes ready or invalidated, whichever
 * happens first.
 * @user_data: user data to be passed to @callback.
 *
 * Start retrieving and monitoring the properties of the base interface of
 * @manager. If they have already been retrieved, call @callback immediately,
 * then return. Otherwise, @callback will be called when the properties are
 * ready.
 */
void
mc_account_manager_call_when_ready (McAccountManager *manager,
				    McAccountManagerWhenReadyCb callback,
				    gpointer user_data)
{
    McIfaceData iface_data;

    iface_data.id = MC_IFACE_QUARK_ACCOUNT_MANAGER;
    iface_data.props_data_ptr = (gpointer)&manager->priv->props;
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

/**
 * mc_account_manager_get_valid_accounts:
 * @manager: the #McAccountManager.
 *
 * Returns: a non-modifyable array of strings representing the DBus object
 * paths to the valid accounts.
 * mc_account_manager_call_when_ready() must have been successfully invoked
 * prior to calling this function.
 */
const gchar * const *
mc_account_manager_get_valid_accounts (McAccountManager *manager)
{
    McAccountManagerProps *props;

    g_return_val_if_fail (MC_IS_ACCOUNT_MANAGER (manager), NULL);
    props = manager->priv->props;
    if (G_UNLIKELY (!props)) return NULL;
    return (const gchar * const *)props->valid_accounts;
}

/**
 * mc_account_manager_get_invalid_accounts:
 * @manager: the #McAccountManager.
 *
 * Returns: a non-modifyable array of strings representing the DBus object
 * paths to the invalid accounts.
 * mc_account_manager_call_when_ready() must have been successfully invoked
 * prior to calling this function.
 */
const gchar * const *
mc_account_manager_get_invalid_accounts (McAccountManager *manager)
{
    McAccountManagerProps *props;

    g_return_val_if_fail (MC_IS_ACCOUNT_MANAGER (manager), NULL);
    props = manager->priv->props;
    if (G_UNLIKELY (!props)) return NULL;
    return (const gchar * const *)props->invalid_accounts;
}

/**
 * McAccountManagerWhenReadyObjectCb:
 * @manager: the #McAccountManager.
 * @error: %NULL if the interface is ready for use, or the error with which it
 * was invalidated if it is now invalid.
 * @user_data: the user data that was passed to
 * mc_account_call_when_iface_ready() or mc_account_call_when_all_ready().
 * @weak_object: the #GObject that was passed to
 * mc_account_call_when_iface_ready() or mc_account_call_when_all_ready().
 */

/**
 * mc_account_manager_call_when_iface_ready:
 * @manager: the #McAccountManager.
 * @interface: a #GQuark representing the interface to process.
 * @callback: called when the interface becomes ready or invalidated, whichever
 * happens first.
 * @user_data: user data to be passed to @callback.
 * @destroy: called with the user_data as argument, after the call has
 * succeeded, failed or been cancelled.
 * @weak_object: If not %NULL, a #GObject which will be weakly referenced; if
 * it is destroyed, this call will automatically be cancelled. Must be %NULL if
 * @callback is %NULL
 *
 * Start retrieving and monitoring the properties of the interface @interface
 * of @account. If they have already been retrieved, call @callback
 * immediately, then return. Otherwise, @callback will be called when the
 * properties are ready.
 */
void
mc_account_manager_call_when_iface_ready (McAccountManager *manager,
				    GQuark interface,
				    McAccountManagerWhenReadyObjectCb callback,
				    gpointer user_data,
				    GDestroyNotify destroy,
				    GObject *weak_object)
{
    _mc_iface_call_when_ready ((TpProxy *)manager,
			       MC_TYPE_ACCOUNT_MANAGER,
			       interface,
			       (McIfaceWhenReadyCb)callback,
			       user_data, destroy, weak_object);
}

/**
 * mc_account_manager_call_when_ready_with_accounts:
 * @manager: the #McAccountManager.
 * @callback: called when the account manager and all the accounts are ready,
 * or some error occurs.
 * @user_data: user data to be passed to @callback.
 * @destroy: called with the user_data as argument, after the call has
 * succeeded, failed or been cancelled.
 * @weak_object: If not %NULL, a #GObject which will be weakly referenced; if
 * it is destroyed, this call will automatically be cancelled. Must be %NULL if
 * @callback is %NULL
 * @Varargs: a list of #GQuark types representing the account interfaces to
 * process, followed by %0.
 *
 * This is a convenience function that waits for the account manager to be
 * ready, after which it requests the desired interfaces out of all accounts
 * and returns only once they are all ready.
 * After this function has been called, all new accounts that should appear
 * will have their desired interfaces retrieved, and the "account-ready" signal
 * will be emitted.
 */
void
mc_account_manager_call_when_ready_with_accounts (McAccountManager *manager,
				    McAccountManagerWhenReadyObjectCb callback,
				    gpointer user_data,
				    GDestroyNotify destroy,
				    GObject *weak_object, ...)
{
    McAccountManagerPrivate *priv;
    GQuark iface;
    guint len, i;
    ReadyWithAccountsData *cb_data;
    va_list ifaces;

    g_return_if_fail (MC_IS_ACCOUNT_MANAGER (manager));
    priv = manager->priv;

    /* Add the requested interfaces to the account_ifaces array */
    va_start (ifaces, weak_object);

    if (!priv->account_ifaces)
	priv->account_ifaces = g_ptr_array_sized_new (8);

    len = priv->account_ifaces->len;
    for (iface = va_arg (ifaces, GQuark); iface != 0;
	 iface = va_arg (ifaces, GQuark))
    {
	for (i = 0; i < len; i++)
	{
	    gpointer ptr = g_ptr_array_index (priv->account_ifaces, i);

	    if ((GQuark)GPOINTER_TO_UINT (ptr) == iface)
		break;
	}
	if (i < len) /* found */
	    continue;

	g_ptr_array_add (priv->account_ifaces, GUINT_TO_POINTER(iface));
    }
    va_end (ifaces);

    /* Get the account manager ready; in the callback, we'll make the accounts
     * ready. */
    cb_data = g_slice_new0 (ReadyWithAccountsData);
    cb_data->callback = callback;
    cb_data->user_data = user_data;
    cb_data->destroy = destroy;
    cb_data->manager = manager;
    cb_data->ref_count = 1;
    cb_data->cb_remaining = 1;

    mc_account_manager_call_when_iface_ready (manager,
					      MC_IFACE_QUARK_ACCOUNT_MANAGER,
					      manager_ready_cb,
					      cb_data,
					      ready_with_accounts_data_unref,
					      weak_object);
}

/**
 * mc_account_manager_get_account:
 * @manager: the #McAccountManager.
 * @account_name: the name of a #McAccount, or an object path.
 *
 * Get the #McAccount for the account whose name is @account_name. It looks up
 * the accounts from an internal cache, and if the #McAccount is not found
 * there it is created, provided that the @account_name refers to a proper
 * account.
 *
 * Returns: a #McAccount, not referenced. %NULL if @account_name does not match
 * any account.
 */
McAccount *
mc_account_manager_get_account (McAccountManager *manager,
				const gchar *account_name)
{
    McAccountManagerPrivate *priv;
    McAccount *account;
    const gchar *object_path, *name;

    g_return_val_if_fail (MC_IS_ACCOUNT_MANAGER (manager), NULL);
    g_return_val_if_fail (account_name != NULL, NULL);

    priv = manager->priv;
    if (G_UNLIKELY (!priv->accounts))
    {
	priv->accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
						NULL, account_cache_remove);
    }
    g_return_val_if_fail (priv->accounts != NULL, NULL);

    /* @account_name can be an account name or an object path; now we need the
     * name */
    if (strncmp (account_name, MC_ACCOUNT_DBUS_OBJECT_BASE,
		 MC_ACCOUNT_DBUS_OBJECT_BASE_LEN) == 0)
    {
	object_path = account_name;
	name = account_name + MC_ACCOUNT_DBUS_OBJECT_BASE_LEN;
    }
    else
    {
	object_path = NULL;
	name = account_name;
    }

    account = g_hash_table_lookup (priv->accounts, name);
    if (!account)
    {
	if (!object_path)
	    object_path = g_strconcat (MC_ACCOUNT_DBUS_OBJECT_BASE,
				       account_name, NULL);
	account = mc_account_new (TP_PROXY(manager)->dbus_daemon,
				  object_path);
	if (G_LIKELY (account))
	{
	    g_hash_table_insert (priv->accounts, account->name, account);
	    g_signal_connect (account, "invalidated",
			      G_CALLBACK (on_account_invalidated), manager);
	}
	if (object_path != account_name)
	    g_free ((gchar *)object_path);
    }
    return account;
}

/**
 * McAccountFilterFunc:
 * @account: a #McAccount.
 * @user_data: the user data that was passed to
 * mc_account_manager_list_accounts().
 *
 * Returns: %TRUE if @account must be listed, %FALSE otherwise.
 */

/**
 * mc_account_manager_list_accounts:
 * @manager: the #McAccountManager.
 * @filter: a function for filtering accounts, or %NULL.
 * @user_data: user data to be supplied to the filtering callback.
 *
 * List all accounts known by the @manager. For this function to be really
 * useful, you first need to have waited for
 * mc_account_manager_call_when_ready_with_accounts() to succeed, or it will
 * only return those accounts for which mc_account_manager_get_account() has
 * been called.
 *
 * Returns: a #GList of #McAccount objects; to be free'd with g_list_free().
 */
GList *
mc_account_manager_list_accounts (McAccountManager *manager,
				  McAccountFilterFunc filter,
				  gpointer user_data)
{
    GHashTable *accounts_ht;
    GHashTableIter iter;
    McAccount *account;
    GList *list = NULL;

    g_return_val_if_fail (MC_IS_ACCOUNT_MANAGER (manager), NULL);
    accounts_ht = manager->priv->accounts;
    if (G_UNLIKELY (accounts_ht == NULL)) return NULL;

    g_hash_table_iter_init (&iter, accounts_ht);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer)&account))
    {
	if (filter && !filter (account, user_data))
	    continue;

	list = g_list_prepend (list, account);
    }

    return g_list_reverse (list);
}

