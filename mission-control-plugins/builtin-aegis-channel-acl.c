/*
 * A pseudo-plugin that checks a prospective handler's Aegis permission tokens
 *
 * Copyright © 2010-2011 Nokia Corporation
 * Copyright © 2010-2011 Collabora Ltd.
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

#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "mission-control-DBus-Channel-ACL"
#define DEBUG(_f, ...) g_debug ("%s: " _f, G_STRLOC, ##__VA_ARGS__)

#include "builtin-aegis-channel-acl.h"

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/util.h>

#define AEGIS_CALL_TOKEN "Cellular"

#define PLUGIN_NAME "dbus-aegis-channel-handler-acl"
#define PLUGIN_DESCRIPTION \
  "This plugin uses libcreds to check the aegis security tokens "   \
  "posessed by a potential channel handler to see if said handler " \
  "should be allowed to take the given channels"

static gboolean token_initialised = FALSE;
static creds_value_t aegis_token = CREDS_BAD;
static creds_type_t aegis_type = CREDS_BAD;

static gchar *restricted[] = { "ring", "mmscm", NULL };

static void aegis_iface_init (McpDBusChannelAclIface *, gpointer);

static GType aegis_channel_acl_get_type (void);

G_DEFINE_TYPE_WITH_CODE (AegisChannelAcl, aegis_channel_acl,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_DBUS_CHANNEL_ACL, aegis_iface_init));

static void
aegis_channel_acl_init (AegisChannelAcl *self)
{
}

static void
aegis_channel_acl_class_init (AegisChannelAclClass *cls)
{
  if (token_initialised == TRUE)
    return;

  aegis_type = creds_str2creds (AEGIS_CALL_TOKEN, &aegis_token);
}

static inline gboolean
cm_is_restricted (const gchar *cm_name)
{
  guint i;

  for (i = 0; restricted[i] != NULL; i++)
    {
      if (!tp_strdiff (restricted[i], cm_name))
        return TRUE;
    }

  return FALSE;
}

static gboolean
channels_are_filtered (const GPtrArray *channels)
{
  guint i;
  gboolean filtered = FALSE;

  for (i = 0; !filtered && i < channels->len; i++)
    {
      gchar *manager = NULL;
      TpChannel *channel = g_ptr_array_index (channels, i);
      TpConnection *connection = tp_channel_borrow_connection (channel);

      if (tp_connection_parse_object_path (connection, NULL, &manager))
        {
          filtered = cm_is_restricted (manager);
          g_free (manager);
        }
    }

  return filtered;
}

static gboolean
pid_is_permitted (const McpDBusChannelAcl *self, pid_t pid)
{
  gboolean ok = FALSE;

  if (pid != 0)
    {
      creds_t caller = creds_gettask (pid);

      DEBUG ("creds_have_p (creds_gettask (%d) -> %p, %d, %ld)",
          pid, caller, aegis_type, aegis_token);
      ok = creds_have_p (caller, aegis_type, aegis_token);
      DEBUG ("  --> %s", ok ? "TRUE" : "FALSE");

      creds_free (caller);
    }

  return ok;
}

static gboolean
caller_authorised (const McpDBusChannelAcl *self,
    const TpDBusDaemon *dbus,
    const TpProxy *recipient,
    const GPtrArray *channels)
{
  gboolean ok = TRUE;

  if (channels_are_filtered (channels))
    {
      pid_t pid = 0;
      GError *error = NULL;
      const gchar *name = tp_proxy_get_bus_name ((TpProxy *) recipient);
      DBusGConnection *dgc =
        tp_proxy_get_dbus_connection ((TpProxy *) recipient);

      DBusGProxy *proxy = dbus_g_proxy_new_for_name (dgc,
          DBUS_SERVICE_DBUS,
          DBUS_PATH_DBUS,
          DBUS_INTERFACE_DBUS);

      dbus_g_proxy_call (proxy, "GetConnectionUnixProcessID", &error,
          G_TYPE_STRING, name,
          G_TYPE_INVALID,
          G_TYPE_UINT, &pid,
          G_TYPE_INVALID);

      ok = pid_is_permitted (self, pid);

      g_object_unref (proxy);
    }

  DEBUG ("sync Aegis Channel ACL check [%s]", ok ? "Allowed" : "Forbidden");

  return ok;
}

static void
aegis_iface_init (McpDBusChannelAclIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  mcp_dbus_channel_acl_iface_set_name (iface, PLUGIN_NAME);
  mcp_dbus_channel_acl_iface_set_desc (iface, PLUGIN_DESCRIPTION);
  mcp_dbus_channel_acl_iface_implement_authorised (iface, caller_authorised);
}

AegisChannelAcl *
aegis_channel_acl_new (void)
{
  return g_object_new (aegis_channel_acl_get_type (), NULL);
}
