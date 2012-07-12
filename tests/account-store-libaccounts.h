/*
 * MC account storage backend inspector, libaccounts backend
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

#ifndef _ACCOUNT_STORE_LIBACCOUNTS_H_
#define _ACCOUNT_STORE_LIBACCOUNTS_H_

G_BEGIN_DECLS

gchar * libaccounts_get (const gchar *mc_account,
    const gchar *key);

gboolean libaccounts_set (const gchar *mc_account,
    const gchar *key,
    const gchar *value);

gboolean libaccounts_delete (const gchar *mc_account);

gboolean libaccounts_exists (const gchar *mc_account);

GStrv libaccounts_list (void);

G_END_DECLS

#endif
