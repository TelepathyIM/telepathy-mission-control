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
 */

#include <config.h>

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include <gmodule.h>
#include <string.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/telepathy-glib.h>

#ifdef G_OS_WIN32
#include <io.h>
#endif

#include "mcd-master.h"
#include "mcd-master-priv.h"
#include "mcd-manager.h"
#include "mcd-dispatcher.h"
#include "mcd-account-manager.h"
#include "mcd-account-manager-priv.h"
#include "mcd-account-conditions.h"
#include "mcd-account-priv.h"
#include "plugin-loader.h"

#ifdef G_OS_UNIX
# ifndef HAVE_UMASK
#   error On Unix, MC relies on umask() for account privacy
# endif
#endif

G_DEFINE_TYPE (McdMaster, mcd_master, MCD_TYPE_OPERATION);

struct _McdMasterPrivate
{
    McdAccountManager *account_manager;
    McdDispatcher *dispatcher;

    /* We create these for our member objects */
    TpDBusDaemon *dbus_daemon;
    TpSimpleClientFactory *client_factory;

    /* Current pending sleep timer */
    gint shutdown_timeout_id;

    gboolean is_disposed;
    gboolean low_memory;
    gboolean idle;
};

enum
{
    PROP_0,
    PROP_PRESENCE_FRAME,
    PROP_DBUS_CONNECTION,
    PROP_DBUS_DAEMON,
    PROP_DISPATCHER,
    PROP_ACCOUNT_MANAGER,
};

/* Used to poison 'default_master' when the object it points to is disposed.
 * The default_master should basically be alive for the duration of the MC run.
 */
#define POISONED_MASTER ((McdMaster *) 0xdeadbeef)
static McdMaster *default_master = NULL;

static void
_mcd_master_get_property (GObject * obj, guint prop_id,
			  GValue * val, GParamSpec * pspec)
{
    McdMasterPrivate *priv = MCD_MASTER (obj)->priv;

    switch (prop_id)
    {
    case PROP_DISPATCHER:
	g_value_set_object (val, priv->dispatcher);
	break;
    case PROP_DBUS_DAEMON:
	g_value_set_object (val, priv->dbus_daemon);
	break;
    case PROP_DBUS_CONNECTION:
        g_value_set_pointer (val, tp_proxy_get_dbus_connection (
                             TP_PROXY (priv->dbus_daemon)));
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
    McdMasterPrivate *priv = MCD_MASTER (obj)->priv;

    switch (prop_id)
    {
    case PROP_DBUS_DAEMON:
	g_assert (priv->dbus_daemon == NULL);
	priv->dbus_daemon = g_value_dup_object (val);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_master_dispose (GObject * object)
{
    McdMasterPrivate *priv = MCD_MASTER (object)->priv;
    
    if (priv->is_disposed)
    {
	return;
    }
    priv->is_disposed = TRUE;

    tp_clear_object (&priv->account_manager);
    tp_clear_object (&priv->dbus_daemon);
    tp_clear_object (&priv->dispatcher);
    tp_clear_object (&priv->client_factory);

    if (default_master == (McdMaster *) object)
    {
        default_master = POISONED_MASTER;
    }

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
    priv = master->priv;

    g_return_val_if_fail (master != NULL, NULL);

#ifdef HAVE_UMASK
    /* mask out group and other rwx bits when creating files */
    umask (0077);
#endif

    priv->client_factory = tp_simple_client_factory_new (priv->dbus_daemon);
    priv->account_manager = mcd_account_manager_new (priv->client_factory);

    priv->dispatcher = mcd_dispatcher_new (priv->dbus_daemon, master);
    g_assert (MCD_IS_DISPATCHER (priv->dispatcher));

    _mcd_account_manager_setup (priv->account_manager);

    dbus_connection_set_exit_on_disconnect (
        dbus_g_connection_get_connection (
            tp_proxy_get_dbus_connection (TP_PROXY (priv->dbus_daemon))),
        TRUE);

    return (GObject *) master;
}

static void
mcd_master_class_init (McdMasterClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdMasterPrivate));

    object_class->constructor = mcd_master_constructor;
    object_class->get_property = _mcd_master_get_property;
    object_class->set_property = _mcd_master_set_property;
    object_class->dispose = _mcd_master_dispose;

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
                              G_PARAM_READABLE));
}

static void
mcd_master_init (McdMaster * master)
{
    master->priv = G_TYPE_INSTANCE_GET_PRIVATE (master,
        MCD_TYPE_MASTER, McdMasterPrivate);

    if (!default_master)
	default_master = master;

    /* This newer plugin API is currently always enabled       */
    /* .... and is enabled before anything else as potentially *
     * any mcd component could have a new-API style plugin     */
    _mcd_plugin_loader_init ();
}

McdMaster *
mcd_master_get_default (void)
{
    if (!default_master)
	default_master = MCD_MASTER (g_object_new (MCD_TYPE_MASTER, NULL));

    g_return_val_if_fail (default_master != POISONED_MASTER, NULL);

    return default_master;
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

    manager = mcd_manager_new (unique_name,
                               master->priv->dispatcher,
                               master->priv->client_factory);
    if (G_UNLIKELY (!manager))
	g_warning ("Manager %s not created", unique_name);
    else
	mcd_operation_take_mission (MCD_OPERATION (master),
				    MCD_MISSION (manager));

    return manager;
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
    return master->priv->dbus_daemon;
}

/* Milliseconds to wait for Connectivity coming back up before exiting MC */
#define EXIT_COUNTDOWN_TIME 5000

static gboolean
_mcd_master_exit_by_timeout (gpointer data)
{
    McdMaster *self = MCD_MASTER (data);

    self->priv->shutdown_timeout_id = 0;

    /* Notify sucide */
    mcd_mission_abort (MCD_MISSION (self));
    return FALSE;
}

void
mcd_master_shutdown (McdMaster *self,
                     const gchar *reason)
{
    McdMasterPrivate *priv;

    g_return_if_fail (MCD_IS_MASTER (self));
    priv = self->priv;

    if(!priv->shutdown_timeout_id)
    {
        DEBUG ("MC will bail out because of \"%s\" out exit after %i",
               reason ? reason : "No reason specified",
               EXIT_COUNTDOWN_TIME);

        priv->shutdown_timeout_id = g_timeout_add (EXIT_COUNTDOWN_TIME,
                                                   _mcd_master_exit_by_timeout,
                                                   self);
    }
    else
    {
        DEBUG ("Already shutting down. This one has the reason %s",
               reason ? reason : "No reason specified");
    }
    mcd_debug_print_tree (self);
}
