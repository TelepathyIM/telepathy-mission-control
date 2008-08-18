/*
 * mc-account_manager-manager-proxy.c - Subclass of TpProxy
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

#include "mc-account-manager-proxy.h"
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/errors.h>
#include "_gen/interfaces.h"
#include "_gen/signals-marshal.h"

/* auto-generated stub code */

static void _mc_ext_register_dbus_glib_marshallers (void);
#include "_gen/register-dbus-glib-marshallers-body.h"
#include "_gen/cli-Account_Manager-body.h"

struct _McAccountManagerProxyClass {
    TpProxyClass parent_class;
};

struct _McAccountManagerProxy {
    TpProxy parent;
};

G_DEFINE_TYPE (McAccountManagerProxy, mc_account_manager_proxy, TP_TYPE_PROXY);

static void
mc_account_manager_proxy_init (McAccountManagerProxy *self)
{
}

static void
mc_account_manager_proxy_class_init (McAccountManagerProxyClass *klass)
{
  GType type = MC_TYPE_ACCOUNT_MANAGER_PROXY;
  TpProxyClass *proxy_class = (TpProxyClass *) klass;

  /* the API is stateless, so we can keep the same proxy across restarts */
  proxy_class->must_have_unique_name = FALSE;

  proxy_class->interface = MC_IFACE_QUARK_ACCOUNT_MANAGER;
  _mc_ext_register_dbus_glib_marshallers ();
  tp_proxy_or_subclass_hook_on_interface_add (type, mc_cli_Account_Manager_add_signals);

  tp_proxy_subclass_add_error_mapping (type, TP_ERROR_PREFIX, TP_ERRORS,
      TP_TYPE_ERROR);
}

