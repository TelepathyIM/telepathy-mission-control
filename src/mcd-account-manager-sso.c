/*
 * A pseudo-plugin that stores/fetches accounts in/from the SSO via libaccounts
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

#include "config.h"
#include "mcd-account-manager-sso.h"
#include "mcd-debug.h"

#include <string.h>
#include <ctype.h>

#define PLUGIN_PRIORITY (MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_KEYRING + 10)
#define PLUGIN_NAME "maemo-libaccounts"
#define PLUGIN_DESCRIPTION \
  "Account storage in the Maemo SSO store via libaccounts-glib API"

#define PARAM_PREFIX_MC "param-"
#define PARAM_PREFIX    "parameters/"
#define LIBACCT_ID_KEY  "libacct-uid"

#define AG_ACCOUNT_KEY "username"
#define MC_ACCOUNT_KEY "account"

#define MC_CMANAGER_KEY "manager"
#define MC_PROTOCOL_KEY "protocol"
#define MC_IDENTITY_KEY "tmc-uid"

static void account_storage_iface_init (McpAccountStorageIface *,
    gpointer);

static gchar *
_ag_accountid_to_mc_key (const McdAccountManagerSso *sso,
    AgAccountId id);

static void
save_value (AgAccount *account,
    const gchar *key,
    const gchar *val);

static AgService *
_provider_get_service (const McdAccountManagerSso *sso,
    const gchar *provider);

G_DEFINE_TYPE_WITH_CODE (McdAccountManagerSso, mcd_account_manager_sso,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_STORAGE,
        account_storage_iface_init));

static gchar *
_gvalue_to_string (const GValue *val)
{
  switch (G_VALUE_TYPE (val))
    {
      case G_TYPE_STRING:
        return g_value_dup_string (val);
      case G_TYPE_BOOLEAN:
        return g_strdup (g_value_get_boolean (val) ? "true" : "false");
      case G_TYPE_CHAR:
        return g_strdup_printf ("%c", g_value_get_uchar (val));
      case G_TYPE_UCHAR:
        return g_strdup_printf ("%c", g_value_get_char (val));
      case G_TYPE_INT:
        return g_strdup_printf ("%i", g_value_get_int (val));
      case G_TYPE_UINT:
        return g_strdup_printf ("%u", g_value_get_uint (val));
      case G_TYPE_LONG:
        return g_strdup_printf ("%ld", g_value_get_long (val));
      case G_TYPE_ULONG:
        return g_strdup_printf ("%lu", g_value_get_ulong (val));
      case G_TYPE_INT64:
        return g_strdup_printf ("%" G_GINT64_FORMAT, g_value_get_int64 (val));
      case G_TYPE_UINT64:
        return g_strdup_printf ("%" G_GUINT64_FORMAT, g_value_get_uint64 (val));
      case G_TYPE_ENUM:
        return g_strdup_printf ("%d" , g_value_get_enum (val));
      case G_TYPE_FLAGS:
        return g_strdup_printf ("%u", g_value_get_flags (val));
      case G_TYPE_FLOAT:
        return g_strdup_printf ("%f", g_value_get_float (val));
      case G_TYPE_DOUBLE:
        return g_strdup_printf ("%g", g_value_get_double (val));
      default:
        DEBUG ("Unsupported type %s", G_VALUE_TYPE_NAME (val));
        return NULL;
    }
}

static void _sso_deleted (GObject *object,
    AgAccountId id,
    gpointer data)
{
  AgManager *ag_manager = AG_MANAGER (object);
  McdAccountManagerSso *sso = MCD_ACCOUNT_MANAGER_SSO (data);
  const gchar *name =
    g_hash_table_lookup (sso->id_name_map, GUINT_TO_POINTER (id));

  /* if we found an account in our cache, then this was a 3rd party delete op *
   * that someone did behind our back: fire the signal and clean up           */
  if (name != NULL)
    {
      McpAccountStorage *mcpa = MCP_ACCOUNT_STORAGE (sso);

      /* forget id->name map first, so the signal can't send us into a loop */
      g_hash_table_remove (sso->id_name_map, GUINT_TO_POINTER (id));
      g_signal_emit_by_name (mcpa, "deleted", name);
      g_hash_table_remove (sso->accounts, name);
    }
}

static void _sso_created (GObject *object,
    AgAccountId id,
    gpointer data)
{
  AgManager *ag_manager = AG_MANAGER (object);
  McdAccountManagerSso *sso = MCD_ACCOUNT_MANAGER_SSO (data);
  const gchar *name =
    g_hash_table_lookup (sso->id_name_map, GUINT_TO_POINTER (id));

  /* if we already know the account's name, we shouldn't fire the new *
   * account signal as it is one we (and our superiors) already have  */
  if (name == NULL)
    {
      McpAccountStorage *mcpa = MCP_ACCOUNT_STORAGE (sso);
      AgAccount *account = ag_manager_get_account (ag_manager, id);

      if (account != NULL)
        {
          const gchar *provider = ag_account_get_provider_name (account);
          AgService *service = _provider_get_service (sso, provider);
          gchar *name = NULL;
          GStrv mc_id = NULL;

          /* make sure values are stored against the right service *
           * or we won't get them back from the SSO store, ever:   */
          ag_account_select_service (account, service);

          name = _ag_accountid_to_mc_key (sso, id);
          mc_id = g_strsplit (name, "/", 3);

          g_hash_table_insert (sso->accounts, name, account);
          g_hash_table_insert (sso->id_name_map, GUINT_TO_POINTER (id),
              g_strdup (name));

          save_value (account, MC_CMANAGER_KEY, mc_id[0]);
          save_value (account, MC_PROTOCOL_KEY, mc_id[1]);
          save_value (account, MC_IDENTITY_KEY, name);

          g_signal_emit_by_name (mcpa, "created", name);

          g_free (name);
          g_strfreev (mc_id);
          ag_service_unref (service);
        }
    }
}

static void
mcd_account_manager_sso_init (McdAccountManagerSso *self)
{
  GList *nth;
  DEBUG ("mcd_account_manager_sso_init");
  self->ag_manager = ag_manager_new ();
  self->accounts =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  self->id_name_map =
    g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

  g_signal_connect(self->ag_manager, "account-deleted",
      G_CALLBACK (_sso_deleted), self);
  g_signal_connect(self->ag_manager, "account-created",
      G_CALLBACK (_sso_created), self);
}

static void
mcd_account_manager_sso_class_init (McdAccountManagerSsoClass *cls)
{
  DEBUG ("mcd_account_manager_sso_class_init");
}

static gboolean
want_this_account (GStrv mc_key)
{
  const gchar *manager  = mc_key[0];
  const gchar *protocol = mc_key[1];

  DEBUG ("want_this_account(%s/%s/%s)", mc_key[0], mc_key[1], mc_key[2]);

  /* we're only grabbing gabble+google talk accounts for now: */
  if ((strcmp (protocol, "jabber") == 0) &&
      (strcmp (manager,  "gabble") == 0))
    return TRUE;

  return FALSE;
}

static void
_ag_account_stored_cb (AgAccount *acct, const GError *err, gpointer ignore)
{
  GValue uid = { 0 };
  const gchar *name = NULL;
  AgSettingSource src = AG_SETTING_SOURCE_NONE;

  g_value_init (&uid, G_TYPE_STRING);
  src = ag_account_get_value (acct, MC_IDENTITY_KEY, &uid);

  if (src != AG_SETTING_SOURCE_NONE && G_VALUE_HOLDS_STRING (&uid))
    {
      name = g_value_get_string (&uid);
      DEBUG ("%p:%s stored: %s", acct, name, err ? err->message : "-");
      g_value_unset (&uid);
    }
  else
    {
      DEBUG ("%p:%s not stored? %s", acct,
          ag_account_get_display_name (acct), err ? err->message : "-");
    }
}

static gchar *
_ag_accountid_to_mc_key (const McdAccountManagerSso *sso,
    AgAccountId id)
{
  GString *key = NULL;
  AgAccount *acct = ag_manager_get_account (sso->ag_manager, id);
  AgSettingSource src = AG_SETTING_SOURCE_NONE;
  GValue value = { 0 };

  DEBUG ("AG Account ID: %u", id);

  g_value_init (&value, G_TYPE_STRING);

  /* first look for the stored TMC uid */
  src = ag_account_get_value (acct, MC_IDENTITY_KEY, &value);
  if (src != AG_SETTING_SOURCE_NONE)
    {
      gchar *uid = g_value_dup_string (&value);
      g_value_unset (&value);
      return uid;
    }

  DEBUG ("no " MC_IDENTITY_KEY " found, synthesising one:\n");

  src = ag_account_get_value (acct, PARAM_PREFIX AG_ACCOUNT_KEY, &value);

  if (src != AG_SETTING_SOURCE_NONE && G_VALUE_HOLDS_STRING (&value))
    {
      GValue cmanager = { 0 };
      GValue protocol = { 0 };
      gchar *cman;
      gchar *proto;
      gchar *c = (gchar *) g_value_get_string (&value);

      ag_account_get_value (acct, MC_CMANAGER_KEY, &cmanager);
      ag_account_get_value (acct, MC_PROTOCOL_KEY, &protocol);

      cman  = _gvalue_to_string (&cmanager);
      proto = _gvalue_to_string (&protocol);

      key = g_string_new ("");

      g_string_append_printf (key, "%s/%s/", cman, proto);

      for (; *c != '\0'; c++)
        if (isalnum (*c))
          g_string_append_c (key, *c);
        else
          g_string_append_printf (key, "_%02x", *c);
      g_string_append_c (key, '0');

      g_free (cman);
      g_free (proto);
      g_value_unset (&value);
      g_value_unset (&cmanager);
      g_value_unset (&protocol);

      return g_string_free (key, FALSE);
    }

  return NULL;
}

static AgAccount *
get_ag_account (const McdAccountManagerSso *sso,
    const McpAccountManager *am,
    const gchar *name,
    AgAccountId *id,
    gboolean create)
{
  AgAccount *account;
  gchar *ident = NULL;

  g_return_val_if_fail (id != NULL, NULL);

  /* we have a cached account, just return that */
  account = g_hash_table_lookup (sso->accounts, name);
  if (account != NULL)
    {
      *id = account->id;
      return account;
    }

  if (!create)
    {
      *id = 0;
      return NULL;
    }

  /* we haven't seen this account before: prep it for libaccounts: */
  ident = mcp_account_manager_get_value (am, name, LIBACCT_ID_KEY);

  /* we have a cached sso ident: see if it's still valid: */
  /* NOTE: no sso ident means there's no account yet, return NULL */
  if (ident != NULL)
    {
      gchar *end;

      *id = (AgAccountId) g_ascii_strtoull (ident, &end, 10);

      if (!(*id == 0 && end == ident))
        account = ag_manager_get_account (sso->ag_manager, *id);

      /* bogus ID, forget we ever saw it: */
      if (account == NULL)
        {
          mcp_account_manager_set_value (am, name, LIBACCT_ID_KEY, NULL);
          *id = 0;
        }
      else
        {
          g_hash_table_insert (sso->accounts, g_strdup (name), account);
        }
    }

  g_free (ident);

  return account;
}

static void
save_param (AgAccount *account,
    const gchar*key,
    const gchar *val)
{
  const gchar *pkey = key + strlen (PARAM_PREFIX_MC);
  gchar *param_key = NULL;

  if (strcmp (pkey, MC_ACCOUNT_KEY) == 0)
    param_key = g_strdup_printf (PARAM_PREFIX "%s", AG_ACCOUNT_KEY);
  else
    param_key = g_strdup_printf (PARAM_PREFIX "%s", pkey);

  if (val != NULL)
    {
      GValue value = { 0 };

      g_value_init (&value, G_TYPE_STRING);
      g_value_set_string (&value, val);
      ag_account_set_value (account, param_key, &value);
      g_value_unset (&value);
    }
  else
    {
      ag_account_set_value (account, param_key, NULL);
    }

  g_free (param_key);
}

static void
save_value (AgAccount *account,
    const gchar *key,
    const gchar *val)
{
  if (strcmp (key, "Enabled") == 0)
    {
      ag_account_set_enabled (account, (strcmp (val, "true") == 0));
    }
  else if (val == NULL)
    {
      ag_account_set_value (account, key, NULL);
    }
  else
    {
      GValue value = { 0 };
      g_value_init (&value, G_TYPE_STRING);
      g_value_set_string (&value, val);
      ag_account_set_value (account, key, &value);
      g_value_unset (&value);
    }
}

static const gchar *
_account_provider_name (const McdAccountManagerSso *sso,
    const gchar *acct)
{
  /* placeholder: we currently only support GTalk, so no logic is required */
  return "google";
}

static AgService *
_provider_get_service (const McdAccountManagerSso *sso,
    const gchar *provider)
{
  GList *nth;

  for (nth = sso->services; nth != NULL; nth = g_list_next (nth))
    {
      if (strcmp (ag_service_get_provider (nth->data), provider) == 0)
        return ag_service_ref (nth->data);
    }

  return NULL;
}

static gboolean
_set (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *acct,
    const gchar *key,
    const gchar *val)
{
  AgAccountId id;
  McdAccountManagerSso *sso = MCD_ACCOUNT_MANAGER_SSO (self);
  AgAccount *account = get_ag_account (sso, am, acct, &id, TRUE);

  /* no account? create one ready for libaccounts to save: */
  if (account == NULL)
    {
      GStrv mck = g_strsplit (acct, "/", 3);

      /* make sure it's one we actually want to grab: */
      if (want_this_account (mck))
        {
          const gchar *prov = _account_provider_name (sso, acct);

          if (prov != NULL)
            {
              AgService *service = _provider_get_service (sso, prov);

              account = ag_manager_create_account (sso->ag_manager, prov);
              id = 0;

              if (account != NULL)
                {
                  ag_account_select_service (account, service);
                  g_hash_table_insert (sso->accounts, g_strdup (acct), account);
                }

              ag_service_unref (service);
            }
        }

      g_strfreev (mck);
    }

  if (account != NULL)
    {
      if (g_str_has_prefix (key, PARAM_PREFIX_MC))
        save_param (account, key, val);
      else
        save_value (account, key, val);

      sso->save = TRUE;
      return TRUE;
    }

  /* no account and we couldn't create one */
  return FALSE;
}

static gchar *
get_mc_key (const gchar *key)
{
  if (g_strcmp0 (key, PARAM_PREFIX AG_ACCOUNT_KEY) == 0)
    return g_strdup (PARAM_PREFIX_MC MC_ACCOUNT_KEY);

  if (g_str_has_prefix (key, PARAM_PREFIX))
    return g_strdup_printf (PARAM_PREFIX_MC "%s", key + strlen (PARAM_PREFIX));

  return g_strdup (key);
}

static gchar *
get_ag_key (const gchar *key)
{
  if (g_strcmp0 (key, PARAM_PREFIX_MC MC_ACCOUNT_KEY) == 0)
    return g_strdup (PARAM_PREFIX AG_ACCOUNT_KEY);

  if (g_str_has_prefix (key, PARAM_PREFIX_MC))
    return g_strdup_printf (PARAM_PREFIX "%s", key + strlen (PARAM_PREFIX_MC));

  return g_strdup (key);
}

static gboolean
_get (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *acct,
    const gchar *key)
{
  AgAccountId id;
  McdAccountManagerSso *sso = MCD_ACCOUNT_MANAGER_SSO (self);
  AgAccount *account = get_ag_account (sso, am, acct, &id, FALSE);

  if (account == NULL)
    return FALSE;

  if (key != NULL)
    {
      if (strcmp (key, "Enabled") == 0)
        {
          const gchar *v = ag_account_get_enabled (account) ? "true" : "false";
          mcp_account_manager_set_value (am, acct, key, v);
        }
      else
        {
          gchar *k = get_ag_key (key);
          GValue v = { 0 };

          g_value_init (&v, G_TYPE_STRING);

          if (ag_account_get_value (account, k, &v) != AG_SETTING_SOURCE_NONE)
            {
              gchar *val = _gvalue_to_string (&v);

              mcp_account_manager_set_value (am, acct, key, val);

              g_free (val);
            }

          g_value_unset (&v);
          g_free (k);
        }
    }
  else
    {
      AgAccountSettingIter setting;
      const gchar *k;
      const GValue *v;
      const gchar *on = ag_account_get_enabled (account) ? "true" : "false";

      ag_account_settings_iter_init (account, &setting, NULL);

      while (ag_account_settings_iter_next (&setting, &k, &v))
        {
          gchar *mc_key = get_mc_key (k);
          gchar *value = _gvalue_to_string (v);

          DEBUG ("%s -> %s.%s := '%s'", k, acct, mc_key, value);

          mcp_account_manager_set_value (am, acct, mc_key, value);

          g_free (value);
          g_free (mc_key);
        }

      /* special case, may not be stored as an explicit key */
      mcp_account_manager_set_value (am, acct, "Enabled", on);
    }

  return TRUE;
}

static gboolean
_delete (const McpAccountStorage *self,
      const McpAccountManager *am,
      const gchar *acct,
      const gchar *key)
{
  AgAccountId id;
  McdAccountManagerSso *sso = MCD_ACCOUNT_MANAGER_SSO (self);
  AgAccount *account = get_ag_account (sso, am, acct, &id, FALSE);

  /* have no values for this account, nothing to do here: */
  if (account == NULL)
    return TRUE;

  if (key == NULL)
    {
      ag_account_delete (account);
      g_hash_table_remove (sso->accounts, acct);
      g_hash_table_remove (sso->id_name_map, GUINT_TO_POINTER (id));
    }
  else
    {
      if (g_str_has_prefix (key, PARAM_PREFIX_MC))
        save_param (account, key, NULL);
      else
        save_value (account, key, NULL);
    }

  return TRUE;
}

static gboolean
_commit (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  GHashTableIter iter;
  McdAccountManagerSso *sso = MCD_ACCOUNT_MANAGER_SSO (self);
  gchar *key;
  AgAccount *account;

  if (!sso->save)
    return TRUE;

  g_hash_table_iter_init (&iter, sso->accounts);

  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &account))
    {
      GStrv mc_key = g_strsplit (key, "/", 3);
      const gchar *provider = ag_account_get_provider_name (account);
      AgService *service = _provider_get_service (sso, provider);

      ag_account_select_service (account, service);
      /* these three values _must_ match the MC unique name contents *
       * or we will explode in a flaming ball of failure:            */
      save_value (account, MC_CMANAGER_KEY, mc_key[0]);
      save_value (account, MC_PROTOCOL_KEY, mc_key[1]);
      save_value (account, MC_IDENTITY_KEY, key);

      ag_account_store (account, _ag_account_stored_cb, NULL);

      ag_service_unref (service);
      g_strfreev (mc_key);
    }

  /* any pending changes should now have been pushed, clear the save-me flag */
  sso->save = FALSE;

  return TRUE;
}

static void
_load_from_libaccounts (McdAccountManagerSso *sso,
    const McpAccountManager *am)
{
  GList *ag_id;
  GList *ag_ids = ag_manager_list_by_service_type (sso->ag_manager, "IM");

  for (ag_id = ag_ids; ag_id != NULL; ag_id = g_list_next (ag_id))
    {
      const gchar *key;
      const GValue *val;
      AgAccountSettingIter iter;
      AgAccountId id = GPOINTER_TO_UINT (ag_id->data);
      AgAccount *account = ag_manager_get_account (sso->ag_manager, id);
      const gchar *enabled = ag_account_get_enabled (account) ? "true": "false";
      gchar *ident = g_strdup_printf ("%u", id);

      if (account != NULL)
        {
          const gchar *provider = ag_account_get_provider_name (account);
          AgService *service = _provider_get_service (sso, provider);
          gchar *name = NULL;
          GStrv mc_id = NULL;

          ag_account_select_service (account, service);
          name = _ag_accountid_to_mc_key (sso, id);
          mc_id = g_strsplit (name, "/", 3);

          /* cache the account object, and the ID->name maping: the latter is *
           * required because we might receive an async delete signal with    *
           * the ID after libaccounts-glib has purged all its account data,   *
           * so we couldn't rely on the MC_IDENTITY_KEY setting.              */
          g_hash_table_insert (sso->accounts, name, account);
          g_hash_table_insert (sso->id_name_map, GUINT_TO_POINTER (id),
              g_strdup (name));

          ag_account_settings_iter_init (account, &iter, NULL);

          while (ag_account_settings_iter_next (&iter, &key, &val))
            {
              gchar *mc_key = get_mc_key (key);
              gchar *value = _gvalue_to_string (val);

              mcp_account_manager_set_value (am, name, mc_key, value);
              g_free (value);
              g_free (mc_key);
            }

          mcp_account_manager_set_value (am, name, "Enabled", enabled);
          mcp_account_manager_set_value (am, name, LIBACCT_ID_KEY, ident);
          mcp_account_manager_set_value (am, name, MC_CMANAGER_KEY, mc_id[0]);
          mcp_account_manager_set_value (am, name, MC_PROTOCOL_KEY, mc_id[1]);
          mcp_account_manager_set_value (am, name, MC_IDENTITY_KEY, name);

          ag_service_unref (service);
          g_strfreev (mc_id);
        }

      g_free (ident);
    }

  sso->loaded = TRUE;
  ag_manager_list_free (ag_ids);
}

static GList *
_list (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  GList *rval = NULL;
  McdAccountManagerSso *sso = MCD_ACCOUNT_MANAGER_SSO (self);
  GList *ag_ids = NULL;
  GList *ag_id;

  if (!sso->loaded)
    _load_from_libaccounts (sso, am);

  ag_ids = ag_manager_list_by_service_type (sso->ag_manager, "IM");

  for (ag_id = ag_ids; ag_id != NULL; ag_id = g_list_next (ag_id))
    {
      AgAccountId id = GPOINTER_TO_UINT (ag_id->data);
      gchar *name = _ag_accountid_to_mc_key (sso, id);

      rval = g_list_prepend (rval, name);
    }

  ag_manager_list_free (ag_ids);

  return rval;
}

static void
account_storage_iface_init (McpAccountStorageIface *iface,
    gpointer unused G_GNUC_UNUSED)
{
  mcp_account_storage_iface_set_name (iface, PLUGIN_NAME);
  mcp_account_storage_iface_set_desc (iface, PLUGIN_DESCRIPTION);
  mcp_account_storage_iface_set_priority (iface, PLUGIN_PRIORITY);

  mcp_account_storage_iface_implement_get (iface, _get);
  mcp_account_storage_iface_implement_set (iface, _set);
  mcp_account_storage_iface_implement_delete (iface, _delete);
  mcp_account_storage_iface_implement_commit (iface, _commit);
  mcp_account_storage_iface_implement_list (iface, _list);
}

McdAccountManagerSso *
mcd_account_manager_sso_new (void)
{
  return g_object_new (MCD_TYPE_ACCOUNT_MANAGER_SSO, NULL);
}
