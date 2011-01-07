/*
 * storage-ag-hidden.c - account backend for "magic" hidden accounts using
 *                       accounts-glib
 * Copyright ©2011 Collabora Ltd.
 * Copyright ©2011 Nokia Corporation
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "mcd-storage-ag-hidden.h"

#include "mcd-debug.h"

G_DEFINE_TYPE (McdStorageAgHidden, mcd_storage_ag_hidden,
    MCD_TYPE_ACCOUNT_MANAGER_SSO);

static void
mcd_storage_ag_hidden_init (McdStorageAgHidden *self)
{
}

static void
mcd_storage_ag_hidden_class_init (McdStorageAgHiddenClass *klass)
{
  McdAccountManagerSsoClass *super = MCD_ACCOUNT_MANAGER_SSO_CLASS (klass);

  super->service_type = "HiddenIM";
}

McdStorageAgHidden *
mcd_storage_ag_hidden_new ()
{
  return g_object_new (MCD_TYPE_STORAGE_AG_HIDDEN, NULL);
}
