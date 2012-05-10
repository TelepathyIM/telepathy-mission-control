/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2010 Nokia Corporation.
 * Copyright (C) 2009-2010 Collabora Ltd.
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

#ifndef CHANNEL_UTILS_H
#define CHANNEL_UTILS_H

#include <glib.h>

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

G_GNUC_INTERNAL
GPtrArray *_mcd_tp_channel_details_build_from_list (const GList *channels);
G_GNUC_INTERNAL
GPtrArray *_mcd_tp_channel_details_build_from_tp_chan (TpChannel *channel);
G_GNUC_INTERNAL
void _mcd_tp_channel_details_free (GPtrArray *channels);

/* NULL-safe for @channel; @verb is for debug */
G_GNUC_INTERNAL gboolean _mcd_tp_channel_should_close (TpChannel *channel,
                                                       const gchar *verb);

G_END_DECLS
#endif

