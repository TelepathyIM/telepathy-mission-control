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

/* IMPORTANT IMPLEMENTATION NOTE:
 *
 * Note for implementors: save_param is for saving account parameters (in MC
 * terms) - anything that ends up stored as "param-" in the standard gkeyfile
 * save_value is for everything else.
 *
 * Whether such a value is stored in the global section of an SSO account or
 * in the IM specific section is orthogonal to the above, and in the mapping
 * is not necessarily from MC "name" to SSO "name", or from MC "param-name"
 * to SSO "parameters/name" - so be careful when making such decisions.
 *
 * The existing mappings have been arrived at empirically.
 */

#define PLUGIN_PRIORITY (MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_KEYRING + 10)
#define PLUGIN_NAME "maemo-libaccounts"
#define PLUGIN_DESCRIPTION \
  "Account storage in the Maemo SSO store via libaccounts-glib API"

#define PARAM_PREFIX_MC "param-"
#define PARAM_PREFIX    "parameters/"
#define LIBACCT_ID_KEY  "libacct-uid"

#define AG_ACCOUNT_KEY "username"
#define MC_ACCOUNT_KEY "account"
#define PASSWORD_KEY   "password"

#define MC_CMANAGER_KEY "manager"
#define MC_PROTOCOL_KEY "protocol"
#define MC_IDENTITY_KEY "tmc-uid"

typedef enum {
  DELAYED_CREATE,
  DELAYED_DELETE,
} DelayedSignal;

typedef struct {
  DelayedSignal signal;
  AgAccountId account_id;
} DelayedSignalData;

static void account_storage_iface_init (McpAccountStorageIface *,
    gpointer);

static gchar *
_ag_accountid_to_mc_key (const McdAccountManagerSso *sso,
    AgAccountId id,
    gboolean create);

static gchar * get_mc_param_key (const gchar *key);

static void
save_value (AgAccount *account,
    const gchar *key,
    const gchar *val);

static void _ag_account_stored_cb (AgAccount *acct,
    const GError *err,
    gpointer ignore);

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

/* Is an AG key corresponding to an MC _parameter_ global? */
static gboolean _ag_key_is_global (const gchar *key)
{
  return g_str_equal (key, AG_ACCOUNT_KEY) || g_str_equal (key, PASSWORD_KEY);
}

/* Is an AG key corresponding to an MC non-parameter service specific? */
static gboolean _ag_value_is_local (const gchar *key)
{
  return g_str_equal (key, MC_IDENTITY_KEY);
}

static gboolean
_ag_account_select_default_im_service (AgAccount *account)
{
  gboolean have_im_service = FALSE;
  GList *first = ag_account_list_services_by_type (account, "IM");

  if (first != NULL && first->data != NULL)
    {
      have_im_service = TRUE;
      DEBUG ("default IM service %s", ag_service_get_name (first->data));
      ag_account_select_service (account, first->data);
    }

  ag_service_list_free (first);

  return have_im_service;
}

static AgSettingSource
_ag_account_global_value (AgAccount *account,
    const gchar *key,
    GValue *value)
{
  AgSettingSource src = AG_SETTING_SOURCE_NONE;
  AgService *service = ag_account_get_selected_service (account);

  if (service != NULL)
    {
      ag_account_select_service (account, NULL);
      src = ag_account_get_value (account, key, value);
      ag_account_select_service (account, service);
    }
  else
    {
      src = ag_account_get_value (account, key, value);
    }

  return src;
}

static AgSettingSource
_ag_account_local_value (AgAccount *account,
    const gchar *key,
    GValue *value)
{
  AgSettingSource src = AG_SETTING_SOURCE_NONE;
  AgService *service = ag_account_get_selected_service (account);

  if (service != NULL)
    {
      src = ag_account_get_value (account, key, value);
    }
  else
    {
      _ag_account_select_default_im_service (account);
      src = ag_account_get_value (account, key, value);
      ag_account_select_service (account, NULL);
    }

  return src;
}

static void _sso_deleted (GObject *object,
    AgAccountId id,
    gpointer data)
{
  AgManager *ag_manager = AG_MANAGER (object);
  McdAccountManagerSso *sso = MCD_ACCOUNT_MANAGER_SSO (data);

  if (sso->ready)
    {
      const gchar *name =
        g_hash_table_lookup (sso->id_name_map, GUINT_TO_POINTER (id));

      /* if the account was in our cache, then this was a 3rd party delete *
       * op that someone did behind our back: fire the signal and clean up */
      if (name != NULL)
        {
          McpAccountStorage *mcpa = MCP_ACCOUNT_STORAGE (sso);

          /* forget id->name map first, so the signal can't start a loop */
          g_hash_table_remove (sso->id_name_map, GUINT_TO_POINTER (id));
          g_signal_emit_by_name (mcpa, "deleted", name);
          g_hash_table_remove (sso->accounts, name);
        }
    }
  else
    {
      DelayedSignalData *data = g_slice_new0 (DelayedSignalData);

      data->signal = DELAYED_DELETE;
      data->account_id = id;
      g_queue_push_tail (sso->pending_signals, data);
    }
}

static void _sso_created (GObject *object,
    AgAccountId id,
    gpointer data)
{
  AgManager *ag_manager = AG_MANAGER (object);
  McdAccountManagerSso *sso = MCD_ACCOUNT_MANAGER_SSO (data);
  gchar *name =
    g_hash_table_lookup (sso->id_name_map, GUINT_TO_POINTER (id));

  if (sso->ready)
    {
      /* if we already know the account's name, we shouldn't fire the new *
       * account signal as it is one we (and our superiors) already have  */
      if (name == NULL)
        {
          McpAccountStorage *mcpa = MCP_ACCOUNT_STORAGE (sso);
          AgAccount *account = ag_manager_get_account (ag_manager, id);

          if (account != NULL)
            {
              /* this will be owned by the ag account hash, do not free it */
              name = _ag_accountid_to_mc_key (sso, id, TRUE);

              if (name != NULL)
                {
                  g_hash_table_insert (sso->accounts, name, account);
                  g_hash_table_insert (sso->id_name_map, GUINT_TO_POINTER (id),
                      g_strdup (name));

                  save_value (account, MC_IDENTITY_KEY, name);

                  ag_account_store (account, _ag_account_stored_cb, NULL);

                  g_signal_emit_by_name (mcpa, "created", name);
                }
              else
                {
                  DEBUG ("SSO account #%u is unnameable, ignoring it", id);
                }
            }
        }
    }
  else
    {
      DelayedSignalData *data = g_slice_new0 (DelayedSignalData);

      data->signal = DELAYED_CREATE;
      data->account_id = id;
      g_queue_push_tail (sso->pending_signals, data);
    }
}

static void
mcd_account_manager_sso_init (McdAccountManagerSso *self)
{
  DEBUG ("mcd_account_manager_sso_init");
  self->ag_manager = ag_manager_new ();
  self->accounts =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  self->id_name_map =
    g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

  self->pending_signals = g_queue_new ();

  self->services = ag_manager_list_services (self->ag_manager);

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

static void
_ag_account_stored_cb (AgAccount *acct, const GError *err, gpointer ignore)
{
  GValue uid = { 0 };
  const gchar *name = NULL;
  AgSettingSource src = AG_SETTING_SOURCE_NONE;

  g_value_init (&uid, G_TYPE_STRING);

  src = _ag_account_local_value (acct, MC_IDENTITY_KEY, &uid);

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
    AgAccountId id,
    gboolean create)
{
  AgAccount *acct = ag_manager_get_account (sso->ag_manager, id);
  AgSettingSource src = AG_SETTING_SOURCE_NONE;
  GValue value = { 0 };

  DEBUG ("AG Account ID: %u", id);

  g_value_init (&value, G_TYPE_STRING);

  /* first look for the stored TMC uid */
  src = _ag_account_local_value (acct, MC_IDENTITY_KEY, &value);

  /* if we found something, our work here is done: */
  if (src != AG_SETTING_SOURCE_NONE)
    {
      gchar *uid = g_value_dup_string (&value);
      g_value_unset (&value);
      return uid;
    }

  if (!create)
    return NULL;

  DEBUG ("no " MC_IDENTITY_KEY " found, synthesising one:\n");

  src = _ag_account_global_value (acct, AG_ACCOUNT_KEY, &value);

  DEBUG (AG_ACCOUNT_KEY ": %s; type: %s",
      src ? "exists" : "missing",
      src ? (G_VALUE_TYPE_NAME (&value)) : "n/a" );

  if (src != AG_SETTING_SOURCE_NONE && G_VALUE_HOLDS_STRING (&value))
    {
      AgAccountSettingIter setting;
      const gchar *k;
      const GValue *v;
      GValue cmanager = { 0 };
      GValue protocol = { 0 };
      const gchar *cman, *proto;
      McpAccountManager *am = sso->manager_interface;
      AgService *service = ag_account_get_selected_service (acct);
      GHashTable *params = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, NULL);
      gchar *name = NULL;

      g_value_init (&cmanager, G_TYPE_STRING);
      g_value_init (&protocol, G_TYPE_STRING);

      /* if we weren't on a service when got here, pick the most likely one: */
      if (service == NULL)
        _ag_account_select_default_im_service (acct);

      ag_account_get_value (acct, MC_CMANAGER_KEY, &cmanager);
      cman = g_value_get_string (&cmanager);

      if (cman == NULL)
        goto cleanup;

      ag_account_get_value (acct, MC_PROTOCOL_KEY, &protocol);
      proto = g_value_get_string (&protocol);

      if (proto == NULL)
        goto cleanup;

      g_hash_table_insert (params, g_strdup (MC_ACCOUNT_KEY), &value);

      /* prepare the hash of MC param keys -> GValue */
      /* NOTE: some AG bare settings map to MC parameters,   *
       * so we must iterate over all AG settings, parameters *
       * and bare settings included                          */

      /* first any matching global values: */
      ag_account_select_service (acct, NULL);
      ag_account_settings_iter_init (acct, &setting, NULL);

      while (ag_account_settings_iter_next (&setting, &k, &v))
        {
          gchar *mc_key = get_mc_param_key (k);

          if (mc_key != NULL && g_str_has_prefix (mc_key, PARAM_PREFIX_MC))
            {
              gchar *param_key = g_strdup (mc_key + strlen (PARAM_PREFIX_MC));

              g_hash_table_insert (params, param_key, (gpointer) v);
            }

          g_free (mc_key);
        }

      /* then any service specific settings */
      if (service != NULL)
        ag_account_select_service (acct, service);
      else
        _ag_account_select_default_im_service (acct);

      ag_account_settings_iter_init (acct, &setting, NULL);
      while (ag_account_settings_iter_next (&setting, &k, &v))
        {
          gchar *mc_key = get_mc_param_key (k);

          if (mc_key != NULL && g_str_has_prefix (mc_key, PARAM_PREFIX_MC))
            {
              gchar *param_key = g_strdup (mc_key + strlen (PARAM_PREFIX_MC));

              g_hash_table_insert (params, param_key, (gpointer) v);
            }

          g_free (mc_key);
        }

      name = mcp_account_manager_get_unique_name (am, cman, proto, params);

    cleanup:
      ag_account_select_service (acct, service);
      g_hash_table_unref (params);
      g_value_unset (&value);
      g_value_unset (&cmanager);
      g_value_unset (&protocol);

      DEBUG (MC_IDENTITY_KEY " value %p:%s synthesised", name, name);
      return name;
    }

  DEBUG (MC_IDENTITY_KEY "not synthesised, returning NULL");
  return NULL;
}

static AgAccount *
get_ag_account (const McdAccountManagerSso *sso,
    const McpAccountManager *am,
    const gchar *name,
    AgAccountId *id)
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

  *id = 0;

  return NULL;
}

static void
save_param (AgAccount *account,
    const gchar *key,
    const gchar *val)
{
  const gchar *pkey = key + strlen (PARAM_PREFIX_MC);
  gchar *param_key = NULL;
  gboolean global = FALSE;
  AgService *service = ag_account_get_selected_service (account);

  /* username and password are parameters in MC but not in AG: *
   * also it's 'username' in AG but 'account' in MC            */
  if (g_str_equal (pkey, MC_ACCOUNT_KEY))
    param_key = g_strdup (AG_ACCOUNT_KEY);
  else if (g_str_equal (pkey, PASSWORD_KEY))
    param_key = g_strdup (PASSWORD_KEY);
  else
    param_key = g_strdup_printf (PARAM_PREFIX "%s", pkey);

  global = _ag_key_is_global (param_key);

  if (global)
    ag_account_select_service (account, NULL);
  else if (service == NULL)
    _ag_account_select_default_im_service (account);

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

  /* leave the selected service as we found it: */
  ag_account_select_service (account, service);

  g_free (param_key);
}

static void
save_value (AgAccount *account,
    const gchar *key,
    const gchar *val)
{
  AgService *service = NULL;
  gboolean local = FALSE;

  /* special cases, never saved */
  if (g_str_equal (key, MC_CMANAGER_KEY) || g_str_equal (key, MC_PROTOCOL_KEY))
    return;

  /* values, unlike parameters, are _mostly_ global - not service specific */
  service = ag_account_get_selected_service (account);
  local = _ag_value_is_local (key);

  /* pick the right service/global section of SSO, and switch if necessary */
  if (local && service == NULL)
    _ag_account_select_default_im_service (account);
  else if (!local && service != NULL)
    ag_account_select_service (account, NULL);

  if (g_str_equal (key, "Enabled"))
    {
      ag_account_set_enabled (account, g_str_equal (val, "true"));
      goto cleanup;
    }

  if (val == NULL)
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

 cleanup:
  /* leave the slected service as we found it */
  ag_account_select_service (account, service);
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
  AgAccount *account = get_ag_account (sso, am, acct, &id);

  /* we no longer create accounts in libaccount: either an account exists *
   * in libaccount as a result of some 3rd party intervention, or it is   *
   * not an account that this plugin should ever concern itself with      */
  g_return_val_if_fail (key != NULL, FALSE);

  if (account != NULL)
    {
      if (g_str_equal (key, "sso-services"))
        return TRUE;

      if (g_str_has_prefix (key, PARAM_PREFIX_MC))
        save_param (account, key, val);
      else
        save_value (account, key, val);

      sso->save = TRUE;
      return TRUE;
    }

  /* no account and we couldn't/wouldn't create one */
  return FALSE;
}

/* get the MC parameter key corresponding to an SSO key          *
 * note that not all MC parameters correspond to SSO parameters, *
 * some correspond to values instead                             */
/* NOTE: value keys are passed through unchanged */
static gchar *
get_mc_param_key (const gchar *key)
{
  /* these two are parameters in MC but not in AG */
  if (g_str_equal (key, AG_ACCOUNT_KEY))
    return g_strdup (PARAM_PREFIX_MC MC_ACCOUNT_KEY);

  if (g_str_equal (key, PASSWORD_KEY))
    return g_strdup (PARAM_PREFIX_MC PASSWORD_KEY);

  /* now check for regular params */
  if (g_str_has_prefix (key, PARAM_PREFIX))
    return g_strdup_printf (PARAM_PREFIX_MC "%s", key + strlen (PARAM_PREFIX));

  return g_strdup (key);
}

/* get the SSO key corresponding to an MC parameter */
/* NOTE: value keys are passed through unchanged */
static gchar *
get_ag_param_key (const gchar *key)
{
  if (g_str_equal (key, PARAM_PREFIX_MC MC_ACCOUNT_KEY))
    return g_strdup (AG_ACCOUNT_KEY);

  if (g_str_equal (key, PARAM_PREFIX_MC PASSWORD_KEY))
    return g_strdup (PASSWORD_KEY);

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
  AgAccount *account = get_ag_account (sso, am, acct, &id);
  AgService *service = ag_account_get_selected_service (account);

  if (account == NULL)
    return FALSE;

  if (key != NULL)
    {
      if (g_str_equal (key, "Enabled"))
        {
          const gchar *v = NULL;

          ag_account_select_service (account, NULL);
          v = ag_account_get_enabled (account) ? "true" : "false";
          mcp_account_manager_set_value (am, acct, key, v);
        }
      else if (g_str_equal (key, "sso-services"))
        {
          GString *result = g_string_new ("");
          AgManager * agm = ag_account_get_manager (account);
          GList *services = ag_manager_list_services (agm);
          GList *item = NULL;

          for (item = services; item != NULL; item = g_list_next (item))
            {
              const gchar *name = ag_service_get_name (item->data);

              g_string_append_printf (result, "%s;", name);
            }

          mcp_account_manager_set_value (am, acct, key, result->str);

          ag_service_list_free (services);
          g_string_free (result, TRUE);
        }
      else
        {
          gchar *k = get_ag_param_key (key);
          GValue v = { 0 };
          AgSettingSource src = AG_SETTING_SOURCE_NONE;

          g_value_init (&v, G_TYPE_STRING);

          if (_ag_key_is_global (k))
            {
              src = _ag_account_global_value (account, k, &v);
            }
          else
            {
              src = _ag_account_local_value (account, k, &v);
            }

          if (src != AG_SETTING_SOURCE_NONE)
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
      const gchar *on = NULL;

      /* pick the IM service if we haven't got one set */
      if (service == NULL)
        _ag_account_select_default_im_service (account);

      ag_account_settings_iter_init (account, &setting, NULL);
      while (ag_account_settings_iter_next (&setting, &k, &v))
        {
          if (!_ag_key_is_global (k))
            {
              gchar *mc_key = get_mc_param_key (k);
              gchar *value = _gvalue_to_string (v);

              mcp_account_manager_set_value (am, acct, mc_key, value);

              g_free (value);
              g_free (mc_key);
            }
        }

      /* deselect any service we may have to get global settings */
      ag_account_select_service (account, NULL);
      ag_account_settings_iter_init (account, &setting, NULL);

      while (ag_account_settings_iter_next (&setting, &k, &v))
        {
          if (_ag_key_is_global (k))
            {
              gchar *mc_key = get_mc_param_key (k);
              gchar *value  = _gvalue_to_string (v);

              mcp_account_manager_set_value (am, acct, mc_key, value);

              g_free (value);
              g_free (mc_key);
            }
        }

      /* special case, global value may not be stored as an explicit key */
      on = ag_account_get_enabled (account) ? "true" : "false";
      mcp_account_manager_set_value (am, acct, "Enabled", on);
    }

  /* leave the selected service as we found it */
  ag_account_select_service (account, service);
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
  AgAccount *account = get_ag_account (sso, am, acct, &id);

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
      /* this value ties MC accounts to SSO accounts */
      save_value (account, MC_IDENTITY_KEY, key);
      ag_account_store (account, _ag_account_stored_cb, NULL);
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
      const gchar *enabled = NULL;

      if (account != NULL)
        {
          AgService *service = ag_account_get_selected_service (account);
          gchar *name = _ag_accountid_to_mc_key (sso, id, FALSE);

          if (name != NULL)
            {
              gchar *ident = g_strdup_printf ("%u", id);
              GStrv mc_id = g_strsplit (name, "/", 3);

              /* cache the account object, and the ID->name maping: the  *
               * latter is required because we might receive an async    *
               * delete signal with the ID after libaccounts-glib has    *
               * purged all its account data, so we couldn't rely on the *
               * MC_IDENTITY_KEY setting.                                */
              g_hash_table_insert (sso->accounts, name, account);
              g_hash_table_insert (sso->id_name_map, GUINT_TO_POINTER (id),
                  g_strdup (name));

              if (service == NULL)
                _ag_account_select_default_im_service (account);

              ag_account_settings_iter_init (account, &iter, NULL);

              while (ag_account_settings_iter_next (&iter, &key, &val))
                {
                  if (!_ag_key_is_global (key))
                    {
                      gchar *mc_key = get_mc_param_key (key);
                      gchar *value = _gvalue_to_string (val);

                      mcp_account_manager_set_value (am, name, mc_key, value);
                      g_free (value);
                      g_free (mc_key);
                    }
                }

              ag_account_select_service (account, NULL);
              ag_account_settings_iter_init (account, &iter, NULL);

              while (ag_account_settings_iter_next (&iter, &key, &val))
                {
                  if (_ag_key_is_global (key))
                    {
                      gchar *mc_key = get_mc_param_key (key);
                      gchar *value = _gvalue_to_string (val);

                      mcp_account_manager_set_value (am, name, mc_key, value);
                      g_free (value);
                      g_free (mc_key);
                    }
                }

              enabled = ag_account_get_enabled (account) ? "true": "false";
              mcp_account_manager_set_value (am, name, "Enabled", enabled);
              mcp_account_manager_set_value (am, name, LIBACCT_ID_KEY, ident);
              mcp_account_manager_set_value (am, name, MC_CMANAGER_KEY, mc_id[0]);
              mcp_account_manager_set_value (am, name, MC_PROTOCOL_KEY, mc_id[1]);
              mcp_account_manager_set_value (am, name, MC_IDENTITY_KEY, name);

              ag_account_select_service (account, service);

              g_strfreev (mc_id);
              g_free (ident);
            }
        }
      else
        {
          DelayedSignalData *data = g_slice_new0 (DelayedSignalData);

          data->account_id = id;
          g_queue_push_tail (sso->pending_signals, data);
        }
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
      gchar *name = NULL;

      name = _ag_accountid_to_mc_key (sso, id, FALSE);

      if (name != NULL)
        {
          DEBUG ("\naccount %s listed", name);
          rval = g_list_prepend (rval, name);
        }
      else
        {
          DelayedSignalData *data = g_slice_new0 (DelayedSignalData);

          DEBUG ("\naccount %u delayed", id);
          data->signal = DELAYED_CREATE;
          data->account_id = id;
          g_queue_push_tail (sso->pending_signals, data);
        }
    }

  ag_manager_list_free (ag_ids);

  return rval;
}

static void
_ready (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  McdAccountManagerSso *sso = MCD_ACCOUNT_MANAGER_SSO (self);

  if (sso->ready)
    return;

  g_assert (sso->manager_interface == NULL);
  sso->manager_interface = g_object_ref (G_OBJECT (am));
  sso->ready = TRUE;

  while (g_queue_get_length (sso->pending_signals) > 0)
    {
      DelayedSignalData *data = g_queue_pop_head (sso->pending_signals);
      GObject *signal_source = G_OBJECT (sso->ag_manager);

      switch (data->signal)
        {
          case DELAYED_CREATE:
            _sso_created (signal_source, data->account_id, sso);
            break;
          case DELAYED_DELETE:
            _sso_deleted (signal_source, data->account_id, sso);
            break;
          default:
            g_assert_not_reached ();
        }

      g_slice_free (DelayedSignalData, data);
    }

  g_queue_free (sso->pending_signals);
  sso->pending_signals = NULL;
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
  mcp_account_storage_iface_implement_ready (iface, _ready);
}

McdAccountManagerSso *
mcd_account_manager_sso_new (void)
{
  return g_object_new (MCD_TYPE_ACCOUNT_MANAGER_SSO, NULL);
}
