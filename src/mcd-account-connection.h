/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008 Nokia Corporation. 
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

#ifndef __MCD_ACCOUNT_CONNECTION_H__
#define __MCD_ACCOUNT_CONNECTION_H__

#include <glib.h>
#include <glib-object.h>
#include "mcd-plugin.h"
#include "mcd-connection-plugin.h"

G_BEGIN_DECLS

void mcd_account_connection_begin (McdAccount *account);
inline void _mcd_account_connection_class_init (McdAccountClass *klass);

G_END_DECLS

#endif /* __MCD_ACCOUNT_CONNECTION_H__ */
