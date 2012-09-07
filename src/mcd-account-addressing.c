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
#include "config.h"

#include "mcd-account-addressing.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "_gen/interfaces.h"

#define SCHEMES TP_IFACE_ACCOUNT_INTERFACE_ADDRESSING ".URISchemes"

static void
addressing_set_uri_scheme_association (TpSvcAccountInterfaceAddressing *iface,
    const gchar *uri_scheme,
    gboolean association,
    DBusGMethodInvocation *context)
{
  McdAccount *self = MCD_ACCOUNT (iface);
  const gchar *account = mcd_account_get_unique_name (self);
  McdStorage *storage = _mcd_account_get_storage (self);
  GValue *stored_value =
    mcd_storage_dup_value (storage, account, SCHEMES, G_TYPE_STRV, NULL);
  gchar **schemes = g_value_get_boxed (stored_value);
  gboolean old_association = tp_strv_contains ((const gchar * const *) schemes,
      uri_scheme);

  if (old_association != association)
    {
      GPtrArray *new_schemes = g_ptr_array_new ();
      gchar **s;

      if (association)
        {
          /* Prepend this new scheme to the existing list of schemes */
          g_ptr_array_add (new_schemes, (gchar *) uri_scheme);

          for (s = schemes; s != NULL && *s != NULL; s++)
            g_ptr_array_add (new_schemes, *s);
        }
      else
        {
          /* Remove this scheme from the existing list of schemes */
          for (s = schemes; s != NULL && *s != NULL; s++)
            if (tp_strdiff (*s, uri_scheme))
              g_ptr_array_add (new_schemes, *s);
        }

      g_ptr_array_add (new_schemes, NULL);
      mcd_storage_set_strv (storage, account, SCHEMES,
          (const gchar * const *) new_schemes->pdata, FALSE);

      g_ptr_array_unref (new_schemes);
    }

  tp_g_value_slice_free (stored_value);
  tp_svc_account_interface_addressing_return_from_set_uri_scheme_association (
      context);
}

static void
addressing_get_uri_schemes (TpSvcDBusProperties *iface,
    const gchar *name,
    GValue *value)
{
  McdAccount *self = MCD_ACCOUNT (iface);
  const gchar *account = mcd_account_get_unique_name (self);
  McdStorage *storage = _mcd_account_get_storage (self);
  GValue *stored_value =
    mcd_storage_dup_value (storage, account, SCHEMES, G_TYPE_STRV, NULL);

  g_value_init (value, G_TYPE_STRV);
  g_value_set_boxed (value, g_value_get_boxed (stored_value));

  tp_g_value_slice_free (stored_value);
}

const McdDBusProp account_addressing_properties[] = {
  { "URISchemes", NULL, addressing_get_uri_schemes },
  { 0 }
};

void
account_addressing_iface_init (TpSvcAccountInterfaceAddressingClass *iface,
    gpointer data)
{
  #define IMPLEMENT(x) tp_svc_account_interface_addressing_implement_##x (\
    iface, addressing_##x)
    IMPLEMENT(set_uri_scheme_association);
#undef IMPLEMENT
}
