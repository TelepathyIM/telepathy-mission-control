/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright © 2010 Nokia Corporation.
 * Copyright © 2010 Collabora Ltd.
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

#ifndef __MCD_ACCOUNT_ADDRESSING_H__
#define __MCD_ACCOUNT_ADDRESSING_H__

#include "mcd-account-priv.h"

G_BEGIN_DECLS

G_GNUC_INTERNAL extern const McdDBusProp account_addressing_properties[];

G_GNUC_INTERNAL void account_addressing_iface_init (
    TpSvcAccountInterfaceAddressingClass *iface,
    gpointer data);

G_END_DECLS

#endif

