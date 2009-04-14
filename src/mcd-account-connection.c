/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
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

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <config.h>

#include "mcd-master.h"
#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "mcd-account-connection.h"
#include "mcd-account-manager.h"

typedef struct {
    GHashTable *params;
    gint i_filter;
} McdAccountConnectionContext;

static GQuark account_connection_context_quark;

static void
context_free (gpointer ptr)
{
    McdAccountConnectionContext *ctx = ptr;

    g_free (ctx);
}

void
_mcd_account_connection_begin (McdAccount *account)
{
    McdAccountConnectionContext *ctx;

    /* check whether a connection process is already ongoing */
    if (g_object_get_qdata (G_OBJECT (account),
                            account_connection_context_quark))
        return;

    /* get account params */
    /* create dynamic params HT */
    /* run the handlers */
    ctx = g_malloc (sizeof (McdAccountConnectionContext));
    ctx->i_filter = 0;
    ctx->params = mcd_account_get_parameters (account);
    g_object_set_qdata_full ((GObject *)account,
			     account_connection_context_quark,
			     ctx, context_free);

    mcd_account_connection_proceed (account, TRUE);
}

void 
mcd_account_connection_proceed (McdAccount *account, gboolean success)
{
    McdAccountConnectionContext *ctx;
    McdAccountConnectionFunc func = NULL;
    gpointer userdata;
    McdMaster *master;

    /* call next handler, or terminate the chain (emitting proper signal).
     * if everything is fine, call mcd_manager_create_connection() and
     * _mcd_connection_connect () with the dynamic parameters. Remove that call
     * from mcd_manager_create_connection() */
    ctx = g_object_get_qdata ((GObject *)account,
			      account_connection_context_quark);
    g_return_if_fail (ctx != NULL);

    if (success)
    {
	master = mcd_master_get_default ();
	mcd_master_get_nth_account_connection (master, ctx->i_filter++,
					       &func, &userdata);
    }
    if (func)
    {
	func (account, ctx->params, userdata);
    }
    else
    {
	/* end of the chain */
	g_signal_emit (account, _mcd_account_signals[CONNECTION_PROCESS], 0,
		       success);
	if (success)
	{
	    _mcd_account_connect (account, ctx->params);
	}
        else
        {
            GError *error;

            error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                                 "Plugins refused connection to account %s",
                                 mcd_account_get_unique_name (account));
            _mcd_account_online_request_completed (account, error);
        }
	g_object_set_qdata ((GObject *)account,
			    account_connection_context_quark, NULL);
    }
}

/**
 * mcd_account_connection_bind_transport:
 * @account: the #McdAccount.
 * @transport: the #McdTransport.
 *
 * Set @account as dependent on @transport; connectivity plugins should call
 * this function in the callback they registered with
 * mcd_plugin_register_account_connection(). This tells the account manager to
 * disconnect @account when @transport goes away.
 */
void
mcd_account_connection_bind_transport (McdAccount *account,
                                       McdTransport *transport)
{
    g_return_if_fail (MCD_IS_ACCOUNT (account));

    g_object_set_data ((GObject *)account, "transport", transport);
}

McdTransport *
_mcd_account_connection_get_transport (McdAccount *account)
{
    g_return_val_if_fail (MCD_IS_ACCOUNT (account), NULL);

    return g_object_get_data ((GObject *)account, "transport");
}

inline void
_mcd_account_connection_class_init (McdAccountClass *klass)
{
    _mcd_account_signals[CONNECTION_PROCESS] =
	g_signal_new ("connection-process",
		      G_OBJECT_CLASS_TYPE (klass),
		      G_SIGNAL_RUN_LAST,
		      0,
		      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
		      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    account_connection_context_quark = g_quark_from_static_string ("accontext");
}

