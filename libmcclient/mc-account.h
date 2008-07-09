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
gboolean mc_account_connects_automatically (McAccount *account);
const gchar *mc_account_get_nickname (McAccount *account);
const GHashTable *mc_account_get_parameters (McAccount *account);
void mc_account_get_automatic_presence (McAccount *account,
					TpConnectionPresenceType *type,
					const gchar **status,
					const gchar **message);
const gchar *mc_account_get_connection_name (McAccount *account);
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

const GHashTable *mc_account_conditions_get (McAccount *account);
TpProxyPendingCall *
mc_account_set_conditions (McAccount *account,
			   const GHashTable *conditions,
			   tp_cli_dbus_properties_callback_for_set callback,
			   gpointer user_data,
			   GDestroyNotify destroy,
			   GObject *weak_object);

G_END_DECLS

#include <libmcclient/_gen/mc-quark.h>

/* auto-generated stubs */
#include <libmcclient/_gen/cli-account.h>

#endif
