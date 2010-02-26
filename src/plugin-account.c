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

enum {
  PROP_KEYFILE = 1,
  PROP_SECRETS,
};

struct _McdPluginAccountManager {
  GObject parent;
  GKeyFile *file;
  GKeyFile *secrets;
};

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
}

static void
plugin_account_manager_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  McdPluginAccountManager *self = (McdPluginAccountManager *) object;

  switch (prop_id)
    {
      case PROP_KEYFILE:
        self->file = g_value_get_pointer (value);
        break;

      case PROP_SECRETS:
        self->secrets = g_value_get_pointer (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
plugin_account_manager_dispose (GObject *object)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (object);
  GObjectFinalizeFunc dispose =
    G_OBJECT_CLASS (mcd_plugin_account_manager_parent_class)->dispose;

  self->file = NULL;
  self->secrets = NULL;

  if (dispose != NULL)
    dispose (object);
}


static void
plugin_account_manager_finalize (GObject *object)
{
  GObjectFinalizeFunc finalize =
    G_OBJECT_CLASS (mcd_plugin_account_manager_parent_class)->finalize;

  if (finalize != NULL)
    finalize (object);
}

static void
mcd_plugin_account_manager_class_init (
    McdPluginAccountManagerClass *cls)
{
  GObjectClass *object_class = (GObjectClass *) cls;

  object_class->set_property = plugin_account_manager_set_property;
  object_class->dispose = plugin_account_manager_dispose;
  object_class->finalize = plugin_account_manager_finalize;

  g_object_class_install_property (object_class, PROP_KEYFILE,
      g_param_spec_pointer ("keyfile", "GKeyFile pointer",
          "The internal storage (a gkeyfile) used by the account manager",
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SECRETS,
      g_param_spec_pointer ("secrets", "GKeyFile secrets map",
          "The keyfile used to remember which params are secret",
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

McdPluginAccountManager *
mcd_plugin_account_manager_new (GKeyFile *file, GKeyFile *secrets)
{
  return g_object_new (MCD_TYPE_PLUGIN_ACCOUNT_MANAGER,
      "keyfile", file,
      "secrets", secrets,
      NULL);
}

static gchar *
get_value (const McpAccountManager *ma,
    const gchar *acct,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);
  return g_key_file_get_value (self->file, acct, key, NULL);
}

static void
set_value (const McpAccountManager *ma,
    const gchar *acct,
    const gchar *key,
    const gchar *value)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);
  g_key_file_set_string (self->file, acct, key, value);
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
