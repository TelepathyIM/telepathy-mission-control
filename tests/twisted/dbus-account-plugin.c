/*
 * A demonstration plugin that diverts account storage to D-Bus, where the
 * regression tests can manipulate it.
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010-2012 Collabora Ltd.
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
#include "dbus-account-plugin.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#define DEBUG(format, ...) g_debug ("%s: " format, G_STRFUNC, ##__VA_ARGS__)
#define CRITICAL(format, ...) \
  g_critical ("%s: " format, G_STRFUNC, ##__VA_ARGS__)

#define TESTDOT "org.freedesktop.Telepathy.Test."
#define TESTSLASH "/org/freedesktop/Telepathy/Test/"

#define TEST_DBUS_ACCOUNT_SERVICE       TESTDOT   "DBusAccountService"
#define TEST_DBUS_ACCOUNT_SERVICE_PATH  TESTSLASH "DBusAccountService"
#define TEST_DBUS_ACCOUNT_SERVICE_IFACE TEST_DBUS_ACCOUNT_SERVICE

#define TEST_DBUS_ACCOUNT_PLUGIN_PATH   TESTSLASH "DBusAccountPlugin"
#define TEST_DBUS_ACCOUNT_PLUGIN_IFACE  TESTDOT   "DBusAccountPlugin"

typedef struct {
    gchar *path;
    /* string => GVariant */
    GHashTable *attributes;
    /* string => GUINT_TO_POINTER(guint32) */
    GHashTable *attribute_flags;
    /* set of strings */
    GHashTable *uncommitted_attributes;
    /* string => GVariant */
    GHashTable *parameters;
    /* string => string */
    GHashTable *untyped_parameters;
    /* string => GUINT_TO_POINTER(guint32) */
    GHashTable *parameter_flags;
    /* set of strings */
    GHashTable *uncommitted_parameters;
    enum { UNCOMMITTED_CREATION = 1 } flags;
    TpStorageRestrictionFlags restrictions;
} Account;

static void
test_dbus_account_free (gpointer p)
{
  Account *account = p;

  g_hash_table_unref (account->attributes);
  g_hash_table_unref (account->attribute_flags);
  g_hash_table_unref (account->parameters);
  g_hash_table_unref (account->untyped_parameters);
  g_hash_table_unref (account->parameter_flags);

  g_hash_table_unref (account->uncommitted_attributes);
  g_hash_table_unref (account->uncommitted_parameters);

  g_slice_free (Account, account);
}

static void account_storage_iface_init (McpAccountStorageIface *);

struct _TestDBusAccountPlugin {
  GObject parent;

  GDBusConnection *bus;
  GHashTable *accounts;
  McpAccountManager *feedback;
  GQueue events;
  gboolean active;
};

struct _TestDBusAccountPluginClass {
  GObjectClass parent_class;
};

G_DEFINE_TYPE_WITH_CODE (TestDBusAccountPlugin, test_dbus_account_plugin,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_STORAGE,
        account_storage_iface_init));

typedef struct {
    TestDBusAccountPlugin *self;
    gchar *account_name;
} AsyncData;

static AsyncData *
async_data_new (TestDBusAccountPlugin *self,
    const gchar *account_name)
{
  AsyncData *ret = g_slice_new0 (AsyncData);

  ret->self = g_object_ref (self);
  ret->account_name = g_strdup (account_name);
  return ret;
}

static void
async_data_free (AsyncData *ad)
{
  g_clear_object (&ad->self);
  g_free (ad->account_name);
  g_slice_free (AsyncData, ad);
}

typedef enum {
    EVENT_PARAMS,
    EVENT_ATTRS,
    EVENT_CREATION,
    EVENT_DELETION
} EventType;

typedef struct {
    EventType type;
    GVariant *args;
} Event;

static Event *
event_new (EventType type,
    GVariant *args)
{
  Event *e = g_slice_new0 (Event);

  e->type = type;
  e->args = g_variant_ref_sink (args);
  return e;
}

static Account *
lookup_account (TestDBusAccountPlugin *self,
    const gchar *account_name)
{
  return g_hash_table_lookup (self->accounts, account_name);
}

static Account *
ensure_account (TestDBusAccountPlugin *self,
    const gchar *account_name)
{
  Account *account = lookup_account (self, account_name);

  if (account == NULL)
    {
      account = g_slice_new0 (Account);
      account->path = g_strdup_printf ("%s%s", TP_ACCOUNT_OBJECT_PATH_BASE,
          account_name);

      account->attributes = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, (GDestroyNotify) g_variant_unref);
      account->attribute_flags = g_hash_table_new_full (g_str_hash,
          g_str_equal, g_free, NULL);
      account->parameters = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, (GDestroyNotify) g_variant_unref);
      account->untyped_parameters = g_hash_table_new_full (g_str_hash,
          g_str_equal, g_free, g_free);
      account->parameter_flags = g_hash_table_new_full (g_str_hash,
          g_str_equal, g_free, NULL);

      account->uncommitted_attributes = g_hash_table_new_full (g_str_hash,
          g_str_equal, g_free, NULL);
      account->uncommitted_parameters = g_hash_table_new_full (g_str_hash,
          g_str_equal, g_free, NULL);

      account->flags = UNCOMMITTED_CREATION;

      g_hash_table_insert (self->accounts, g_strdup (account_name), account);
    }

  return account;
}

static void
service_appeared_cb (GDBusConnection *bus,
    const gchar *name,
    const gchar *owner,
    gpointer user_data)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (user_data);

  self->active = TRUE;

  /* FIXME: for now, we assume there are no accounts. */

  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "Active", NULL, NULL);
}

static void
service_vanished_cb (GDBusConnection *bus,
    const gchar *name,
    gpointer user_data)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (user_data);
  GHashTableIter iter;
  gpointer k;

  self->active = FALSE;
  g_hash_table_iter_init (&iter, self->accounts);

  while (g_hash_table_iter_next (&iter, &k, NULL))
    {
      mcp_account_storage_emit_deleted (MCP_ACCOUNT_STORAGE (self), k);
      g_hash_table_iter_remove (&iter);
    }

  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "Inactive", NULL, NULL);
}

static void
test_dbus_account_plugin_init (TestDBusAccountPlugin *self)
{
  GError *error = NULL;

  DEBUG ("called");

  self->accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, test_dbus_account_free);

  self->bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert (self->bus != NULL);

  g_queue_init (&self->events);

  g_bus_watch_name (G_BUS_TYPE_SESSION, TEST_DBUS_ACCOUNT_SERVICE,
      G_BUS_NAME_WATCHER_FLAGS_NONE,
      service_appeared_cb,
      service_vanished_cb,
      g_object_ref (self),
      g_object_unref);
}

static void
test_dbus_account_plugin_class_init (TestDBusAccountPluginClass *cls)
{
  DEBUG ("called");
}

static Account *
test_dbus_account_plugin_add_account (TestDBusAccountPlugin *self,
    const gchar *account_name,
    GVariant *attributes,
    GVariant *attribute_flags,
    GVariant *parameters,
    GVariant *untyped_parameters,
    GVariant *param_flags,
    TpStorageRestrictionFlags restrictions)
{
  GVariantIter iter;
  const gchar *k;
  GVariant *v;
  const gchar *s;
  guint32 u;
  Account *account = ensure_account (self, account_name);

  g_variant_iter_init (&iter, attributes);

  while (g_variant_iter_loop (&iter, "{sv}", &k, &v))
    g_hash_table_insert (account->attributes, g_strdup (k),
        g_variant_ref (v));

  g_variant_iter_init (&iter, attribute_flags);

  while (g_variant_iter_loop (&iter, "{su}", &k, &u))
    g_hash_table_insert (account->attribute_flags, g_strdup (k),
        GUINT_TO_POINTER (u));

  g_variant_iter_init (&iter, parameters);

  while (g_variant_iter_loop (&iter, "{sv}", &k, &v))
    g_hash_table_insert (account->parameters, g_strdup (k),
        g_variant_ref (v));

  g_variant_iter_init (&iter, untyped_parameters);

  while (g_variant_iter_loop (&iter, "{ss}", &k, &s))
    g_hash_table_insert (account->untyped_parameters,
        g_strdup (k), g_strdup (s));

  g_variant_iter_init (&iter, param_flags);

  while (g_variant_iter_loop (&iter, "{su}", &k, &u))
    g_hash_table_insert (account->parameter_flags, g_strdup (k),
        GUINT_TO_POINTER (u));

  account->restrictions = restrictions;

  return account;
}

static void
test_dbus_account_plugin_process_account_creation (TestDBusAccountPlugin *self,
    GVariant *args)
{
  const gchar *account_name;
  Account *account;
  GVariant *attrs;
  GVariant *params;
  GVariant *untyped_params;
  GVariant *attr_flags;
  GVariant *param_flags;
  guint32 restrictions;

  g_variant_get (args, "(&s@a{sv}@a{su}@a{sv}@a{ss}@a{su}u)",
      &account_name, &attrs, &attr_flags,
      &params, &untyped_params, &param_flags,
      &restrictions);
  DEBUG ("%s", account_name);
  account = lookup_account (self, account_name);

  if (account != NULL)
    {
      /* we already knew about it; assume nothing changed? */
    }
  else
    {
      /* FIXME: this silently drops any uncommitted changes,
       * if we're racing with the service, is that right? */
      /* we don't have to emit altered-one so we can skip
       * a lot of rubbish */
      account = test_dbus_account_plugin_add_account (self,
          account_name, attrs, attr_flags,
          params, untyped_params, param_flags, restrictions);

      mcp_account_storage_emit_created (
          MCP_ACCOUNT_STORAGE (self), account_name);

      g_dbus_connection_emit_signal (self->bus, NULL,
          TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
          "AccountCreated", g_variant_new_parsed ("(%o,)", account->path),
          NULL);
    }

  g_variant_unref (attrs);
  g_variant_unref (attr_flags);
  g_variant_unref (params);
  g_variant_unref (untyped_params);
  g_variant_unref (param_flags);
}

static void
test_dbus_account_plugin_process_account_deletion (TestDBusAccountPlugin *self,
    GVariant *args)
{
  const gchar *account_name;
  Account *account;

  g_variant_get (args, "(&s)", &account_name);
  DEBUG ("%s", account_name);
  account = lookup_account (self, account_name);

  if (account == NULL)
    {
      CRITICAL ("accounts service deleted %s but we don't "
          "have any record of that account", account_name);
    }
  else
    {
      gchar *path = g_strdup (account->path);

      /* FIXME: this silently drops any uncommitted changes,
       * is that right? */
      g_hash_table_remove (self->accounts, account_name);
      mcp_account_storage_emit_deleted (
          MCP_ACCOUNT_STORAGE (self), account_name);

      g_dbus_connection_emit_signal (self->bus, NULL,
          TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
          "AccountDeleted", g_variant_new_parsed ("(%o,)", path),
          NULL);
      g_free (path);
    }
}

static void
test_dbus_account_plugin_process_attributes (TestDBusAccountPlugin *self,
    GVariant *args)
{
  const gchar *account_name;
  Account *account;
  GVariant *attrs;
  GVariant *attr_flags;
  GVariant *deleted;

  g_variant_get (args, "(&s@a{sv}@a{su}@as)",
      &account_name, &attrs, &attr_flags, &deleted);
  DEBUG ("%s", account_name);
  account = lookup_account (self, account_name);

  if (account == NULL)
    {
      CRITICAL ("accounts service altered %s but we don't "
          "have any record of that account", account_name);
    }
  else
    {
      GVariantIter iter;
      const gchar *attr;
      GVariant *value;
      gboolean enabled;

      g_variant_iter_init (&iter, attrs);

      while (g_variant_iter_loop (&iter, "{sv}", &attr, &value))
        {
          GVariant *stored = g_hash_table_lookup (
              account->attributes, attr);

          if (tp_strdiff (attr, "Enabled") &&
              !g_hash_table_contains (
                account->uncommitted_attributes, attr) &&
              (stored == NULL ||
                !g_variant_equal (value, stored)))
            {
              gchar *repr = g_variant_print (value, TRUE);
              guint32 flags;

              DEBUG ("%s changed to %s, signalling MC", attr, repr);
              g_free (repr);

              if (!g_variant_lookup (attr_flags, attr, "u", &flags))
                flags = 0;

              g_hash_table_insert (account->attributes,
                  g_strdup (attr), g_variant_ref (value));
              mcp_account_storage_emit_altered_one (
                  MCP_ACCOUNT_STORAGE (self), account_name, attr);

              g_dbus_connection_emit_signal (self->bus, NULL,
                  TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                  "AttributeChanged",
                  g_variant_new_parsed ("(%o, %s)", account->path, attr),
                  NULL);
            }
        }

      g_variant_iter_init (&iter, deleted);

      while (g_variant_iter_loop (&iter, "s", &attr))
        {
          if (!g_hash_table_contains (
                account->uncommitted_attributes, attr) &&
              g_hash_table_contains (account->attributes,
                  attr))
            {
              DEBUG ("%s deleted", attr);

              g_hash_table_remove (account->attributes, attr);
              mcp_account_storage_emit_altered_one (
                  MCP_ACCOUNT_STORAGE (self), account_name, attr);

              g_dbus_connection_emit_signal (self->bus, NULL,
                  TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                  "AttributeDeleted",
                  g_variant_new_parsed ("(%o, %s)", account->path, attr),
                  NULL);
            }
        }

      /* Deal with Enabled separately: we don't have to call set_value()
       * for this one */
      if (g_variant_lookup (attrs, "Enabled", "b", &enabled))
        {
          DEBUG ("Enabled changed to %s", enabled ? "true" : "false");

          mcp_account_storage_emit_toggled (
              MCP_ACCOUNT_STORAGE (self), account_name, enabled);

          g_dbus_connection_emit_signal (self->bus, NULL,
              TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
              "Toggled",
              g_variant_new_parsed ("(%o, %b)", account->path, enabled), NULL);
        }
    }

    g_variant_unref (attrs);
    g_variant_unref (attr_flags);
    g_variant_unref (deleted);
}

static void
test_dbus_account_plugin_process_parameters (TestDBusAccountPlugin *self,
    GVariant *args)
{
  const gchar *account_name;
  Account *account;
  GVariant *params;
  GVariant *untyped_params;
  GVariant *param_flags;
  GVariant *deleted;

  g_variant_get (args, "(&s@a{sv}@a{ss}@a{su}@as)",
      &account_name, &params, &untyped_params, &param_flags, &deleted);
  DEBUG ("%s", account_name);
  account = lookup_account (self, account_name);

  if (account == NULL)
    {
      CRITICAL ("accounts service altered %s but we don't "
          "have any record of that account", account_name);
    }
  else
    {
      GVariantIter iter;
      const gchar *param;
      GVariant *value;
      gchar *key;
      gchar *escaped;

      g_variant_iter_init (&iter, params);

      while (g_variant_iter_loop (&iter, "{sv}", &param, &value))
        {
          GVariant *stored = g_hash_table_lookup (
              account->parameters, param);

          if (!g_hash_table_contains (
                account->uncommitted_parameters,
                param) &&
              (stored == NULL ||
               !g_variant_equal (value, stored)))
            {
              guint32 flags;

              if (!g_variant_lookup (param_flags, param, "u", &flags))
                flags = 0;

              g_hash_table_insert (account->parameters,
                  g_strdup (param), g_variant_ref (value));
              key = g_strdup_printf ("param-%s", param);
              mcp_account_storage_emit_altered_one (
                  MCP_ACCOUNT_STORAGE (self), account_name, key);
              g_free (key);

              g_dbus_connection_emit_signal (self->bus, NULL,
                  TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                  TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                  "ParameterChanged",
                  g_variant_new_parsed ("(%o, %s)", account->path, param),
                  NULL);
            }
        }

      g_variant_iter_init (&iter, untyped_params);

      while (g_variant_iter_loop (&iter, "{ss}", &param,
            &escaped))
        {
          GVariant *stored = g_hash_table_lookup (
              account->untyped_parameters, param);

          if (!g_hash_table_contains (
                account->uncommitted_parameters,
                param) &&
              (stored == NULL ||
               !g_variant_equal (value, stored)))
            {
              g_hash_table_remove (account->parameters, param);
              g_hash_table_insert (account->untyped_parameters,
                  g_strdup (param), g_strdup (escaped));
              key = g_strdup_printf ("param-%s", param);
              mcp_account_storage_emit_altered_one (
                  MCP_ACCOUNT_STORAGE (self), account_name, key);
              g_free (key);

              g_dbus_connection_emit_signal (self->bus, NULL,
                  TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                  TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                  "ParameterChanged",
                  g_variant_new_parsed ("(%o, %s)", account->path, param),
                  NULL);
            }
        }

      g_variant_iter_init (&iter, deleted);

      while (g_variant_iter_loop (&iter, "s", &param))
        {
          if (!g_hash_table_contains (
                account->uncommitted_parameters, param) &&
              (g_hash_table_contains (account->parameters,
                  param) ||
                g_hash_table_contains (
                  account->untyped_parameters,
                  param)))
            {
              g_hash_table_remove (account->parameters, param);
              g_hash_table_remove (account->untyped_parameters,
                  param);
              key = g_strdup_printf ("param-%s", param);
              mcp_account_storage_emit_altered_one (
                  MCP_ACCOUNT_STORAGE (self), account_name, key);
              g_free (key);

              g_dbus_connection_emit_signal (self->bus, NULL,
                  TEST_DBUS_ACCOUNT_PLUGIN_PATH,
                  TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
                  "ParameterDeleted",
                  g_variant_new_parsed ("(%o, %s)", account->path, param),
                  NULL);
            }
        }
    }

  g_variant_unref (params);
  g_variant_unref (untyped_params);
  g_variant_unref (param_flags);
  g_variant_unref (deleted);
}

static void
test_dbus_account_plugin_process_events (TestDBusAccountPlugin *self)
{
  Event *event;

  if (self->feedback == NULL)
    return;

  while ((event = g_queue_pop_head (&self->events)) != NULL)
    {
      switch (event->type)
        {
          case EVENT_CREATION:
            test_dbus_account_plugin_process_account_creation (self,
                event->args);
            break;

          case EVENT_DELETION:
            test_dbus_account_plugin_process_account_deletion (self,
                event->args);
            break;

          case EVENT_ATTRS:
            test_dbus_account_plugin_process_attributes (self,
                event->args);
            break;

          case EVENT_PARAMS:
            test_dbus_account_plugin_process_parameters (self,
                event->args);
            break;
        }

      g_variant_unref (event->args);
      g_slice_free (Event, event);
    }
}

static void
account_created_cb (GDBusConnection *bus,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *iface_name,
    const gchar *signal_name,
    GVariant *tuple,
    gpointer user_data)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (user_data);
  const gchar *account_name;

  g_variant_get (tuple, "(&s@a{sv}@a{su}@a{sv}@a{ss}@a{su}u)",
      &account_name, NULL, NULL, NULL, NULL, NULL, NULL);
  DEBUG ("%s", account_name);

  g_queue_push_tail (&self->events, event_new (EVENT_CREATION, tuple));
  test_dbus_account_plugin_process_events (self);
}

static void
account_deleted_cb (GDBusConnection *bus,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *iface_name,
    const gchar *signal_name,
    GVariant *tuple,
    gpointer user_data)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (user_data);
  const gchar *account_name;

  g_variant_get (tuple, "(&s)", &account_name);
  DEBUG ("%s", account_name);

  g_queue_push_tail (&self->events, event_new (EVENT_DELETION, tuple));
  test_dbus_account_plugin_process_events (self);
}

static void
attributes_changed_cb (GDBusConnection *bus,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *iface_name,
    const gchar *signal_name,
    GVariant *tuple,
    gpointer user_data)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (user_data);
  const gchar *account_name;

  g_variant_get (tuple, "(&s@a{sv}@a{su}@as)", &account_name,
      NULL, NULL, NULL);
  DEBUG ("%s", account_name);

  g_queue_push_tail (&self->events, event_new (EVENT_ATTRS, tuple));
  test_dbus_account_plugin_process_events (self);
}

static void
parameters_changed_cb (GDBusConnection *bus,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *iface_name,
    const gchar *signal_name,
    GVariant *tuple,
    gpointer user_data)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (user_data);
  const gchar *account_name;

  g_variant_get (tuple, "(&s@a{sv}@a{ss}@a{su}@as)", &account_name,
      NULL, NULL, NULL, NULL);
  DEBUG ("%s", account_name);

  g_queue_push_tail (&self->events, event_new (EVENT_PARAMS, tuple));
  test_dbus_account_plugin_process_events (self);
}

static GList *
test_dbus_account_plugin_list (McpAccountStorage *storage,
    McpAccountManager *am)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  GError *error = NULL;
  GVariant *tuple, *accounts;
  GVariant *attributes, *attribute_flags;
  GVariant *parameters, *untyped_parameters, *param_flags;
  GVariantIter account_iter;
  const gchar *account_name;
  GList *ret = NULL;
  guint32 restrictions;

  DEBUG ("called");

  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "Listing", NULL, NULL);

  g_dbus_connection_signal_subscribe (self->bus,
      TEST_DBUS_ACCOUNT_SERVICE,
      TEST_DBUS_ACCOUNT_SERVICE_IFACE,
      "AccountCreated",
      TEST_DBUS_ACCOUNT_SERVICE_PATH,
      NULL, /* no arg0 */
      G_DBUS_SIGNAL_FLAGS_NONE,
      account_created_cb,
      g_object_ref (self),
      g_object_unref);

  g_dbus_connection_signal_subscribe (self->bus,
      TEST_DBUS_ACCOUNT_SERVICE,
      TEST_DBUS_ACCOUNT_SERVICE_IFACE,
      "AccountDeleted",
      TEST_DBUS_ACCOUNT_SERVICE_PATH,
      NULL, /* no arg0 */
      G_DBUS_SIGNAL_FLAGS_NONE,
      account_deleted_cb,
      g_object_ref (self),
      g_object_unref);

  g_dbus_connection_signal_subscribe (self->bus,
      TEST_DBUS_ACCOUNT_SERVICE,
      TEST_DBUS_ACCOUNT_SERVICE_IFACE,
      "AttributesChanged",
      TEST_DBUS_ACCOUNT_SERVICE_PATH,
      NULL, /* no arg0 */
      G_DBUS_SIGNAL_FLAGS_NONE,
      attributes_changed_cb,
      g_object_ref (self),
      g_object_unref);

  g_dbus_connection_signal_subscribe (self->bus,
      TEST_DBUS_ACCOUNT_SERVICE,
      TEST_DBUS_ACCOUNT_SERVICE_IFACE,
      "ParametersChanged",
      TEST_DBUS_ACCOUNT_SERVICE_PATH,
      NULL, /* no arg0 */
      G_DBUS_SIGNAL_FLAGS_NONE,
      parameters_changed_cb,
      g_object_ref (self),
      g_object_unref);

  /* list is allowed to block */
  tuple = g_dbus_connection_call_sync (self->bus,
      TEST_DBUS_ACCOUNT_SERVICE,
      TEST_DBUS_ACCOUNT_SERVICE_PATH,
      TEST_DBUS_ACCOUNT_SERVICE_IFACE,
      "GetAccounts",
      NULL, /* no parameters */
      G_VARIANT_TYPE ("(a{s(a{sv}a{su}a{sv}a{ss}a{su}u)})"),
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      NULL, /* no cancellable */
      &error);

  if (g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
      g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN))
    {
      /* this regression test isn't using the fake accounts service */
      g_clear_error (&error);
      return NULL;
    }

  self->active = TRUE;

  g_assert_no_error (error);

  g_variant_get (tuple, "(@*)", &accounts);

  g_variant_iter_init (&account_iter, accounts);

  while (g_variant_iter_loop (&account_iter,
        "{s(@a{sv}@a{su}@a{sv}@a{ss}@a{su}u)}", &account_name,
        &attributes, &attribute_flags,
        &parameters, &untyped_parameters, &param_flags, &restrictions))
    {
      test_dbus_account_plugin_add_account (self, account_name,
          attributes, attribute_flags, parameters, untyped_parameters,
          param_flags, restrictions);

      ret = g_list_prepend (ret, g_strdup (account_name));
    }

  g_variant_unref (accounts);
  g_variant_unref (tuple);
  return ret;
}

static void
test_dbus_account_plugin_ready (McpAccountStorage *storage,
    McpAccountManager *am)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);

  DEBUG ("called");
  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "Ready", NULL, NULL);
  self->feedback = MCP_ACCOUNT_MANAGER (am);

  test_dbus_account_plugin_process_events (self);
}

static gchar *
test_dbus_account_plugin_create (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *manager,
    const gchar *protocol,
    const gchar *identifier,
    GError **error)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account;
  gchar *name;

  if (!self->active)
    return FALSE;

  name = mcp_account_manager_get_unique_name ((McpAccountManager *) am,
      manager, protocol, identifier);
  account = ensure_account (self, name);
  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "DeferringCreate", g_variant_new_parsed ("(%o,)", account->path), NULL);
  return name;
}

static void delete_account_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data);

static void
test_dbus_account_plugin_delete_async (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account_name,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account = lookup_account (self, account_name);
  GTask *task = g_task_new (self, cancellable, callback, user_data);

  g_task_set_task_data (task, g_strdup (user_data), g_free);

  DEBUG ("called");

  g_return_if_fail (self->active);
  g_return_if_fail (account != NULL);

  /* deletion used to be delayed, so the regression tests will expect this
   * to happen - leave them unmodified for now */
  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "DeferringDelete", g_variant_new_parsed ("(%o,)", account->path),
      NULL);
  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "CommittingOne", g_variant_new_parsed ("(%o,)", account->path), NULL);

  g_dbus_connection_call (self->bus,
      TEST_DBUS_ACCOUNT_SERVICE,
      TEST_DBUS_ACCOUNT_SERVICE_PATH,
      TEST_DBUS_ACCOUNT_SERVICE_IFACE,
      "DeleteAccount",
      g_variant_new_parsed ("(%s,)", account_name),
      G_VARIANT_TYPE_UNIT,
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      NULL, /* no cancellable */
      delete_account_cb,
      task);
}

static gboolean
test_dbus_account_plugin_delete_finish (McpAccountStorage *storage,
    GAsyncResult *res,
    GError **error)
{
  return g_task_propagate_boolean (G_TASK (res), error);
}

static GVariant *
test_dbus_account_plugin_get_attribute (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account_name,
    const gchar *attribute,
    const GVariantType *type,
    McpAttributeFlags *flags)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account = lookup_account (self, account_name);
  GVariant *v;

  if (flags != NULL)
    *flags = 0;

  g_return_val_if_fail (self->active, NULL);
  g_return_val_if_fail (account != NULL, NULL);

  v = g_hash_table_lookup (account->attributes, attribute);

  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "GetAttribute",
      g_variant_new_parsed ("(%o, %s)", account->path, attribute), NULL);

  if (v != NULL)
    {
      return g_variant_ref (v);
    }
  else
    {
      return NULL;
    }
}

static GVariant *
test_dbus_account_plugin_get_parameter (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account_name,
    const gchar *parameter,
    const GVariantType *type,
    McpParameterFlags *flags)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account = lookup_account (self, account_name);
  GVariant *v;
  const gchar *s;

  if (flags != NULL)
    *flags = 0;

  g_return_val_if_fail (self->active, NULL);
  g_return_val_if_fail (account != NULL, NULL);

  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "GetParameter",
      g_variant_new_parsed ("(%o, %s)", account->path, parameter), NULL);

  v = g_hash_table_lookup (account->parameters, parameter);
  s = g_hash_table_lookup (account->untyped_parameters, parameter);

  if (v != NULL)
    {
      return g_variant_ref (v);
    }
  else if (s != NULL && type != NULL)
    {
      return mcp_account_manager_unescape_variant_from_keyfile (am,
          s, type, NULL);
    }
  else
    {
      return NULL;
    }
}

static gchar **
test_dbus_account_plugin_list_typed_parameters (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account_name)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account = lookup_account (self, account_name);
  GPtrArray *arr;
  GHashTableIter iter;
  gpointer k;

  g_return_val_if_fail (self->active, NULL);
  g_return_val_if_fail (account != NULL, NULL);

  arr = g_ptr_array_sized_new (g_hash_table_size (account->parameters) + 1);

  g_hash_table_iter_init (&iter, account->parameters);

  while (g_hash_table_iter_next (&iter, &k, NULL))
    g_ptr_array_add (arr, g_strdup (k));

  g_ptr_array_add (arr, NULL);

  return (gchar **) g_ptr_array_free (arr, FALSE);
}

static gchar **
test_dbus_account_plugin_list_untyped_parameters (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account_name)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account = lookup_account (self, account_name);
  GPtrArray *arr;
  GHashTableIter iter;
  gpointer k;

  g_return_val_if_fail (self->active, NULL);
  g_return_val_if_fail (account != NULL, NULL);

  arr = g_ptr_array_sized_new (
      g_hash_table_size (account->untyped_parameters) + 1);

  g_hash_table_iter_init (&iter, account->untyped_parameters);

  while (g_hash_table_iter_next (&iter, &k, NULL))
    g_ptr_array_add (arr, g_strdup (k));

  g_ptr_array_add (arr, NULL);

  return (gchar **) g_ptr_array_free (arr, FALSE);
}

static McpAccountStorageSetResult
test_dbus_account_plugin_set_attribute (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account_name,
    const gchar *attribute,
    GVariant *value,
    McpAttributeFlags flags)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account = lookup_account (self, account_name);
  GVariant *old;
  guint old_flags;

  g_return_val_if_fail (account_name != NULL, FALSE);
  g_return_val_if_fail (attribute != NULL, FALSE);

  DEBUG ("%s of %s", attribute, account_name);

  g_return_val_if_fail (self->active, MCP_ACCOUNT_STORAGE_SET_RESULT_FAILED);
  g_return_val_if_fail (account != NULL, MCP_ACCOUNT_STORAGE_SET_RESULT_FAILED);

  if (value == NULL)
    {
      if (!g_hash_table_contains (account->attributes, attribute))
        return MCP_ACCOUNT_STORAGE_SET_RESULT_UNCHANGED;

      g_hash_table_remove (account->attributes, attribute);
      g_hash_table_remove (account->attribute_flags, attribute);
      g_hash_table_add (account->uncommitted_attributes, g_strdup (attribute));

      g_dbus_connection_emit_signal (self->bus, NULL,
          TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
          "DeferringDeleteAttribute",
          g_variant_new_parsed ("(%o, %s)", account->path, attribute), NULL);

      return MCP_ACCOUNT_STORAGE_SET_RESULT_CHANGED;
    }

  old = g_hash_table_lookup (account->attributes, attribute);
  old_flags = GPOINTER_TO_UINT (g_hash_table_lookup (
        account->attribute_flags, attribute));

  if (old != NULL && g_variant_equal (old, value) && old_flags == flags)
    return MCP_ACCOUNT_STORAGE_SET_RESULT_UNCHANGED;

  g_hash_table_insert (account->attributes, g_strdup (attribute),
      g_variant_ref (value));
  g_hash_table_insert (account->attribute_flags, g_strdup (attribute),
      GUINT_TO_POINTER (flags));
  g_hash_table_add (account->uncommitted_attributes, g_strdup (attribute));

  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "DeferringSetAttribute",
      g_variant_new_parsed ("(%o, %s, %v)", account->path, attribute, value),
      NULL);

  return MCP_ACCOUNT_STORAGE_SET_RESULT_CHANGED;
}

static McpAccountStorageSetResult
test_dbus_account_plugin_set_parameter (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account_name,
    const gchar *parameter,
    GVariant *value,
    McpParameterFlags flags)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account = lookup_account (self, account_name);
  GVariant *old;
  guint old_flags;

  g_return_val_if_fail (account_name != NULL, FALSE);
  g_return_val_if_fail (parameter != NULL, FALSE);

  DEBUG ("%s of %s", parameter, account_name);

  g_return_val_if_fail (self->active, MCP_ACCOUNT_STORAGE_SET_RESULT_FAILED);
  g_return_val_if_fail (account != NULL, MCP_ACCOUNT_STORAGE_SET_RESULT_FAILED);

  if (value == NULL)
    {
      if (!g_hash_table_contains (account->parameters, parameter) &&
          !g_hash_table_contains (account->untyped_parameters, parameter))
        return MCP_ACCOUNT_STORAGE_SET_RESULT_UNCHANGED;

      g_hash_table_remove (account->parameters, parameter);
      g_hash_table_remove (account->untyped_parameters, parameter);
      g_hash_table_remove (account->parameter_flags, parameter);
      g_hash_table_add (account->uncommitted_parameters, g_strdup (parameter));

      g_dbus_connection_emit_signal (self->bus, NULL,
          TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
          "DeferringDeleteParameter",
          g_variant_new_parsed ("(%o, %s)", account->path, parameter), NULL);

      return TRUE;
    }

  old = g_hash_table_lookup (account->parameters, parameter);
  old_flags = GPOINTER_TO_UINT (g_hash_table_lookup (
        account->parameter_flags, parameter));

  if (old != NULL && g_variant_equal (old, value) && old_flags == flags)
    return MCP_ACCOUNT_STORAGE_SET_RESULT_UNCHANGED;

  g_hash_table_remove (account->untyped_parameters, parameter);
  g_hash_table_insert (account->parameters, g_strdup (parameter),
      g_variant_ref (value));
  g_hash_table_insert (account->parameter_flags, g_strdup (parameter),
      GUINT_TO_POINTER (flags));
  g_hash_table_add (account->uncommitted_parameters, g_strdup (parameter));

  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "DeferringSetParameter",
      g_variant_new_parsed ("(%o, %s, %v)", account->path, parameter, value),
      NULL);

  return MCP_ACCOUNT_STORAGE_SET_RESULT_CHANGED;
}

static void
delete_account_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GTask *task = user_data;
  GVariant *tuple;
  GError *error = NULL;
  TestDBusAccountPlugin *self = g_task_get_source_object (task);
  const gchar *account_name = g_task_get_task_data (task);

  tuple = g_dbus_connection_call_finish (self->bus, res, &error);

  if (tuple != NULL)
    {
      /* we'll emit ::deleted when we see the signal, which probably
       * already happened */
      g_hash_table_remove (self->accounts, account_name);
      g_variant_unref (tuple);
      g_task_return_boolean (task, TRUE);
    }
  else
    {
      g_prefix_error (&error, "Unable to delete account %s: ",
          account_name);
      g_warning ("%s", error->message);
      g_task_return_error (task, error);
    }

  g_object_unref (task);
}

static void
create_account_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  AsyncData *ad = user_data;
  GVariant *tuple;
  GError *error = NULL;

  tuple = g_dbus_connection_call_finish (ad->self->bus, res, &error);

  if (tuple != NULL)
    {
      Account *account = lookup_account (ad->self, ad->account_name);

      if (account != NULL)
        account->flags &= ~UNCOMMITTED_CREATION;

      g_variant_unref (tuple);
    }
  else
    {
      g_warning ("Unable to create account %s: %s", ad->account_name,
          error->message);
      g_clear_error (&error);
      /* FIXME: we could roll back the creation by claiming that
       * the service deleted the account? If we do, we will have
       * to do it in an idle because we might be iterating over
       * all accounts in commit() */
    }

  async_data_free (ad);
}

static void
update_attributes_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  AsyncData *ad = user_data;
  GVariant *tuple;
  GError *error = NULL;

  tuple = g_dbus_connection_call_finish (ad->self->bus, res, &error);

  if (tuple != NULL)
    {
      Account *account = lookup_account (ad->self, ad->account_name);

      DEBUG ("Successfully committed attributes of %s", ad->account_name);

      if (account != NULL)
        g_hash_table_remove_all (account->uncommitted_attributes);

      g_variant_unref (tuple);
    }
  else
    {
      g_warning ("Unable to update attributes on %s: %s", ad->account_name,
          error->message);
      g_clear_error (&error);
      /* FIXME: we could roll back the creation by claiming that
       * the service restored the old attributes? */
    }

  async_data_free (ad);
}

static void
update_parameters_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  AsyncData *ad = user_data;
  GVariant *tuple;
  GError *error = NULL;

  tuple = g_dbus_connection_call_finish (ad->self->bus, res, &error);

  if (tuple != NULL)
    {
      Account *account = lookup_account (ad->self, ad->account_name);

      DEBUG ("Successfully committed parameters of %s", ad->account_name);

      if (account != NULL)
        g_hash_table_remove_all (account->uncommitted_parameters);

      g_variant_unref (tuple);
    }
  else
    {
      g_warning ("Unable to update parameters on %s: %s", ad->account_name,
          error->message);
      g_clear_error (&error);
      /* FIXME: we could roll back the creation by claiming that
       * the service restored the old parameters? */
    }

  async_data_free (ad);
}

static gboolean
test_dbus_account_plugin_commit (McpAccountStorage *storage,
    McpAccountManager *am,
    const gchar *account_name)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account;
  GHashTableIter iter;
  gpointer k;
  GVariantBuilder a_sv_builder;
  GVariantBuilder a_ss_builder;
  GVariantBuilder a_su_builder;
  GVariantBuilder as_builder;

  DEBUG ("%s", account_name);

  account = lookup_account (self, account_name);

  g_return_val_if_fail (self->active, FALSE);
  g_return_val_if_fail (account != NULL, FALSE);

  g_dbus_connection_emit_signal (self->bus, NULL,
      TEST_DBUS_ACCOUNT_PLUGIN_PATH, TEST_DBUS_ACCOUNT_PLUGIN_IFACE,
      "CommittingOne", g_variant_new_parsed ("(%o,)", account->path), NULL);

  if (account->flags & UNCOMMITTED_CREATION)
    {
      g_dbus_connection_call (self->bus,
          TEST_DBUS_ACCOUNT_SERVICE,
          TEST_DBUS_ACCOUNT_SERVICE_PATH,
          TEST_DBUS_ACCOUNT_SERVICE_IFACE,
          "CreateAccount",
          g_variant_new_parsed ("(%s,)", account_name),
          G_VARIANT_TYPE_UNIT,
          G_DBUS_CALL_FLAGS_NONE,
          -1,
          NULL, /* no cancellable */
          create_account_cb,
          async_data_new (self, account_name));
    }

  if (g_hash_table_size (account->uncommitted_attributes) != 0)
    {
      g_variant_builder_init (&a_sv_builder, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_init (&a_su_builder, G_VARIANT_TYPE ("a{su}"));
      g_variant_builder_init (&as_builder, G_VARIANT_TYPE_STRING_ARRAY);
      g_hash_table_iter_init (&iter, account->uncommitted_attributes);

      while (g_hash_table_iter_next (&iter, &k, NULL))
        {
          GVariant *v = g_hash_table_lookup (account->attributes, k);

          DEBUG ("Attribute %s uncommitted, committing it now",
              (const gchar *) k);

          if (v != NULL)
            {
              g_variant_builder_add (&a_sv_builder, "{sv}", k, v);
              g_variant_builder_add (&a_su_builder, "{su}", k,
                  GPOINTER_TO_UINT (g_hash_table_lookup (account->attribute_flags,
                      k)));
            }
          else
            {
              g_variant_builder_add (&as_builder, "s", k);
            }
        }

      g_dbus_connection_call (self->bus,
          TEST_DBUS_ACCOUNT_SERVICE,
          TEST_DBUS_ACCOUNT_SERVICE_PATH,
          TEST_DBUS_ACCOUNT_SERVICE_IFACE,
          "UpdateAttributes",
          g_variant_new_parsed ("(%s, %v, %v, %v)", account_name,
            g_variant_builder_end (&a_sv_builder),
            g_variant_builder_end (&a_su_builder),
            g_variant_builder_end (&as_builder)),
          G_VARIANT_TYPE_UNIT,
          G_DBUS_CALL_FLAGS_NONE,
          -1,
          NULL, /* no cancellable */
          update_attributes_cb,
          async_data_new (self, account_name));
    }
  else
    {
      DEBUG ("no attributes to commit");
    }

  if (g_hash_table_size (account->uncommitted_parameters) != 0)
    {
      g_variant_builder_init (&a_sv_builder, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_init (&a_ss_builder, G_VARIANT_TYPE ("a{ss}"));
      g_variant_builder_init (&a_su_builder, G_VARIANT_TYPE ("a{su}"));
      g_variant_builder_init (&as_builder, G_VARIANT_TYPE_STRING_ARRAY);
      g_hash_table_iter_init (&iter, account->uncommitted_parameters);

      while (g_hash_table_iter_next (&iter, &k, NULL))
        {
          GVariant *v = g_hash_table_lookup (account->parameters, k);
          const gchar *s = g_hash_table_lookup (account->untyped_parameters, k);

          DEBUG ("Parameter %s uncommitted, committing it now",
              (const gchar *) k);

          if (v != NULL)
            {
              g_variant_builder_add (&a_sv_builder, "{sv}", k, v);
              g_variant_builder_add (&a_su_builder, "{su}", k,
                  GPOINTER_TO_UINT (g_hash_table_lookup (account->parameter_flags,
                      k)));
            }
          else if (s != NULL)
            {
              g_variant_builder_add (&a_ss_builder, "{ss}", k, s);
              g_variant_builder_add (&a_su_builder, "{su}", k,
                  GPOINTER_TO_UINT (g_hash_table_lookup (account->parameter_flags,
                      k)));
            }
          else
            {
              g_variant_builder_add (&as_builder, "s", k);
            }
        }

      g_dbus_connection_call (self->bus,
          TEST_DBUS_ACCOUNT_SERVICE,
          TEST_DBUS_ACCOUNT_SERVICE_PATH,
          TEST_DBUS_ACCOUNT_SERVICE_IFACE,
          "UpdateParameters",
          g_variant_new_parsed ("(%s, %v, %v, %v, %v)", account_name,
            g_variant_builder_end (&a_sv_builder),
            g_variant_builder_end (&a_ss_builder),
            g_variant_builder_end (&a_su_builder),
            g_variant_builder_end (&as_builder)),
          G_VARIANT_TYPE_UNIT,
          G_DBUS_CALL_FLAGS_NONE,
          -1,
          NULL, /* no cancellable */
          update_parameters_cb,
          async_data_new (self, account_name));
    }
  else
    {
      DEBUG ("no parameters to commit");
    }

  return TRUE;
}

static void
test_dbus_account_plugin_get_identifier (McpAccountStorage *storage,
    const gchar *account_name,
    GValue *identifier)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account = lookup_account (self, account_name);

  DEBUG ("%s", account_name);

  g_return_if_fail (self->active);
  g_return_if_fail (account != NULL);

  /* Our "library-specific unique identifier" is just the object-path
   * as a string. */
  g_value_init (identifier, G_TYPE_STRING);
  g_value_set_string (identifier, account->path);
}

static GHashTable *
test_dbus_account_plugin_get_additional_info (McpAccountStorage *storage,
    const gchar *account_name)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account = lookup_account (self, account_name);
  GHashTable *ret;

  DEBUG ("%s", account_name);

  g_return_val_if_fail (self->active, NULL);
  g_return_val_if_fail (account != NULL, NULL);

  ret = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) tp_g_value_slice_free);
  g_hash_table_insert (ret, g_strdup ("hello"),
      tp_g_value_slice_new_static_string ("world"));

  return ret;
}

static guint
test_dbus_account_plugin_get_restrictions (McpAccountStorage *storage,
    const gchar *account_name)
{
  TestDBusAccountPlugin *self = TEST_DBUS_ACCOUNT_PLUGIN (storage);
  Account *account = lookup_account (self, account_name);

  DEBUG ("%s", account_name);

  g_return_val_if_fail (self->active, 0);
  g_return_val_if_fail (account != NULL, 0);

  return account->restrictions;
}

static void
account_storage_iface_init (McpAccountStorageIface *iface)
{
  iface->name = "TestDBusAccount";
  iface->desc = "Regression test plugin";
  /* this should be higher priority than the diverted-keyfile one */
  iface->priority = MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_NORMAL + 100;

  iface->get_attribute = test_dbus_account_plugin_get_attribute;
  iface->get_parameter = test_dbus_account_plugin_get_parameter;
  iface->list_typed_parameters =
    test_dbus_account_plugin_list_typed_parameters;
  iface->list_untyped_parameters =
    test_dbus_account_plugin_list_untyped_parameters;
  iface->set_attribute = test_dbus_account_plugin_set_attribute;
  iface->set_parameter = test_dbus_account_plugin_set_parameter;
  iface->list = test_dbus_account_plugin_list;
  iface->ready = test_dbus_account_plugin_ready;
  iface->delete_async = test_dbus_account_plugin_delete_async;
  iface->delete_finish = test_dbus_account_plugin_delete_finish;
  iface->commit = test_dbus_account_plugin_commit;
  iface->get_identifier = test_dbus_account_plugin_get_identifier;
  iface->get_additional_info = test_dbus_account_plugin_get_additional_info;
  iface->get_restrictions = test_dbus_account_plugin_get_restrictions;
  iface->create = test_dbus_account_plugin_create;
}
