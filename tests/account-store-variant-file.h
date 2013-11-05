/*
 * MC account storage inspector: MC 5.14 GVariant-file backend
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010-2012 Collabora Ltd.
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

#ifndef _ACCOUNT_STORE_VARIANT_FILE_H_
#define _ACCOUNT_STORE_VARIANT_FILE_H_

#include <glib.h>
#include <glib-object.h>

gchar *variant_get (const gchar *account,
    const gchar *key);

gboolean variant_delete (const gchar *account);

gboolean variant_exists (const gchar *account);

GStrv variant_list (void);

#endif
