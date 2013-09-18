/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
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

#undef DEBUG

#ifdef ENABLE_DEBUG

#define DEBUGGING (_mcd_debug_get_level () > 0)
#define DEBUG(format, ...) \
  mcd_debug ("%s: " format, G_STRFUNC, ##__VA_ARGS__)
#define WARNING(format, ...) \
  g_warning ("%s: " format, G_STRFUNC, ##__VA_ARGS__)

#else /* !defined ENABLE_DEBUG */

#define DEBUGGING (0)
#define DEBUG(format, ...) do {} while (0)

#endif /* ENABLE_DEBUG */

#define MESSAGE(format, ...) \
  g_message ("%s: " format, G_STRFUNC, ##__VA_ARGS__)
#define WARNING(format, ...) \
  g_warning ("%s: " format, G_STRFUNC, ##__VA_ARGS__)
#define CRITICAL(format, ...) \
  g_critical ("%s: " format, G_STRFUNC, ##__VA_ARGS__)
#define ERROR(format, ...) \
  g_error ("%s: " format, G_STRFUNC, ##__VA_ARGS__)

extern gint mcd_debug_level;

void mcd_debug_init (void);

void mcd_debug_set_level (gint level);
static inline gint _mcd_debug_get_level (void)
{
    return mcd_debug_level;
}

void mcd_debug_print_tree (gpointer obj);

void mcd_debug (const gchar *format, ...) G_GNUC_PRINTF (1, 2);

G_END_DECLS

#endif /* __MCD_DEBUG_H__ */
