/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
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

/**
 * SECTION:mcd-master
 * @title: McdMaster
 * @short_description: Server master class
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-master.h
 * 
 * This class implements actual mission-control. It keeps track of
 * individual account presence and connection states in a McdPresenceFrame
 * member object, which is available as a property.
 * 
 * The McdPresenceFrame object could be easily utilized for
 * any presence releated events and actions, either within this class or
 * any other class subclassing it or using it.
 *
 * It is basically a container for all McdManager objects and
 * takes care of their management. It also takes care of sleep and awake
 * cycles (e.g. translates to auto away somewhere down the hierarchy).
 *
 * McdMaster is a subclass of McdConroller, which essentially means it
 * is subject to all device control.
 */

#include <config.h>

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include <glib/gi18n.h>
#include <gmodule.h>
#include <string.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "mcd-master.h"
#include "mcd-master-priv.h"
#include "mcd-proxy.h"
#include "mcd-manager.h"
#include "mcd-dispatcher.h"
#include "mcd-account-manager.h"
#include "mcd-account-manager-priv.h"
#include "mcd-account-conditions.h"
#include "mcd-account-compat.h"
#include "mcd-account-priv.h"
#include "mcd-plugin.h"
#include "mcd-transport.h"

#include <libmcclient/mc-errors.h>

#define MCD_MASTER_PRIV(master) (G_TYPE_INSTANCE_GET_PRIVATE ((master), \
				  MCD_TYPE_MASTER, \
				  McdMasterPrivate))

G_DEFINE_TYPE (McdMaster, mcd_master, MCD_TYPE_CONTROLLER);

typedef struct _McdMasterPrivate
{
    McdAccountManager *account_manager;
    McdDispatcher *dispatcher;
    McdProxy *proxy;

    /* We create this for our member objects */
    TpDBusDaemon *dbus_daemon;

    GHashTable *extra_parameters;

    GPtrArray *plugins;
    GPtrArray *transport_plugins;
    GList *account_connections;

    gboolean is_disposed;
    gboolean low_memory;
    gboolean idle;
} McdMasterPrivate;

enum
{
    PROP_0,
    PROP_PRESENCE_FRAME,
    PROP_DBUS_CONNECTION,
    PROP_DBUS_DAEMON,
    PROP_DISPATCHER,
    PROP_ACCOUNT_MANAGER,
};

typedef struct {
    gint priority;
    McdAccountConnectionFunc func;
    gpointer userdata;
} McdAccountConnectionData;

static McdMaster *default_master = NULL;


static void
mcd_master_transport_connected (McdMaster *master, McdTransportPlugin *plugin,
				McdTransport *transport)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GHashTable *accounts;
    GHashTableIter iter;
    gpointer v;

    DEBUG ("%s", mcd_transport_get_name (plugin, transport));

    accounts = _mcd_account_manager_get_accounts (priv->account_manager);
    g_hash_table_iter_init (&iter, accounts);

    while (g_hash_table_iter_next (&iter, NULL, &v))
    {
        McdAccount *account = MCD_ACCOUNT (v);
        GHashTable *conditions;

        /* get all enabled accounts, which have the "ConnectAutomatically"
         * flag set and that are not connected */
        if (!mcd_account_is_valid (account) ||
            !mcd_account_is_enabled (account) ||
            !mcd_account_get_connect_automatically (account) ||
            mcd_account_get_connection_status (account) ==
            TP_CONNECTION_STATUS_CONNECTED)
            continue;

        DEBUG ("account %s would like to connect",
               mcd_account_get_unique_name (account));
        conditions = mcd_account_get_conditions (account);
        if (mcd_transport_plugin_check_conditions (plugin, transport,
                                                   conditions))
        {
            DEBUG ("conditions matched");
            _mcd_account_connect_with_auto_presence (account);
            if (g_hash_table_size (conditions) > 0)
                mcd_account_connection_bind_transport (account, transport);
        }
        g_hash_table_unref (conditions);
    }
}

static void
mcd_master_transport_disconnected (McdMaster *master, McdTransportPlugin *plugin,
				   McdTransport *transport)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GHashTable *accounts;
    GHashTableIter iter;
    gpointer v;

    DEBUG ("%s", mcd_transport_get_name (plugin, transport));

    accounts = _mcd_account_manager_get_accounts (priv->account_manager);
    g_hash_table_iter_init (&iter, accounts);

    while (g_hash_table_iter_next (&iter, NULL, &v))
    {
        McdAccount *account = MCD_ACCOUNT (v);

        if (transport == _mcd_account_connection_get_transport (account))
        {
            McdConnection *connection;

            DEBUG ("account %s must disconnect",
                   mcd_account_get_unique_name (account));
            connection = mcd_account_get_connection (account);
            if (connection)
                mcd_connection_close (connection);
            mcd_account_connection_bind_transport (account, NULL);

            /* it may be that there is another transport to which the account
             * can reconnect */
            if (_mcd_master_account_replace_transport (master, account))
            {
                DEBUG ("conditions matched");
                _mcd_account_connect_with_auto_presence (account);
            }

        }
    }
}

static void
mcd_master_connect_automatic_accounts (McdMaster *master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GHashTable *accounts;
    GHashTableIter iter;
    gpointer ht_key, ht_value;

    accounts = _mcd_account_manager_get_accounts (priv->account_manager);
    g_hash_table_iter_init (&iter, accounts);
    while (g_hash_table_iter_next (&iter, &ht_key, &ht_value))
    {
        _mcd_account_maybe_autoconnect (ht_value);
    }
}

static void
on_transport_status_changed (McdTransportPlugin *plugin,
			     McdTransport *transport,
			     McdTransportStatus status, McdMaster *master)
{
    DEBUG ("Transport %s changed status to %u",
           mcd_transport_get_name (plugin, transport), status);

    if (status == MCD_TRANSPORT_STATUS_CONNECTED)
	mcd_master_transport_connected (master, plugin, transport);
    else if (status == MCD_TRANSPORT_STATUS_DISCONNECTING ||
	     status == MCD_TRANSPORT_STATUS_DISCONNECTED)
    {
	/* disconnect all accounts that were using this transport */
	mcd_master_transport_disconnected (master, plugin, transport);
    }
}

#ifdef ENABLE_PLUGINS
static void
mcd_master_unload_plugins (McdMaster *master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GModule *module;
    guint i;

    for (i = 0; i < priv->plugins->len; i++)
    {
	module = g_ptr_array_index (priv->plugins, i);
	g_module_close (module);
    }
    g_ptr_array_free (priv->plugins, TRUE);
    priv->plugins = NULL;
}

static const gchar *
mcd_master_get_plugin_dir (void)
{
    const gchar *dir = g_getenv ("MC_FILTER_PLUGIN_DIR");

    if (dir == NULL)
        dir = MCD_DEFAULT_FILTER_PLUGIN_DIR;

    return dir;
}

static void
mcd_master_load_plugins (McdMaster *master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    const gchar *plugin_dir;
    GDir *dir = NULL;
    GError *error = NULL;
    const gchar *name;

    plugin_dir = mcd_master_get_plugin_dir ();

    dir = g_dir_open (plugin_dir, 0, &error);
    if (!dir)
    {
        DEBUG ("Could not open plugin directory %s: %s", plugin_dir,
               error->message);
	g_error_free (error);
	return;
    }

    DEBUG ("Looking for plugins in %s", plugin_dir);

    priv->plugins = g_ptr_array_new ();
    while ((name = g_dir_read_name (dir)))
    {
	GModule *module;
	gchar *path;

	if (name[0] == '.' || !g_str_has_suffix (name, ".so")) continue;

	path = g_build_filename (plugin_dir, name, NULL);
	module = g_module_open (path, 0);
	g_free (path);
	if (module)
	{
	    McdPluginInitFunc init_func;
	    if (g_module_symbol (module, MCD_PLUGIN_INIT_FUNC,
				 (gpointer)&init_func))
	    {
                DEBUG ("Initializing plugin %s", name);
		init_func ((McdPlugin *)master);
		g_ptr_array_add (priv->plugins, module);
	    }
	    else
                DEBUG ("Error looking up symbol " MCD_PLUGIN_INIT_FUNC
                       " from plugin %s: %s", name, g_module_error ());
	}
	else
	{
            DEBUG ("Error opening plugin: %s: %s", name, g_module_error ());
	}
    }
    g_dir_close (dir);
}
#endif

static void
_mcd_master_finalize (GObject * object)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (object);

    g_list_foreach (priv->account_connections, (GFunc)g_free, NULL);
    g_list_free (priv->account_connections);

    g_hash_table_destroy (priv->extra_parameters);

    G_OBJECT_CLASS (mcd_master_parent_class)->finalize (object);
}

static void
_mcd_master_get_property (GObject * obj, guint prop_id,
			  GValue * val, GParamSpec * pspec)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (obj);

    switch (prop_id)
    {
    case PROP_DISPATCHER:
	g_value_set_object (val, priv->dispatcher);
	break;
    case PROP_DBUS_DAEMON:
	g_value_set_object (val, priv->dbus_daemon);
	break;
    case PROP_DBUS_CONNECTION:
	g_value_set_pointer (val,
			     TP_PROXY (priv->dbus_daemon)->dbus_connection);
	break;
    case PROP_ACCOUNT_MANAGER:
	g_value_set_object (val, priv->account_manager);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_master_set_property (GObject *obj, guint prop_id,
			  const GValue *val, GParamSpec *pspec)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (obj);

    switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
	g_assert (priv->dbus_daemon == NULL);
	priv->dbus_daemon = g_value_dup_object (val);
	break;
    case PROP_ACCOUNT_MANAGER:
	g_assert (priv->account_manager == NULL);
	priv->account_manager = g_value_dup_object (val);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_master_dispose (GObject * object)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (object);
    
    if (priv->is_disposed)
    {
	return;
    }
    priv->is_disposed = TRUE;

    if (priv->transport_plugins)
    {
	guint i;

	for (i = 0; i < priv->transport_plugins->len; i++)
	{
	    McdTransportPlugin *plugin;
	    plugin = g_ptr_array_index (priv->transport_plugins, i);
	    g_signal_handlers_disconnect_by_func (plugin, 
						  on_transport_status_changed,
						  object);
	    g_object_unref (plugin);
	}
	g_ptr_array_free (priv->transport_plugins, TRUE);
	priv->transport_plugins = NULL;
    }

#ifdef ENABLE_PLUGINS
    if (priv->plugins)
    {
	mcd_master_unload_plugins (MCD_MASTER (object));
    }
#endif

    if (priv->account_manager)
    {
	g_object_unref (priv->account_manager);
	priv->account_manager = NULL;
    }

    if (priv->dbus_daemon)
    {
	g_object_unref (priv->dbus_daemon);
	priv->dbus_daemon = NULL;
    }

    /* Don't unref() the dispatcher: it will be unref()ed by the McdProxy */
    priv->dispatcher = NULL;
    g_object_unref (priv->proxy);

    G_OBJECT_CLASS (mcd_master_parent_class)->dispose (object);
}

static GObject *
mcd_master_constructor (GType type, guint n_params,
			GObjectConstructParam *params)
{
    GObjectClass *object_class = (GObjectClass *)mcd_master_parent_class;
    McdMaster *master;
    McdMasterPrivate *priv;

    master =  MCD_MASTER (object_class->constructor (type, n_params, params));
    priv = MCD_MASTER_PRIV (master);

    g_return_val_if_fail (master != NULL, NULL);

#ifdef HAVE_UMASK
    /* mask out group and other rwx bits when creating files */
    umask (0077);
#endif

    if (!priv->account_manager)
	priv->account_manager = mcd_account_manager_new (priv->dbus_daemon);

    priv->dispatcher = mcd_dispatcher_new (priv->dbus_daemon, master);
    g_assert (MCD_IS_DISPATCHER (priv->dispatcher));

    _mcd_account_manager_setup (priv->account_manager);

    dbus_connection_set_exit_on_disconnect (
        dbus_g_connection_get_connection (
            TP_PROXY (priv->dbus_daemon)->dbus_connection),
        TRUE);

    /* propagate the signals to dispatcher, too */
    priv->proxy = mcd_proxy_new (MCD_MISSION (master));
    mcd_operation_take_mission (MCD_OPERATION (priv->proxy),
				MCD_MISSION (priv->dispatcher));

#ifdef ENABLE_PLUGINS
    mcd_master_load_plugins (master);
#endif

    /* we assume that at this point all transport plugins have been registered.
     * We get the active transports and check whether some accounts should be
     * automatically connected */
    mcd_master_connect_automatic_accounts (master);

    return (GObject *) master;
}

static McdManager *
mcd_master_create_manager (McdMaster *master, const gchar *unique_name)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    return mcd_manager_new (unique_name, priv->dispatcher, priv->dbus_daemon);
}

static void
mcd_master_class_init (McdMasterClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdMasterPrivate));

    object_class->constructor = mcd_master_constructor;
    object_class->finalize = _mcd_master_finalize;
    object_class->get_property = _mcd_master_get_property;
    object_class->set_property = _mcd_master_set_property;
    object_class->dispose = _mcd_master_dispose;

    klass->create_manager = mcd_master_create_manager;

    /* Properties */
    g_object_class_install_property
        (object_class, PROP_DISPATCHER,
         g_param_spec_object ("dispatcher",
                              "Dispatcher",
                              "Dispatcher",
                              MCD_TYPE_DISPATCHER,
                              G_PARAM_READABLE));

    g_object_class_install_property
        (object_class, PROP_DBUS_DAEMON,
         g_param_spec_object ("dbus-daemon", "DBus daemon", "DBus daemon",
                              TP_TYPE_DBUS_DAEMON,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_DBUS_CONNECTION,
         g_param_spec_pointer ("dbus-connection",
                               "D-Bus Connection",
                               "D-Bus Connection",
                               G_PARAM_READABLE));

    g_object_class_install_property
        (object_class, PROP_ACCOUNT_MANAGER,
         g_param_spec_object ("account-manager",
                              "AccountManager", "AccountManager",
                              MCD_TYPE_ACCOUNT_MANAGER,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
_g_value_free (gpointer data)
{
  GValue *value = (GValue *) data;
  g_value_unset (value);
  g_free (value);
}

static void
mcd_master_init (McdMaster * master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    if (!default_master)
	default_master = master;

    priv->extra_parameters = g_hash_table_new_full (g_str_hash, g_str_equal,
						    g_free, _g_value_free);

    priv->transport_plugins = g_ptr_array_new ();
}

McdMaster *
mcd_master_get_default (void)
{
    if (!default_master)
	default_master = MCD_MASTER (g_object_new (MCD_TYPE_MASTER, NULL));
    return default_master;
}

/**
 * mcd_master_add_connection_parameter:
 * @master: the #McdMaster.
 * @name: the name of the parameter to add.
 * @value: a #GValue.
 *
 * Set a global connection parameter to be passed to all connection managers
 * (which support this parameter).  If called twice for the same parameter, the
 * new value will replace the previous one.
 */
void
mcd_master_add_connection_parameter (McdMaster *master, const gchar *name,
				     const GValue *value)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GValue *val;

    g_return_if_fail (name != NULL);
    g_return_if_fail (value != NULL);

    val = g_malloc0 (sizeof (GValue));
    g_value_init (val, G_VALUE_TYPE (value));
    g_value_copy (value, val);
    g_hash_table_replace (priv->extra_parameters, g_strdup (name), val);
}

/*
 * _mcd_master_lookup_manager:
 * @master: the #McdMaster.
 * @unique_name: the name of the manager.
 *
 * Gets the manager whose name is @unique_name. If the manager object doesn't
 * exists yet, it is created.
 *
 * Returns: a #McdManager. Caller must call g_object_ref() on it to ensure it
 * will stay alive as long as needed.
 */
McdManager *
_mcd_master_lookup_manager (McdMaster *master,
                            const gchar *unique_name)
{
    const GList *managers, *list;
    McdManager *manager;

    managers = mcd_operation_get_missions (MCD_OPERATION (master));
    for (list = managers; list; list = list->next)
    {
	manager = MCD_MANAGER (list->data);
	if (strcmp (unique_name,
		    mcd_manager_get_name (manager)) == 0)
	    return manager;
    }

    manager = MCD_MASTER_GET_CLASS (master)->create_manager
        (master, unique_name);
    if (G_UNLIKELY (!manager))
	g_warning ("Manager %s not created", unique_name);
    else
	mcd_operation_take_mission (MCD_OPERATION (master),
				    MCD_MISSION (manager));

    return manager;
}

/**
 * mcd_master_get_dispatcher:
 * @master: the #McdMaster.
 *
 * Returns: the #McdDispatcher. It will go away when @master is disposed,
 * unless you keep a reference to it.
 */
McdDispatcher *
mcd_master_get_dispatcher (McdMaster *master)
{
    g_return_val_if_fail (MCD_IS_MASTER (master), NULL);
    return MCD_MASTER_PRIV (master)->dispatcher;
}

/**
 * mcd_plugin_get_dispatcher:
 * @plugin: the #McdPlugin
 * 
 * Gets the McdDispatcher, to be used for registering channel filters. The
 * reference count of the returned object is not incremented, and the object is
 * guaranteed to stay alive during the whole lifetime of the plugin.
 *
 * Returns: the #McdDispatcher
 */
McdDispatcher *
mcd_plugin_get_dispatcher (McdPlugin *plugin)
{
    return MCD_MASTER_PRIV (plugin)->dispatcher;
}

/**
 * mcd_master_get_dbus_daemon:
 * @master: the #McdMaster.
 *
 * Returns: the #TpDBusDaemon.
 */
TpDBusDaemon *
mcd_master_get_dbus_daemon (McdMaster *master)
{
    g_return_val_if_fail (MCD_IS_MASTER (master), NULL);
    return MCD_MASTER_PRIV (master)->dbus_daemon;
}

/**
 * mcd_plugin_register_transport:
 * @plugin: the #McdPlugin.
 * @transport_plugin: the #McdTransportPlugin.
 *
 * Registers @transport_plugin as a transport monitoring object.
 * The @plugin takes ownership of the transport (i.e., it doesn't increment its
 * reference count).
 */
void
mcd_plugin_register_transport (McdPlugin *plugin,
			       McdTransportPlugin *transport_plugin)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (plugin);

    DEBUG ("called");
    g_signal_connect (transport_plugin, "status-changed",
		      G_CALLBACK (on_transport_status_changed),
		      MCD_MASTER (plugin));
    g_ptr_array_add (priv->transport_plugins, transport_plugin);
}

void
mcd_plugin_register_account_connection (McdPlugin *plugin,
					McdAccountConnectionFunc func,
					gint priority,
					gpointer userdata)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (plugin);
    McdAccountConnectionData *acd;
    GList *list;

    DEBUG ("called");
    acd = g_malloc (sizeof (McdAccountConnectionData));
    acd->priority = priority;
    acd->func = func;
    acd->userdata = userdata;
    for (list = priv->account_connections; list; list = list->next)
	if (((McdAccountConnectionData *)list->data)->priority >= priority) break;

    priv->account_connections =
       	g_list_insert_before (priv->account_connections, list, acd);
}

void
_mcd_master_get_nth_account_connection (McdMaster *master,
                                        gint i,
                                        McdAccountConnectionFunc *func,
                                        gpointer *userdata)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    McdAccountConnectionData *acd;

    acd = g_list_nth_data (priv->account_connections, i);
    if (acd)
    {
	*func = acd->func;
	*userdata = acd->userdata;
    }
    else
	*func = NULL;
}

gboolean
_mcd_master_account_replace_transport (McdMaster *master,
                                       McdAccount *account)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    GHashTable *conditions;
    gboolean ret = FALSE;

    g_return_val_if_fail (MCD_IS_ACCOUNT (account), FALSE);

    conditions = mcd_account_get_conditions (account);
    if (g_hash_table_size (conditions) == 0)
        ret = TRUE;
    else
    {
        guint i;
        for (i = 0; i < priv->transport_plugins->len; i++)
        {
            McdTransportPlugin *plugin;
            const GList *transports;

            plugin = g_ptr_array_index (priv->transport_plugins, i);
            transports = mcd_transport_plugin_get_transports (plugin);
            while (transports)
            {
                McdTransport *transport = transports->data;
                if (mcd_transport_get_status (plugin, transport) ==
                    MCD_TRANSPORT_STATUS_CONNECTED &&
                    mcd_transport_plugin_check_conditions (plugin, transport,
                                                           conditions))
                {
                    mcd_account_connection_bind_transport (account, transport);
                    ret = TRUE;
                    goto finish;
                }
                transports = transports->next;
            }
        }
    }
finish:
    g_hash_table_unref (conditions);
    return ret;
}

gboolean
mcd_master_has_low_memory (McdMaster *master)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    return priv->low_memory;
}

/* For the moment, this is implemented in terms of McdSystemFlags. */
void
mcd_master_set_low_memory (McdMaster *master,
                           gboolean low_memory)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);

    priv->low_memory = low_memory;
}

/* For the moment, this is implemented in terms of McdSystemFlags. When
 * McdSystemFlags are abolished, move the processing from set_flags to
 * this function. */
void
mcd_master_set_idle (McdMaster *master,
                     gboolean idle)
{
    McdMasterPrivate *priv = MCD_MASTER_PRIV (master);
    gboolean idle_flag_old;

    idle_flag_old = priv->idle;
    priv->idle = idle != 0;

    if (idle_flag_old != priv->idle)
    {
        GHashTableIter iter;
        gpointer v;

        g_hash_table_iter_init (&iter,
            _mcd_account_manager_get_accounts (priv->account_manager));

        while (g_hash_table_iter_next (&iter, NULL, &v))
        {
            McdAccount *account = MCD_ACCOUNT (v);

            if (priv->idle)
            {
                TpConnectionPresenceType presence;

                /* If the current presence is not Available then we don't go
                 * auto-away - this avoids (a) manipulating offline accounts
                 * and (b) messing up people's busy or invisible status */
                mcd_account_get_current_presence (account, &presence, NULL,
                                                  NULL);

                if (presence != TP_CONNECTION_PRESENCE_TYPE_AVAILABLE)
                {
                    continue;
                }

                /* Set the Connection to be "away" if the CM supports it
                 * (if not, it'll just fail - no harm done) */
                _mcd_account_request_temporary_presence (account,
                    TP_CONNECTION_PRESENCE_TYPE_AWAY, "away");
            }
            else
            {
                TpConnectionPresenceType presence;
                const gchar *status;
                const gchar *message;

                /* Go back to the requested presence */
                mcd_account_get_requested_presence (account, &presence,
                                                    &status, &message);
                mcd_account_request_presence (account, presence, status,
                                              message);
            }
        }
    }
}
