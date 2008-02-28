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

#ifndef MISSION_CONTROL_LIB_H
#define MISSION_CONTROL_LIB_H

#ifndef DBUS_API_SUBJECT_TO_CHANGE
#define DBUS_API_SUBJECT_TO_CHANGE
#endif

#include <sys/types.h>
#include <unistd.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/channel.h>

#include <libmissioncontrol/dbus-api.h>

#define MISSIONCONTROL_TYPE       (mission_control_get_type ())

#define MISSIONCONTROL(obj)       (G_TYPE_CHECK_INSTANCE_CAST \
				   ((obj), MISSIONCONTROL_TYPE, \
				    MissionControl))

#define MISSIONCONTROL_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST \
					   ((klass), MISSIONCONTROL_TYPE, \
					    MissionControlClass))

#define IS_MISSIONCONTROL(obj)    (G_TYPE_CHECK_INSTANCE_TYPE \
				   ((obj), MISSIONCONTROL_TYPE))

#define IS_MISSIONCONTROL_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE \
					   ((klass), MISSIONCONTROL_TYPE))

#define MISSIONCONTROL_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS \
					   ((obj), MISSIONCONTROL_TYPE, \
					    MissionControlClass))

#define MC_ERROR (mission_control_error_quark())

typedef struct _missioncontrol MissionControl;
typedef struct _missioncontrolclass MissionControlClass;

typedef enum {
    MC_DISCONNECTED_ERROR,
    MC_INVALID_HANDLE_ERROR,
    MC_NO_MATCHING_CONNECTION_ERROR,
    MC_INVALID_ACCOUNT_ERROR, 
    MC_PRESENCE_FAILURE_ERROR,
    MC_NO_ACCOUNTS_ERROR,
    MC_NETWORK_ERROR,
    MC_CONTACT_DOES_NOT_SUPPORT_VOICE_ERROR,
    MC_LOWMEM_ERROR,
    MC_CHANNEL_REQUEST_GENERIC_ERROR,
    MC_CHANNEL_BANNED_ERROR,
    MC_CHANNEL_FULL_ERROR,
    MC_CHANNEL_INVITE_ONLY_ERROR,
    MC_LAST_ERROR /*< skip >*/
} MCError;

typedef enum {
    MC_PRESENCE_UNSET,
    MC_PRESENCE_OFFLINE,
    MC_PRESENCE_AVAILABLE,
    MC_PRESENCE_AWAY,
    MC_PRESENCE_EXTENDED_AWAY,
    MC_PRESENCE_HIDDEN,
    MC_PRESENCE_DO_NOT_DISTURB,
    LAST_MC_PRESENCE /*< skip >*/
} McPresence;

typedef enum {
    MC_STATUS_DISCONNECTED,
    MC_STATUS_CONNECTING,
    MC_STATUS_CONNECTED,
} McStatus;

typedef enum {
	MC_FILTER_PRIORITY_CRITICAL = 0,
	MC_FILTER_PRIORITY_SYSTEM = 1000,
	MC_FILTER_PRIORITY_NOTICE = 2000,
	MC_FILTER_PRIORITY_DIALOG = 3000,
	MC_FILTER_PRIORITY_MONITOR = 4000
} McFilterPriority;

typedef enum {
	MC_FILTER_FLAG_INCOMING = 1 << 0,
	MC_FILTER_FLAG_OUTGOING = 1 << 1
} McFilterFlag;
#define MC_FILTER_FLAG_OUTCOMING MC_FILTER_FLAG_OUTGOING

struct _missioncontrol
{
    DBusGProxy parent;

    gboolean first_run;
    GHashTable *active_callbacks;
};


struct _missioncontrolclass
{
    DBusGProxyClass parent_class;
}; 

typedef struct _McAccountStatus {
    gchar *unique_name;
    TpConnectionStatus status;
    McPresence presence;
    TpConnectionStatusReason reason;
} McAccountStatus;

typedef void (*McCallback) (MissionControl *mc,
			    GError *error,
			    gpointer user_data);

#include <libmissioncontrol/mc-account.h>

GQuark mission_control_error_quark (void);
GType mission_control_get_type (void);


MissionControl *mission_control_new (DBusGConnection *connection);

void mission_control_set_presence (MissionControl *self,
				   McPresence presence,
				   const gchar *message,
				   McCallback callback,
				   gpointer user_data);

McPresence mission_control_get_presence (MissionControl *self, GError **error);
gchar *mission_control_get_presence_message (MissionControl *self,
					     GError **error);
McPresence mission_control_get_presence_actual (MissionControl *self,
						GError **error);
gchar *mission_control_get_presence_message_actual (MissionControl *self,
						    GError **error);

guint mission_control_request_channel (MissionControl *self,
				       McAccount *account,
				       const gchar *type,
				       guint handle,
				       TpHandleType handle_type,
				       McCallback callback,
				       gpointer user_data);

guint mission_control_request_channel_with_string_handle (MissionControl *self,
							  McAccount *account,
							  const gchar *type,
							  const gchar *handle,
							  TpHandleType handle_type,
							  McCallback callback,
							  gpointer user_data);

guint mission_control_request_channel_with_string_handle_and_vcard_field (MissionControl *self,
							  McAccount *account,
							  const gchar *type,
							  const gchar *handle,
							  const gchar *vcard_field,
							  TpHandleType handle_type,
							  McCallback callback,
							  gpointer user_data);

gboolean mission_control_cancel_channel_request (MissionControl *self,
						 guint operation_id,
						 GError **error);

void mission_control_connect_all_with_default_presence (MissionControl *self,
							McCallback callback,
							gpointer user_data);

guint mission_control_get_connection_status (MissionControl *self,
					     McAccount *account,
					     GError **error);

GSList *mission_control_get_online_connections (MissionControl *self,
						GError **error);

TpConnection *mission_control_get_connection (MissionControl *self,
					      McAccount *account,
					      GError **error);

McAccount *mission_control_get_account_for_connection (MissionControl *self,
						       TpConnection *connection,
						       GError **error);

gint mission_control_get_used_channels_count (MissionControl *self,
					      GQuark type, GError **error);

typedef void (*McGetCurrentStatusCallback) (MissionControl *mc,
					    McStatus status,
					    McPresence presence,
					    McPresence requested_presence,
					    McAccountStatus *accounts,
					    gsize n_accounts,
					    GError *error,
					    gpointer user_data);

void mission_control_get_current_status (MissionControl *self,
					 McGetCurrentStatusCallback callback,
					 gpointer user_data);

void mission_control_free_account_statuses (McAccountStatus *accounts);

gboolean mission_control_remote_avatar_changed (MissionControl *self,
						TpConnection *connection,
						guint contact_id,
						const gchar *token,
						GError **error);

gboolean mission_control_register_filter (MissionControl *self,
					  const gchar *bus_name,
					  const gchar *object_path,
					  const gchar *channel_type,
					  McFilterPriority priority,
					  McFilterFlag flags,
					  GError **error);

#endif
