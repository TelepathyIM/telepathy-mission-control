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
#include <telepathy-glib/util.h>

enum {
  PROP_DBUS_DAEMON = 1,
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
plugin_account_manager_dispose (GObject *object)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (object);
  GObjectFinalizeFunc dispose =
    G_OBJECT_CLASS (mcd_plugin_account_manager_parent_class)->dispose;

  g_object_unref (self->dbusd);
  self->dbusd = NULL;

  if (dispose != NULL)
    dispose (object);
}

static void
plugin_account_manager_set_property (GObject *obj, guint prop_id,
	      const GValue *val, GParamSpec *pspec)
{
    McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (obj);

    switch (prop_id)
    {
      case PROP_DBUS_DAEMON:
        if (self->dbusd != NULL)
          g_object_unref (self->dbusd);

        self->dbusd = TP_DBUS_DAEMON (g_value_dup_object (val));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
        break;
    }
}

static void
plugin_account_manager_get_property (GObject *obj, guint prop_id,
	      GValue *val, GParamSpec *pspec)
{
    McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (obj);

    switch (prop_id)
    {
      case PROP_DBUS_DAEMON:
        g_value_set_object (val, self->dbusd);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
        break;
    }
}

static void
mcd_plugin_account_manager_class_init (McdPluginAccountManagerClass *cls)
{
  GObjectClass *object_class = (GObjectClass *) cls;
  GParamSpec *spec = g_param_spec_object ("dbus-daemon",
      "DBus daemon",
      "DBus daemon",
      TP_TYPE_DBUS_DAEMON,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  object_class->set_property = plugin_account_manager_set_property;
  object_class->get_property = plugin_account_manager_get_property;
  object_class->dispose = plugin_account_manager_dispose;
  object_class->finalize = plugin_account_manager_finalize;

  g_object_class_install_property (object_class, PROP_DBUS_DAEMON, spec);
}

McdPluginAccountManager *
mcd_plugin_account_manager_new ()
{
  return g_object_new (MCD_TYPE_PLUGIN_ACCOUNT_MANAGER,
      NULL);
}

void
mcd_plugin_account_manager_set_dbus_daemon (McdPluginAccountManager *self,
    TpDBusDaemon *dbusd)
{
  GValue value = { 0 };

  g_value_init (&value, G_TYPE_OBJECT);
  g_value_take_object (&value, dbusd);

  g_object_set_property (G_OBJECT (self), "dbus-daemon", &value);
}

static gchar *
get_value (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);
  return g_key_file_get_value (self->keyfile, account, key, NULL);
}

static void
set_value (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key,
    const gchar *value)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);

  if (value != NULL)
    g_key_file_set_value (self->keyfile, account, key, value);
  else
    g_key_file_remove_key (self->keyfile, account, key, NULL);
}

static GStrv
list_keys (const McpAccountManager *ma,
           const gchar * account)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);

  return g_key_file_get_keys (self->keyfile, account, NULL, NULL);
}

static gboolean
is_secret (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);

  return g_key_file_get_boolean (self->secrets, account, key, NULL);
}

static void
make_secret (const McpAccountManager *ma,
    const gchar *account,
    const gchar *key)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);
  DEBUG ("flagging %s.%s as secret", account, key);
  g_key_file_set_boolean (self->secrets, account, key, TRUE);
}

static gchar *
unique_name (const McpAccountManager *ma,
    const gchar *manager,
    const gchar *protocol,
    const GHashTable *params)
{
  McdPluginAccountManager *self = MCD_PLUGIN_ACCOUNT_MANAGER (ma);
  const gchar *base = NULL;
  gchar *esc_manager, *esc_protocol, *esc_base;
  guint i;
  gsize base_len = sizeof (MC_ACCOUNT_DBUS_OBJECT_BASE) - 1;
  DBusGConnection *connection = tp_proxy_get_dbus_connection (self->dbusd);

  base = tp_asv_get_string (params, "account");

  if (base == NULL)
    base = "account";

  esc_manager = tp_escape_as_identifier (manager);
  esc_protocol = g_strdelimit (g_strdup (protocol), "-", '_');
  esc_base = tp_escape_as_identifier (base);

  for (i = 0; i < G_MAXUINT; i++)
    {
      gchar *path = g_strdup_printf ("%s%s/%s/%s%u",
          MC_ACCOUNT_DBUS_OBJECT_BASE,
          esc_manager, esc_protocol, esc_base, i);

      if (!g_key_file_has_group (self->keyfile, path + base_len) &&
          dbus_g_connection_lookup_g_object (connection, path) == NULL)
        {
          gchar *ret = g_strdup (path + base_len);

          g_free (path);
          return ret;
        }

      g_free (path);
    }

  return NULL;
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
  iface->unique_name = unique_name;
  iface->list_keys = list_keys;
}
