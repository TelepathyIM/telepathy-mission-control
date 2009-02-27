/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008 Nokia Corporation. 
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

#ifndef __LIBMCCLIENT_ERRORS_H__
#define __LIBMCCLIENT_ERRORS_H__

#include <glib.h>
#include <glib-object.h>

#define MC_ERROR_PREFIX "com.nokia.MissionControl.Errors"

#define MC_TYPE_ERROR (mc_error_get_type())
GType mc_error_get_type (void);

#define MC_ERROR (mc_error_quark())

typedef enum {
    MC_DISCONNECTED_ERROR,
    MC_INVALID_HANDLE_ERROR,
    MC_NO_MATCHING_CONNECTION_ERROR,
    MC_INVALID_ACCOUNT_ERROR, 
    MC_PRESENCE_FAILURE_ERROR,
    MC_NO_ACCOUNTS_ERROR,
    MC_NETWORK_ERROR,
    MC_CONTACT_DOES_NOT_SUPPORT_VOICE_ERROR,
    MC_LOWMEM_ERROR,
    MC_CHANNEL_REQUEST_GENERIC_ERROR,
    MC_CHANNEL_BANNED_ERROR,
    MC_CHANNEL_FULL_ERROR,
    MC_CHANNEL_INVITE_ONLY_ERROR,
    MC_LAST_ERROR /*< skip >*/
} McError;

/* Keep API compatibility: */
#define MCError McError

GQuark mc_error_quark (void);

#endif
