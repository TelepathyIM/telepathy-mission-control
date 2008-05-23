/*
 * mc-account-proxy.c - Subclass of TpProxy
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "mc-account-proxy.h"
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/errors.h>
#include "_gen/interfaces.h"
#include "_gen/cli-Account-body.h"
#include "_gen/cli-Account_Interface_Avatar-body.h"

struct _McAccountProxyClass {
    TpProxyClass parent_class;
};

struct _McAccountProxy {
    TpProxy parent;
};

G_DEFINE_TYPE (McAccountProxy, mc_account_proxy, TP_TYPE_PROXY);

static void
mc_account_proxy_init (McAccountProxy *self)
{
    tp_proxy_add_interface_by_id ((TpProxy *)self,
				  MC_IFACE_QUARK_ACCOUNT_INTERFACE_AVATAR);
}

static void
mc_account_proxy_class_init (McAccountProxyClass *klass)
{
  GType type = MC_TYPE_ACCOUNT_PROXY;
  TpProxyClass *proxy_class = (TpProxyClass *) klass;

  /* the API is stateless, so we can keep the same proxy across restarts */
  proxy_class->must_have_unique_name = FALSE;

  proxy_class->interface = MC_IFACE_QUARK_ACCOUNT;
  tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_Account_add_signals);
  tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_Account_Interface_Avatar_add_signals);

  tp_proxy_subclass_add_error_mapping (type, TP_ERROR_PREFIX, TP_ERRORS,
      TP_TYPE_ERROR);
}

