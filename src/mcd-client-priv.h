/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * Mission Control client proxy.
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef MCD_CLIENT_PRIV_H
#define MCD_CLIENT_PRIV_H

#include <glib.h>
#include <glib-object.h>

#include <telepathy-glib/client.h>

G_BEGIN_DECLS

typedef struct _McdClientProxy McdClientProxy;
typedef struct _McdClientProxyClass McdClientProxyClass;
typedef struct _McdClientProxyPrivate McdClientProxyPrivate;

struct _McdClientProxy
{
  TpClient parent;
  McdClientProxyPrivate *priv;
};

struct _McdClientProxyClass
{
  TpClientClass parent_class;
};

G_GNUC_INTERNAL GType _mcd_client_proxy_get_type (void);

#define MCD_TYPE_CLIENT_PROXY \
  (_mcd_client_proxy_get_type ())
#define MCD_CLIENT_PROXY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MCD_TYPE_CLIENT_PROXY, \
                               McdClientProxy))
#define MCD_CLIENT_PROXY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MCD_TYPE_CLIENT_PROXY, \
                            McdClientProxyClass))
#define MCD_IS_CLIENT_PROXY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCD_TYPE_CLIENT_PROXY))
#define MCD_IS_CLIENT_PROXY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MCD_TYPE_CLIENT_PROXY))
#define MCD_CLIENT_PROXY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_CLIENT_PROXY, \
                              McdClientProxyClass))

G_GNUC_INTERNAL McdClientProxy *_mcd_client_proxy_new (
    TpDBusDaemon *dbus_daemon, const gchar *name_suffix,
    const gchar *unique_name_if_known);

G_GNUC_INTERNAL gboolean _mcd_client_check_valid_name (
    const gchar *name_suffix, GError **error);

G_GNUC_INTERNAL gboolean _mcd_client_proxy_is_active (McdClientProxy *self);
G_GNUC_INTERNAL const gchar *_mcd_client_proxy_get_unique_name (
    McdClientProxy *self);

G_GNUC_INTERNAL void _mcd_client_proxy_set_inactive (McdClientProxy *self);
G_GNUC_INTERNAL void _mcd_client_proxy_set_active (McdClientProxy *self,
                                                   const gchar *unique_name);

#define MC_CLIENT_BUS_NAME_BASE_LEN (sizeof (TP_CLIENT_BUS_NAME_BASE) - 1)

G_END_DECLS

#endif
