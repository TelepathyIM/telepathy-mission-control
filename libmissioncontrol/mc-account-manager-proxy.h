/*
 * mc-account-manager-proxy.h - Subclass of TpProxy
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

#ifndef __LIBMISSIONCONTROL_ACCOUNT_MANAGER_PROXY_H__
#define __LIBMISSIONCONTROL_ACCOUNT_MANAGER_PROXY_H__

#include <telepathy-glib/proxy.h>

G_BEGIN_DECLS

typedef struct _McAccountManagerProxy McAccountManagerProxy;
typedef struct _McAccountManagerProxyClass McAccountManagerProxyClass;
typedef struct _McAccountManagerProxyPrivate McAccountManagerProxyPrivate;

GType mc_account_manager_proxy_get_type (void);

#define MC_TYPE_ACCOUNT_MANAGER_PROXY \
    (mc_account_manager_proxy_get_type ())
#define MC_ACCOUNT_MANAGER_PROXY(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), MC_TYPE_ACCOUNT_MANAGER_PROXY, \
				 McAccountManagerProxy))
#define MC_ACCOUNT_MANAGER_PROXY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), MC_TYPE_ACCOUNT_MANAGER_PROXY, \
			      McAccountManagerProxyClass))
#define MC_IS_ACCOUNT_MANAGER_PROXY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MC_TYPE_ACCOUNT_MANAGER_PROXY))
#define MC_IS_ACCOUNT_MANAGER_PROXY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), MC_TYPE_ACCOUNT_MANAGER_PROXY))
#define MC_ACCOUNT_MANAGER_PROXY_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), MC_TYPE_ACCOUNT_MANAGER_PROXY, \
				McAccountManagerProxyClass))


G_END_DECLS

/* auto-generated stubs */
#include <libmissioncontrol/_gen/cli-Account_Manager.h>

#endif
