/*
 * This file is part of mission-control
 *
 * Copyright © 2011 Nokia Corporation.
 * Copyright © 2011 Collabora Ltd.
 *
 * Contact: Vivek Dasmohapatra  <vivek@collabora.co.uk>
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

#ifndef MCD_CONNECTION_SERVICE_POINTS_H
#define MCD_CONNECTION_SERVICE_POINTS_H

#include <glib.h>
#include <glib-object.h>
#include "mcd-connection.h"

G_BEGIN_DECLS

G_GNUC_INTERNAL void mcd_connection_service_point_setup (
    McdConnection *connection,
    gboolean watch);

G_END_DECLS

#endif
