/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * mc-account.h - the Telepathy Account D-Bus interface
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

#ifndef __LIBMCCLIENT_ACCOUNT_H__
#define __LIBMCCLIENT_ACCOUNT_H__

#include <telepathy-glib/proxy.h>
#include <telepathy-glib/enums.h>
#include <libmcclient/mc-enums.h>
#include <libmcclient/mc-errors.h>
#include <libmcclient/mc-gtypes.h>
#include <libmcclient/mc-interfaces.h>

G_BEGIN_DECLS

typedef struct _McAccountClass McAccountClass;
typedef struct _McAccountPrivate McAccountPrivate;
typedef struct _McAccount McAccount;

struct _McAccount {
    /*<public>*/
    TpProxy parent;
    gchar *name;
    gchar *manager_name;
    gchar *protocol_name;
    /*<private>*/
    McAccountPrivate *priv;
};

#ifndef MC_ACCOUNT_DBUS_OBJECT_BASE
#define MC_ACCOUNT_DBUS_OBJECT_BASE "/org/freedesktop/Telepathy/Account/"
#define MC_ACCOUNT_DBUS_OBJECT_BASE_LEN \
    (sizeof (MC_ACCOUNT_DBUS_OBJECT_BASE) - 1)
#endif

GType mc_account_get_type (void);

#define MC_TYPE_ACCOUNT (mc_account_get_type ())
#define MC_ACCOUNT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MC_TYPE_ACCOUNT, \
                               McAccount))
#define MC_ACCOUNT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MC_TYPE_ACCOUNT, \
                            McAccountClass))
#define MC_IS_ACCOUNT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MC_TYPE_ACCOUNT))
#define MC_IS_ACCOUNT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MC_TYPE_ACCOUNT))
#define MC_ACCOUNT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MC_TYPE_ACCOUNT, \
                              McAccountClass))

McAccount *mc_account_new (TpDBusDaemon *dbus, const gchar *object_path);

typedef void (*McAccountWhenReadyCb) (McAccount *account, const GError *error,
				      gpointer user_data);

void mc_account_call_when_ready (McAccount *account,
				 McAccountWhenReadyCb callback,
				 gpointer user_data);

typedef void (*McAccountWhenReadyObjectCb) (McAccount *account,
					    const GError *error,
					    gpointer user_data,
					    GObject *weak_object);

void mc_account_call_when_iface_ready (McAccount *account,
				       GQuark interface,
				       McAccountWhenReadyObjectCb callback,
				       gpointer user_data,
				       GDestroyNotify destroy,
				       GObject *weak_object);
void mc_account_call_when_all_ready (McAccount *account,
				     McAccountWhenReadyObjectCb callback,
				     gpointer user_data,
				     GDestroyNotify destroy,
				     GObject *weak_object, ...);

const gchar *mc_account_get_display_name (McAccount *account);
const gchar *mc_account_get_icon (McAccount *account);
gboolean mc_account_is_valid (McAccount *account);
gboolean mc_account_is_enabled (McAccount *account);
gboolean mc_account_has_been_online (McAccount *account);
gboolean mc_account_connects_automatically (McAccount *account);
const gchar *mc_account_get_nickname (McAccount *account);
GHashTable *mc_account_get_parameters (McAccount *account);
void mc_account_get_automatic_presence (McAccount *account,
					TpConnectionPresenceType *type,
					const gchar **status,
					const gchar **message);
const gchar *mc_account_get_connection_path (McAccount *account);
const gchar *mc_account_get_connection_name (McAccount *account)
    G_GNUC_DEPRECATED;
TpConnectionStatus mc_account_get_connection_status (McAccount *account);
TpConnectionStatusReason mc_account_get_connection_status_reason (McAccount *account);
void mc_account_get_current_presence (McAccount *account,
				      TpConnectionPresenceType *type,
				      const gchar **status,
				      const gchar **message);
void mc_account_get_requested_presence (McAccount *account,
					TpConnectionPresenceType *type,
					const gchar **status,
					const gchar **message);
const gchar *mc_account_get_normalized_name (McAccount *account);
TpProxyPendingCall *
mc_account_set_display_name (McAccount *account, const gchar *display_name,
			     tp_cli_dbus_properties_callback_for_set callback,
			     gpointer user_data,
			     GDestroyNotify destroy,
			     GObject *weak_object);
TpProxyPendingCall *
mc_account_set_icon (McAccount *account, const gchar *icon,
		     tp_cli_dbus_properties_callback_for_set callback,
		     gpointer user_data,
		     GDestroyNotify destroy,
		     GObject *weak_object);
TpProxyPendingCall *
mc_account_set_enabled (McAccount *account, gboolean enabled,
			tp_cli_dbus_properties_callback_for_set callback,
			gpointer user_data,
			GDestroyNotify destroy,
			GObject *weak_object);
TpProxyPendingCall *
mc_account_set_connect_automatically (McAccount *account, gboolean connect,
				      tp_cli_dbus_properties_callback_for_set callback,
				      gpointer user_data,
				      GDestroyNotify destroy,
				      GObject *weak_object);
TpProxyPendingCall *
mc_account_set_nickname (McAccount *account, const gchar *nickname,
			 tp_cli_dbus_properties_callback_for_set callback,
			 gpointer user_data,
			 GDestroyNotify destroy,
			 GObject *weak_object);
TpProxyPendingCall *
mc_account_set_automatic_presence (McAccount *account,
				   TpConnectionPresenceType type,
				   const gchar *status,
				   const gchar *message,
				   tp_cli_dbus_properties_callback_for_set callback,
				   gpointer user_data,
				   GDestroyNotify destroy,
				   GObject *weak_object);
TpProxyPendingCall *
mc_account_set_requested_presence (McAccount *account,
				   TpConnectionPresenceType type,
				   const gchar *status,
				   const gchar *message,
				   tp_cli_dbus_properties_callback_for_set callback,
				   gpointer user_data,
				   GDestroyNotify destroy,
				   GObject *weak_object);


void mc_account_avatar_call_when_ready (McAccount *account,
				       	McAccountWhenReadyCb callback,
					gpointer user_data);

void mc_account_avatar_get (McAccount *account,
			    const gchar **avatar, gsize *length,
			    const gchar **mime_type);
TpProxyPendingCall *
mc_account_avatar_set (McAccount *account, const gchar *avatar, gsize len,
		       const gchar *mime_type,
		       tp_cli_dbus_properties_callback_for_set callback,
		       gpointer user_data,
		       GDestroyNotify destroy,
		       GObject *weak_object);


void mc_account_compat_call_when_ready (McAccount *account,
				       	McAccountWhenReadyCb callback,
					gpointer user_data);

const gchar *mc_account_compat_get_profile (McAccount *account);
const gchar *mc_account_compat_get_avatar_file (McAccount *account);
const gchar * const *mc_account_compat_get_secondary_vcard_fields (McAccount *account);
TpProxyPendingCall *
mc_account_compat_set_profile (McAccount *account, const gchar *profile,
			       tp_cli_dbus_properties_callback_for_set callback,
			       gpointer user_data,
			       GDestroyNotify destroy,
			       GObject *weak_object);
TpProxyPendingCall *
mc_account_compat_set_secondary_vcard_fields (McAccount *account,
					      const gchar * const *fields,
					      tp_cli_dbus_properties_callback_for_set callback,
					      gpointer user_data,
					      GDestroyNotify destroy,
					      GObject *weak_object);


void mc_account_conditions_call_when_ready (McAccount *account,
					    McAccountWhenReadyCb callback,
					    gpointer user_data);

GHashTable *mc_account_conditions_get (McAccount *account);
TpProxyPendingCall *
mc_account_conditions_set (McAccount *account,
			   const GHashTable *conditions,
			   tp_cli_dbus_properties_callback_for_set callback,
			   gpointer user_data,
			   GDestroyNotify destroy,
			   GObject *weak_object);


/* Channel requests */
typedef struct {
    guint32 _mask;
    GQuark fld_channel_type;
    guint fld_target_handle;
    TpHandleType fld_target_handle_type;
    const gchar *fld_target_id;
} McAccountChannelrequestData;

enum
{
    _MC_IS_SET_channel_type        = 1 << 0,
    _MC_IS_SET_target_handle       = 1 << 1,
    _MC_IS_SET_target_handle_type  = 1 << 2,
    _MC_IS_SET_target_id           = 1 << 3,
};

#define MC_ACCOUNT_CRD_INIT(crd) ((crd)->_mask = 0)
#define MC_ACCOUNT_CRD_IS_SET(crd, FIELD) ((crd)->_mask & _MC_IS_SET_ ## FIELD)
#define MC_ACCOUNT_CRD_GET(crd, FIELD) ((crd)->fld_ ## FIELD)
#define MC_ACCOUNT_CRD_SET(crd, FIELD, value) \
    do {\
        (crd)->_mask |= _MC_IS_SET_ ## FIELD; \
        (crd)->fld_ ## FIELD = value;\
    } while(0)
#define MC_ACCOUNT_CRD_UNSET(crd, FIELD) (crd)->_mask &= ~ _MC_IS_SET_ ## FIELD;

typedef enum
{
    MC_ACCOUNT_CR_SUCCEEDED,
    MC_ACCOUNT_CR_FAILED,
    MC_ACCOUNT_CR_CANCELLED,
} McAccountChannelrequestEvent;

typedef void (*McAccountChannelrequestCb) (McAccount *account,
                                           guint request_id,
                                           McAccountChannelrequestEvent event,
                                           gpointer user_data,
                                           GObject *weak_object);

typedef enum
{
    MC_ACCOUNT_CR_FLAG_USE_EXISTING = 1 << 0, /* if set, call EnsureChannel */
} McAccountChannelrequestFlags;

guint mc_account_channelrequest (McAccount *account,
                                 const McAccountChannelrequestData *req_data,
                                 time_t user_action_time,
                                 const gchar *handler,
                                 McAccountChannelrequestFlags flags,
                                 McAccountChannelrequestCb callback,
                                 gpointer user_data,
                                 GDestroyNotify destroy,
                                 GObject *weak_object);

guint mc_account_channelrequest_ht (McAccount *account,
                                    GHashTable *properties,
                                    time_t user_action_time,
                                    const gchar *handler,
                                    McAccountChannelrequestFlags flags,
                                    McAccountChannelrequestCb callback,
                                    gpointer user_data,
                                    GDestroyNotify destroy,
                                    GObject *weak_object);

guint mc_account_channelrequest_add (McAccount *account,
                                     const gchar *object_path,
                                     GHashTable *properties,
                                     McAccountChannelrequestCb callback,
                                     gpointer user_data,
                                     GDestroyNotify destroy,
                                     GObject *weak_object);

void mc_account_channelrequest_cancel (McAccount *account, guint request_id);
const GError *mc_account_channelrequest_get_error (McAccount *account,
                                                   guint request_id);
const gchar *mc_account_channelrequest_get_path (McAccount *account,
                                                 guint request_id);
guint mc_account_channelrequest_get_from_path (McAccount *account,
                                               const gchar *object_path);
const gchar *mc_channelrequest_get_path (guint request_id);
guint mc_channelrequest_get_from_path (const gchar *object_path);
McAccount *mc_channelrequest_get_account (guint request_id);


/* Account statistics */

GHashTable *mc_account_stats_get_channel_count (McAccount *account);

G_END_DECLS

#include <libmcclient/_gen/mc-quark.h>

/* auto-generated stubs */
#include <libmcclient/_gen/cli-account.h>

#endif
