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

#include "config.h"

#include "mcd-storage-ag-hidden.h"

#include <telepathy-glib/telepathy-glib.h>

#include "mcd-debug.h"
/* FIXME: if we weren't in-tree, we wouldn't be able to include this header and
 * we'd have to re-hardcode magic strings like "Hidden".
 */
#include "mcd-account-config.h"

static void account_storage_iface_init (
    McpAccountStorageIface *iface,
    gpointer unused G_GNUC_UNUSED);

G_DEFINE_TYPE_WITH_CODE (McdStorageAgHidden, mcd_storage_ag_hidden,
    MCD_TYPE_ACCOUNT_MANAGER_SSO,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_STORAGE,
        account_storage_iface_init);
    );

static void
mcd_storage_ag_hidden_init (McdStorageAgHidden *self)
{
}

static void
mcd_storage_ag_hidden_class_init (McdStorageAgHiddenClass *klass)
{
  McdAccountManagerSsoClass *super = MCD_ACCOUNT_MANAGER_SSO_CLASS (klass);

  super->service_type = ACCOUNTS_GLIB_HIDDEN_SERVICE_TYPE;
}

static gboolean
_mcd_storage_ag_hidden_get (
    const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account_suffix,
    const gchar *key)
{
  /* Chain up to the real implementation, checking whether this is an account
   * we care about in the process.
   */
  if (!_mcd_account_manager_sso_get (self, am, account_suffix, key))
    return FALSE;

  /* If the caller is looking for the "Hidden" key (or NULL, which means
   * everything), let's fill it in. (Every account this plugin cares about
   * should be hidden.)
   */
  if (key == NULL || !tp_strdiff (key, MC_ACCOUNTS_KEY_HIDDEN))
    mcp_account_manager_set_value (am, account_suffix, MC_ACCOUNTS_KEY_HIDDEN,
        "true");

  return TRUE;
}

static void
account_storage_iface_init (
    McpAccountStorageIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  mcp_account_storage_iface_set_name (iface,
      "maemo-libaccounts-hidden");
  mcp_account_storage_iface_set_desc (iface,
      "Loads accounts with service type '" ACCOUNTS_GLIB_HIDDEN_SERVICE_TYPE
      "' from accounts-glib, and marks them as Hidden");
  mcp_account_storage_iface_implement_get (iface,
      _mcd_storage_ag_hidden_get);
}

McdStorageAgHidden *
mcd_storage_ag_hidden_new ()
{
  return g_object_new (MCD_TYPE_STORAGE_AG_HIDDEN, NULL);
}
