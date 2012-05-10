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

#define DEBUG(_f, ...) MCP_DEBUG (MCP_DEBUG_DBUS_ACL, _f, ##__VA_ARGS__)

#include <dbus/dbus-glib.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <mission-control-plugins/mission-control-plugins.h>

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

/* implemented by the Aegis-patched dbus-daemon */
#define AEGIS_INTERFACE "com.meego.DBus.Creds"

#define PLUGIN_NAME "dbus-aegis-acl"
#define PLUGIN_DESCRIPTION \
  "This plugin uses libcreds to check the aegis security tokens " \
  "associated with the calling process ID and determine whether " \
  "the DBus call or property access should be allowed"

static creds_value_t aegis_token = CREDS_BAD;
static creds_type_t aegis_type = CREDS_BAD;

static void aegis_acl_iface_init (McpDBusAclIface *,
    gpointer);
static void aegis_cdo_policy_iface_init (McpDispatchOperationPolicyIface *,
    gpointer);

static GType aegis_acl_get_type (void);

G_DEFINE_TYPE_WITH_CODE (AegisAcl, aegis_acl,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_DBUS_ACL, aegis_acl_iface_init);
    G_IMPLEMENT_INTERFACE (MCP_TYPE_DISPATCH_OPERATION_POLICY,
      aegis_cdo_policy_iface_init))

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

/* For simplicity we don't implement non-trivial conversion between
 * dbus-glib's arrays of guint, and libcreds' arrays of uint32_t.
 * If this assertion fails on your platform, you'll need to implement it. */
G_STATIC_ASSERT (sizeof (guint) == sizeof (uint32_t));

static gboolean
caller_creds_are_enough (const gchar *name,
    const GArray *au)
{
  creds_t caller_creds = creds_import ((const uint32_t *) au->data, au->len);
  gboolean ok = creds_have_p (caller_creds, aegis_type, aegis_token);

#ifdef ENABLE_DEBUG
  if (ok)
    {
      DEBUG ("Caller %s is appropriately privileged", name);
    }
  else
    {
      char buf[1024];
      creds_type_t debug_type;
      creds_value_t debug_value;
      int i = 0;

      DEBUG ("Caller %s has these credentials:", name);

      while ((debug_type = creds_list (caller_creds, i++, &debug_value))
          != CREDS_BAD)
        {
          creds_creds2str (debug_type, debug_value, buf, sizeof (buf));
          DEBUG ("- %s", buf);
        }

      DEBUG ("but they are insufficient");
    }
#endif

  creds_free (caller_creds);
  return ok;
}

static gboolean
check_peer_creds_sync (DBusGConnection *dgc,
    const gchar *bus_name,
    gboolean activate)
{
  DBusGProxy *proxy = dbus_g_proxy_new_for_name (dgc,
      DBUS_SERVICE_DBUS,
      DBUS_PATH_DBUS,
      AEGIS_INTERFACE);
  GArray *au = NULL;
  GError *error = NULL;
  gboolean ok = FALSE;

  if (dbus_g_proxy_call (proxy, "GetConnectionCredentials", &error,
      G_TYPE_STRING, bus_name,
      G_TYPE_INVALID,
      DBUS_TYPE_G_UINT_ARRAY, &au,
      G_TYPE_INVALID))
    {
      ok = caller_creds_are_enough (bus_name, au);
      g_array_unref (au);
    }
  else if (activate && error->code == DBUS_GERROR_NAME_HAS_NO_OWNER)
    {
      guint status;
      GError *start_error = NULL;
      DBusGProxy *dbus = dbus_g_proxy_new_for_name (dgc,
          DBUS_SERVICE_DBUS,
          DBUS_PATH_DBUS,
          DBUS_INTERFACE_DBUS);

      DEBUG ("Trying to activate %s for aegis credentials check", bus_name);
      if (dbus_g_proxy_call (dbus, "StartServiceByName", &start_error,
              G_TYPE_STRING, bus_name,
              G_TYPE_UINT, 0,
              G_TYPE_INVALID,
              G_TYPE_UINT, &status,
              G_TYPE_INVALID))
        {
          ok = check_peer_creds_sync (dgc, bus_name, FALSE);
        }
      else
        {
          DEBUG ("GetConnectionCredentials failed: %s", start_error->message);
          g_clear_error (&start_error);
        }

      g_object_unref (dbus);
      g_clear_error (&error);
    }
  else
    {
      DEBUG ("GetConnectionCredentials failed: %s", error->message);
      g_clear_error (&error);
      ok = FALSE;
    }

  g_object_unref (proxy);
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
      gchar *caller = dbus_g_method_get_sender ((DBusGMethodInvocation *) call);

      ok = check_peer_creds_sync (dgc, caller, FALSE);

      g_free (caller);
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
  GArray *au = NULL;
  const McpDBusAcl *self = ad->acl;
  gboolean permitted = FALSE;

  /* if this returns FALSE, there are no credentials, which means something
   * untrustworthy is going on, which in turn means we must deny: can't
   * authorise without first authenticating */
  permitted = dbus_g_proxy_end_call (proxy, call, &error,
      DBUS_TYPE_G_UINT_ARRAY, &au,
      G_TYPE_INVALID);

  if (permitted)
    {
      permitted = caller_creds_are_enough (ad->name, au);
      g_array_unref (au);
    }
  else
    {
      DEBUG ("GetConnectionCredentials failed: %s", error->message);
      g_clear_error (&error);
    }

  DEBUG ("finished async Aegis ACL check [%s]",
      permitted ? "Allowed" : "Forbidden");

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
          AEGIS_INTERFACE);

      dbus_g_proxy_begin_call (proxy, "GetConnectionCredentials",
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

static void
handler_is_suitable_async (McpDispatchOperationPolicy *self,
    TpClient *recipient,
    const gchar *unique_name,
    McpDispatchOperation *dispatch_op,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  const gchar *manager = mcp_dispatch_operation_get_cm_name (dispatch_op);
  GSimpleAsyncResult *simple = g_simple_async_result_new ((GObject *) self,
      callback, user_data, handler_is_suitable_async);
  gboolean ok = TRUE;

  if (cm_is_restricted (manager))
    {
      TpDBusDaemon *dbus = tp_dbus_daemon_dup (NULL);

      /* if MC started successfully, we ought to have one */
      g_assert (dbus != NULL);

      if (!tp_str_empty (unique_name))
        {
          ok = check_peer_creds_sync (tp_proxy_get_dbus_connection (dbus),
              unique_name, TRUE);
        }
      else
        {
          g_assert (recipient != NULL);

          ok = check_peer_creds_sync (tp_proxy_get_dbus_connection (dbus),
              tp_proxy_get_bus_name (recipient), TRUE);
        }

      if (!ok)
        {
          g_simple_async_result_set_error (simple, TP_ERROR,
              TP_ERROR_PERMISSION_DENIED, "insufficient Aegis credentials");
        }

      g_object_unref (dbus);
    }

  DEBUG ("sync Aegis CDO policy check [%s]", ok ? "Allowed" : "Forbidden");

  g_simple_async_result_complete_in_idle (simple);
  g_object_unref (simple);
}

static void
aegis_cdo_policy_iface_init (McpDispatchOperationPolicyIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  iface->handler_is_suitable_async = handler_is_suitable_async;
  /* the default finish function accepts our GSimpleAsyncResult */
}

GObject *
aegis_acl_new (void)
{
  return g_object_new (aegis_acl_get_type (), NULL);
}
