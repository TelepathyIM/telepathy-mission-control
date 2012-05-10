/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
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

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>

#include "mcd-master.h"
#include "mcd-master-priv.h"
#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-manager.h"
#include "mcd-connection-priv.h"

struct _McdAccountConnectionContext {
    GHashTable *params;
    gint i_filter;
    gboolean user_initiated;
};

static guint _mcd_account_signal_connection_process = 0;

void
_mcd_account_connection_context_free (McdAccountConnectionContext *c)
{
    g_hash_table_unref (c->params);
    g_free (c);
}

void
_mcd_account_connection_begin (McdAccount *account,
                               gboolean user_initiated)
{
    McdAccountConnectionContext *ctx;

    /* check whether a connection process is already ongoing */
    if (_mcd_account_get_connection_context (account) != NULL)
    {
        DEBUG ("already trying to connect");
        return;
    }

    /* get account params */
    /* create dynamic params HT */
    /* run the handlers */
    ctx = g_malloc (sizeof (McdAccountConnectionContext));
    ctx->i_filter = 0;
    ctx->user_initiated = user_initiated;

    /* If we get this far, the account should be valid, so getting the
     * parameters should succeed.
     */
    ctx->params = _mcd_account_dup_parameters (account);
    g_assert (ctx->params != NULL);

    _mcd_account_set_connection_status (account,
                                        TP_CONNECTION_STATUS_CONNECTING,
                                        TP_CONNECTION_STATUS_REASON_REQUESTED,
                                        NULL, NULL, NULL);
    _mcd_account_set_connection_context (account, ctx);
    mcd_account_connection_proceed (account, TRUE);
}

void
mcd_account_connection_proceed_with_reason (McdAccount *account,
                                            gboolean success,
                                            TpConnectionStatusReason reason)
{
    McdAccountConnectionContext *ctx;
    McdAccountConnectionFunc func = NULL;
    gpointer userdata;
    McdMaster *master;

    /* call next handler, or terminate the chain (emitting proper signal).
     * if everything is fine, call mcd_manager_create_connection() and
     * _mcd_connection_connect () with the dynamic parameters. Remove that call
     * from mcd_manager_create_connection() */
    ctx = _mcd_account_get_connection_context (account);
    g_return_if_fail (ctx != NULL);
    g_return_if_fail (ctx->params != NULL);

    if (success)
    {
        master = mcd_master_get_default ();
        _mcd_master_get_nth_account_connection (master, ctx->i_filter++,
                                                &func, &userdata);
    }
    if (func)
    {
	func (account, ctx->params, userdata);
    }
    else
    {
	/* end of the chain */
	g_signal_emit (account, _mcd_account_signal_connection_process, 0,
		       success);
	if (success)
	{
	    _mcd_account_connect (account, ctx->params);
	}
        else
        {
            _mcd_account_set_connection_status
                (account, TP_CONNECTION_STATUS_DISCONNECTED, reason, NULL,
                 TP_ERROR_STR_DISCONNECTED, NULL);
        }
        _mcd_account_set_connection_context (account, NULL);
    }
}

void
mcd_account_connection_proceed (McdAccount *account, gboolean success)
{
    mcd_account_connection_proceed_with_reason
        (account, success, TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}

inline void
_mcd_account_connection_class_init (McdAccountClass *klass)
{
    _mcd_account_signal_connection_process =
	g_signal_new ("connection-process",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST,
		      0,
		      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
		      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

/**
 * Returns: %TRUE if the current attempt to connect was user-initiated.
 */
gboolean
mcd_account_connection_is_user_initiated (McdAccount *account)
{
    McdAccountConnectionContext *ctx;

    ctx = _mcd_account_get_connection_context (account);

    if (ctx == NULL)
        return FALSE;

    return ctx->user_initiated;
}
