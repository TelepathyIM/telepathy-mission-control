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

#include "mcd-account-addressing.h"

#include <config.h>

#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/svc-generic.h>

#include "mcd-account.h"
#include "mcd-account-priv.h"
#include "_gen/interfaces.h"

#define SCHEMES MC_IFACE_ACCOUNT_INTERFACE_ADDRESSING ".URISchemes"

static void
addressing_set_uri_scheme_association (McSvcAccountInterfaceAddressing *iface,
    const gchar *uri_scheme,
    gboolean association,
    DBusGMethodInvocation *context)
{
  McdAccount *self = MCD_ACCOUNT (iface);
  const gchar *account = mcd_account_get_unique_name (self);
  McdStorage *storage = _mcd_account_get_storage (self);
  gboolean old_association = FALSE;
  GValue *stored_value =
    mcd_storage_dup_value (storage, account, SCHEMES, G_TYPE_STRV, NULL);
  gchar **schemes = g_value_get_boxed (stored_value);
  gchar *scheme;
  gsize pos = 0;

  if (schemes == NULL)
    {
      gchar *empty[] = { NULL };
      schemes = empty;
    }

  for(scheme = schemes[pos++]; scheme != NULL; scheme = schemes[pos++])
    {
      if (!tp_strdiff (scheme, uri_scheme))
        old_association = TRUE;
    }

  /* requested state different from stored, update required: */
  if (old_association != association)
    {
      GValue new_box = { 0 };
      gchar **new_schemes = NULL;

      if (association)
        {
          new_schemes = g_new0 (gchar *, pos + 1);
          new_schemes[pos] = NULL;

          for (--pos; pos > 0; pos--)
            new_schemes[pos] = schemes[pos - 1];

          new_schemes[0] = (gchar *) uri_scheme;
        }
      else
        {
          guint i = 0;

          new_schemes = g_new0 (gchar *, pos - 1);
          new_schemes[pos - 2] = NULL;
          pos = 0;

          for (scheme = schemes[pos++]; scheme != NULL; scheme = schemes[pos++])
            {
              if (tp_strdiff (scheme, uri_scheme))
                new_schemes[i++]  = scheme;
            }
        }

      g_value_init (&new_box, G_TYPE_STRV);
      g_value_set_static_boxed (&new_box, new_schemes);

      mcd_storage_set_value (storage, account, SCHEMES, &new_box, FALSE);

      g_free (new_schemes);
      g_value_unset (&new_box);
    }

  mc_svc_account_interface_addressing_return_from_set_uri_scheme_association(
      context, old_association);

  tp_g_value_slice_free (stored_value);
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
account_addressing_iface_init (McSvcAccountInterfaceAddressingClass *iface,
    gpointer data)
{
  #define IMPLEMENT(x) mc_svc_account_interface_addressing_implement_##x (\
    iface, addressing_##x)
    IMPLEMENT(set_uri_scheme_association);
#undef IMPLEMENT
}
