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

/**
 * SECTION:mcd-proxy
 * @title: McdProxy
 * @short_description: Mission proxy class
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-proxy.h
 * 
 * This is a simple container class that proxies the events from a proxy
 * object to self container.
 */

#include "mcd-proxy.h"

#include <telepathy-glib/telepathy-glib.h>

#define MCD_PROXY_PRIV(proxy) (G_TYPE_INSTANCE_GET_PRIVATE ((proxy), \
				       MCD_TYPE_PROXY, \
				       McdProxyPrivate))

G_DEFINE_TYPE (McdProxy, mcd_proxy, MCD_TYPE_OPERATION);

/* Private */

typedef struct _McdProxyPrivate
{
    McdMission *proxy_object;
    gboolean is_disposed;
} McdProxyPrivate;

enum
{
    PROP_0,
    PROP_PROXY_OBJECT
};

static void
_mcd_proxy_abort (McdProxy * proxy)
{
    /* Releases the reference */
    g_object_set (proxy, "proxy-object", NULL, NULL);
    /* Propagate the "abort" event to our listeners */
    mcd_mission_abort (MCD_MISSION (proxy));
}

static void
_mcd_proxy_connect_signals (McdProxy * proxy)
{
    McdProxyPrivate *priv = MCD_PROXY_PRIV (proxy);

    g_signal_connect_swapped (priv->proxy_object, "connected",
			      G_CALLBACK (mcd_mission_connect), proxy);
    g_signal_connect_swapped (priv->proxy_object, "disconnected",
			      G_CALLBACK (mcd_mission_disconnect), proxy);
    g_signal_connect_swapped (priv->proxy_object, "abort",
			      G_CALLBACK (_mcd_proxy_abort), proxy);
}

static void
_mcd_proxy_disconnect_signals (McdProxy * proxy)
{
    McdProxyPrivate *priv = MCD_PROXY_PRIV (proxy);

    g_signal_handlers_disconnect_by_func (priv->proxy_object,
					  G_CALLBACK (mcd_mission_connect),
					  proxy);
    g_signal_handlers_disconnect_by_func (priv->proxy_object,
					  G_CALLBACK (mcd_mission_disconnect),
					  proxy);
    g_signal_handlers_disconnect_by_func (priv->proxy_object,
					  G_CALLBACK (_mcd_proxy_abort), proxy);
}

static void
_mcd_proxy_finalize (GObject * object)
{
    G_OBJECT_CLASS (mcd_proxy_parent_class)->finalize (object);
}

static void
_mcd_proxy_dispose (GObject * object)
{
    McdProxyPrivate *priv = MCD_PROXY_PRIV (object);

    if (priv->is_disposed)
    {
	return;
    }

    priv->is_disposed = TRUE;
    DEBUG ("proxy disposed\n");

    if (priv->proxy_object)
    {
	/* Disconnect proxy signals */
	_mcd_proxy_disconnect_signals (MCD_PROXY (object));
    }

    tp_clear_object (&priv->proxy_object);

    G_OBJECT_CLASS (mcd_proxy_parent_class)->dispose (object);
}

static void
_mcd_proxy_set_property (GObject * obj, guint prop_id,
			 const GValue * val, GParamSpec * pspec)
{
    McdMission *proxy_object;
    McdProxyPrivate *priv = MCD_PROXY_PRIV (obj);

    switch (prop_id)
    {
    case PROP_PROXY_OBJECT:
	proxy_object = g_value_get_object (val);
	if (proxy_object)
	{
	    g_return_if_fail (MCD_IS_MISSION (proxy_object));
	    g_object_ref (proxy_object);
	}

	if (priv->proxy_object)
	{
	    /* Disconnect proxy signals */
	    _mcd_proxy_disconnect_signals (MCD_PROXY (obj));
	    g_object_unref (priv->proxy_object);
	}
	priv->proxy_object = proxy_object;
	if (priv->proxy_object)
	{
	    /* Connect proxy signals */
	    _mcd_proxy_connect_signals (MCD_PROXY (obj));
	}
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
_mcd_proxy_get_property (GObject * obj, guint prop_id,
			 GValue * val, GParamSpec * pspec)
{
    McdProxyPrivate *priv = MCD_PROXY_PRIV (obj);

    switch (prop_id)
    {
    case PROP_PROXY_OBJECT:
	g_value_set_pointer (val, priv->proxy_object);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
	break;
    }
}

static void
mcd_proxy_class_init (McdProxyClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (McdProxyPrivate));

    object_class->finalize = _mcd_proxy_finalize;
    object_class->dispose = _mcd_proxy_dispose;
    object_class->set_property = _mcd_proxy_set_property;
    object_class->get_property = _mcd_proxy_get_property;

    g_object_class_install_property
        (object_class, PROP_PROXY_OBJECT,
         g_param_spec_object ("proxy-object",
                              "Proxy object",
                              "Object to be monitored for McdMission signals",
                              MCD_TYPE_MISSION,
                              G_PARAM_READWRITE));
}

static void
mcd_proxy_init (McdProxy * obj)
{
    McdProxyPrivate *priv = MCD_PROXY_PRIV (obj);
    priv->proxy_object = NULL;
}

/* Public */

McdProxy *
mcd_proxy_new (McdMission * proxy_object)
{
    McdProxy *obj;
    obj = MCD_PROXY (g_object_new (MCD_TYPE_PROXY, "proxy-object",
				   proxy_object, NULL));
    return obj;
}

const McdMission *
mcd_proxy_get_proxy_object (McdProxy * proxy)
{
    McdProxyPrivate *priv = MCD_PROXY_PRIV (proxy);
    return priv->proxy_object;
}
