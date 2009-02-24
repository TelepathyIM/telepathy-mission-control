/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * mc-svc-client.h - the Telepathy Client D-Bus interface
 * (service side)
 *
 * Copyright (C) 2009 Nokia Corporation
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

#ifndef __MC_META_SVC_CLIENT_H__
#define __MC_META_SVC_CLIENT_H__

#include <libmcclient/_gen/svc-client.h>

#ifndef MC_CLIENT_DBUS_OBJECT_BASE
#define MC_CLIENT_DBUS_OBJECT_BASE "/org/freedesktop/Telepathy/Client/"
#define MC_CLIENT_DBUS_OBJECT_BASE_LEN \
    (sizeof (MC_CLIENT_DBUS_OBJECT_BASE) - 1)
#endif

#ifndef MC_CLIENT_DBUS_SERVICE_BASE
#define MC_CLIENT_DBUS_SERVICE_BASE "org.freedesktop.Telepathy.Client."
#define MC_CLIENT_DBUS_SERVICE_BASE_LEN \
    (sizeof (MC_CLIENT_DBUS_SERVICE_BASE) - 1)
#endif

#endif
