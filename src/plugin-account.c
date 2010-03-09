/* Representation of the account manager as presented to plugins. This is
 * deliberately a "smaller" API than McdAccountManager.
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include "plugin-account.h"

#include "mission-control-plugins/implementation.h"

#include <glib.h>

struct _McdPluginAccountManagerClass {
    GObjectClass parent;
};

static void plugin_iface_init (McpAccountManagerIface *iface,
    gpointer unused G_GNUC_UNUSED);

G_DEFINE_TYPE_WITH_CODE (McdPluginAccountManager, mcd_plugin_account_manager, \
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_MANAGER, plugin_iface_init))

static void
mcd_plugin_account_manager_init (McdPluginAccountManager *self)
{
  self->keyfile = g_key_file_new ();
  self->secrets = g_key_file_new ();
}

static void
plugin_account_manager_finalize (GObject *object)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (object);
  GObjectFinalizeFunc finalize =
    G_OBJECT_CLASS (mcd_plugin_account_manager_parent_class)->finalize;

  g_key_file_free (self->keyfile);
  g_key_file_free (self->secrets);
  self->keyfile = NULL;
  self->secrets = NULL;

  if (finalize != NULL)
    finalize (object);
}

static void
mcd_plugin_account_manager_class_init (McdPluginAccountManagerClass *cls)
{
  GObjectClass *object_class = (GObjectClass *) cls;

  object_class->finalize = plugin_account_manager_finalize;
}

McdPluginAccountManager *
mcd_plugin_account_manager_new ()
{
  return g_object_new (MCD_TYPE_PLUGIN_ACCOUNT_MANAGER,
      NULL);
}

static gchar *
get_value (const McpAccountManager *ma,
    const gchar *acct,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);
  return g_key_file_get_value (self->keyfile, acct, key, NULL);
}

static void
set_value (const McpAccountManager *ma,
    const gchar *acct,
    const gchar *key,
    const gchar *value)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);

  if (value != NULL)
    g_key_file_set_string (self->keyfile, acct, key, value);
  else
    g_key_file_remove_key (self->keyfile, acct, key, NULL);
}

static gboolean
is_secret (const McpAccountManager *ma,
    const gchar *acct,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);

  return g_key_file_get_boolean (self->secrets, acct, key, NULL);
}

static void
make_secret (const McpAccountManager *ma,
    const gchar *acct,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);
  DEBUG ("flagging %s.%s as secret", acct, key);
  g_key_file_set_boolean (self->secrets, acct, key, TRUE);
}

static void
plugin_iface_init (McpAccountManagerIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  DEBUG ();

  iface->get_value = get_value;
  iface->set_value = set_value;
  iface->is_secret = is_secret;
  iface->make_secret = make_secret;
}
