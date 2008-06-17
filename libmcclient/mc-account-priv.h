/*
 * mc-account-priv.h - the Telepathy Account D-Bus interface
 * (client side)
 *
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

#ifndef __LIBMCCLIENT_ACCOUNT_PRIV_H__
#define __LIBMCCLIENT_ACCOUNT_PRIV_H__

#include <telepathy-glib/proxy.h>
#include <telepathy-glib/enums.h>

G_BEGIN_DECLS

enum
{
    PRESENCE_CHANGED,
    STRING_CHANGED,
    CONNECTION_STATUS_CHANGED,
    FLAG_CHANGED,
    PARAMETERS_CHANGED,
    AVATAR_CHANGED,
    LAST_SIGNAL
};

extern guint _mc_account_signals[LAST_SIGNAL];

typedef struct _McAccountProps McAccountProps;
typedef struct _McAccountAvatarProps McAccountAvatarProps;
typedef struct _McAccountCompatProps McAccountCompatProps;
typedef struct _McAccountConditionsProps McAccountConditionsProps;

struct _McAccountPrivate {
    McAccountProps *props;
    McAccountAvatarProps *avatar_props;
    McAccountCompatProps *compat_props;
    McAccountConditionsProps *conditions_props;
};

void _mc_account_avatar_props_free (McAccountAvatarProps *props);
void _mc_account_avatar_class_init (McAccountClass *klass);

void _mc_account_compat_props_free (McAccountCompatProps *props);

void _mc_account_conditions_props_free (McAccountConditionsProps *props);

G_END_DECLS

#endif
