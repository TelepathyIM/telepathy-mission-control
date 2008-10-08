/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
 *
 * Contact: Naba Kumar  <naba.kumar@nokia.com>
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

#ifndef MCD_PROXY_H
#define MCD_PROXY_H

#include <glib.h>
#include <glib-object.h>

#include "mcd-operation.h"

G_BEGIN_DECLS
#define MCD_TYPE_PROXY         (mcd_proxy_get_type ())
#define MCD_PROXY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_PROXY, McdProxy))
#define MCD_PROXY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_PROXY, McdProxyClass))
#define MCD_IS_PROXY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_PROXY))
#define MCD_IS_PROXY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_PROXY))
#define MCD_PROXY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_PROXY, McdProxyClass))
typedef struct _McdProxy McdProxy;
typedef struct _McdProxyClass McdProxyClass;

struct _McdProxy
{
    McdOperation parent;
};

struct _McdProxyClass
{
    McdOperationClass parent_class;
};

GType mcd_proxy_get_type (void);
McdProxy *mcd_proxy_new (McdMission * proxy_mission);
const McdMission *mcd_proxy_get_proxy_object (McdProxy * proxy);

G_END_DECLS
#endif /* MCD_PROXY_H */
