/* Mission Control storage API - interface which provides access to account
 * parameter/setting storage
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010 Collabora Ltd.
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

#include <glib-object.h>
#include <mission-control-plugins/mission-control-plugins.h>
#include "mcd-storage.h"

#ifndef MCD_STORAGE_PRIV_H
#define MCD_STORAGE_PRIV_H

G_BEGIN_DECLS

G_GNUC_INTERNAL void _mcd_storage_store_connections (McdStorage *storage);

G_END_DECLS

#endif /* MCD_STORAGE_H */
