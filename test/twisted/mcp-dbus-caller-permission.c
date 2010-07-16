/*
 * A demonstration plugin that checks a DBus caller's md5sum
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010 Collabora Ltd.
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

#include <mission-control-plugins/mission-control-plugins.h>
#include <sys/types.h>

#define CONFFILE "mcp-dbus-caller-permissions.conf"

#define DEBUG g_debug

#define PLUGIN_NAME "dbus-caller-permission-checker"
#define PLUGIN_DESCRIPTION \
  "Test plugin that checks the md5 checksum of a DBus caller. "     \
  "gkeyfile g_get_user_cache_dir()/" CONFFILE " holds the [paths "  \
  "to] the binaries, and the permission tokens associated with each."

/* Example conf file:
[/usr/local/bin/mc-tool]
org.freedesktop.Telepathy.AccountManager=1
*=1
*/

static void dbus_acl_iface_init (McpDBusAclIface *,
    gpointer);

typedef struct {
  GObject parent;
  GKeyFile *permits;
  gboolean loaded;
} DBusCallerPermission;

typedef struct {
  GObjectClass parent_class;
} DBusCallerPermissionClass;

GType dbus_caller_permission_get_type (void) G_GNUC_CONST;

#define DBUS_CALLER_PERMISSION(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), dbus_caller_permission_get_type (), \
      DBusCallerPermission))

G_DEFINE_TYPE_WITH_CODE (DBusCallerPermission, dbus_caller_permission,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_DBUS_ACL, dbus_acl_iface_init));

static void
dbus_caller_permission_init (DBusCallerPermission *self)
{
  const gchar *dir;
  gchar *file = NULL;

  self->permits = g_key_file_new ();

  dir  = g_get_user_cache_dir ();
  file = g_build_path (G_DIR_SEPARATOR_S, dir, CONFFILE, NULL);

  if (!g_file_test (file, G_FILE_TEST_EXISTS))
    {
      g_mkdir_with_parents (dir, 0700);
      g_file_set_contents (file, "# MC DBus permissions\n", -1, NULL);
    }

  DEBUG ("conf file %s", file);
  g_key_file_load_from_file (self->permits, file, G_KEY_FILE_NONE, NULL);

  g_free (file);
}

static void
dbus_caller_permission_class_init (DBusCallerPermissionClass *cls)
{
}

static gboolean is_filtered (const McpDBusAcl *self,
    DBusAclType type,
    const gchar *name)
{
  DBusCallerPermission *plugin = DBUS_CALLER_PERMISSION (self);
  GKeyFile *permits = plugin->permits;

  switch (type)
    {
      case DBUS_ACL_TYPE_METHOD:
        return g_key_file_get_boolean (permits, "methods", name, NULL);
      case DBUS_ACL_TYPE_GET_PROPERTY:
        return g_key_file_get_boolean (permits, "get-property", name, NULL);
      case DBUS_ACL_TYPE_SET_PROPERTY:
        return g_key_file_get_boolean (permits, "set-property", name, NULL);
      default:
        return FALSE;
    }
}

static gboolean
pid_is_permitted (const McpDBusAcl *self, const gchar *name, pid_t pid)
{
  gboolean ok = FALSE;

  if (pid != 0)
    {
      gchar *path = g_strdup_printf ("/proc/%d/exe", pid);
      gchar *executable = g_file_read_link (path, NULL);

      if (executable != NULL)
        {
          DBusCallerPermission *plugin = DBUS_CALLER_PERMISSION (self);
          GKeyFile *permits = plugin->permits;

          DEBUG ("executable to check for permission is %s", executable);
          ok = g_key_file_get_boolean (permits, executable, name, NULL);
          DEBUG ("%s:%s = %s", executable, name, ok ? "TRUE" : "FALSE");

          g_free (executable);
        }

      g_free (path);
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

  if (is_filtered (self, type, name))
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

      ok = pid_is_permitted (self, name, pid);

      g_free (caller);
      g_object_unref (proxy);
    }

  DEBUG ("sync caller-permission ACL check [%s]", ok ? "Allowed" : "Forbidden");

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
    permitted = pid_is_permitted (self, ad->name, pid);
  else
    g_error_free (error);

  DEBUG ("finished async caller-permission ACL check [%u -> %s]",
      pid, permitted ? "Allowed" : "Forbidden");

  mcp_dbus_acl_authorised_async_step (ad, permitted);
}

static void
caller_async_authorised (const McpDBusAcl *self,
    DBusAclAuthData *data)
{
  DBusGConnection *dgc = tp_proxy_get_dbus_connection (data->dbus);
  DBusGProxy *proxy = dbus_g_proxy_new_for_name (dgc,
      DBUS_SERVICE_DBUS,
      DBUS_PATH_DBUS,
      DBUS_INTERFACE_DBUS);

  DEBUG ("starting async caller-permission ACL check");

  if (is_filtered (self, data->type, data->name))
    {
      gchar *caller = dbus_g_method_get_sender (data->context);

      dbus_g_proxy_begin_call (proxy, "GetConnectionUnixProcessID",
          async_authorised_cb,
          data,
          NULL,
          G_TYPE_STRING, caller,
          G_TYPE_INVALID);

      g_free (caller);
    }
  else /* not filtered, so the call is allowed: */
    {
      mcp_dbus_acl_authorised_async_step (data, TRUE);
    }
}


static void
dbus_acl_iface_init (McpDBusAclIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  mcp_dbus_acl_iface_set_name (iface, PLUGIN_NAME);
  mcp_dbus_acl_iface_set_desc (iface, PLUGIN_DESCRIPTION);

  mcp_dbus_acl_iface_implement_authorised (iface, caller_authorised);
  mcp_dbus_acl_iface_implement_authorised_async (iface, caller_async_authorised);
}

GObject *
mcp_plugin_ref_nth_object (guint n)
{
  DEBUG ("Initializing mcp-dbus-caller-id plugin (n=%u)", n);

  switch (n)
    {
    case 0:
      return g_object_new (dbus_caller_permission_get_type (), NULL);

    default:
      return NULL;
    }
}

