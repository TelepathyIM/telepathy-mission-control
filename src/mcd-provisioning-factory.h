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

#ifndef _MCD_PROVISIONING_FACTORY_H_
#define _MCD_PROVISIONING_FACTORY_H_

#include <glib-object.h>
#include "mcd-provisioning.h"

G_BEGIN_DECLS

#define MCD_TYPE_PROVISIONING_FACTORY             (mcd_provisioning_factory_get_type ())
#define MCD_PROVISIONING_FACTORY(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), MCD_TYPE_PROVISIONING_FACTORY, McdProvisioningFactory))
#define MCD_PROVISIONING_FACTORY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), MCD_TYPE_PROVISIONING_FACTORY, McdProvisioningFactoryClass))
#define MCD_IS_PROVISIONING_FACTORY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCD_TYPE_PROVISIONING_FACTORY))
#define MCD_IS_PROVISIONING_FACTORY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), MCD_TYPE_PROVISIONING_FACTORY))
#define MCD_PROVISIONING_FACTORY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_PROVISIONING_FACTORY, McdProvisioningFactoryClass))

typedef struct _McdProvisioningFactoryClass McdProvisioningFactoryClass;
typedef struct _McdProvisioningFactoryPriv McdProvisioningFactoryPriv;
typedef struct _McdProvisioningFactory McdProvisioningFactory;

struct _McdProvisioningFactoryClass
{
	GObjectClass parent_class;
};

struct _McdProvisioningFactory
{
	GObject parent_instance;
};

GType mcd_provisioning_factory_get_type (void) G_GNUC_CONST;
McdProvisioning* mcd_provisioning_factory_lookup (McdProvisioningFactory* prov_factory,
						  const gchar *service);
void mcd_provisioning_factory_add (McdProvisioningFactory* prov_factory,
				   const gchar *service,
				   McdProvisioning *provisioning);
McdProvisioningFactory* mcd_provisioning_factory_get (void);

G_END_DECLS

#endif /* _MCD_PROVISIONING_FACTORY_H_ */
