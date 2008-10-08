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

#ifndef __MCD_DEBUG_H__
#define __MCD_DEBUG_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define g_object_ref(obj)    (mcd_debug_ref (obj, __FILE__, __LINE__))
#define g_object_unref(obj)  (mcd_debug_unref (obj, __FILE__, __LINE__))

void mcd_debug_init (void);

inline gint mcd_debug_get_level (void);

void mcd_debug_ref (gpointer obj, const gchar *filename, gint linenum);
void mcd_debug_unref (gpointer obj, const gchar *filename, gint linenum);

void mcd_debug_print_tree (gpointer obj);

G_END_DECLS

#endif /* __MCD_DEBUG_H__ */
