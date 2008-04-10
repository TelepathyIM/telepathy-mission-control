/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008 Nokia Corporation. 
 *
 * Contact: Alberto Mardegan  <alberto.mardegan@nokia.com>
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

#ifndef __MCD_DBUSPROP_H__
#define __MCD_DBUSPROP_H__

#include <telepathy-glib/svc-generic.h>

G_BEGIN_DECLS

typedef void (*mcd_setprop) (TpSvcDBusProperties *self, const gchar *name,
			     const GValue *value);
typedef void (*mcd_getprop) (TpSvcDBusProperties *self, const gchar *name,
			     GValue *value);

typedef struct _McdDBusProp
{
    const gchar *name;
    mcd_setprop setprop;
    mcd_getprop getprop;
} McdDBusProp;

void mcd_dbusprop_get_property (TpSvcDBusProperties *self,
				const gchar *interface_name,
				const gchar *property_name,
				GValue *value,
				GError **error);

void dbusprop_set (TpSvcDBusProperties *self,
		   const gchar *interface_name,
		   const gchar *property_name,
		   const GValue *value,
		   DBusGMethodInvocation *context);

void dbusprop_get (TpSvcDBusProperties *self,
		   const gchar *interface_name,
		   const gchar *property_name,
		   DBusGMethodInvocation *context);

void dbusprop_get_all (TpSvcDBusProperties *self,
		       const gchar *interface_name,
		       DBusGMethodInvocation *context);

void dbusprop_add_interface (TpSvcDBusProperties *self,
			     const gchar *interface_name,
			     const McdDBusProp *properties);

G_END_DECLS
#endif /* __MCD_DBUSPROP_H__ */
