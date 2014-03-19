/* Representation of the account manager as presented to plugins. This is
 * deliberately a "smaller" API than McdAccountManager.
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010-2012 Collabora Ltd.
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
#include "config.h"
#include "mcd-storage.h"

#include "mcd-account.h"
#include "mcd-account-config.h"
#include "mcd-debug.h"
#include "mcd-misc.h"
#include "plugin-loader.h"

#include <errno.h>
#include <string.h>

#include <dbus/dbus.h>
#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "mission-control-plugins/implementation.h"

/* these pseudo-plugins take care of the actual account storage/retrieval */
#include "mcd-account-manager-default.h"

#define MAX_KEY_LENGTH (DBUS_MAXIMUM_NAME_LENGTH + 6)

static GList *stores = NULL;
static void sort_and_cache_plugins (void);

enum {
  PROP_CLIENT_FACTORY = 1,
};

struct _McdStorageClass {
    GObjectClass parent;
};

static void plugin_iface_init (McpAccountManagerIface *iface,
    gpointer unused G_GNUC_UNUSED);

G_DEFINE_TYPE_WITH_CODE (McdStorage, mcd_storage,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_MANAGER, plugin_iface_init))

static void
mcd_storage_init (McdStorage *self)
{
  self->accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);
}

static void
storage_finalize (GObject *object)
{
  McdStorage *self = MCD_STORAGE (object);
  GObjectFinalizeFunc finalize =
    G_OBJECT_CLASS (mcd_storage_parent_class)->finalize;

  g_hash_table_unref (self->accounts);
  self->accounts = NULL;

  if (finalize != NULL)
    finalize (object);
}

static void
storage_dispose (GObject *object)
{
  McdStorage *self = MCD_STORAGE (object);
  GObjectFinalizeFunc dispose =
    G_OBJECT_CLASS (mcd_storage_parent_class)->dispose;

  tp_clear_object (&self->factory);

  if (dispose != NULL)
    dispose (object);
}

static void
storage_set_property (GObject *obj, guint prop_id,
	      const GValue *val, GParamSpec *pspec)
{
    McdStorage *self = MCD_STORAGE (obj);

    switch (prop_id)
    {
      case PROP_CLIENT_FACTORY:
        g_assert (self->factory == NULL); /* construct only */
        self->factory = g_value_dup_object (val);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
        break;
    }
}

static void
storage_get_property (GObject *obj, guint prop_id,
	      GValue *val, GParamSpec *pspec)
{
    McdStorage *self = MCD_STORAGE (obj);

    switch (prop_id)
    {
      case PROP_CLIENT_FACTORY:
        g_value_set_object (val, self->factory);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
        break;
    }
}

static void
mcd_storage_class_init (McdStorageClass *cls)
{
  GObjectClass *object_class = (GObjectClass *) cls;
  GParamSpec *spec = g_param_spec_object ("client-factory",
      "client-factory",
      "TpClientFactory",
      TP_TYPE_CLIENT_FACTORY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  object_class->set_property = storage_set_property;
  object_class->get_property = storage_get_property;
  object_class->dispose = storage_dispose;
  object_class->finalize = storage_finalize;

  g_object_class_install_property (object_class, PROP_CLIENT_FACTORY, spec);
}

McdStorage *
mcd_storage_new (TpClientFactory *factory)
{
  return g_object_new (MCD_TYPE_STORAGE,
      "client-factory", factory,
      NULL);
}

static gchar *
mcd_keyfile_escape_variant (GVariant *variant)
{
  GKeyFile *keyfile;
  gchar *ret;

  g_return_val_if_fail (variant != NULL, NULL);

  keyfile = g_key_file_new ();
  mcd_keyfile_set_variant (keyfile, "g", "k", variant);
  ret = g_key_file_get_value (keyfile, "g", "k", NULL);
  g_key_file_free (keyfile);
  return ret;
}

static struct {
    const gchar *type;
    const gchar *name;
} known_attributes[] = {
    /* Please keep this sorted by type, then by name. */

    /* Structs */
      { "(uss)", MC_ACCOUNTS_KEY_AUTOMATIC_PRESENCE },

    /* Array of object path */
      { "ao", MC_ACCOUNTS_KEY_SUPERSEDES },

    /* Array of string */
      { "as", MC_ACCOUNTS_KEY_URI_SCHEMES },

    /* Booleans */
      { "b", MC_ACCOUNTS_KEY_ALWAYS_DISPATCH },
      { "b", MC_ACCOUNTS_KEY_CONNECT_AUTOMATICALLY },
      { "b", MC_ACCOUNTS_KEY_ENABLED },
      { "b", MC_ACCOUNTS_KEY_HAS_BEEN_ONLINE },

    /* Strings */
      { "s", MC_ACCOUNTS_KEY_AUTO_PRESENCE_MESSAGE },
      { "s", MC_ACCOUNTS_KEY_AUTO_PRESENCE_STATUS },
      { "s", MC_ACCOUNTS_KEY_AVATAR_MIME },
      { "s", MC_ACCOUNTS_KEY_AVATAR_TOKEN },
      { "s", MC_ACCOUNTS_KEY_DISPLAY_NAME },
      { "s", MC_ACCOUNTS_KEY_ICON },
      { "s", MC_ACCOUNTS_KEY_MANAGER },
      { "s", MC_ACCOUNTS_KEY_NICKNAME },
      { "s", MC_ACCOUNTS_KEY_NORMALIZED_NAME },
      { "s", MC_ACCOUNTS_KEY_PROTOCOL },
      { "s", MC_ACCOUNTS_KEY_SERVICE },

    /* Integers */
      { "u", MC_ACCOUNTS_KEY_AUTO_PRESENCE_TYPE },

      { NULL, NULL }
};

const GVariantType *
mcd_storage_get_attribute_type (const gchar *attribute)
{
  guint i;

  for (i = 0; known_attributes[i].type != NULL; i++)
    {
      if (!tp_strdiff (attribute, known_attributes[i].name))
        return G_VARIANT_TYPE (known_attributes[i].type);
    }

  return NULL;
}

gboolean
mcd_storage_init_value_for_attribute (GValue *value,
    const gchar *attribute,
    const GVariantType **variant_type)
{
  const GVariantType *s = mcd_storage_get_attribute_type (attribute);

  if (s == NULL)
    return FALSE;

  if (variant_type != NULL)
    *variant_type = s;

  switch (g_variant_type_peek_string (s)[0])
    {
      case 's':
        g_value_init (value, G_TYPE_STRING);
        return TRUE;

      case 'b':
        g_value_init (value, G_TYPE_BOOLEAN);
        return TRUE;

      case 'u':
        /* this seems wrong but it's how we've always done it */
        g_value_init (value, G_TYPE_INT);
        return TRUE;

      case 'a':
          {
            switch (g_variant_type_peek_string (s)[1])
              {
                case 'o':
                  g_value_init (value, TP_ARRAY_TYPE_OBJECT_PATH_LIST);
                  return TRUE;

                case 's':
                  g_value_init (value, G_TYPE_STRV);
                  return TRUE;
              }
          }
        break;

      case '(':
          {
            if (g_variant_type_equal (s, G_VARIANT_TYPE ("(uss)")))
              {
                g_value_init (value, TP_STRUCT_TYPE_PRESENCE);
                return TRUE;
              }
          }
        break;
    }

  return FALSE;
}

static gchar *
unique_name (const McpAccountManager *ma,
    const gchar *manager,
    const gchar *protocol,
    const gchar *identification)
{
  McdStorage *self = MCD_STORAGE (ma);
  gchar *esc_manager, *esc_protocol, *esc_base;
  guint i;
  gsize base_len = strlen (TP_ACCOUNT_OBJECT_PATH_BASE);
  TpDBusDaemon *dbus = tp_client_factory_get_dbus_daemon (self->factory);
  DBusGConnection *connection = tp_proxy_get_dbus_connection (dbus);

  esc_manager = tp_escape_as_identifier (manager);
  esc_protocol = g_strdelimit (g_strdup (protocol), "-", '_');
  esc_base = tp_escape_as_identifier (identification);

  for (i = 0; i < G_MAXUINT; i++)
    {
      gchar *path = g_strdup_printf (
          TP_ACCOUNT_OBJECT_PATH_BASE "%s/%s/%s%u",
          esc_manager, esc_protocol, esc_base, i);

      if (!g_hash_table_contains (self->accounts, path + base_len) &&
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
identify_account_cb (TpProxy *proxy,
    const gchar *identification,
    const GError *error,
    gpointer task,
    GObject *weak_object G_GNUC_UNUSED)
{
  if (error == NULL)
    {
      DEBUG ("identified account: %s", identification);
      g_task_return_pointer (task, g_strdup (identification), g_free);
    }
  else if (g_error_matches (error, TP_ERROR, TP_ERROR_INVALID_HANDLE) ||
      g_error_matches (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT))
    {
      /* The connection manager didn't like our account parameters.
       * Give up now. */
      DEBUG ("failed to identify account: %s #%d: %s",
          g_quark_to_string (error->domain), error->code, error->message);
      g_task_return_error (task, g_error_copy (error));
    }
  else
    {
      /* We weren't able to identify the account, but carry on and hope
       * for the best... */
      DEBUG ("ignoring failure to identify account: %s #%d: %s",
          g_quark_to_string (error->domain), error->code, error->message);
      g_task_return_pointer (task, g_strdup (g_task_get_task_data (task)),
          g_free);
    }
}

static gchar *
identify_account_finish (McpAccountManager *mcpa,
    GAsyncResult *result,
    GError **error)
{
  g_return_val_if_fail (g_task_is_valid (result, mcpa), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
identify_account_async (McpAccountManager *mcpa,
    const gchar *manager,
    const gchar *protocol_name,
    GVariant *parameters,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  McdStorage *self = MCD_STORAGE (mcpa);
  GError *error = NULL;
  TpProtocol *protocol;
  GTask *task;
  GValue value = G_VALUE_INIT;
  const gchar *base;

  task = g_task_new (self, cancellable, callback, user_data);

  /* in case IdentifyAccount fails and we need to make something up */
  if (!g_variant_lookup (parameters, "account", "&s", &base))
    base = "account";

  g_task_set_task_data (task, g_strdup (base), g_free);

  protocol = tp_client_factory_ensure_protocol (self->factory, manager,
      protocol_name, NULL, &error);

  if (protocol == NULL)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  dbus_g_value_parse_g_variant (parameters, &value);

  tp_cli_protocol_call_identify_account (protocol, -1,
      g_value_get_boxed (&value), identify_account_cb, task, g_object_unref,
      NULL);
  g_object_unref (protocol);
  g_value_unset (&value);
}

/* sort in descending order of priority (ie higher prio => earlier in list) */
static gint
account_storage_cmp (gconstpointer a, gconstpointer b)
{
    gint pa = mcp_account_storage_priority ((McpAccountStorage *) a);
    gint pb = mcp_account_storage_priority ((McpAccountStorage *) b);

    if (pa > pb) return -1;
    if (pa < pb) return 1;

    return 0;
}

static void
add_storage_plugin (McpAccountStorage *plugin)
{
  stores = g_list_insert_sorted (stores, plugin, account_storage_cmp);
}

static void
sort_and_cache_plugins ()
{
  const GList *p;
  static gboolean plugins_cached = FALSE;

  if (plugins_cached)
    return;

  /* not guaranteed to have been called, but idempotent: */
  _mcd_plugin_loader_init ();

  /* Add compiled-in plugins */
  add_storage_plugin (MCP_ACCOUNT_STORAGE (mcd_account_manager_default_new ()));

  for (p = mcp_list_objects(); p != NULL; p = g_list_next (p))
    {
      if (MCP_IS_ACCOUNT_STORAGE (p->data))
        {
          McpAccountStorage *plugin = g_object_ref (p->data);

          add_storage_plugin (plugin);
        }
    }

  for (p = stores; p != NULL; p = g_list_next (p))
    {
      McpAccountStorage *plugin = p->data;

      DEBUG ("found plugin %s [%s; priority %d]\n%s",
          mcp_account_storage_name (plugin),
          g_type_name (G_TYPE_FROM_INSTANCE (plugin)),
          mcp_account_storage_priority (plugin),
          mcp_account_storage_description (plugin));
    }

    plugins_cached = TRUE;
}

void
mcd_storage_connect_signal (const gchar *signame,
    GCallback func,
    gpointer user_data)
{
  GList *p;

  for (p = stores; p != NULL; p = g_list_next (p))
    {
      McpAccountStorage *plugin = p->data;

      DEBUG ("connecting handler to %s plugin signal %s ",
          mcp_account_storage_name (plugin), signame);
      g_signal_connect (plugin, signame, func, user_data);
    }
}

/*
 * mcd_storage_load:
 * @storage: An object implementing the #McdStorage interface
 *
 * Load the long term account settings storage into our internal cache.
 * Should only really be called during startup, ie before our DBus names
 * have been claimed and other people might be relying on responses from us.
 */
void
mcd_storage_load (McdStorage *self)
{
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  GList *store = NULL;

  g_return_if_fail (MCD_IS_STORAGE (self));

  sort_and_cache_plugins ();

  /* fetch accounts stored in plugins, highest priority first, so that
   * low priority plugins can be overidden by high priority */
  for (store = stores; store != NULL; store = store->next)
    {
      GList *account;
      McpAccountStorage *plugin = store->data;
      GList *stored = mcp_account_storage_list (plugin, ma);
      const gchar *pname = mcp_account_storage_name (plugin);
      const gint prio = mcp_account_storage_priority (plugin);

      DEBUG ("listing from plugin %s [prio: %d]", pname, prio);
      for (account = stored; account != NULL; account = g_list_next (account))
        {
          GError *error = NULL;
          gchar *name = account->data;

          DEBUG ("fetching %s from plugin %s [prio: %d]", name, pname, prio);

          if (!mcd_storage_add_account_from_plugin (self, plugin, name,
                &error))
            {
              DEBUG ("%s", error->message);
              g_clear_error (&error);
            }

          g_free (name);
        }

      /* already freed the contents, just need to free the list itself */
      g_list_free (stored);
    }
}

/*
 * mcd_storage_get_accounts:
 * @storage: An object implementing the #McdStorage interface
 * @n: place for the number of accounts to be written to (or %NULL)
 *
 * Returns: (transfer none) (element-type utf8 Mcp.AccountStorage): a
 *  map from account object path tail to plugin
 */
GHashTable *
mcd_storage_get_accounts (McdStorage *self)
{
  return self->accounts;
}

/*
 * mcd_storage_get_plugin:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 *
 * Returns: the #McpAccountStorage object which is handling the account,
 * if any (if a new account has not yet been flushed to storage this can
 * be %NULL).
 *
 * Plugins are kept in permanent storage and can never be unloaded, so
 * the returned pointer need not be reffed or unreffed. (Indeed, it's
 * probably safer not to)
 */
McpAccountStorage *
mcd_storage_get_plugin (McdStorage *self,
    const gchar *account)
{
  McpAccountStorage *plugin;

  g_return_val_if_fail (MCD_IS_STORAGE (self), NULL);
  g_return_val_if_fail (account != NULL, NULL);

  plugin = g_hash_table_lookup (self->accounts, account);
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin;
}

/*
 * mcd_storage_dup_string:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @attribute: name of the attribute to be retrieved (which must not be a
 *  parameter)
 *
 * Returns: a newly allocated gchar * which must be freed with g_free().
 */
gchar *
mcd_storage_dup_string (McdStorage *self,
    const gchar *account,
    const gchar *attribute)
{
  GValue tmp = G_VALUE_INIT;
  gchar *ret;

  g_return_val_if_fail (MCD_IS_STORAGE (self), NULL);
  g_return_val_if_fail (account != NULL, NULL);
  g_return_val_if_fail (attribute != NULL, NULL);
  g_return_val_if_fail (!g_str_has_prefix (attribute, "param-"), NULL);

  g_value_init (&tmp, G_TYPE_STRING);

  if (!mcd_storage_get_attribute (self, account, attribute,
        G_VARIANT_TYPE_STRING, &tmp, NULL))
    return NULL;

  ret = g_value_dup_string (&tmp);
  g_value_unset (&tmp);
  return ret;
}

static gboolean
mcd_storage_coerce_variant_to_value (GVariant *variant,
    GValue *value,
    GError **error)
{
  GValue tmp = G_VALUE_INIT;
  gboolean ret;
  gchar *escaped;

  dbus_g_value_parse_g_variant (variant, &tmp);

  if (G_VALUE_TYPE (&tmp) == G_VALUE_TYPE (value))
    {
      memcpy (value, &tmp, sizeof (tmp));
      return TRUE;
    }

  /* This is really pretty stupid but it'll do for now.
   * FIXME: implement a better similar-type-coercion mechanism than
   * round-tripping through a GKeyFile. */
  g_value_unset (&tmp);
  escaped = mcd_keyfile_escape_variant (variant);
  ret = mcd_keyfile_unescape_value (escaped, value, error);
  g_free (escaped);
  return ret;
}

/*
 * mcd_storage_get_attribute:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @attribute: name of the attribute to be retrieved, e.g. 'DisplayName'
 * @value: location to return the value, initialized to the right #GType
 * @error: a place to store any #GError<!-- -->s that occur
 */
gboolean
mcd_storage_get_attribute (McdStorage *self,
    const gchar *account,
    const gchar *attribute,
    const GVariantType *type,
    GValue *value,
    GError **error)
{
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  McpAccountStorage *plugin;
  GVariant *variant;
  gboolean ret;

  g_return_val_if_fail (MCD_IS_STORAGE (self), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (attribute != NULL, FALSE);
  g_return_val_if_fail (!g_str_has_prefix (attribute, "param-"), FALSE);

  plugin = g_hash_table_lookup (self->accounts, account);

  if (plugin == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Account %s does not exist", account);
      return FALSE;
    }

  variant = mcp_account_storage_get_attribute (plugin, ma, account,
      attribute, type, NULL);

  if (variant == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Account %s has no attribute '%s'", account, attribute);
      return FALSE;
    }

  ret = mcd_storage_coerce_variant_to_value (variant, value, error);
  g_variant_unref (variant);
  return ret;
}

/*
 * mcd_storage_get_parameter:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @parameter: name of the parameter to be retrieved, e.g. 'account'
 * @value: location to return the value, initialized to the right #GType
 * @error: a place to store any #GError<!-- -->s that occur
 */
gboolean
mcd_storage_get_parameter (McdStorage *self,
    const gchar *account,
    const gchar *parameter,
    const GVariantType *type,
    GValue *value,
    GError **error)
{
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  McpAccountStorage *plugin;
  GVariant *variant;
  gboolean ret;

  g_return_val_if_fail (MCD_IS_STORAGE (self), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (parameter != NULL, FALSE);
  g_return_val_if_fail (!g_str_has_prefix (parameter, "param-"), FALSE);

  plugin = g_hash_table_lookup (self->accounts, account);

  if (plugin == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Account %s does not exist", account);
      return FALSE;
    }

  variant = mcp_account_storage_get_parameter (plugin, ma, account,
      parameter, type, NULL);

  if (variant == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Account %s has no parameter '%s'", account, parameter);
      return FALSE;
    }

  ret = mcd_storage_coerce_variant_to_value (variant, value, error);
  g_variant_unref (variant);
  return ret;
}

/*
 * @escaped: a keyfile-escaped string
 * @value: a #GValue initialized with a supported #GType
 * @error: used to raise an error if %FALSE is returned
 *
 * Try to interpret @escaped as a value of the type of @value. If we can,
 * write the resulting value into @value and return %TRUE.
 *
 * Returns: %TRUE if @escaped could be interpreted as a value of that type
 */
gboolean
mcd_keyfile_unescape_value (const gchar *escaped,
    GValue *value,
    GError **error)
{
  GKeyFile *keyfile;
  gboolean ret;

  g_return_val_if_fail (escaped != NULL, FALSE);
  g_return_val_if_fail (G_IS_VALUE (value), FALSE);

  keyfile = g_key_file_new ();
  g_key_file_set_value (keyfile, "g", "k", escaped);
  ret = mcd_keyfile_get_value (keyfile, "g", "k", value, error);
  g_key_file_free (keyfile);
  return ret;
}

/*
 * mcd_keyfile_get_value:
 * @keyfile: A #GKeyFile
 * @group: name of a group
 * @key: name of a key
 * @value: location to return the value, initialized to the right #GType
 * @error: a place to store any #GError<!-- -->s that occur
 */
gboolean
mcd_keyfile_get_value (GKeyFile *keyfile,
    const gchar *group,
    const gchar *key,
    GValue *value,
    GError **error)
{
  GType type;
  GVariant *variant = NULL;

  g_return_val_if_fail (keyfile != NULL, FALSE);
  g_return_val_if_fail (group != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (G_IS_VALUE (value), FALSE);

  type = G_VALUE_TYPE (value);

  switch (type)
    {
      case G_TYPE_STRING:
        variant = mcd_keyfile_get_variant (keyfile, group, key,
            G_VARIANT_TYPE_STRING, error);
        break;

      case G_TYPE_INT:
        variant = mcd_keyfile_get_variant (keyfile, group, key,
            G_VARIANT_TYPE_INT32, error);
        break;

      case G_TYPE_INT64:
        variant = mcd_keyfile_get_variant (keyfile, group, key,
            G_VARIANT_TYPE_INT64, error);
        break;

      case G_TYPE_UINT:
        variant = mcd_keyfile_get_variant (keyfile, group, key,
            G_VARIANT_TYPE_UINT32, error);
        break;

      case G_TYPE_UCHAR:
        variant = mcd_keyfile_get_variant (keyfile, group, key,
            G_VARIANT_TYPE_BYTE, error);
        break;

      case G_TYPE_UINT64:
        variant = mcd_keyfile_get_variant (keyfile, group, key,
            G_VARIANT_TYPE_UINT64, error);
        break;

      case G_TYPE_BOOLEAN:
        variant = mcd_keyfile_get_variant (keyfile, group, key,
            G_VARIANT_TYPE_BOOLEAN, error);
        break;

      case G_TYPE_DOUBLE:
        variant = mcd_keyfile_get_variant (keyfile, group, key,
            G_VARIANT_TYPE_DOUBLE, error);
        break;

      default:
        if (type == G_TYPE_STRV)
          {
            variant = mcd_keyfile_get_variant (keyfile, group, key,
                G_VARIANT_TYPE_STRING_ARRAY, error);
          }
        else if (type == DBUS_TYPE_G_OBJECT_PATH)
          {
            variant = mcd_keyfile_get_variant (keyfile, group, key,
                G_VARIANT_TYPE_OBJECT_PATH, error);
          }
        else if (type == TP_ARRAY_TYPE_OBJECT_PATH_LIST)
          {
            variant = mcd_keyfile_get_variant (keyfile, group, key,
                G_VARIANT_TYPE_OBJECT_PATH_ARRAY, error);
          }
        else if (type == TP_STRUCT_TYPE_PRESENCE)
          {
            variant = mcd_keyfile_get_variant (keyfile, group, key,
                G_VARIANT_TYPE ("(uss)"), error);
          }
        else
          {
            gchar *message =
              g_strdup_printf ("cannot get key %s from group %s: "
                  "unknown type %s",
                  key, group, g_type_name (type));

            g_warning ("%s: %s", G_STRFUNC, message);
            g_set_error (error, MCD_ACCOUNT_ERROR,
                MCD_ACCOUNT_ERROR_GET_PARAMETER,
                "%s", message);
            g_free (message);
          }
    }

  if (variant == NULL)
    return FALSE;

  g_variant_ref_sink (variant);
  g_value_unset (value);
  dbus_g_value_parse_g_variant (variant, value);
  g_assert (G_VALUE_TYPE (value) == type);
  g_variant_unref (variant);
  return TRUE;
}

/*
 * mcd_keyfile_get_variant:
 * @keyfile: A #GKeyFile
 * @group: name of a group
 * @key: name of a key
 * @type: the desired type
 * @error: a place to store any #GError<!-- -->s that occur
 *
 * Returns: a new floating #GVariant
 */
GVariant *
mcd_keyfile_get_variant (GKeyFile *keyfile,
    const gchar *group,
    const gchar *key,
    const GVariantType *type,
    GError **error)
{
  const gchar *type_str = g_variant_type_peek_string (type);
  GVariant *ret = NULL;

  g_return_val_if_fail (keyfile != NULL, NULL);
  g_return_val_if_fail (group != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);
  g_return_val_if_fail (g_variant_type_string_scan (type_str, NULL, NULL),
      NULL);

  switch (type_str[0])
    {
      case G_VARIANT_CLASS_STRING:
          {
            gchar *v_string = g_key_file_get_string (keyfile, group,
                key, error);

            if (v_string != NULL)
              ret = g_variant_new_string (v_string);
            /* else error is already set */
          }
        break;

      case G_VARIANT_CLASS_INT32:
          {
            GError *e = NULL;
            gint v_int = g_key_file_get_integer (keyfile, group,
                key, &e);

            if (e != NULL)
              {
                g_propagate_error (error, e);
              }
            else
              {
                ret = g_variant_new_int32 (v_int);
              }
          }
        break;

      case G_VARIANT_CLASS_INT64:
          {
            GError *e = NULL;
            gint64 v_int = g_key_file_get_int64 (keyfile, group,
                key, &e);

            if (e != NULL)
              {
                g_propagate_error (error, e);
              }
            else
              {
                ret = g_variant_new_int64 (v_int);
              }
          }
        break;

      case G_VARIANT_CLASS_UINT32:
          {
            GError *e = NULL;
            guint64 v_uint = g_key_file_get_uint64 (keyfile, group,
                key, &e);

            if (e != NULL)
              {
                g_propagate_error (error, e);
              }
            else if (v_uint > G_MAXUINT32)
              {
                g_set_error (error, MCD_ACCOUNT_ERROR,
                    MCD_ACCOUNT_ERROR_GET_PARAMETER,
                    "Parameter '%s' out of range for an unsigned 32-bit "
                    "integer: %" G_GUINT64_FORMAT, key, v_uint);
              }
            else
              {
                ret = g_variant_new_uint32 (v_uint);
              }
          }
        break;

    case G_VARIANT_CLASS_BYTE:
          {
            GError *e = NULL;
            gint v_int = g_key_file_get_integer (keyfile, group,
                key, &e);

            if (e != NULL)
              {
                g_propagate_error (error, e);
              }
            else if (v_int < 0 || v_int > 0xFF)
              {
                g_set_error (error, MCD_ACCOUNT_ERROR,
                    MCD_ACCOUNT_ERROR_GET_PARAMETER,
                    "Parameter '%s' out of range for an unsigned byte: "
                    "%d", key, v_int);
              }
            else
              {
                ret = g_variant_new_byte (v_int);
              }
          }
        break;

      case G_VARIANT_CLASS_UINT64:
          {
            GError *e = NULL;
            guint64 v_uint = g_key_file_get_uint64 (keyfile, group,
                key, &e);

            if (e != NULL)
              {
                g_propagate_error (error, e);
              }
            else
              {
                ret = g_variant_new_uint64 (v_uint);
              }
          }
        break;

      case G_VARIANT_CLASS_BOOLEAN:
          {
            GError *e = NULL;
            gboolean v_bool = g_key_file_get_boolean (keyfile, group,
                key, &e);

            if (e != NULL)
              {
                g_propagate_error (error, e);
              }
            else
              {
                ret = g_variant_new_boolean (v_bool);
              }
          }
        break;

      case G_VARIANT_CLASS_DOUBLE:
          {
            GError *e = NULL;
            gdouble v_double = g_key_file_get_double (keyfile, group,
                key, &e);

            if (e != NULL)
              {
                g_propagate_error (error, e);
              }
            else
              {
                ret = g_variant_new_double (v_double);
              }
          }
        break;

      default:
        if (g_variant_type_equal (type, G_VARIANT_TYPE_STRING_ARRAY))
          {
            gchar **v = g_key_file_get_string_list (keyfile, group,
                key, NULL, error);

            if (v != NULL)
              {
                ret = g_variant_new_strv ((const gchar **) v, -1);
                g_strfreev (v);
              }
          }
        else if (g_variant_type_equal (type, G_VARIANT_TYPE_OBJECT_PATH))
          {
            gchar *v_string = g_key_file_get_string (keyfile, group,
                key, error);

            if (v_string == NULL)
              {
                /* do nothing, error is already set */
              }
            else if (!tp_dbus_check_valid_object_path (v_string, NULL))
              {
                g_set_error (error, MCD_ACCOUNT_ERROR,
                    MCD_ACCOUNT_ERROR_GET_PARAMETER,
                    "Invalid object path %s", v_string);
              }
            else
              {
                ret = g_variant_new_object_path (v_string);
              }

            g_free (v_string);
          }
        else if (g_variant_type_equal (type, G_VARIANT_TYPE_OBJECT_PATH_ARRAY))
          {
            gchar **v = g_key_file_get_string_list (keyfile, group,
                key, NULL, error);

            if (v != NULL)
              {
                gchar **iter;

                for (iter = v; iter != NULL && *iter != NULL; iter++)
                  {
                    if (!g_variant_is_object_path (*iter))
                      {
                        g_set_error (error, MCD_ACCOUNT_ERROR,
                            MCD_ACCOUNT_ERROR_GET_PARAMETER,
                            "Invalid object path %s stored in keyfile", *iter);
                        g_strfreev (v);
                        v = NULL;
                        break;
                      }
                  }

                ret = g_variant_new_objv ((const gchar **) v, -1);
                g_strfreev (v);
              }
          }
        else if (g_variant_type_equal (type, G_VARIANT_TYPE ("(uss)")))
          {
            gchar **v = g_key_file_get_string_list (keyfile, group,
                key, NULL, error);

            if (v == NULL)
              {
                /* error is already set, do nothing */
              }
            else if (g_strv_length (v) != 3)
              {
                g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
                    "Invalid presence structure stored in keyfile");
              }
            else
              {
                guint64 u;
                gchar *endptr;

                errno = 0;
                u = g_ascii_strtoull (v[0], &endptr, 10);

                if (errno != 0 || *endptr != '\0' || u > G_MAXUINT32)
                  {
                    g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
                        "Invalid presence type stored in keyfile: %s", v[0]);
                  }
                else
                  {
                    /* a syntactically valid simple presence */
                    ret = g_variant_new_parsed ("(%u, %s, %s)",
                        (guint32) u, v[1], v[2]);
                  }
              }

            g_strfreev (v);
          }
        else
          {
            gchar *message =
              g_strdup_printf ("cannot get key %s from group %s: "
                  "unknown type %.*s", key, group,
                  (int) g_variant_type_get_string_length (type),
                  type_str);

            g_warning ("%s: %s", G_STRFUNC, message);
            g_set_error (error, MCD_ACCOUNT_ERROR,
                MCD_ACCOUNT_ERROR_GET_PARAMETER,
                "%s", message);
            g_free (message);
          }
    }

  g_assert (ret == NULL || g_variant_is_of_type (ret, type));
  return ret;
}

/*
 * mcd_storage_get_boolean:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @key: name of the attribute to be retrieved
 *
 * Returns: a #gboolean. Unset/unparseable values are returned as %FALSE
 */
gboolean
mcd_storage_get_boolean (McdStorage *self,
    const gchar *account,
    const gchar *attribute)
{
  GValue tmp = G_VALUE_INIT;

  g_return_val_if_fail (MCD_IS_STORAGE (self), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (attribute != NULL, FALSE);
  g_return_val_if_fail (!g_str_has_prefix (attribute, "param-"), FALSE);

  g_value_init (&tmp, G_TYPE_BOOLEAN);

  if (!mcd_storage_get_attribute (self, account, attribute,
        G_VARIANT_TYPE_BOOLEAN, &tmp, NULL))
    return FALSE;

  return g_value_get_boolean (&tmp);
}

/*
 * mcd_storage_get_integer:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @attribute: name of the attribute to be retrieved
 *
 * Returns: a #gint. Unset or non-numeric values are returned as 0
 */
gint
mcd_storage_get_integer (McdStorage *self,
    const gchar *account,
    const gchar *attribute)
{
  GValue tmp = G_VALUE_INIT;

  g_return_val_if_fail (MCD_IS_STORAGE (self), 0);
  g_return_val_if_fail (account != NULL, 0);
  g_return_val_if_fail (attribute != NULL, 0);
  g_return_val_if_fail (!g_str_has_prefix (attribute, "param-"), 0);

  g_value_init (&tmp, G_TYPE_INT);

  if (!mcd_storage_get_attribute (self, account, attribute,
        G_VARIANT_TYPE_INT32, &tmp, NULL))
    return FALSE;

  return g_value_get_int (&tmp);
}

static gboolean
update_storage (McdStorage *self,
    const gchar *account,
    gboolean parameter,
    const gchar *key,
    GVariant *variant)
{
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  gboolean updated = FALSE;
  McpAccountStorage *plugin;
  const gchar *pn;
  McpAccountStorageSetResult res;

  plugin = g_hash_table_lookup (self->accounts, account);
  g_return_val_if_fail (plugin != NULL, FALSE);
  pn = mcp_account_storage_name (plugin);

  if (parameter)
    res = mcp_account_storage_set_parameter (plugin, ma, account,
        key, variant, MCP_PARAMETER_FLAG_NONE);
  else
    res = mcp_account_storage_set_attribute (plugin, ma, account,
        key, variant, MCP_ATTRIBUTE_FLAG_NONE);

  switch (res)
    {
      case MCP_ACCOUNT_STORAGE_SET_RESULT_CHANGED:
        DEBUG ("MCP:%s -> store %s %s.%s", pn,
            parameter ? "parameter" : "attribute", account, key);
        updated = TRUE;
        break;

      case MCP_ACCOUNT_STORAGE_SET_RESULT_FAILED:
        DEBUG ("MCP:%s -> failed to store %s %s.%s",
            pn, parameter ? "parameter" : "attribute", account, key);
        break;

      case MCP_ACCOUNT_STORAGE_SET_RESULT_UNCHANGED:
        DEBUG ("MCP:%s -> no change to %s %s.%s",
            pn, parameter ? "parameter" : "attribute", account, key);
        break;

      default:
        g_warn_if_reached ();
    }

  return updated;
}

/*
 * mcd_storage_set_string:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 * @key: the name of the attribute
 * @value: the value to be stored (or %NULL to erase it)
 *
 * Copies and stores the supplied @value (or removes it if %NULL) to the
 * internal cache.
 *
 * Returns: a #gboolean indicating whether the cache actually required an
 * update (so that the caller can decide whether to request a commit to
 * long term storage or not). %TRUE indicates the cache was updated and
 * may not be in sync with the store any longer, %FALSE indicates we already
 * held the value supplied.
 */
gboolean
mcd_storage_set_string (McdStorage *self,
    const gchar *account,
    const gchar *attribute,
    const gchar *val)
{
  gboolean updated;

  g_return_val_if_fail (MCD_IS_STORAGE (self), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (attribute != NULL, FALSE);
  g_return_val_if_fail (!g_str_has_prefix (attribute, "param-"), FALSE);

  if (val == NULL)
    {
      updated = mcd_storage_set_attribute (self, account, attribute, NULL);
    }
  else
    {
      GValue tmp = G_VALUE_INIT;

      g_value_init (&tmp, G_TYPE_STRING);
      g_value_set_string (&tmp, val);
      updated = mcd_storage_set_attribute (self, account, attribute, &tmp);
      g_value_unset (&tmp);
    }

  return updated;
}

/*
 * mcd_storage_set_attribute:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 * @attribute: the name of the attribute, e.g. "DisplayName"
 * @value: the value to be stored (or %NULL to erase it)
 *
 * Copies and stores the supplied @value (or removes it if %NULL) in the
 * internal cache.
 *
 * Returns: a #gboolean indicating whether the cache actually required an
 * update (so that the caller can decide whether to request a commit to
 * long term storage or not). %TRUE indicates the cache was updated and
 * may not be in sync with the store any longer, %FALSE indicates we already
 * held the value supplied.
 */
gboolean
mcd_storage_set_attribute (McdStorage *self,
    const gchar *account,
    const gchar *attribute,
    const GValue *value)
{
  GVariant *new_v;
  gboolean updated = FALSE;
  McpAccountStorage *plugin;

  g_return_val_if_fail (MCD_IS_STORAGE (self), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (attribute != NULL, FALSE);
  g_return_val_if_fail (!g_str_has_prefix (attribute, "param-"), FALSE);

  plugin = g_hash_table_lookup (self->accounts, account);
  g_return_val_if_fail (plugin != NULL, FALSE);

  if (value != NULL)
    new_v = g_variant_ref_sink (dbus_g_value_build_g_variant (value));
  else
    new_v = NULL;

  updated = update_storage (self, account, FALSE, attribute, new_v);

  tp_clear_pointer (&new_v, g_variant_unref);
  return updated;
}

/*
 * mcd_storage_set_parameter:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 * @parameter: the name of the parameter, e.g. "account"
 * @value: the value to be stored (or %NULL to erase it)
 *
 * Copies and stores the supplied @value (or removes it if %NULL) in the
 * internal cache.
 *
 * Returns: a #gboolean indicating whether the cache actually required an
 * update (so that the caller can decide whether to request a commit to
 * long term storage or not). %TRUE indicates the cache was updated and
 * may not be in sync with the store any longer, %FALSE indicates we already
 * held the value supplied.
 */
gboolean
mcd_storage_set_parameter (McdStorage *self,
    const gchar *account,
    const gchar *parameter,
    const GValue *value)
{
  GVariant *new_v = NULL;
  gboolean updated = FALSE;
  McpAccountStorage *plugin;

  g_return_val_if_fail (MCD_IS_STORAGE (self), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (parameter != NULL, FALSE);

  plugin = g_hash_table_lookup (self->accounts, account);
  g_return_val_if_fail (plugin != NULL, FALSE);

  if (value != NULL)
    {
      new_v = g_variant_ref_sink (dbus_g_value_build_g_variant (value));
    }

  updated = update_storage (self, account, TRUE, parameter, new_v);

  tp_clear_pointer (&new_v, g_variant_unref);
  return updated;
}

/*
 * @value: a populated #GValue of a supported #GType
 *
 * Escape the contents of @value to go in a #GKeyFile. Return the
 * value that would go in the keyfile.
 *
 * For instance, for a boolean value TRUE this would return "true",
 * and for a string containing one space, it would return "\s".
 */
gchar *
mcd_keyfile_escape_value (const GValue *value)
{
  GVariant *variant;
  gchar *ret;

  g_return_val_if_fail (G_IS_VALUE (value), NULL);

  variant = dbus_g_value_build_g_variant (value);

  if (variant == NULL)
    {
      g_warning ("Unable to convert %s to GVariant",
          G_VALUE_TYPE_NAME (value));
      return NULL;
    }

  g_variant_ref_sink (variant);
  ret = mcd_keyfile_escape_variant (variant);
  g_variant_unref (variant);
  return ret;
}

static gchar *
mcpa_escape_variant_for_keyfile (const McpAccountManager *unused G_GNUC_UNUSED,
    GVariant *variant)
{
  return mcd_keyfile_escape_variant (variant);
}

/*
 * mcd_keyfile_set_value:
 * @keyfile: a keyfile
 * @name: the name of a group
 * @key: the key in the group
 * @value: the value to be stored (or %NULL to erase it)
 *
 * Copies and stores the supplied @value (or removes it if %NULL) to the
 * internal cache.
 *
 * Returns: a #gboolean indicating whether the cache actually required an
 * update (so that the caller can decide whether to request a commit to
 * long term storage or not). %TRUE indicates the cache was updated and
 * may not be in sync with the store any longer, %FALSE indicates we already
 * held the value supplied.
 */
gboolean
mcd_keyfile_set_value (GKeyFile *keyfile,
    const gchar *name,
    const gchar *key,
    const GValue *value)
{
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  if (value == NULL)
    {
      return mcd_keyfile_set_variant (keyfile, name, key, NULL);
    }
  else
    {
      GVariant *variant;
      gboolean ret;

      variant = dbus_g_value_build_g_variant (value);

      if (variant == NULL)
        {
          g_warning ("Unable to convert %s to GVariant",
              G_VALUE_TYPE_NAME (value));
          return FALSE;
        }

      g_variant_ref_sink (variant);
      ret = mcd_keyfile_set_variant (keyfile, name, key, variant);
      g_variant_unref (variant);
      return ret;
    }
}

/*
 * mcd_keyfile_set_variant:
 * @keyfile: a keyfile
 * @name: the name of a group
 * @key: the key in the group
 * @value: the value to be stored (or %NULL to erase it)
 *
 * Escape @variant and store it in the keyfile.
 *
 * Returns: %TRUE if the keyfile actually changed,
 * so that the caller can decide whether to request a commit to
 * long term storage or not.
 */
gboolean
mcd_keyfile_set_variant (GKeyFile *keyfile,
    const gchar *name,
    const gchar *key,
    GVariant *value)
{
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  if (value == NULL)
    {
      gchar *old = g_key_file_get_value (keyfile, name, key, NULL);
      gboolean updated = (old != NULL);

      g_free (old);
      g_key_file_remove_key (keyfile, name, key, NULL);
      return updated;
    }
  else
    {
      gboolean updated = FALSE;
      gchar *old = g_key_file_get_value (keyfile, name, key, NULL);
      gchar *new = NULL;
      gchar *buf = NULL;

      switch (g_variant_classify (value))
        {
          case G_VARIANT_CLASS_STRING:
          case G_VARIANT_CLASS_OBJECT_PATH:
          case G_VARIANT_CLASS_SIGNATURE:
            g_key_file_set_string (keyfile, name, key,
                g_variant_get_string (value, NULL));
            break;

          case G_VARIANT_CLASS_UINT16:
            buf = g_strdup_printf ("%u", g_variant_get_uint16 (value));
            break;

          case G_VARIANT_CLASS_UINT32:
            buf = g_strdup_printf ("%u", g_variant_get_uint32 (value));
            break;

          case G_VARIANT_CLASS_INT16:
            buf = g_strdup_printf ("%d", g_variant_get_int16 (value));
            break;

          case G_VARIANT_CLASS_INT32:
            buf = g_strdup_printf ("%d", g_variant_get_int32 (value));
            break;

          case G_VARIANT_CLASS_BOOLEAN:
            g_key_file_set_boolean (keyfile, name, key,
                g_variant_get_boolean (value));
            break;

          case G_VARIANT_CLASS_BYTE:
            buf = g_strdup_printf ("%u", g_variant_get_byte (value));
            break;

          case G_VARIANT_CLASS_UINT64:
            buf = g_strdup_printf ("%" G_GUINT64_FORMAT,
                                   g_variant_get_uint64 (value));
            break;

          case G_VARIANT_CLASS_INT64:
            buf = g_strdup_printf ("%" G_GINT64_FORMAT,
                                   g_variant_get_int64 (value));
            break;

          case G_VARIANT_CLASS_DOUBLE:
            g_key_file_set_double (keyfile, name, key,
                g_variant_get_double (value));
            break;

          case G_VARIANT_CLASS_ARRAY:
            if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING_ARRAY))
              {
                gsize len;
                const gchar **strings = g_variant_get_strv (value, &len);

                g_key_file_set_string_list (keyfile, name, key, strings, len);
              }
            else if (g_variant_is_of_type (value,
                  G_VARIANT_TYPE_OBJECT_PATH_ARRAY))
              {
                gsize len;
                const gchar **strings = g_variant_get_objv (value, &len);

                g_key_file_set_string_list (keyfile, name, key, strings, len);
              }
            else
              {
                g_warning ("Unexpected array type %s",
                    g_variant_get_type_string (value));
                return FALSE;
              }
            break;

          case G_VARIANT_CLASS_TUPLE:
            if (g_variant_is_of_type (value, G_VARIANT_TYPE ("(uss)")))
              {
                guint32 type;
                /* enough for "4294967296" + \0 */
                gchar printf_buf[11];
                const gchar * strv[4] = { NULL, NULL, NULL, NULL };

                g_variant_get (value, "(u&s&s)",
                    &type,
                    &(strv[1]),
                    &(strv[2]));
                g_snprintf (printf_buf, sizeof (printf_buf), "%u", type);
                strv[0] = printf_buf;

                g_key_file_set_string_list (keyfile, name, key, strv, 3);
              }
            else
              {
                g_warning ("Unexpected struct type %s",
                    g_variant_get_type_string (value));
                return FALSE;
              }
            break;

          default:
              {
                g_warning ("Unexpected variant type %s",
                    g_variant_get_type_string (value));
                return FALSE;
              }
        }

      if (buf != NULL)
        g_key_file_set_string (keyfile, name, key, buf);

      new = g_key_file_get_value (keyfile, name, key, NULL);

      if (tp_strdiff (old, new))
        updated = TRUE;

      g_free (new);
      g_free (buf);
      g_free (old);

      return updated;
    }
}

/*
 * mcd_storage_create_account:
 * @storage: An object implementing the #McdStorage interface
 * @provider: the desired storage provider, or %NULL
 * @manager: the name of the manager
 * @protocol: the name of the protocol
 * @identification: the result of IdentifyAccount
 * @plugin_out: (out) (transfer full): the plugin we used
 * @error: a #GError to fill when returning %NULL
 *
 * Create a new account in storage. This should not store any
 * information on the long term storage until mcd_storage_commit() is called.
 *
 * See mcp_account_storage_create().
 *
 * Returns: the unique name to use for the new account, or %NULL on error.
 */
gchar *
mcd_storage_create_account (McdStorage *self,
    const gchar *provider,
    const gchar *manager,
    const gchar *protocol,
    const gchar *identification,
    McpAccountStorage **plugin_out,
    GError **error)
{
  GList *store;
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  gchar *ret;

  if (plugin_out != NULL)
    *plugin_out = NULL;

  g_return_val_if_fail (MCD_IS_STORAGE (self), NULL);
  g_return_val_if_fail (!tp_str_empty (manager), NULL);
  g_return_val_if_fail (!tp_str_empty (protocol), NULL);

  /* If a storage provider is specified, use only it or fail */
  if (provider != NULL)
    {
      for (store = stores; store != NULL; store = g_list_next (store))
        {
          McpAccountStorage *plugin = store->data;

          if (!tp_strdiff (mcp_account_storage_provider (plugin), provider))
            {
              ret = mcp_account_storage_create (plugin, ma, manager,
                  protocol, identification, error);
              if (mcd_storage_add_account_from_plugin (self, plugin, ret,
                    error))
                {
                  if (plugin_out != NULL)
                    *plugin_out = g_object_ref (plugin);

                  return ret;
                }
              else
                {
                  g_free (ret);
                  return NULL;
                }
            }
        }

      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Storage provider '%s' does not exist", provider);

      return NULL;
    }

  /* No provider specified, let's pick the first plugin able to create this
   * account in priority order.
   */
  for (store = stores; store != NULL; store = g_list_next (store))
    {
      McpAccountStorage *plugin = store->data;

      ret = mcp_account_storage_create (plugin, ma, manager, protocol,
          identification, error);

      if (ret != NULL)
        {
          if (mcd_storage_add_account_from_plugin (self, plugin, ret,
                error))
            {
              if (plugin_out != NULL)
                *plugin_out = g_object_ref (plugin);

              return ret;
            }
          else
            {
              g_free (ret);
              return NULL;
            }
        }

      g_clear_error (error);
    }

  /* This should never happen since the default storage is always able to create
   * an account */
  g_warn_if_reached ();
  g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
      "None of the storage provider are able to create the account");

  return NULL;
}

static void
delete_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  GError *error = NULL;
  const gchar *account_name = user_data;

  if (mcp_account_storage_delete_finish (MCP_ACCOUNT_STORAGE (source),
        res, &error))
    {
      DEBUG ("deleted account %s", account_name);
    }
  else
    {
      DEBUG ("could not delete account %s (but no way to signal that): "
          "%s #%d: %s", account_name,
          g_quark_to_string (error->domain), error->code, error->message);
      g_error_free (error);
    }

  g_free (user_data);
}

/*
 * mcd_storage_delete_account:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 *
 * Removes an account's settings from long term storage.
 * This does not handle any of the other logic to do with removing
 * accounts, it merely ensures that no trace of the account remains
 * in long term storage once mcd_storage_commit() has been called.
 */
void
mcd_storage_delete_account (McdStorage *self,
    const gchar *account)
{
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  McpAccountStorage *plugin;

  g_return_if_fail (MCD_IS_STORAGE (self));
  g_return_if_fail (account != NULL);

  plugin = g_hash_table_lookup (self->accounts, account);
  g_return_if_fail (plugin != NULL);

  /* FIXME: stop ignoring the error (if any), and make this method async
   * in order to pass the error up to McdAccount */
  mcp_account_storage_delete_async (plugin, ma, account, NULL,
      delete_cb, g_strdup (account));
}

/*
 * mcd_storage_commit:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 *
 * Sync the long term storage (whatever it might be) with the current
 * state of our internal cache.
 */
void
mcd_storage_commit (McdStorage *self, const gchar *account)
{
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);
  McpAccountStorage *plugin;
  const gchar *pname;

  g_return_if_fail (MCD_IS_STORAGE (self));
  g_return_if_fail (account != NULL);

  plugin = g_hash_table_lookup (self->accounts, account);
  g_return_if_fail (plugin != NULL);

  pname = mcp_account_storage_name (plugin);

  /* FIXME: fd.o #29563: this should be async, really */
  DEBUG ("flushing plugin %s %s to long term storage", pname, account);
  mcp_account_storage_commit (plugin, ma, account);
}

/*
 * mcd_storage_set_strv:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 * @attribute: the name of the attribute
 * @strv: the string vector to be stored (where %NULL is treated as equivalent
 * to an empty vector)
 *
 * Copies and stores the supplied string vector to the internal cache.
 *
 * Returns: a #gboolean indicating whether the cache actually required an
 * update (so that the caller can decide whether to request a commit to
 * long term storage or not). %TRUE indicates the cache was updated and
 * may not be in sync with the store any longer, %FALSE indicates we already
 * held the value supplied.
 */
gboolean
mcd_storage_set_strv (McdStorage *storage,
    const gchar *account,
    const gchar *attribute,
    const gchar * const *strv)
{
  GValue v = G_VALUE_INIT;
  static const gchar * const *empty = { NULL };
  gboolean ret;

  g_return_val_if_fail (MCD_IS_STORAGE (storage), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (attribute != NULL, FALSE);
  g_return_val_if_fail (!g_str_has_prefix (attribute, "param-"), FALSE);

  g_value_init (&v, G_TYPE_STRV);
  g_value_set_static_boxed (&v, strv == NULL ? empty : strv);
  ret = mcd_storage_set_attribute (storage, account, attribute, &v);
  g_value_unset (&v);
  return ret;
}

void
mcd_storage_ready (McdStorage *self)
{
  GList *store;
  McpAccountManager *ma = MCP_ACCOUNT_MANAGER (self);

  for (store = stores; store != NULL; store = g_list_next (store))
    {
      McpAccountStorage *plugin = store->data;
      const gchar *plugin_name = mcp_account_storage_name (plugin);

      DEBUG ("Unblocking async account ops by %s", plugin_name);
      mcp_account_storage_ready (plugin, ma);
    }
}

static GVariant *
mcpa_unescape_variant_from_keyfile (const McpAccountManager *mcpa,
    const gchar *escaped,
    const GVariantType *type,
    GError **error)
{
  GKeyFile *keyfile;
  GVariant *ret;

  g_return_val_if_fail (escaped != NULL, NULL);
  g_return_val_if_fail (type != NULL, NULL);

  keyfile = g_key_file_new ();
  g_key_file_set_value (keyfile, "g", "k", escaped);
  ret = mcd_keyfile_get_variant (keyfile, "g", "k", type, error);
  g_key_file_free (keyfile);

  if (ret != NULL)
    g_variant_ref_sink (ret);

  return ret;
}

static void
plugin_iface_init (McpAccountManagerIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  DEBUG ();

  iface->unique_name = unique_name;
  iface->identify_account_async = identify_account_async;
  iface->identify_account_finish = identify_account_finish;
  iface->escape_variant_for_keyfile = mcpa_escape_variant_for_keyfile;
  iface->unescape_variant_from_keyfile = mcpa_unescape_variant_from_keyfile;
}

gboolean
mcd_storage_add_account_from_plugin (McdStorage *self,
    McpAccountStorage *plugin,
    const gchar *account,
    GError **error)
{
  McpAccountStorage *other = g_hash_table_lookup (self->accounts, account);
  McpAccountManager *api = (McpAccountManager *) self;
  gchar **typed_parameters;
  gchar **untyped_parameters;

  if (other != NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "account %s already exists in plugin '%s', cannot create "
          "for plugin '%s'",
          account,
          mcp_account_storage_name (other),
          mcp_account_storage_name (plugin));
      return FALSE;
    }

  g_hash_table_insert (self->accounts, g_strdup (account),
      g_object_ref (plugin));

  typed_parameters = mcp_account_storage_list_typed_parameters (plugin, api,
      account);
  untyped_parameters = mcp_account_storage_list_untyped_parameters (plugin,
      api, account);

  DEBUG ("Account parameters for %s", account);

  if (typed_parameters != NULL)
    {
      gsize i;

      for (i = 0; typed_parameters[i] != NULL; i++)
        {
          GVariant *v = mcp_account_storage_get_parameter (plugin, api, account,
              typed_parameters[i], NULL, NULL);

          if (v == NULL)
            {
              CRITICAL ("%s: could not be retrieved", typed_parameters[i]);
            }
          else
            {
              DEBUG ("%s: type '%s'", typed_parameters[i],
                  g_variant_get_type_string (v));
              g_variant_unref (v);
            }
        }
    }

  if (untyped_parameters != NULL)
    {
      gsize i;

      for (i = 0; untyped_parameters[i] != NULL; i++)
        {
          DEBUG ("%s: type not stored", untyped_parameters[i]);
        }
    }

  DEBUG ("End of parameters");

  g_strfreev (typed_parameters);
  g_strfreev (untyped_parameters);

  return TRUE;
}

GHashTable *
mcd_storage_dup_typed_parameters (McdStorage *self,
    const gchar *account_name)
{
  McpAccountStorage *plugin;
  McpAccountManager *api = (McpAccountManager *) self;
  gsize i;
  gchar **typed_parameters;
  GHashTable *params;

  g_return_val_if_fail (MCD_IS_STORAGE (self), NULL);

  plugin = g_hash_table_lookup (self->accounts, account_name);
  g_return_val_if_fail (plugin != NULL, NULL);

  typed_parameters = mcp_account_storage_list_typed_parameters (plugin, api,
      account_name);

  params = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) tp_g_value_slice_free);

  for (i = 0;
       typed_parameters != NULL && typed_parameters[i] != NULL;
       i++)
    {
      GVariant *v = mcp_account_storage_get_parameter (plugin, api,
          account_name, typed_parameters[i], NULL, NULL);
      GValue *value;

      if (v == NULL)
        {
          CRITICAL ("%s was in list_typed_parameters() but could not be "
              "retrieved", typed_parameters[i]);
          continue;
        }

      value = g_slice_new0 (GValue);
      dbus_g_value_parse_g_variant (v, value);

      if (!G_IS_VALUE (value))
        {
          CRITICAL ("could not turn %s into a GValue", typed_parameters[i]);
          g_slice_free (GValue, value);
          continue;
        }

      g_hash_table_insert (params, g_strdup (typed_parameters[i]),
          value);
      g_variant_unref (v);
    }

  return params;
}

/* See whether we can migrate the parameters from being stored without
 * their types, to being stored with their types.
 * Commit changes and return TRUE if anything happened. */
gboolean
mcd_storage_maybe_migrate_parameters (McdStorage *self,
    const gchar *account_name,
    TpProtocol *protocol)
{
  McpAccountManager *api = MCP_ACCOUNT_MANAGER (self);
  McpAccountStorage *plugin;
  gchar **untyped_parameters = NULL;
  gsize i;
  gboolean ret = FALSE;

  plugin = g_hash_table_lookup (self->accounts, account_name);
  g_return_val_if_fail (plugin != NULL, FALSE);

  /* If the storage backend can't store typed parameters, there's no point. */
  if (!mcp_account_storage_has_any_flag (plugin, account_name,
        MCP_ACCOUNT_STORAGE_FLAG_STORES_TYPES))
    goto finally;

  untyped_parameters = mcp_account_storage_list_untyped_parameters (
      plugin, api, account_name);

  /* If there's nothing to migrate, there's also no point. */
  if (untyped_parameters == NULL || untyped_parameters[0] == NULL)
    goto finally;

  DEBUG ("trying to migrate %s", account_name);

  for (i = 0; untyped_parameters[i] != NULL; i++)
    {
      const gchar *param_name = untyped_parameters[i];
      const TpConnectionManagerParam *param = tp_protocol_get_param (protocol,
          param_name);
      GError *error = NULL;
      GVariantType *type = NULL;
      GVariant *value;
      McpAccountStorageSetResult res;

      if (param == NULL)
        {
          DEBUG ("cannot migrate parameter '%s': not supported by %s/%s",
              param_name, tp_protocol_get_cm_name (protocol),
              tp_protocol_get_name (protocol));
          goto next_param;
        }

      type = tp_connection_manager_param_dup_variant_type (param);

      DEBUG ("Migrating parameter '%s' of type '%.*s'",
          param_name,
          (gint) g_variant_type_get_string_length (type),
          g_variant_type_peek_string (type));

      value = mcp_account_storage_get_parameter (plugin, api,
          account_name, param_name, type, NULL);

      if (value == NULL)
        {
          DEBUG ("cannot migrate parameter '%s': %s #%d: %s",
              param_name, g_quark_to_string (error->domain), error->code,
              error->message);
          g_error_free (error);
          goto next_param;
        }

      if (!g_variant_is_of_type (value, type))
        {
          DEBUG ("trying to convert parameter from type '%s'",
              g_variant_get_type_string (value));

          /* consumes parameter */
          value = tp_variant_convert (value, type);

          if (value == NULL)
            {
              DEBUG ("could not convert parameter to desired type");
              goto next_param;
            }
        }

      res = mcp_account_storage_set_parameter (plugin, api,
          account_name, param_name, value, MCP_PARAMETER_FLAG_NONE);

      switch (res)
        {
          case MCP_ACCOUNT_STORAGE_SET_RESULT_UNCHANGED:
            /* it really ought to be CHANGED, surely? */
            DEBUG ("Tried to upgrade parameter %s but the "
                "storage backend claims not to have changed it? "
                "Not sure I really believe that", param_name);
            /* fall through to the CHANGED case */

          case MCP_ACCOUNT_STORAGE_SET_RESULT_CHANGED:
            ret = TRUE;
            break;

          case MCP_ACCOUNT_STORAGE_SET_RESULT_FAILED:
            WARNING ("Failed to set parameter %s", param_name);
            break;

          default:
            WARNING ("set_parameter returned invalid result code %d "
                "for parameter %s", res, param_name);
        }

next_param:
      if (type != NULL)
        g_variant_type_free (type);
    }

  if (ret)
    mcp_account_storage_commit (plugin, api, account_name);

finally:
  g_strfreev (untyped_parameters);
  return ret;
}
