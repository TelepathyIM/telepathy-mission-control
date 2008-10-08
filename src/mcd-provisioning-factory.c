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

#include "mcd-provisioning-factory.h"

#define MCD_PROVISIONING_FACTORY_GET_PRIV(master) \
		(G_TYPE_INSTANCE_GET_PRIVATE ((master), \
			MCD_TYPE_PROVISIONING_FACTORY, \
			McdProvisioningFactoryPriv))

G_DEFINE_TYPE (McdProvisioningFactory, mcd_provisioning_factory, G_TYPE_OBJECT);


struct _McdProvisioningFactoryPriv
{
    GHashTable *provs;
};

static void
mcd_provisioning_factory_init (McdProvisioningFactory *object)
{
    McdProvisioningFactoryPriv *priv = MCD_PROVISIONING_FACTORY_GET_PRIV (object);
    priv->provs = g_hash_table_new_full (g_str_hash, g_str_equal,
					 (GDestroyNotify) g_free,
					 (GDestroyNotify) g_object_unref);
}

static void
mcd_provisioning_factory_dispose (GObject *object)
{
    McdProvisioningFactoryPriv *priv = MCD_PROVISIONING_FACTORY_GET_PRIV (object);
    if (priv->provs)
    {
	g_hash_table_destroy (priv->provs);
	priv->provs = NULL;
    }
    G_OBJECT_CLASS (mcd_provisioning_factory_parent_class)->dispose (object);
}

static void
mcd_provisioning_factory_class_init (McdProvisioningFactoryClass *klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class, sizeof (McdProvisioningFactoryPriv));
    
    object_class->dispose = mcd_provisioning_factory_dispose;
}

/**
 * mcd_provisioning_factory_lookup:
 * @prov_factory: the #McdProvisioningFactory.
 * @service: name of the service for which provisioning will be retrieved.
 *
 * Gets a #McdProvisioning object for @service.
 *
 * Returns: a #McdProvisioning, or %NULL if none found. The reference count
 * will not be incremented.
 */
McdProvisioning*
mcd_provisioning_factory_lookup (McdProvisioningFactory* prov_factory,
				 const gchar *service)
{
    McdProvisioningFactoryPriv *priv;
    
    g_return_val_if_fail (service != NULL, NULL);
    g_return_val_if_fail (MCD_IS_PROVISIONING_FACTORY (prov_factory), NULL);
    
    priv = MCD_PROVISIONING_FACTORY_GET_PRIV (prov_factory);
    return g_hash_table_lookup (priv->provs, service);
}

/**
 * mcd_provisioning_factory_add:
 * @prov_factory: the #McdProvisioningFactory.
 * @service: name of the service for which provisioning will be provided.
 * @provisioning: the #McdProvisioning object to add.
 *
 * Add a new provisioning object to the factory. Note that the factory will
 * take ownership of the @provisioning object.
 */
void
mcd_provisioning_factory_add (McdProvisioningFactory* prov_factory,
			      const gchar *service,
			      McdProvisioning *provisioning)
{
    McdProvisioningFactoryPriv *priv;
    
    g_return_if_fail (service != NULL);
    g_return_if_fail (MCD_IS_PROVISIONING_FACTORY (prov_factory));
    g_return_if_fail (MCD_IS_PROVISIONING (provisioning));
    
    priv = MCD_PROVISIONING_FACTORY_GET_PRIV (prov_factory);
    g_hash_table_insert (priv->provs, g_strdup (service), provisioning);
}

/**
 * mcd_provisioning_factory_get:
 * 
 * Get the #McdProvisioningFactory. One doesn't have to hold a reference to the
 * returned object: just call this function whenever needed.
 *
 * Returns: a #McdProvisioningFactory.
 */
McdProvisioningFactory*
mcd_provisioning_factory_get (void)
{
    static McdProvisioningFactory *factory = NULL;
    if (!factory)
    {
	factory = g_object_new (MCD_TYPE_PROVISIONING_FACTORY, NULL);
    }
    return factory;
}
