/*
 * dbus-api.h - Mission Control D-Bus API strings, enums etc.
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

#ifndef __LIBMCCLIENT_DBUS_API_H__
#define __LIBMCCLIENT_DBUS_API_H__

#include <glib/gquark.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <telepathy-glib/proxy.h>
#include <telepathy-glib/errors.h>

#define MC_ACCOUNT_MANAGER_DBUS_SERVICE "org.freedesktop.Telepathy.AccountManager"
#define MC_ACCOUNT_MANAGER_DBUS_OBJECT "/org/freedesktop/Telepathy/AccountManager"
#define MC_ACCOUNT_DBUS_OBJECT_BASE "/org/freedesktop/Telepathy/Account/"

#include <libmcclient/_gen/enums.h>
#include <libmcclient/_gen/gtypes.h>
#include <libmcclient/_gen/interfaces.h>

void _mc_ext_register_dbus_glib_marshallers (void);

inline void _mc_gvalue_stolen (GValue *value);


typedef struct _McIfaceData McIfaceData;

typedef void (*McIfaceCreateProps) (TpProxy *proxy, GHashTable *props);

struct _McIfaceData {
    /* id of the interface */
    GQuark id;

    /* pointer to the interface private data */
    gpointer *props_data_ptr;

    /* pointer to the function to be called when GetAll has returned */
    McIfaceCreateProps create_props;
};

typedef void (*McIfaceWhenReadyCb) (TpProxy *proxy, const GError *error,
				    gpointer user_data);

void _mc_iface_call_when_ready_int (TpProxy *proxy,
				    McIfaceWhenReadyCb callback,
				    gpointer user_data,
				    McIfaceData *iface_data);

#endif
