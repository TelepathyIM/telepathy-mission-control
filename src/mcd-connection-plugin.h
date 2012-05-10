/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008-2009 Nokia Corporation.
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

#ifndef __MCD_CONNECTION_PLUGIN_H__
#define __MCD_CONNECTION_PLUGIN_H__

#include <glib.h>
#include <glib-object.h>
#include "mcd-transport.h"

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

void mcd_account_connection_proceed (McdAccount *account, gboolean success);
void mcd_account_connection_proceed_with_reason
    (McdAccount *account, gboolean success, TpConnectionStatusReason reason);
void mcd_account_connection_bind_transport (McdAccount *account,
                                            McdTransport *transport);
gboolean mcd_account_connection_is_user_initiated (McdAccount *account);

#define MCD_ACCOUNT_CONNECTION_PRIORITY_POLICY 10000
#define MCD_ACCOUNT_CONNECTION_PRIORITY_TRANSPORT 20000
#define MCD_ACCOUNT_CONNECTION_PRIORITY_PARAMS   30000

G_END_DECLS

#endif /* __MCD_CONNECTION_PLUGIN_H__ */
