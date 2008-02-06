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

#include "mc-client-lib-gen.h"
#include "mission-control.h"
#include "mission-control-signals-marshal.h"
#include <glib.h>
#include <string.h>

static void _handle_mcd_errors (DBusGProxy * missioncontrol, guint serial,
				gchar *client_id, guint reason,
			       	gpointer userdata);

static gboolean check_for_accounts (MissionControl * self);

static GObjectClass *parent_class = NULL;
static guint operation_id; /* A simple counter for execution order tracking;
			      must be global per process */
static GList *instances = NULL;
static DBusConnection *dbus_connection = NULL;
static gboolean mc_is_running = FALSE;

/* Signals */

enum
{
    ERROR,
    SERVICE_ENDED,
    LAST_SIGNAL
};

static guint libmc_signals[LAST_SIGNAL] = { 0 };

struct dbus_cb_data {
    McCallback callback;
    gpointer user_data;
};

struct idle_cb_data {
    MissionControl *self;
    McCallback callback;
    GError *error;
    gpointer user_data;
    guint id;
};

struct get_current_status_cb_data {
    McGetCurrentStatusCallback callback;
    gpointer user_data;
};

#define INVOKE_CALLBACK(mc, callback, data, code, ...) \
    if (callback) { \
	GError *error = NULL; \
	error = g_error_new (MC_ERROR, code, __VA_ARGS__); \
	queue_callback (mc, callback, error, data); \
    }

static void
free_idle_cb_data (gpointer data)
{
    struct idle_cb_data *cbdata = data;

    if (cbdata->error)
	g_error_free (cbdata->error);
    g_free (cbdata);
}

static gboolean
invoke_callback (gpointer data)
{
    struct idle_cb_data *cbdata = data;

    cbdata->callback (cbdata->self, cbdata->error, cbdata->user_data);
    cbdata->error = NULL;
    g_hash_table_remove (cbdata->self->active_callbacks,
			 GINT_TO_POINTER (cbdata->id));
    return FALSE;
}

static void
queue_callback (MissionControl *self, McCallback callback,
		GError *error, gpointer user_data)
{
    struct idle_cb_data *data;

    data = g_malloc (sizeof (struct idle_cb_data));
    data->self = self;
    data->callback = callback;
    data->error = error;
    data->user_data = user_data;
    data->id = g_idle_add (invoke_callback, data);
    g_hash_table_insert (self->active_callbacks, GINT_TO_POINTER (data->id),
			 data);
}

static void
dbus_async_cb (DBusGProxy * proxy, GError * error, gpointer userdata)
{
    struct dbus_cb_data *cb_data = (struct dbus_cb_data *)userdata;

    if (error)
	g_debug ("%s: Error: %s (%u)", G_STRFUNC, error->message, error->code);

    if (cb_data->callback)
	cb_data->callback ((MissionControl *)proxy, error, cb_data->user_data);
    else
    {
	/* there's callback to free the error; we must do that ourselves */
	if (error)
	    g_error_free (error);
    }

    g_free (cb_data);
}

static DBusHandlerResult
dbus_filter_func (DBusConnection *connection,
		  DBusMessage    *message,
		  gpointer        data)
{
    DBusHandlerResult result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (dbus_message_is_signal (message,
				"org.freedesktop.DBus",
				"NameOwnerChanged")) {
	const gchar *name = NULL;
	const gchar *prev_owner = NULL;
	const gchar *new_owner = NULL;
	DBusError error = {0};

	dbus_error_init (&error);

	if (!dbus_message_get_args (message,
				    &error,
				    DBUS_TYPE_STRING,
				    &name,
				    DBUS_TYPE_STRING,
				    &prev_owner,
				    DBUS_TYPE_STRING,
				    &new_owner,
				    DBUS_TYPE_INVALID)) {

	    g_debug ("error: %s", error.message);
	    dbus_error_free (&error);

	    return result;
	}

	if (name &&
	    strcmp (name, MISSION_CONTROL_SERVICE) == 0)
	{
	    if (prev_owner && prev_owner[0] != '\0')
	    {
		GList *list;

		for (list = instances; list; list = list->next)
		    g_signal_emit (list->data, libmc_signals[SERVICE_ENDED], 0);
	    }
	    mc_is_running = new_owner && (new_owner[0] != '\0');
	}
    }

    return result;
}

GQuark mission_control_error_quark (void)
{
    static GQuark quark = 0;
    if (quark == 0)
        quark = g_quark_from_static_string ("mission-control-quark");
    return quark;
}

static void instance_finalized (gpointer data, GObject *object)
{
    instances = g_list_remove (instances, object);
    if (!instances)
    {
	dbus_connection_remove_filter (dbus_connection, dbus_filter_func,
				       NULL);
	dbus_connection_unref (dbus_connection);
	dbus_connection = NULL;
    }
}

static void initialize_dbus_filter (DBusGConnection *connection)
{
    DBusError error;

    /* Add a filter to detect the service exits and emit the ServiceEnded
     * signal accordingly */
    dbus_connection = dbus_g_connection_get_connection (connection);
    dbus_connection_ref (dbus_connection);
    dbus_error_init (&error);
    dbus_connection_add_filter (dbus_connection,
				dbus_filter_func,
				NULL, NULL);
    dbus_bus_add_match (dbus_connection,
			"type='signal'," "interface='org.freedesktop.DBus',"
			"member='NameOwnerChanged'", &error);
    if (dbus_error_is_set (&error))
    {
	g_warning ("Match rule adding failed");
	dbus_error_free (&error);
    }

    mc_is_running = dbus_bus_name_has_owner (dbus_connection,
					     MISSION_CONTROL_SERVICE, NULL);
}

static void
_missioncontrol_register_signal_marshallers (void)
{
    /* Register the AccountStatusChanged signal */
    dbus_g_object_register_marshaller
	(mission_control_signals_marshal_VOID__UINT_UINT_UINT_STRING,
	 G_TYPE_NONE, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING,
	 G_TYPE_INVALID);
    /* Register the error signal */
    dbus_g_object_register_marshaller
	(mission_control_signals_marshal_VOID__UINT_STRING_UINT, G_TYPE_NONE,
	 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
    /* Register the PresenceStatusRequested/Actual signal */
    dbus_g_object_register_marshaller
	(mission_control_signals_marshal_VOID__UINT, G_TYPE_NONE, G_TYPE_UINT,
	 G_TYPE_INVALID);
    /* Register the StatusActual signal */
    dbus_g_object_register_marshaller
	(mission_control_signals_marshal_VOID__UINT_UINT, G_TYPE_NONE,
	 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
    /* Register the UsedChannelsCountChanged signal */
    dbus_g_object_register_marshaller
	(mission_control_signals_marshal_VOID__STRING_UINT, G_TYPE_NONE,
	 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
#ifndef NO_NEW_PRESENCE_SIGNALS
    /* Register the AccountPresenceChanged signal */
    dbus_g_object_register_marshaller
	(mission_control_signals_marshal_VOID__UINT_UINT_STRING_UINT_STRING,
	 G_TYPE_NONE, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT,
	 G_TYPE_STRING, G_TYPE_INVALID);
    /* Register the PresenceChanged signal */
    dbus_g_object_register_marshaller
	(mission_control_signals_marshal_VOID__UINT_STRING, G_TYPE_NONE,
	 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
#endif
}

static void
mission_control_init (GTypeInstance * instance, gpointer g_class)
{
    MissionControl *self = MISSIONCONTROL (instance);
    self->first_run = TRUE;

    self->active_callbacks =
	g_hash_table_new_full (g_direct_hash, g_direct_equal,
			       (GDestroyNotify)g_source_remove,
			       free_idle_cb_data);
}


static void
mission_control_dispose (GObject * obj)
{
    MissionControl *self = MISSIONCONTROL (obj);

    if (self->first_run)
    {
	self->first_run = FALSE;
    }

    if (self->active_callbacks)
    {
	g_hash_table_destroy (self->active_callbacks);
	self->active_callbacks = NULL;
    }

    if (G_OBJECT_CLASS (parent_class)->dispose)
    {
	G_OBJECT_CLASS (parent_class)->dispose (obj);
    }
}


static void
mission_control_class_init (MissionControlClass * klass)
{
    GObjectClass *obj = G_OBJECT_CLASS (klass);
    parent_class = g_type_class_peek_parent (klass);

    obj->set_property = parent_class->set_property;
    obj->get_property = parent_class->get_property;
    obj->dispose = mission_control_dispose;
    _missioncontrol_register_signal_marshallers ();

    /**
     * MissionControl::Error:
     * @self: The #MissionControl object.
     * @operation_id: The unique ID of the operation which caused the error.
     * When this signal is emitted to report a failure in handling a channel,
     * this parameter holds the same @operation_id returned by the channel
     * request function call.
     * @error_code: The #MCError code describing the error.
     *
     * This signal is emitted when an error is raised from the mission-control
     * server. This is not raised in response to some API call failing (they
     * already provide a way to report errors), but rather for informing the
     * client of some unexpected event, such as a channel handler failing.
     */
    libmc_signals[ERROR] =
	g_signal_new ("Error",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		      0,
		      NULL, NULL,
		      mission_control_signals_marshal_VOID__UINT_UINT,
		      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

    /**
     * MissionControl::ServiceEnded:
     * @self: The #MissionControl object
     *
     * This signal is emitted when a mission-control server process has exited.
     */
    libmc_signals[SERVICE_ENDED] =
	g_signal_new ("ServiceEnded",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST,
		      0,
		      NULL, NULL,
		      g_cclosure_marshal_VOID__VOID,
		      G_TYPE_NONE, 0);
}


GType
mission_control_get_type (void)
{
    static GType type = 0;
    if (type == 0)
    {
	static const GTypeInfo info = {
	    sizeof (MissionControlClass),
	    NULL,
	    NULL,
	    (GClassInitFunc) mission_control_class_init,
	    NULL,
	    NULL,
	    sizeof (MissionControl),
	    0,
	    (GInstanceInitFunc) mission_control_init
	};
	type = g_type_register_static (DBUS_TYPE_G_PROXY,
				       "MissionControl", &info, 0);
    }
    return type;
}


/**
 * mission_control_new:
 * @connection: The D-BUS connection for this object
 *
 * Creates a new Mission Control client library object.
 * 
 * Return value: A new mc (Mission Control) library object, or NULL if unsuccesful
 */
MissionControl *
mission_control_new (DBusGConnection * connection)
{
    g_return_val_if_fail (connection != NULL, NULL);
    MissionControl *mc_obj = NULL;

    /* Create the proxy object that is used for performing
     * the method calls on the Mission Control service */

    mc_obj = g_object_new (MISSIONCONTROL_TYPE,
			   "name", MISSION_CONTROL_SERVICE,
			   "path", MISSION_CONTROL_PATH,
			   "interface", MISSION_CONTROL_IFACE,
			   "connection", connection, NULL);
    if (!instances)
    {
	/* this is the first instance created in this process:
	 * perform some global initializations */
	initialize_dbus_filter (connection);
    }
    /* Add the object to the list of living MC instances, and add a watch for
     * its finalization */
    instances = g_list_prepend (instances, mc_obj);
    g_object_weak_ref (G_OBJECT (mc_obj), instance_finalized, NULL);

    dbus_g_proxy_add_signal (DBUS_G_PROXY (mc_obj), "AccountStatusChanged",
			     G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
			     G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (DBUS_G_PROXY (mc_obj), "McdError", G_TYPE_UINT,
			     G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (DBUS_G_PROXY (mc_obj), "McdError",
				 G_CALLBACK (_handle_mcd_errors), mc_obj, NULL);
    dbus_g_proxy_add_signal (DBUS_G_PROXY (mc_obj), "PresenceStatusRequested",
			     G_TYPE_UINT, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (DBUS_G_PROXY (mc_obj), "PresenceStatusActual",
			     G_TYPE_UINT, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (DBUS_G_PROXY (mc_obj), "UsedChannelsCountChanged",
			     G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (DBUS_G_PROXY (mc_obj), "StatusActual",
			     G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
#ifndef NO_NEW_PRESENCE_SIGNALS
    dbus_g_proxy_add_signal (DBUS_G_PROXY (mc_obj), "AccountPresenceChanged",
			     G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING,
			     G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (DBUS_G_PROXY (mc_obj), "PresenceChanged",
			     G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
#endif
    return mc_obj;
}


/**
 * mission_control_set_presence:
 * @self: The #MissionControl object.
 * @presence: Integer specifying the presence status code
 * @message: Optional presence associated message
 * @callback: a #McCallback function to be notified about any errors
 * @user_data: data to be passed to the @callback function
 *
 * Sets presence for the accounts.
 */
void
mission_control_set_presence (MissionControl * self,
			      McPresence presence, const gchar * message,
			      McCallback callback, gpointer user_data)
{
    struct dbus_cb_data *cb_data;
  
    /* Check whether we have any accounts to set presence for */
    if (!check_for_accounts (self))
    {
	INVOKE_CALLBACK (self, callback, user_data, MC_NO_ACCOUNTS_ERROR, " ");
	return;
    }

    cb_data = g_malloc (sizeof (struct dbus_cb_data));
    g_assert (cb_data != NULL);
    cb_data->callback = callback;
    cb_data->user_data = user_data;
    mission_control_dbus_set_presence_async (DBUS_G_PROXY (self),
					     (gint) presence, message,
					     dbus_async_cb, cb_data);
}

/**
 * mission_control_get_presence:
 * @self: The #MissionControl object.
 * @error: address where an error can be returned, or NULL.
 *
 * Gets the currently requested presence status.
 *
 * Return value: The currently requested presence status
 */
McPresence
mission_control_get_presence (MissionControl * self, GError **error)
{
    /* To indicate failure, we set the presence to unset */
    McPresence presence = MC_PRESENCE_UNSET;

    /* Check whether Mission Control is running; if not, it's safe to
     * say that we're offline without starting it to perform the
     * query.  */
    if (!mc_is_running)
    {
	g_debug ("%s: MC not running.", G_STRFUNC);
	g_set_error (error, MC_ERROR, MC_DISCONNECTED_ERROR, "MC not running");
	return MC_PRESENCE_OFFLINE;
    }

    if (!mission_control_dbus_get_presence (DBUS_G_PROXY (self),
					    &presence, error))
    {
	presence = MC_PRESENCE_UNSET;
    }

    return presence;
}

/**
 * mission_control_get_presence_message:
 * @self: The #MissionControl object.
 * @error: address where an error can be returned, or NULL.
 *
 * Gets the currently requested presence message.
 *
 * Returns: The currently requested presence message
 */
gchar *
mission_control_get_presence_message (MissionControl * self, GError **error)
{
    gchar *message;

    /* Check whether Mission Control is running; if not, it's safe to
     * say that we're offline without starting it to perform the
     * query.  */
    if (!mc_is_running)
    {
	g_debug ("%s: MC not running.", G_STRFUNC);
	g_set_error (error, MC_ERROR, MC_DISCONNECTED_ERROR, "MC not running");
	return NULL;
    }

    if (!mission_control_dbus_get_presence_message (DBUS_G_PROXY (self),
						    &message, error))
    {
	message = NULL;
    }

    return message;
}

/**
 * mission_control_get_presence_actual:
 * @self: The #MissionControl object.
 * @error: address where an error can be returned, or NULL.
 *
 * Gets the actual presence status.
 *
 * Return value: The actual presence status
 */
McPresence
mission_control_get_presence_actual (MissionControl * self, GError **error)
{
    /* To indicate failure, we set the presence to unset */
    McPresence presence = MC_PRESENCE_UNSET;

    /* Check whether Mission Control is running; if not, it's safe to
     * say that we're offline without starting it to perform the
     * query.  */
    if (!mc_is_running)
    {
	g_debug ("%s: MC not running.", G_STRFUNC);
	g_set_error (error, MC_ERROR, MC_DISCONNECTED_ERROR, "MC not running");
	return MC_PRESENCE_OFFLINE;
    }

    if (!mission_control_dbus_get_presence_actual (DBUS_G_PROXY (self),
						   &presence, error))
    {
	presence = MC_PRESENCE_UNSET;
    }

    return presence;
}

/**
 * mission_control_get_presence_message_actual:
 * @self: The #MissionControl object.
 * @error: address where an error can be returned, or NULL.
 *
 * Gets the actual presence message.
 *
 * Returns: The actual presence message
 */
gchar *
mission_control_get_presence_message_actual (MissionControl * self,
					     GError **error)
{
    gchar *message;

    /* Check whether Mission Control is running; if not, it's safe to
     * say that we're offline without starting it to perform the
     * query.  */
    if (!mc_is_running)
    {
	g_debug ("%s: MC not running.", G_STRFUNC);
	g_set_error (error, MC_ERROR, MC_DISCONNECTED_ERROR, "MC not running");
	return NULL;
    }

    if (!mission_control_dbus_get_presence_message_actual (DBUS_G_PROXY (self),
							   &message, error))
    {
	message = NULL;
    }

    return message;
}

/**
 * mission_control_request_channel:
 * @self: The #MissionControl object.
 * @account: The account which will join a new channel or request
 * joining to an existing channel
 * @type: a D-Bus interface name representing base channel type
 * @handle: The handle we want to initiate the communication with
 * @handle_type: The type of the handle we are initiating the
 *  communication with. See #TelepathyHandleType
 * @callback: a #McCallback function to be notified about any errors
 * @user_data: data to be passed to the @callback function
 *
 * Requests creation of a new channel, or join to an existing channel.
 *
 * Return value: An operation ID which can be used to cancel the request using
 * #mission_control_cancel_channel_request.
 */
guint
mission_control_request_channel (MissionControl * self,
				 McAccount * account,
				 const gchar * type,
				 guint handle,
				 TelepathyHandleType handle_type,
				 McCallback callback,
				 gpointer user_data)
{
    struct dbus_cb_data *cb_data;
    const gchar *account_name = mc_account_get_unique_name (account);
    operation_id++;

    if (account_name == NULL)
    {
	INVOKE_CALLBACK (self, callback, user_data, MC_INVALID_ACCOUNT_ERROR,
			 " ");
	return operation_id;
    }

    /* Check whether we have any accounts to request channel for */
    if (!check_for_accounts (self))
    {
	INVOKE_CALLBACK (self, callback, user_data, MC_NO_ACCOUNTS_ERROR, " ");
	return operation_id;
    }

    cb_data = g_malloc (sizeof (struct dbus_cb_data));
    g_assert (cb_data != NULL);
    cb_data->callback = callback;
    cb_data->user_data = user_data;
    mission_control_dbus_request_channel_async (DBUS_G_PROXY (self),
						account_name,
						type, handle, handle_type,
						operation_id,
						dbus_async_cb,
						cb_data);

    return operation_id;
}


/**
 * mission_control_request_channel_with_string_handle:
 * @self: The #MissionControl object.
 * @account: The account which will join a new channel or request joining to an
 * existing channel
 * @type: a D-Bus interface name representing base channel type
 * @handle: The handle we want to initiate the communication with
 * @vcard_field: The vcard_field of the handle, may be null for default profile
 * vcard field
 * @handle_type: The type of the handle we are initiating the communication
 * with. See #TelepathyHandleType
 * @callback: a #McCallback function to be notified about any errors
 * @user_data: data to be passed to the @callback function
 *
 * Requests creation of a new channel, or join to an existing channel. Differs
 * from the plain #mission_control_request_channel by taking handles as
 * strings, which will be resolved to integers by MC.
 *
 * Return value: An operation ID which can be used to cancel the request using
 * #mission_control_cancel_channel_request.
 */
guint
mission_control_request_channel_with_string_handle_and_vcard_field (MissionControl * self,
						    McAccount * account,
						    const gchar * type,
						    const gchar * handle,
						    const gchar * vcard_field,
						    TelepathyHandleType
						    handle_type,
						    McCallback callback,
						    gpointer user_data)
{
    struct dbus_cb_data *cb_data;
    operation_id++;
    const gchar *account_name = mc_account_get_unique_name (account);
    char * mangled_handle = NULL;

    if (account_name == NULL)
    {
	INVOKE_CALLBACK (self, callback, user_data, MC_INVALID_ACCOUNT_ERROR,
			 " ");
	return operation_id;
    }

    /* Check whether we have any accounts to request channel for */
    if (!check_for_accounts (self))
    {
	INVOKE_CALLBACK (self, callback, user_data, MC_NO_ACCOUNTS_ERROR, " ");
	return operation_id;
    }

    /* mangle the handle with the vcard_field */
    if (vcard_field != NULL) {
	McProfile *profile = mc_account_get_profile (account);

	if (G_LIKELY (profile))
	{
	    const char * profile_vcard_field = mc_profile_get_vcard_field (profile);

	    // TODO: this is where from the profiles or from the provisioning
	    // we must figure out how to actually mangle user addresses from
	    // foreign vcard fields to something the connection manager will
	    // understand.
	    // For now this is just lowercasing the vcard field and prepending
	    // it to the address

	    /* only mangle if it is not the default vcard field */
	    if (profile_vcard_field == NULL ||
	       	strcmp(vcard_field, profile_vcard_field) != 0) {

		const char * mangle = mc_profile_get_vcard_mangle(profile, vcard_field);
		g_debug("MANGLE: %s", mangle);
		if (mangle) {
		    mangled_handle = g_strdup_printf(mangle, handle);
		} else {
		    if (strcmp(vcard_field, "TEL") == 0) {
			// TEL mangling
			char ** split = g_strsplit_set(handle, " -,.:;", -1);
			mangled_handle = g_strjoinv("", split);
			g_strfreev(split);
		    } else {
			// generic mangling
			char * lower_vcard_field = g_utf8_strdown(vcard_field, -1);
			mangled_handle = g_strdup_printf("%s:%s", lower_vcard_field, handle);
			g_free(lower_vcard_field);
		    }
		}
		g_debug ("%s: mangling: %s (%s)", G_STRFUNC, mangled_handle, vcard_field);
	    }
	    g_object_unref (profile);
        }
    }

    cb_data = g_malloc (sizeof (struct dbus_cb_data));
    g_assert (cb_data != NULL);
    cb_data->callback = callback;
    cb_data->user_data = user_data;

    mission_control_dbus_request_channel_with_string_handle_async(
            DBUS_G_PROXY (self),
            account_name,
            type,
            (mangled_handle)?mangled_handle:handle,
            handle_type,
            operation_id,
            dbus_async_cb, cb_data);

    g_free(mangled_handle);

    return operation_id;
}


/**
 * see @mission_control_request_channel_with_string_handle_and_vcard_field.
 */
guint
mission_control_request_channel_with_string_handle (MissionControl * self,
						    McAccount * account,
						    const gchar * type,
						    const gchar * handle,
						    TelepathyHandleType
						    handle_type,
						    McCallback callback,
						    gpointer user_data)
{
    return mission_control_request_channel_with_string_handle_and_vcard_field(
            self,
            account,
            type,
            handle,
            NULL,
            handle_type,
            callback,
            user_data);
}


/**
 * mission_control_cancel_channel_request:
 * @self: The #MissionControl object.
 * @operation_id: the operation id of the request to cancel, as returned
 * by #mission_control_request_channel_with_string_handle.
 * @error: address where an error can be returned, or NULL.
 *
 * Cancel a channel request; a process can only cancel the requests that were
 * originated by itself.
 *
 * Returns: %TRUE on success, %FALSE otherwise.
 */
gboolean
mission_control_cancel_channel_request (MissionControl *self,
					guint operation_id,
					GError **error)
{
    return mission_control_dbus_cancel_channel_request (DBUS_G_PROXY (self),
						       	operation_id,
						       	error);
}

/**
 * mission_control_connect_all_with_default_presence:
 * @self: The #MissionControl object.
 * @callback: a #McCallback function to be notified about any errors
 * @user_data: data to be passed to the @callback function
 *
 * Connect all accounts using default presence,
 * or HIDDEN if default presence is OFFLINE.
 * If accounts are already connected do nothing.
 */
void
mission_control_connect_all_with_default_presence (MissionControl * self,
						   McCallback callback,
						   gpointer user_data)
{
    struct dbus_cb_data *cb_data;

    /* Check whether we have any accounts to set presence for */
    if (!check_for_accounts (self))
    {
	INVOKE_CALLBACK (self, callback, user_data, MC_NO_ACCOUNTS_ERROR,
			 " ");
	return;
    }

    cb_data = g_malloc (sizeof (struct dbus_cb_data));
    g_assert (cb_data != NULL);
    cb_data->callback = callback;
    cb_data->user_data = user_data;
    mission_control_dbus_connect_all_with_default_presence_async
	(DBUS_G_PROXY (self),
	 dbus_async_cb, cb_data);
}

/**
 * mission_control_get_connection_status:
 * @self: The #MissionControl object.
 * @account: The account whose connection status is inspected
 * @error: address where an error can be returned, or NULL.
 *
 * Request a status code describing the status of the connection that the
 * provided account currently uses.
 *
 * Return value: A status code describing the status of the specified connection
 * eg. CONNECTED = 0, CONNECTING = 1, DISCONNECTED = 2
 */
guint
mission_control_get_connection_status (MissionControl * self,
				       McAccount * account,
				       GError **error)
{
    /* XXX TP_CONN_STATUS_DISCONNECTED is used as an UNKNOWN status is not
     * available */
    guint conn_status = TP_CONN_STATUS_DISCONNECTED;
    const gchar *account_name = mc_account_get_unique_name (account);

    if (account_name == NULL)
    {
	g_set_error (error, MC_ERROR, MC_INVALID_ACCOUNT_ERROR, " ");
	return conn_status;
    }

    /* Check whether we have any accounts to connection status for */
    if (!check_for_accounts (self))
    {
	g_set_error (error, MC_ERROR, MC_NO_ACCOUNTS_ERROR, " ");
	return conn_status;
    }

    /* Check whether Mission Control is running; if not, it's safe to
     * say that we're offline. */
    if (!mc_is_running)
    {
	g_debug ("%s: MC not running.", G_STRFUNC);
	g_set_error (error, MC_ERROR, MC_DISCONNECTED_ERROR, "MC not running");
	return TP_CONN_STATUS_DISCONNECTED;
    }

    mission_control_dbus_get_connection_status (DBUS_G_PROXY (self),
						account_name,
						&conn_status, error);
    return conn_status;
}

/**
 * mission_control_get_online_connections:
 * @self: The #MissionControl object.
 * @error: address where an error can be returned, or NULL.
 *
 * Request an array of the accounts that have an active connection.
 * 
 * Return value: A list of McAccounts corresponding to the online
 * connections
 */
GSList *
mission_control_get_online_connections (MissionControl * self, GError **error)
{
    GSList *online_conns = NULL;
    gchar **names = NULL, **name;

    /* Check whether we have any accounts, otherwise we do not have
     * connections either */

    if (!check_for_accounts (self))
    {
	g_set_error (error, MC_ERROR, MC_NO_ACCOUNTS_ERROR, " ");
	return NULL;
    }

    if (!mc_is_running)
    {
	g_debug ("%s: MC not running.", G_STRFUNC);
	g_set_error (error, MC_ERROR, MC_NO_MATCHING_CONNECTION_ERROR,
		     "MC not running");
	return NULL;
    }

    if (!mission_control_dbus_get_online_connections (DBUS_G_PROXY (self),
						      &names, error))
    {
	return NULL;
    }
    /* Create McAccounts with all the account names */
    for (name = names; *name != NULL; name++)
    {
	McAccount *acc = mc_account_lookup (*name);

	if (acc != NULL)
	{
	    online_conns = g_slist_prepend (online_conns, acc);
	}
    }
    g_strfreev (names);

    return online_conns;
}

/**
 * mission_control_get_connection:
 * @self: The #MissionControl object.
 * @account: The account the connection is created for.
 * @error: address where an error can be returned, or NULL.
 *
 * Gets a connection object for the specified account name.
 * 
 * Return value: An existing TpConn object, NULL if the account is not connected
 */
TpConn *
mission_control_get_connection (MissionControl * self, McAccount * account,
				GError **error)
{
    TpConn *tp_conn = NULL;
    gchar *bus_name = NULL, *obj_path = NULL;
    const gchar *account_name = mc_account_get_unique_name (account);
    DBusGConnection *connection = NULL;
    guint status;

    if (account_name == NULL)
    {
	g_set_error (error, MC_ERROR, MC_INVALID_ACCOUNT_ERROR, " ");
	return NULL;
    }

    /* Check whether we have any accounts to request connection for */
    if (!check_for_accounts (self))
    {
	g_set_error (error, MC_ERROR, MC_NO_ACCOUNTS_ERROR, " ");
	return NULL;
    }

    /* If MC isn't running there won't be any connections. */
    if (!mc_is_running)
    {
	g_debug ("%s: MC not running.", G_STRFUNC);
	g_set_error (error, MC_ERROR, MC_DISCONNECTED_ERROR, "MC not running");
	return NULL;
    }

    g_object_get (G_OBJECT (self), "connection", &connection, NULL);

    if (connection == NULL)
    {
	g_set_error (error, MC_ERROR, MC_DISCONNECTED_ERROR,
		     "Cannot get D-BUS connection");
	return NULL;
    }

    /* Match the account name and corresponding connection parameters in
     * Mission Control */

    if (!mission_control_dbus_get_connection (DBUS_G_PROXY (self), account_name,
					      &bus_name, &obj_path, error))
    {
	dbus_g_connection_unref (connection);
	return NULL;
    }

    /* Create a local copy of the TpConn object from the acquired information.
     * We do not need to use the connect method via a connection manager,
     * because the connection is already initialized by MissionControl. */

    tp_conn = tp_conn_new_without_connect (connection, bus_name, obj_path,
					   &status, NULL);

    if (tp_conn == NULL)
    {
	g_set_error (error, MC_ERROR, MC_DISCONNECTED_ERROR,
		     "Cannot get telepathy connection");
    }

    g_free (bus_name);
    g_free (obj_path);
    dbus_g_connection_unref (connection);

    return tp_conn;
}

/**
 * mission_control_get_account_for_connection:
 * @self: The #MissionControl object.
 * @connection: connection object to get the account for
 * @error: address where an error can be returned, or NULL.
 *
 * Gets the account corresponding to the connection object.
 * Note that as a result the caller owns a reference to the account object.
 *
 * Return value: The matching account object, NULL on error
 */
McAccount *
mission_control_get_account_for_connection (MissionControl * self,
					    TpConn * connection,
					    GError **error)
{
    const gchar *connection_object_path;
    gchar *account_unique_name;
    McAccount *account;

    /* Check whether Mission Control is running; if not, it's safe to
     * say that there are no accounts or connections in that case
     * without starting it to perform the query.  */
    if (!mc_is_running)
    {
	g_debug ("%s: MC not running.", G_STRFUNC);
	g_set_error (error, MC_ERROR, MC_DISCONNECTED_ERROR, "MC not running");
	return NULL;
    }

    connection_object_path = dbus_g_proxy_get_path (DBUS_G_PROXY (connection));

    if (!mission_control_dbus_get_account_for_connection (DBUS_G_PROXY (self),
							  connection_object_path,
							  &account_unique_name,
							  error))
    {
	g_warning ("%s: Getting account for the connection failed", G_STRFUNC);
	return NULL;
    }

    account = mc_account_lookup (account_unique_name);

    g_free (account_unique_name);

    return account;
}

/**
 * mission_control_get_used_channels_count:
 * @self: The #MissionControl object.
 * @type: Type of the counted channels as a GQuark (see the defines in
 * tp-chan.h)
 * @error: address where an error can be returned, or NULL.
 *
 * Counts the number of active channels of specified type.
 *
 * Return value: The number of channels currently in use (negative if the query
 * fails)
 */
gint mission_control_get_used_channels_count(MissionControl *self,
					     GQuark type, GError **error)
{
    gint ret;

    /* Check whether Mission Control is running; if not, there should be
       no active channels without starting it to perform the query.  */

    if (!mc_is_running)
    {
	g_set_error (error, MC_ERROR, MC_DISCONNECTED_ERROR, "MC not running");
	return 0;
    }

    /* We'll have to convert the quark here to a string, because it will
       not match the quarks in another process */

    if (!mission_control_dbus_get_used_channels_count(DBUS_G_PROXY(self),
						      g_quark_to_string(type),
						      (guint *)&ret,
						      error))
    {
	/* We'll have to make a difference between a failed request and 0
	   channels in use */
	return -1;
    }

    return ret;
}

static void
get_current_status_cb (DBusGProxy * proxy,
		       McStatus status,
		       McPresence presence,
		       McPresence requested_presence,
		       GPtrArray *accounts_array,
		       GError *error, gpointer userdata)
{
    struct get_current_status_cb_data *cb_data = (struct get_current_status_cb_data *)userdata;
    McAccountStatus *accounts, *account;
    GType type;
    gsize n_accounts;
    gint i;

    if (error)
	g_debug ("%s: Error: %s (%u)", G_STRFUNC, error->message, error->code);

    type = dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_UINT,
				   G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
    accounts = g_new (McAccountStatus, accounts_array->len);
    for (i = 0, account = accounts; i < accounts_array->len; i++, account++)
    {
	GValue account_value = { 0, };

	g_value_init (&account_value, type);
	g_value_take_boxed (&account_value,
				  g_ptr_array_index (accounts_array, i));
	dbus_g_type_struct_get (&account_value,
				0, &account->unique_name,
				1, &account->status,
				2, &account->presence,
				3, &account->reason,
				G_MAXUINT);
	g_value_unset (&account_value);
    }
    n_accounts = accounts_array->len;

    g_ptr_array_free (accounts_array, TRUE);
    cb_data->callback ((MissionControl *)proxy,
		       status, presence, requested_presence,
		       accounts, n_accounts,
		       error, cb_data->user_data);

    g_free (cb_data);
}

/**
 * mission_control_get_current_status:
 * @self: The #MissionControl object.
 * @callback: a #McGetCurrentStatusCallback function which will be called with
 * the requested information.
 * @user_data: data to be passed to the @callback function
 *
 * Queries the status of all the enabled accounts, as well as the global
 * presence and status. This information will be returned in the registered
 * @callback, which will be resposible for freeing all the dynamic data.
 */
void
mission_control_get_current_status (MissionControl * self,
				    McGetCurrentStatusCallback callback,
				    gpointer user_data)
{
    struct get_current_status_cb_data *cb_data;

    /* Check whether Mission Control is running; if not, it's safe to
     * say that we're offline without starting it to perform the
     * query.  */
    g_assert (callback != NULL);
    if (!mc_is_running)
    {
	GError *error = NULL;
	g_debug ("%s: MC not running.", G_STRFUNC);
	error = g_error_new (MC_ERROR, MC_DISCONNECTED_ERROR, " ");
	callback (self, 0, 0, 0, NULL, 0, error, user_data);
	return;
    }

    cb_data = g_malloc (sizeof (struct get_current_status_cb_data));
    g_assert (cb_data != NULL);
    cb_data->callback = callback;
    cb_data->user_data = user_data;
    mission_control_dbus_get_current_status_async (DBUS_G_PROXY (self),
						   get_current_status_cb,
						   cb_data);
}

/**
 * mission_control_free_account_statuses:
 * @accounts: The array of #McAccountStatus.
 *
 * Frees the @accounts array.
 */
void
mission_control_free_account_statuses (McAccountStatus *accounts)
{
    McAccountStatus *account;

    for (account = accounts; account != NULL; account++)
	g_free (account->unique_name);
    g_free (accounts);
}


/* We handle errors coming via MCD here. If the pid for the error
   matches our pid, we will emit the signal, otherwise we just
   silently ignore it to avoid other instances using libmissioncontrol
   getting confused */

static void
_handle_mcd_errors (DBusGProxy * missioncontrol, guint serial,
		    gchar *client_id,
		    guint reason, gpointer userdata)
{
    MissionControl *self = (MissionControl *) userdata;
    DBusGConnection *connection;
    const gchar *self_client_id = NULL;

    g_object_get (G_OBJECT (missioncontrol), "connection", &connection, NULL);

    if (!connection)
	return;

    self_client_id = dbus_bus_get_unique_name (
			dbus_g_connection_get_connection (connection));
    dbus_g_connection_unref (connection);

    g_debug ("%s: client id is %s (error comes for %s)", G_STRFUNC, self_client_id, client_id);
    if (client_id == NULL || (self_client_id != NULL &&
			      strcmp(client_id, self_client_id) == 0))
    {
	g_signal_emit_by_name (self, "Error", serial, reason);
    }
}


/* A helper function to determine if there are valid accounts. Mainly
   useful for avoiding useless launches of Mission Control */

static gboolean
check_for_accounts (MissionControl * self)
{
    GList *enabled_accounts = mc_accounts_list_by_enabled (TRUE);

    /* Do we have any enabled accounts? If not, fail. */

    /* ? Should we add another error definition for situations where we
     * have accounts, but none of them are enabled? */

    if (!enabled_accounts || g_list_length (enabled_accounts) == 0)
    {
	mc_accounts_list_free (enabled_accounts);
	g_debug ("%s: No enabled accounts", G_STRFUNC);
	return FALSE;
    }

    mc_accounts_list_free (enabled_accounts);
    return TRUE;
}

/**
 * mission_control_remote_avatar_changed:
 * @self: the #MissionControl object.
 * @connection: connection object which received the avatar update.
 * @contact_id: the Telepathy self contact handle.
 * @token: the Telepathy token for the new avatar.
 * @error: address where an error can be returned, or NULL.
 *
 * This function is responsible for taking actions in response to the own
 * avatar being received from the server. Depending on the situation, this
 * function can update the local avatar in our #McAccount.
 *
 * Returns: %TRUE if success, %FALSE if some error occurred.
 */
gboolean
mission_control_remote_avatar_changed (MissionControl *self,
				       TpConn *connection, guint contact_id,
				       const gchar *token, GError **error)
{
    const gchar *connection_object_path;

    /* Check whether Mission Control is running; if not, it's safe to
     * say that there are no accounts or connections in that case
     * without starting it to perform the query.  */
    if (!mc_is_running)
    {
	g_debug ("%s: MC not running.", G_STRFUNC);
	g_set_error (error, MC_ERROR, MC_DISCONNECTED_ERROR, "MC not running");
	return FALSE;
    }

    connection_object_path = dbus_g_proxy_get_path (DBUS_G_PROXY (connection));

    return mission_control_dbus_remote_avatar_changed (DBUS_G_PROXY (self),
						       connection_object_path,
						       contact_id, token,
						       error);
}

gboolean
mission_control_register_filter (MissionControl *self,
				 const gchar *bus_name,
				 const gchar *object_path,
				 const gchar *channel_type,
				 McFilterPriority priority,
				 McFilterFlag flags,
				 GError **error)
{
    return mission_control_dbus_register_filter (DBUS_G_PROXY (self),
						 bus_name, object_path,
						 channel_type,
						 priority, flags,
						 error);
}


