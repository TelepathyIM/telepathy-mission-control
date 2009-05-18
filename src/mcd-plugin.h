/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * mcd-plugin.h - Loadable plugin support
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

#ifndef __MCD_PLUGIN_H__
#define __MCD_PLUGIN_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _McdPlugin McdPlugin;

#include "mcd-dispatcher.h"
#include "mcd-transport.h"

typedef void (*McdPluginInitFunc) (McdPlugin *plugin);

#define MCD_PLUGIN_INIT_FUNC  "mcd_plugin_init"

McdDispatcher *mcd_plugin_get_dispatcher (McdPlugin *plugin);
void mcd_plugin_register_transport (McdPlugin *plugin,
				    McdTransportPlugin *transport_plugin);
				    

G_END_DECLS

#endif
