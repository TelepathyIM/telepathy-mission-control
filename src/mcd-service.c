/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

/**
 * SECTION:mcd-service
 * @title: McdService
 * @short_description: Service interface implementation
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-service.h
 * 
 * It is the frontline interface object that exposes mission-control to outside
 * world through a dbus interface. It basically subclasses McdMaster and
 * wraps up everything inside it and translate them into mission-control
 * dbus interface.
 */

#include <dbus/dbus.h>
#include <string.h>
#include <dlfcn.h>
#include <sched.h>
#include <stdlib.h>

#include <glib.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>
#include <gconf/gconf-client.h>

#include "mcd-signals-marshal.h"
#include "mcd-dispatcher.h"
#include "mcd-dispatcher-context.h"
#include "mcd-connection.h"
#include "mcd-service.h"

#include <libmcclient/mc-errors.h>

/* DBus service specifics */
#define MISSION_CONTROL_DBUS_SERVICE "org.freedesktop.Telepathy.MissionControl"
#define MISSION_CONTROL_DBUS_OBJECT  "/org/freedesktop/Telepathy/MissionControl"
#define MISSION_CONTROL_DBUS_IFACE   "org.freedesktop.Telepathy.MissionControl"

#define LAST_MC_PRESENCE (TP_CONNECTION_PRESENCE_TYPE_BUSY + 1)

typedef enum {
    MC_STATUS_DISCONNECTED,
    MC_STATUS_CONNECTING,
    MC_STATUS_CONNECTED,
} McStatus;

/* Signals */

enum
{
    ACCOUNT_STATUS_CHANGED,
    ACCOUNT_PRESENCE_CHANGED,
    ERROR,
    PRESENCE_STATUS_REQUESTED,
    PRESENCE_STATUS_ACTUAL,
    USED_CHANNELS_COUNT_CHANGED,
    STATUS_ACTUAL,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
static GObjectClass *parent_class = NULL;

#define MCD_OBJECT_PRIV(mission) (G_TYPE_INSTANCE_GET_PRIVATE ((mission), \
				   MCD_TYPE_SERVICE, \
				   McdServicePrivate))

G_DEFINE_TYPE (McdService, mcd_service, MCD_TYPE_MASTER);

/* Private */

typedef struct _McdServicePrivate
{
    McdPresenceFrame *presence_frame;
    McdDispatcher *dispatcher;

    McStatus last_status;

    gboolean is_disposed;
} McdServicePrivate;

#define MC_EMIT_ERROR_ASYNC(mi, err) \
    g_assert (err != NULL); \
    dbus_g_method_return_error (mi, err); \
    g_warning ("%s: Returning async error '%s'", G_STRFUNC, err->message); \
    g_error_free (err);

#define MC_SHOW_ERROR(err) \
    g_assert ((err) != NULL); \
    g_warning ("%s: Returning error '%s'", G_STRFUNC, (err)->message);

/* Dbus interface implementation */
static gboolean
mcd_service_set_presence (GObject * obj, gint presence, gchar * message,
			  GError ** error)
{
    if (presence >= LAST_MC_PRESENCE)
    {
	g_set_error (error, MC_ERROR, MC_PRESENCE_FAILURE_ERROR,
		     "Invalid presence");
	return FALSE;
    }

    mcd_master_request_presence (MCD_MASTER (obj), presence, message);
    return TRUE;
}

static gboolean
mcd_service_get_presence (GObject *obj, gint *ret, GError **error)
{
    *ret = mcd_master_get_requested_presence (MCD_MASTER (obj));
    return TRUE;
}

static gboolean
mcd_service_get_presence_message (GObject *obj, gchar **ret, GError **error)
{
    *ret = mcd_master_get_requested_presence_message (MCD_MASTER (obj));
    return TRUE;
}

static gboolean
mcd_service_get_presence_actual (GObject *obj, gint *ret, GError **error)
{
    *ret = mcd_master_get_actual_presence (MCD_MASTER (obj));
    return TRUE;
}

static gboolean
mcd_service_get_presence_message_actual (GObject *obj, gchar **ret,
					 GError **error)
{
    *ret = mcd_master_get_actual_presence_message (MCD_MASTER (obj));
    return TRUE;
}

static void
mcd_service_connect_all_with_default_presence (GObject * obj,
					       DBusGMethodInvocation *mi)
{
    gchar *sender = dbus_g_method_get_sender (mi);
    mcd_master_set_default_presence (MCD_MASTER (obj), sender);
    g_free (sender);
    dbus_g_method_return (mi);
}

static gboolean
mcd_service_get_connection_status (GObject * obj, gchar * account_name,
				   guint * ret, GError ** error)
{
    *ret = mcd_master_get_account_status (MCD_MASTER (obj), account_name);
    return TRUE;
}

static gboolean
mcd_service_get_online_connections (GObject * obj,
				    gchar *** ret, GError ** error)
{
    return mcd_master_get_online_connection_names (MCD_MASTER (obj), ret);
}

static gboolean
mcd_service_get_connection (GObject * obj, const gchar * account_name,
			    gchar ** ret_servname,
			    gchar ** ret_objpath, GError ** error)
{
    return mcd_master_get_account_connection_details (MCD_MASTER (obj),
						      account_name,
						      ret_servname,
						      ret_objpath,
						      error);
}

static void
mcd_service_request_channel (GObject * obj,
			     const gchar * account_name,
			     const gchar * type,
			     guint handle,
			     gint handle_type,
			     guint serial,
			     DBusGMethodInvocation *mi)
{
    struct mcd_channel_request req;
    GError *err = NULL;

    memset (&req, 0, sizeof (req));
    req.account_name = account_name;
    req.channel_type = type;
    req.channel_handle = handle;
    req.channel_handle_type = handle_type;
    req.requestor_serial = serial;
    req.requestor_client_id = dbus_g_method_get_sender (mi);
    if (!mcd_master_request_channel (MCD_MASTER (obj), &req, &err))
    {
	g_free ((gchar *)req.requestor_client_id);
	MC_EMIT_ERROR_ASYNC (mi, err);
	return;
    }
    g_free ((gchar *)req.requestor_client_id);
    dbus_g_method_return (mi);
}

static void
mcd_service_request_channel_with_string_handle (GObject * obj,
						const gchar * account_name,
						const gchar * type,
						const gchar * handle,
						gint handle_type,
						guint serial,
					       	DBusGMethodInvocation *mi)
{
    struct mcd_channel_request req;
    GError *err = NULL;

    memset (&req, 0, sizeof (req));
    req.account_name = account_name;
    req.channel_type = type;
    req.channel_handle_string = handle;
    req.channel_handle_type = handle_type;
    req.requestor_serial = serial;
    req.requestor_client_id = dbus_g_method_get_sender (mi);
    mcd_controller_cancel_shutdown (MCD_CONTROLLER (obj));
    if (!mcd_master_request_channel (MCD_MASTER (obj), &req, &err))
    {
	g_free ((gchar *)req.requestor_client_id);
	MC_EMIT_ERROR_ASYNC (mi, err);
	return;
    }
    g_free ((gchar *)req.requestor_client_id);
    dbus_g_method_return (mi);
}

static void
mcd_service_cancel_channel_request (GObject * obj, guint operation_id,
				    DBusGMethodInvocation *mi)
{
    GError *err = NULL;
    gchar *sender = dbus_g_method_get_sender (mi);
    g_debug ("%s (%u)", G_STRFUNC, operation_id);
    if (!mcd_master_cancel_channel_request (MCD_MASTER (obj), operation_id,
					    sender, &err))
    {
	g_warning ("%s: channel not found", G_STRFUNC);
	g_free (sender);
	dbus_g_method_return (mi);
	return;
    }
    g_free (sender);
    if (err)
    {
	MC_EMIT_ERROR_ASYNC (mi, err);
	return;
    }
    dbus_g_method_return (mi);
}

static gboolean
mcd_service_get_used_channels_count (GObject * obj, const gchar *chan_type,
				     guint * ret, GError ** error)
{
    if (!mcd_master_get_used_channels_count (MCD_MASTER (obj),
					     g_quark_from_string (chan_type),
					     ret, error))
    {
	MC_SHOW_ERROR (*error);
	return FALSE;
    }
    return TRUE;
}

static gboolean
mcd_service_get_account_for_connection(GObject *obj,
				       const gchar *object_path,
				       gchar **ret_unique_name,
				       GError **error)
{
    g_debug ("%s: object_path = %s", __FUNCTION__, object_path);
    
    if (!mcd_master_get_account_for_connection (MCD_MASTER (obj),
						object_path,
						ret_unique_name,
						error))
    {
	MC_SHOW_ERROR (*error);
	return FALSE;
    }
    return TRUE;
}

static gboolean
mcd_service_get_current_status(GObject *obj,
			       McStatus *status, TpConnectionPresenceType *presence,
			       TpConnectionPresenceType *requested_presence,
			       GPtrArray **accounts, GError **error)
{
    McdServicePrivate *priv = MCD_OBJECT_PRIV (obj);
    GList *account_list, *account_node;
    GType type;

    *status = priv->last_status;
    *presence = mcd_master_get_actual_presence (MCD_MASTER (obj));
    *requested_presence = mcd_master_get_requested_presence (MCD_MASTER (obj));
    *accounts = g_ptr_array_new ();

    type = dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_UINT,
				   G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
    account_list = mcd_presence_frame_get_accounts (priv->presence_frame);
    for (account_node = account_list; account_node != NULL;
	 account_node = g_list_next (account_node))
    {
	McdAccount *account = account_node->data;
	GValue account_data = { 0, };
	const gchar *name, *p_status, *p_message;
	TpConnectionStatus status;
	TpConnectionStatusReason reason;
	TpConnectionPresenceType presence;

	name = mcd_account_get_unique_name (account);
	mcd_account_get_current_presence (account, &presence,
					  &p_status, &p_message);
	status = mcd_presence_frame_get_account_status (priv->presence_frame,
						       	account);
	reason =
	    mcd_presence_frame_get_account_status_reason (priv->presence_frame,
							  account);

	g_value_init (&account_data, type);
	g_value_take_boxed (&account_data,
			    dbus_g_type_specialized_construct (type));
	dbus_g_type_struct_set (&account_data,
				0, name,
				1, status,
				2, presence,
				3, reason,
				G_MAXUINT);

	g_ptr_array_add (*accounts, g_value_get_boxed (&account_data));
    }
    return TRUE;
}

static void
mcd_service_remote_avatar_changed(GObject *obj,
				  const gchar *object_path,
				  guint contact_id,
				  const gchar *token,
				  DBusGMethodInvocation *mi)
{
    McdConnection *connection;
    GError *error = NULL;

    g_debug ("%s: object_path = %s, id = %u, token = %s", __FUNCTION__,
	     object_path, contact_id, token);
 
    connection = mcd_master_get_connection (MCD_MASTER (obj),
					    object_path, &error);
    if (!connection)
    {
	MC_EMIT_ERROR_ASYNC (mi, error);
	return;
    }
    /* let the D-Bus call return immediately, there's no need for the caller to
     * be blocked while we get the avatar */
    dbus_g_method_return (mi);

    mcd_connection_remote_avatar_changed (connection, contact_id, token);
}

static void
_on_filter_process (DBusGProxy *proxy, guint counter, gboolean process)
{
    McdDispatcherContext *ctx;
    GHashTable *ctx_table = g_object_get_data (G_OBJECT (proxy), "table");

    ctx = g_hash_table_lookup (ctx_table, GUINT_TO_POINTER (counter));
    if (ctx)
    {
        g_debug ("%s: Process channel %d", __FUNCTION__, counter);
        g_hash_table_remove (ctx_table, GUINT_TO_POINTER (counter));
        mcd_dispatcher_context_process (ctx, process);
    }
}

static void
_on_filter_new_channel (McdDispatcherContext *ctx, DBusGProxy *proxy)
{
    TpConnection *tp_conn;
    const McdConnection *connection = mcd_dispatcher_context_get_connection (ctx);
    McdChannel *channel = mcd_dispatcher_context_get_channel (ctx); 
    static guint counter = 0;
    GHashTable *ctx_table = g_object_get_data (G_OBJECT (proxy), "table");

    g_hash_table_insert (ctx_table, GUINT_TO_POINTER (++counter), ctx);

    g_object_get (G_OBJECT (connection), "tp-connection", &tp_conn, NULL);

    g_debug ("%s: Filtering new channel", __FUNCTION__);
    dbus_g_proxy_call_no_reply (proxy, "FilterChannel",
				G_TYPE_STRING, TP_PROXY (tp_conn)->bus_name,
				DBUS_TYPE_G_OBJECT_PATH, TP_PROXY (tp_conn)->object_path,
				G_TYPE_STRING, mcd_channel_get_channel_type (channel),
				DBUS_TYPE_G_OBJECT_PATH, mcd_channel_get_object_path (channel),
				G_TYPE_UINT, mcd_channel_get_handle_type (channel),
				G_TYPE_UINT, mcd_channel_get_handle (channel),
				G_TYPE_UINT, counter,
				G_TYPE_INVALID);
}

static gboolean
_ctx_table_remove_foreach (guint counter,
			   McdDispatcherContext *ctx,
			   gpointer user_data)
{
    mcd_dispatcher_context_process (ctx, TRUE);
    return TRUE;
}

static void
_on_filter_proxy_destroy (DBusGProxy *proxy)
{
    McdDispatcher *dispatcher;
    GHashTable *ctx_table;
    guint quark;
    guint flags;

    dispatcher = g_object_get_data (G_OBJECT (proxy), "dispatcher");
    ctx_table = g_object_get_data (G_OBJECT (proxy), "table");
    quark = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (proxy), "quark"));
    flags = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (proxy), "flags"));

    g_hash_table_foreach_remove (ctx_table,
    				 (GHRFunc) _ctx_table_remove_foreach,
    				 NULL);

    g_debug ("%s: Unregistering filter", __FUNCTION__);
    mcd_dispatcher_unregister_filter (dispatcher,
				      (McdFilterFunc) _on_filter_new_channel,
				      quark, flags);

    g_object_unref (proxy);
}

static gboolean
mcd_service_register_filter(GObject *obj,
			    const gchar *bus_name,
			    const gchar *object_path,
			    const gchar *channel_type,
			    guint priority,
			    guint flags,
			    GError **error)
{
    McdServicePrivate *priv = MCD_OBJECT_PRIV (obj);
    DBusGProxy *proxy;
    GHashTable *ctx_table;
    static gboolean initialized = FALSE;
    guint quark = g_quark_from_string (channel_type);

    g_debug ("%s: Registering new filter", __FUNCTION__);

    if (!initialized)
    {
        dbus_g_object_register_marshaller (_mcd_marshal_VOID__UINT_BOOLEAN,
					   G_TYPE_NONE,
					   G_TYPE_UINT,
					   G_TYPE_BOOLEAN,
					   G_TYPE_INVALID);
        initialized = TRUE;
    }

    proxy = dbus_g_proxy_new_for_name (tp_get_bus (),
    				       bus_name,
    				       object_path,
    				       "org.freedesktop.Telepathy.MissionControl.Filter");

    ctx_table = g_hash_table_new (g_direct_hash, g_direct_equal);
    g_object_set_data_full (G_OBJECT (proxy), "table", ctx_table,
    			    (GDestroyNotify) g_hash_table_destroy);
    g_object_set_data (G_OBJECT (proxy), "dispatcher", priv->dispatcher);
    g_object_set_data (G_OBJECT (proxy), "flags", GUINT_TO_POINTER (flags));
    g_object_set_data (G_OBJECT (proxy), "quark", GUINT_TO_POINTER (quark));

    dbus_g_proxy_add_signal (proxy, "Process",
			     G_TYPE_UINT,
			     G_TYPE_BOOLEAN,
			     G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (proxy, "Process",
    				 G_CALLBACK (_on_filter_process),
    				 NULL, NULL);
    g_signal_connect (proxy, "destroy",
    		      G_CALLBACK (_on_filter_proxy_destroy),
    		      NULL);

    mcd_dispatcher_register_filter (priv->dispatcher,
    				    (McdFilterFunc) _on_filter_new_channel,
    				    quark,
    				    flags, priority,
    				    proxy);

    return TRUE;
}

#include "mcd-service-gen.h"

static void
mcd_register_dbus_object (McdService * obj)
{
    DBusError error;
    DBusGConnection *connection;
    
    g_object_get (obj, "dbus-connection", &connection, NULL);
    
    dbus_error_init (&error);
    
    g_debug ("Requesting MC dbus service");
    
    dbus_bus_request_name (dbus_g_connection_get_connection (connection),
			   MISSION_CONTROL_DBUS_SERVICE, 0, &error);
    if (dbus_error_is_set (&error))
    {
	g_error ("Service name '%s' is already in use - request failed",
		 MISSION_CONTROL_DBUS_SERVICE);
	dbus_error_free (&error);
    }
    
    g_debug ("Registering MC object");
    mcd_debug_print_tree (obj);
    dbus_g_connection_register_g_object (connection,
					 MISSION_CONTROL_DBUS_OBJECT,
					 G_OBJECT (obj));
    g_debug ("Registered MC object");
    mcd_debug_print_tree (obj);
}

static void
_on_account_status_changed (McdPresenceFrame * presence_frame,
			    McdAccount *account,
			    TpConnectionStatus connection_status,
			    TpConnectionStatusReason connection_reason,
			    McdService * obj)
{
    TpConnectionPresenceType presence;
    const gchar *status, *message;

    mcd_account_get_current_presence (account, &presence, &status, &message);

    /* Emit the AccountStatusChanged signal */
    g_debug ("Emitting account status changed for %s: status = %d, reason = %d",
	     mcd_account_get_unique_name (account), connection_status,
	     connection_reason);

    /* HACK for old MC compatibility */
    if (connection_status == TP_CONNECTION_STATUS_CONNECTED &&
	presence < TP_CONNECTION_PRESENCE_TYPE_AVAILABLE)
	presence = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;

    g_signal_emit_by_name (G_OBJECT (obj),
			   "account-status-changed", connection_status,
			   presence,
			   connection_reason,
			   mcd_account_get_unique_name (account));
#ifndef NO_NEW_PRESENCE_SIGNALS
    g_signal_emit_by_name (G_OBJECT (obj),
			   "account-presence-changed", connection_status,
			   presence, message,
			   connection_reason,
			   mcd_account_get_unique_name (account));
#endif
}

static void
_on_account_presence_changed (McdPresenceFrame * presence_frame,
			      McdAccount * account,
			      TpConnectionPresenceType presence,
			      gchar * presence_message, McdService * obj)
{
    /* Emit the AccountStatusChanged signal */
    g_debug ("Emitting presence changed for %s: presence = %d, message = %s",
	     mcd_account_get_unique_name (account), presence,
	     presence_message);
    
    /* HACK for old MC compatibility */
    if (mcd_presence_frame_get_account_status (presence_frame, account)
       	== TP_CONNECTION_STATUS_CONNECTED &&
	presence == TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
	return;

    g_signal_emit_by_name (G_OBJECT (obj),
			   "account-status-changed",
			   mcd_presence_frame_get_account_status
			   (presence_frame, account), presence,
			   mcd_presence_frame_get_account_status_reason
			   (presence_frame, account),
			   mcd_account_get_unique_name (account));
#ifndef NO_NEW_PRESENCE_SIGNALS
    g_signal_emit_by_name (G_OBJECT (obj),
			   "account-presence-changed",
			   mcd_presence_frame_get_account_status
			   (presence_frame, account), presence,
			   presence_message,
			   mcd_presence_frame_get_account_status_reason
			   (presence_frame, account),
			   mcd_account_get_unique_name (account));
#endif
}

static void
_on_presence_requested (McdPresenceFrame * presence_frame,
			TpConnectionPresenceType presence,
			gchar * presence_message, McdService * obj)
{
    /* Begin shutdown if it is offline request */
    if (presence == TP_CONNECTION_PRESENCE_TYPE_OFFLINE ||
	presence == TP_CONNECTION_PRESENCE_TYPE_UNSET)
	mcd_controller_shutdown (MCD_CONTROLLER (obj),
				 "Offline presence requested");
    else
	/* If there is a presence request, make sure shutdown is canceled */
	mcd_controller_cancel_shutdown (MCD_CONTROLLER (obj));
    
    /* Emit the AccountStatusChanged signal */
    g_signal_emit_by_name (G_OBJECT (obj),
			   "presence-status-requested", presence);
#ifndef NO_NEW_PRESENCE_SIGNALS
    g_signal_emit_by_name (G_OBJECT (obj),
			   "presence-requested", presence, presence_message);
#endif
}

static void
_on_presence_actual (McdPresenceFrame * presence_frame,
		     TpConnectionPresenceType presence,
		     gchar * presence_message, McdService * obj)
{
    /* Emit the AccountStatusChanged signal */
    g_signal_emit_by_name (G_OBJECT (obj), "presence-status-actual", presence);
#ifndef NO_NEW_PRESENCE_SIGNALS
    g_signal_emit_by_name (G_OBJECT (obj), "presence-changed", presence, presence_message);
#endif
}

static void
mcd_service_disconnect (McdMission *mission)
{
    MCD_MISSION_CLASS (mcd_service_parent_class)->disconnect (mission);
    mcd_controller_shutdown (MCD_CONTROLLER (mission), "Disconnected");
}

static void
_on_status_actual (McdPresenceFrame *presence_frame,
		   TpConnectionStatus tpstatus,
		   McdService *service)
{
    McdServicePrivate *priv = MCD_OBJECT_PRIV (service);
    TpConnectionPresenceType req_presence;
    McStatus status;

    req_presence = mcd_presence_frame_get_requested_presence (presence_frame);
    switch (tpstatus)
    {
    case TP_CONNECTION_STATUS_CONNECTED:
	status = MC_STATUS_CONNECTED;
	break;
    case TP_CONNECTION_STATUS_CONNECTING:
	status = MC_STATUS_CONNECTING;
	break;
    case TP_CONNECTION_STATUS_DISCONNECTED:
	status = MC_STATUS_DISCONNECTED;
	break;
    default:
	status = MC_STATUS_DISCONNECTED;
	g_warning ("Unexpected status %d", tpstatus);
    }

    if (status != priv->last_status)
    {
	g_signal_emit (service, signals[STATUS_ACTUAL], 0, status,
		       req_presence);
	priv->last_status = status;
    }
}

static void
_on_dispatcher_channel_added (McdDispatcher *dispatcher,
			      McdChannel *channel, McdService *obj)
{
    /* Nothing to do for now */
}

static void
_on_dispatcher_channel_removed (McdDispatcher *dispatcher,
				McdChannel *channel, McdService *obj)
{
    const gchar *chan_type;
    GQuark chan_type_quark;
    gint usage;
    
    chan_type = mcd_channel_get_channel_type (channel);
    chan_type_quark = mcd_channel_get_channel_type_quark (channel);
    usage = mcd_dispatcher_get_channel_type_usage (dispatcher,
						   chan_type_quark);

    /* Signal that the channel count has changed */
    g_signal_emit_by_name (G_OBJECT (obj),
			   "used-channels-count-changed",
			   chan_type, usage);
}

static void
_on_dispatcher_channel_dispatched (McdDispatcher *dispatcher,
				   McdChannel *channel,
				   McdService *obj)
{
    const gchar *chan_type;
    GQuark chan_type_quark;
    gint usage;
    
    chan_type = mcd_channel_get_channel_type (channel);
    chan_type_quark = mcd_channel_get_channel_type_quark (channel);
    usage = mcd_dispatcher_get_channel_type_usage (dispatcher,
						   chan_type_quark);
    
    /* Signal that the channel count has changed */
    g_signal_emit_by_name (G_OBJECT (obj),
			   "used-channels-count-changed",
			   chan_type, usage);
}

static void
_on_dispatcher_channel_dispatch_failed (McdDispatcher *dispatcher,
					McdChannel *channel, GError *error,
					McdService *obj)
{
    guint requestor_serial;
    gchar *requestor_client_id;
 
    g_debug ("%s", G_STRFUNC);
    g_object_get (channel, "requestor-serial", &requestor_serial,
		  "requestor-client-id", &requestor_client_id, NULL);
    
    if (requestor_client_id)
    {
	g_signal_emit_by_name (obj, "mcd-error", requestor_serial,
			       requestor_client_id, error->code);
	g_free (requestor_client_id);
    }
    
    g_debug ("MC ERROR (channel request): %s", error->message);
}

static void
mcd_dispose (GObject * obj)
{
    McdServicePrivate *priv;
    McdService *self = MCD_OBJECT (obj);

    priv = MCD_OBJECT_PRIV (self);

    if (priv->is_disposed)
    {
	return;
    }

    priv->is_disposed = TRUE;

    if (priv->presence_frame)
    {
	g_signal_handlers_disconnect_by_func (priv->presence_frame,
					      _on_account_status_changed,
					      self);
	g_signal_handlers_disconnect_by_func (priv->presence_frame,
					      _on_account_presence_changed,
					      self);
	g_signal_handlers_disconnect_by_func (priv->presence_frame,
					      _on_presence_requested, self);
	g_signal_handlers_disconnect_by_func (priv->presence_frame,
					      _on_presence_actual, self);
	g_signal_handlers_disconnect_by_func (priv->presence_frame,
					      _on_status_actual, self);
	g_object_unref (priv->presence_frame);
    }

    if (priv->dispatcher)
    {
	g_signal_handlers_disconnect_by_func (priv->dispatcher,
					      _on_dispatcher_channel_added,
					      self);
	g_signal_handlers_disconnect_by_func (priv->dispatcher,
					      _on_dispatcher_channel_removed,
					      self);
	g_signal_handlers_disconnect_by_func (priv->dispatcher,
					  _on_dispatcher_channel_dispatched,
					  self);
	g_signal_handlers_disconnect_by_func (priv->dispatcher,
				      _on_dispatcher_channel_dispatch_failed,
				      self);
	g_object_unref (priv->dispatcher);
    }

    if (self->main_loop)
    {
	g_main_loop_quit (self->main_loop);
	g_main_loop_unref (self->main_loop);
	self->main_loop = NULL;
    }

    if (G_OBJECT_CLASS (parent_class)->dispose)
    {
	G_OBJECT_CLASS (parent_class)->dispose (obj);
    }
}

static void
mcd_service_constructed (GObject *obj)
{
    McdServicePrivate *priv = MCD_OBJECT_PRIV (obj);

    g_debug ("%s called", G_STRFUNC);
    g_object_get (obj,
                  "presence-frame", &priv->presence_frame,
                  "dispatcher", &priv->dispatcher,
		  NULL);

    /* Setup presence signals */
    g_signal_connect (priv->presence_frame, "status-changed",
		      G_CALLBACK (_on_account_status_changed), obj);
    g_signal_connect (priv->presence_frame, "presence-changed",
		      G_CALLBACK (_on_account_presence_changed), obj);
    g_signal_connect (priv->presence_frame, "presence-requested",
		      G_CALLBACK (_on_presence_requested), obj);
    g_signal_connect (priv->presence_frame, "presence-actual",
		      G_CALLBACK (_on_presence_actual), obj);
    g_signal_connect (priv->presence_frame, "status-actual",
		      G_CALLBACK (_on_status_actual), obj);

    /* Setup dispatcher signals */
    g_signal_connect (priv->dispatcher, "channel-added",
		      G_CALLBACK (_on_dispatcher_channel_added), obj);
    g_signal_connect (priv->dispatcher, "channel-removed",
		      G_CALLBACK (_on_dispatcher_channel_removed), obj);
    g_signal_connect (priv->dispatcher, "dispatched",
		      G_CALLBACK (_on_dispatcher_channel_dispatched), obj);
    g_signal_connect (priv->dispatcher, "dispatch-failed",
		      G_CALLBACK (_on_dispatcher_channel_dispatch_failed), obj);

    mcd_register_dbus_object (MCD_OBJECT (obj));
    mcd_debug_print_tree (obj);

    if (G_OBJECT_CLASS (parent_class)->constructed)
	G_OBJECT_CLASS (parent_class)->constructed (obj);
}

static void
mcd_service_init (McdService * obj)
{
    McdServicePrivate *priv = MCD_OBJECT_PRIV (obj);

    obj->main_loop = g_main_loop_new (NULL, FALSE);

    priv->last_status = -1;
    g_debug ("%s called", G_STRFUNC);
}

static void
mcd_service_class_init (McdServiceClass * self)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (self);
    McdMissionClass *mission_class = MCD_MISSION_CLASS (self);

    parent_class = g_type_class_peek_parent (self);
    gobject_class->constructed = mcd_service_constructed;
    gobject_class->dispose = mcd_dispose;
    mission_class->disconnect = mcd_service_disconnect;

    g_type_class_add_private (gobject_class, sizeof (McdServicePrivate));

    /* AccountStatusChanged signal */
    signals[ACCOUNT_STATUS_CHANGED] =
	g_signal_new ("account-status-changed",
		      G_OBJECT_CLASS_TYPE (self),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, _mcd_marshal_VOID__UINT_UINT_UINT_STRING,
		      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT,
		      G_TYPE_UINT, G_TYPE_STRING);
#ifndef NO_NEW_PRESENCE_SIGNALS
    /* AccountStatusChanged signal */
    signals[ACCOUNT_PRESENCE_CHANGED] =
	g_signal_new ("account-presence-changed",
		      G_OBJECT_CLASS_TYPE (self),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, _mcd_marshal_VOID__UINT_UINT_UINT_STRING,
		      G_TYPE_NONE, 5, G_TYPE_UINT, G_TYPE_UINT,
		      G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
#endif
    /* libmc request_error signal */
    signals[ERROR] =
	g_signal_new ("mcd-error",
		      G_OBJECT_CLASS_TYPE (self),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, _mcd_marshal_VOID__UINT_STRING_UINT, G_TYPE_NONE,
		      3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT);
    /* PresenceStatusRequested signal */
    g_signal_new ("presence-status-requested",
		  G_OBJECT_CLASS_TYPE (self),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		  0,
		  NULL, NULL, g_cclosure_marshal_VOID__UINT,
		  G_TYPE_NONE, 1, G_TYPE_UINT);
#ifndef NO_NEW_PRESENCE_SIGNALS
    /* PresenceRequested signal */
    g_signal_new ("presence-requested",
		  G_OBJECT_CLASS_TYPE (self),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		  0,
		  NULL, NULL, _mcd_marshal_VOID__UINT_STRING,
		  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
#endif
    /* PresenceStatusActual signal */
    g_signal_new ("presence-status-actual",
		  G_OBJECT_CLASS_TYPE (self),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		  0,
		  NULL, NULL, g_cclosure_marshal_VOID__UINT,
		  G_TYPE_NONE, 1, G_TYPE_UINT);
#ifndef NO_NEW_PRESENCE_SIGNALS
    /* PresenceChanged signal */
    g_signal_new ("presence-changed",
		  G_OBJECT_CLASS_TYPE (self),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		  0,
		  NULL, NULL, _mcd_marshal_VOID__UINT_STRING,
		  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
#endif
    /* UsedChannelsCountChanged signal */
    signals[USED_CHANNELS_COUNT_CHANGED] =
	g_signal_new ("used-channels-count-changed",
		      G_OBJECT_CLASS_TYPE (self),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, _mcd_marshal_VOID__STRING_UINT,
		      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);
    /* StatusActual signal */
    signals[STATUS_ACTUAL] =
	g_signal_new ("status-actual",
		      G_OBJECT_CLASS_TYPE (self),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL, _mcd_marshal_VOID__UINT_UINT,
		      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
    
    dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (self),
				     &dbus_glib_mcd_service_object_info);
}

McdService *
mcd_service_new (void)
{
    McdService *obj;
    DBusGConnection *dbus_connection;
    TpDBusDaemon *dbus_daemon;
    GError *error = NULL;

    /* Initialize DBus connection */
    dbus_connection = dbus_g_bus_get (DBUS_BUS_STARTER, &error);
    if (dbus_connection == NULL)
    {
	g_printerr ("Failed to open connection to bus: %s", error->message);
	g_error_free (error);
	return NULL;
    }
    dbus_daemon = tp_dbus_daemon_new (dbus_connection);
    obj = g_object_new (MCD_TYPE_SERVICE,
			"dbus-daemon", dbus_daemon,
			NULL);
    g_object_unref (dbus_daemon);
    return obj;
}

void
mcd_service_run (McdService * self)
{
    g_main_loop_run (self->main_loop);
}

