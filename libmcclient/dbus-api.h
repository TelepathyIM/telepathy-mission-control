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
#include <telepathy-glib/errors.h>

#define MISSION_CONTROL_SERVICE "org.freedesktop.Telepathy.MissionControl"
#define MISSION_CONTROL_IFACE "org.freedesktop.Telepathy.MissionControl"
#define MISSION_CONTROL_PATH "/org/freedesktop/Telepathy/MissionControl"

#include <libmcclient/_gen/enums.h>
#include <libmcclient/_gen/gtypes.h>
#include <libmcclient/_gen/interfaces.h>

#endif