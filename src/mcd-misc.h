/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
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

#ifndef MCD_MISC_H
#define MCD_MISC_H

#include <glib.h>
#include <glib-object.h>
#include <telepathy-glib/gtypes.h>

G_BEGIN_DECLS

typedef gboolean (*McdXdgDataSubdirFunc) (const gchar *path,
                                          const gchar *filename,
                                          gpointer user_data);
void _mcd_xdg_data_subdir_foreach (const gchar *subdir,
                                   McdXdgDataSubdirFunc callback,
                                   gpointer user_data);

GHashTable *_mcd_deepcopy_asv (GHashTable *asv);

const gchar *_mcd_get_error_string (const GError *error);

/* FIXME: we should use the definition from telepathy-glib, but
 * https://bugs.freedesktop.org/show_bug.cgi?id=18515
 * TP_ARRAY_TYPE_CHANNEL_CLASS_LIST is not defined at the moment */
#ifdef TP_ARRAY_TYPE_CHANNEL_CLASS_LIST
#error "telepathy-glib has been fixed, this should be removed"
#endif
#define TP_ARRAY_TYPE_CHANNEL_CLASS_LIST (type_dbus_aasv ())
GType type_dbus_aasv (void);


G_END_DECLS
#endif /* MCD_MISC_H */
