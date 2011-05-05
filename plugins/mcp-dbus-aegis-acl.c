/*
 * A pseudo-plugin that checks the caller's Aegis permission tokens
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

#include "config.h"

#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "mission-control-DBus-Access-ACL"

#ifdef ENABLE_DEBUG
#define DEBUG(_f, ...) g_debug ("%s: " _f, G_STRLOC, ##__VA_ARGS__)
#else
#define DEBUG(_f, ...) do {} while (0)
#endif

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/defs.h>

#include "mcp-dbus-aegis-acl.h"

#include <sys/types.h>
#include <sys/creds.h>

typedef struct _AegisAcl AegisAcl;
typedef struct _AegisAclClass AegisAclClass;

struct _AegisAcl {
  GObject parent;
};

struct _AegisAclClass {
  GObjectClass parent_class;
};

#define CREATE_CHANNEL TP_IFACE_CONNECTION_INTERFACE_REQUESTS ".CreateChannel"
#define ENSURE_CHANNEL TP_IFACE_CONNECTION_INTERFACE_REQUESTS ".EnsureChannel"
#define SEND_MESSAGE \
  TP_IFACE_CHANNEL_DISPATCHER ".Interface.Messages.DRAFT.SendMessage"

#define AEGIS_CALL_TOKEN "Cellular"

#define PLUGIN_NAME "dbus-aegis-acl"
#define PLUGIN_DESCRIPTION \
  "This plugin uses libcreds to check the aegis security tokens " \
  "associated with the calling process ID and determine whether " \
  "the DBus call or property access should be allowed"

static creds_value_t aegis_token = CREDS_BAD;
static creds_type_t aegis_type = CREDS_BAD;

static void aegis_acl_iface_init (McpDBusAclIface *,
    gpointer);
static void aegis_channel_acl_iface_init (McpDBusChannelAclIface *,
    gpointer);

static GType aegis_acl_get_type (void);

G_DEFINE_TYPE_WITH_CODE (AegisAcl, aegis_acl,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_DBUS_ACL, aegis_acl_iface_init);
    G_IMPLEMENT_INTERFACE (MCP_TYPE_DBUS_CHANNEL_ACL,
      aegis_channel_acl_iface_init))

static void
aegis_acl_init (AegisAcl *self)
{
}

static void
aegis_acl_class_init (AegisAclClass *cls)
{
  if (aegis_type != CREDS_BAD)
    return;

  aegis_type = creds_str2creds (AEGIS_CALL_TOKEN, &aegis_token);
}

static gchar *restricted_methods[] =
  {
    CREATE_CHANNEL,
    ENSURE_CHANNEL,
    SEND_MESSAGE,
    NULL
  };

static gboolean
method_is_filtered (const gchar *method)
{
  guint i;

  for (i = 0; restricted_methods[i] != NULL; i++)
    {
      if (!tp_strdiff (method, restricted_methods[i]))
        return TRUE;
    }

  return FALSE;
}

static gboolean
is_filtered (DBusAclType type,
    const gchar *name,
    const GHashTable *params)
{
  const GValue *account = NULL;
  const gchar *path = NULL;

  /* only  bothered with method calls */
  if (type != DBUS_ACL_TYPE_METHOD)
    return FALSE;

  /* only create/ensure channel concern us (and send message, now): */
  if (!method_is_filtered (name))
    return FALSE;

  /* must have at least the account-path to check */
  if (params == NULL)
    return FALSE;

  account = g_hash_table_lookup ((GHashTable *) params, "account-path");

  if (account == NULL)
    return FALSE;

  path = g_value_get_string (account);

  DEBUG ("should we check account %s?", path);
  /* account must belong to the ring or MMS connection manager: */
  if (g_str_has_prefix (path, TP_ACCOUNT_OBJECT_PATH_BASE "ring/") ||
      g_str_has_prefix (path, TP_ACCOUNT_OBJECT_PATH_BASE "mmscm/"))
    return TRUE;

  return FALSE;
}

static gboolean
pid_is_permitted (pid_t pid)
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
caller_authorised (const McpDBusAcl *self,
    const TpDBusDaemon *dbus,
    const DBusGMethodInvocation *call,
    DBusAclType type,
    const gchar *name,
    const GHashTable *params)
{
  DBusGConnection *dgc = tp_proxy_get_dbus_connection ((TpDBusDaemon *)dbus);
  gboolean ok = TRUE;

  if (is_filtered (type, name, params))
    {
      pid_t pid = 0;
      GError *error = NULL;
      gchar *caller = dbus_g_method_get_sender ((DBusGMethodInvocation *) call);
      DBusGProxy *proxy = dbus_g_proxy_new_for_name (dgc,
          DBUS_SERVICE_DBUS,
          DBUS_PATH_DBUS,
          DBUS_INTERFACE_DBUS);

      dbus_g_proxy_call (proxy, "GetConnectionUnixProcessID", &error,
          G_TYPE_STRING, caller,
          G_TYPE_INVALID,
          G_TYPE_UINT, &pid,
          G_TYPE_INVALID);

      ok = pid_is_permitted (pid);

      g_free (caller);
      g_object_unref (proxy);
    }

  DEBUG ("sync Aegis ACL check [%s]", ok ? "Allowed" : "Forbidden");

  return ok;
}

static void
async_authorised_cb (DBusGProxy *proxy,
    DBusGProxyCall *call,
    gpointer data)
{
  GError *error = NULL;
  DBusAclAuthData *ad = data;
  pid_t pid = 0;
  const McpDBusAcl *self = ad->acl;
  gboolean permitted = FALSE;

  /* if this returns FALSE, there's no PID, which means something bizarre   *
   * and untrustowrthy is going on, which in turn means we must deny: can't *
   * authorise without first authenticating                                 */
  permitted = dbus_g_proxy_end_call (proxy, call, &error,
      G_TYPE_UINT, &pid,
      G_TYPE_INVALID);

  if (permitted)
    permitted = pid_is_permitted (pid);
  else
    g_error_free (error);

  DEBUG ("finished async Aegis ACL check [%u -> %s]",
      pid, permitted ? "Allowed" : "Forbidden");

  mcp_dbus_acl_authorised_async_step (ad, permitted);

  g_object_unref (proxy);
}

static void
caller_async_authorised (const McpDBusAcl *self,
    DBusAclAuthData *data)
{
  DEBUG ("starting async caller-permission ACL check");

  if (is_filtered (data->type, data->name, data->params))
    {
      DBusGConnection *dgc;
      DBusGProxy *proxy;
      gchar *caller = dbus_g_method_get_sender (data->context);

      dgc = tp_proxy_get_dbus_connection (data->dbus);
      proxy = dbus_g_proxy_new_for_name (dgc,
          DBUS_SERVICE_DBUS,
          DBUS_PATH_DBUS,
          DBUS_INTERFACE_DBUS);

      dbus_g_proxy_begin_call (proxy, "GetConnectionUnixProcessID",
          async_authorised_cb,
          data,
          NULL,
          G_TYPE_STRING, caller,
          G_TYPE_INVALID);

      g_free (caller);
    }
  else
    {
      mcp_dbus_acl_authorised_async_step (data, TRUE);
    }
}


static void
aegis_acl_iface_init (McpDBusAclIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  mcp_dbus_acl_iface_set_name (iface, PLUGIN_NAME);
  mcp_dbus_acl_iface_set_desc (iface, PLUGIN_DESCRIPTION);

  mcp_dbus_acl_iface_implement_authorised (iface, caller_authorised);
  mcp_dbus_acl_iface_implement_authorised_async (iface, caller_async_authorised);
}

static gchar *restricted_cms[] = { "ring", "mmscm", NULL };

static inline gboolean
cm_is_restricted (const gchar *cm_name)
{
  guint i;

  for (i = 0; restricted_cms[i] != NULL; i++)
    {
      if (!tp_strdiff (restricted_cms[i], cm_name))
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
channel_authorised (const McpDBusChannelAcl *self,
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

      ok = pid_is_permitted (pid);

      g_object_unref (proxy);
    }

  DEBUG ("sync Aegis Channel ACL check [%s]", ok ? "Allowed" : "Forbidden");

  return ok;
}

static void
aegis_channel_acl_iface_init (McpDBusChannelAclIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  mcp_dbus_channel_acl_iface_set_name (iface, PLUGIN_NAME);
  mcp_dbus_channel_acl_iface_set_desc (iface, PLUGIN_DESCRIPTION);
  mcp_dbus_channel_acl_iface_implement_authorised (iface, channel_authorised);
}

GObject *
aegis_acl_new (void)
{
  return g_object_new (aegis_acl_get_type (), NULL);
}
