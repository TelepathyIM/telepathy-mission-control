/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * Keep track of which handlers own which channels.
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

#ifndef MCD_HANDLER_MAP_H_
#define MCD_HANDLER_MAP_H_

#include "mcd-channel.h"

G_BEGIN_DECLS

typedef struct _McdHandlerMap McdHandlerMap;
typedef struct _McdHandlerMapClass McdHandlerMapClass;
typedef struct _McdHandlerMapPrivate McdHandlerMapPrivate;

GType _mcd_handler_map_get_type (void);

#define MCD_TYPE_HANDLER_MAP \
  (_mcd_handler_map_get_type ())
#define MCD_HANDLER_MAP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MCD_TYPE_HANDLER_MAP, \
                               McdHandlerMap))
#define MCD_HANDLER_MAP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MCD_TYPE_HANDLER_MAP, \
                            McdHandlerMapClass))
#define MCD_IS_HANDLER_MAP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCD_TYPE_HANDLER_MAP))
#define MCD_IS_HANDLER_MAP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MCD_TYPE_HANDLER_MAP))
#define MCD_HANDLER_MAP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_HANDLER_MAP, \
                              McdHandlerMapClass))

struct _McdHandlerMap
{
    GObject parent;
    McdHandlerMapPrivate *priv;
};

struct _McdHandlerMapClass
{
    GObjectClass parent_class;
};

McdHandlerMap *_mcd_handler_map_new (void);

const gchar *_mcd_handler_map_get_handler (McdHandlerMap *self,
                                           const gchar *channel_path);

void _mcd_handler_map_set_path_handled (McdHandlerMap *self,
                                        const gchar *channel_path,
                                        const gchar *unique_name);

void _mcd_handler_map_set_channel_handled (McdHandlerMap *self,
                                           McdChannel *channel,
                                           const gchar *unique_name);

void _mcd_handler_map_set_handler_crashed (McdHandlerMap *self,
                                           const gchar *unique_name);

G_END_DECLS

#endif
