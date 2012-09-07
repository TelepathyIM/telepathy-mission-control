/*
 * MC account storage backend inspector, libaccounts backend
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

#include "account-store-libaccounts.h"

#include <libaccounts-glib/ag-manager.h>
#include <libaccounts-glib/ag-account.h>
#include <libaccounts-glib/ag-service.h>

#include <string.h>

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "account-store-libaccounts"

/* MC <-> AG global/local setting meta data */
#define MCPP "param-"
#define AGPP "parameters/"
#define LIBACCT_ID_KEY  "libacct-uid"

#define MC_ENABLED_KEY  "Enabled"
#define AG_ENABLED_KEY  "enabled"

#define AG_LABEL_KEY    "name"
#define MC_LABEL_KEY    "DisplayName"

#define AG_ACCOUNT_KEY  "username"
#define MC_ACCOUNT_KEY  "account"
#define PASSWORD_KEY    "password"
#define AG_ACCOUNT_ALT_KEY AGPP "account"

#define MC_CMANAGER_KEY "manager"
#define MC_PROTOCOL_KEY "protocol"
#define MC_IDENTITY_KEY "tmc-uid"

#define SERVICES_KEY    "sso-services"

#define MC_SERVICE_KEY  "Service"

typedef struct {
  gchar *mc_name;
  gchar *ag_name;
  gboolean global;   /* global ag setting or service specific? */
  gboolean readable; /* does the _standard_ read method copy this into MC?  */
  gboolean writable; /* does the _standard_ write method copy this into AG? */
  gboolean freeable; /* should clear_setting_data deallocate the names? */
} Setting;

#define GLOBAL     TRUE
#define SERVICE    FALSE
#define READABLE   TRUE
#define UNREADABLE FALSE
#define WRITABLE   TRUE
#define UNWRITABLE FALSE

typedef enum {
  SETTING_MC,
  SETTING_AG,
} SettingType;

Setting setting_map[] = {
  { MC_ENABLED_KEY     , AG_ENABLED_KEY , GLOBAL , UNREADABLE, UNWRITABLE },
  { MCPP MC_ACCOUNT_KEY, AG_ACCOUNT_KEY , GLOBAL , READABLE  , UNWRITABLE },
  { MCPP PASSWORD_KEY  , PASSWORD_KEY   , GLOBAL , READABLE  , WRITABLE   },
  { MC_LABEL_KEY       , AG_LABEL_KEY   , GLOBAL , READABLE  , WRITABLE   },
  { LIBACCT_ID_KEY     , LIBACCT_ID_KEY , GLOBAL , UNREADABLE, UNWRITABLE },
  { MC_IDENTITY_KEY    , MC_IDENTITY_KEY, SERVICE, READABLE  , WRITABLE   },
  { MC_CMANAGER_KEY    , MC_CMANAGER_KEY, SERVICE, READABLE  , UNWRITABLE },
  { MC_PROTOCOL_KEY    , MC_PROTOCOL_KEY, SERVICE, READABLE  , UNWRITABLE },
  { MC_SERVICE_KEY     , MC_SERVICE_KEY , SERVICE, UNREADABLE, UNWRITABLE },
  { SERVICES_KEY       , SERVICES_KEY   , GLOBAL , UNREADABLE, UNWRITABLE },
  { NULL               , NULL           , SERVICE, UNREADABLE, UNWRITABLE }
};

static void
clear_setting_data (Setting *setting)
{
  if (setting == NULL)
    return;

  if (!setting->freeable)
    return;

  g_free (setting->mc_name);
  g_free (setting->ag_name);
  setting->mc_name = NULL;
  setting->ag_name = NULL;
}

static Setting *
setting_data (const gchar *name, SettingType type)
{
  guint i = 0;
  static Setting parameter = { NULL, NULL, SERVICE, READABLE, WRITABLE, TRUE };
  const gchar *prefix;

  for (; setting_map[i].mc_name != NULL; i++)
    {
      const gchar *setting_name = NULL;

      if (type == SETTING_MC)
        setting_name = setting_map[i].mc_name;
      else
        setting_name = setting_map[i].ag_name;

      if (g_strcmp0 (name, setting_name) == 0)
        return &setting_map[i];
    }

  prefix = (type == SETTING_MC) ? MCPP : AGPP;

  if (!g_str_has_prefix (name, prefix))
    { /* a non-parameter setting */
      parameter.mc_name = g_strdup (name);
      parameter.ag_name = g_strdup (name);
    }
  else
    { /* a setting that is a parameter on both sides (AG & MC) */
      const guint plength = strlen (prefix);

      parameter.mc_name = g_strdup_printf ("%s%s", MCPP, name + plength);
      parameter.ag_name = g_strdup_printf ("%s%s", AGPP, name + plength);
    }

  return &parameter;
}


/* logging helpers: */
static void
_g_log_handler (const gchar   *log_domain,
    GLogLevelFlags log_level,
    const gchar   *message,
    gpointer	      unused_data)
{
  /* the libaccounts code is currently very chatty when debugging: *
   * we are only interested in or own debugging output for now.    */
  if ((gchar *)log_domain != (gchar *)G_LOG_DOMAIN)
    return;

  g_log_default_handler (log_domain, log_level, message, unused_data);
}

static void
toggle_mute (void)
{
  static GLogFunc old = NULL;

  if (old == NULL)
    {
      old = g_log_set_default_handler (_g_log_handler, NULL);
    }
  else
    {
      g_log_set_default_handler (old, NULL);
      old = NULL;
    }
}

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
        g_warning ("Unsupported type %s", G_VALUE_TYPE_NAME (val));
        return NULL;
    }
}

static AgManager *
get_ag_manager (void)
{
  static AgManager *agm = NULL;

  toggle_mute ();

  if (agm != NULL)
    return agm;

  agm = ag_manager_new ();

  toggle_mute ();

  return agm;
}

static AgAccount *
get_ag_account (const gchar *mc_account)
{
  AgAccount *ag_account = NULL;
  AgManager *ag_manager = get_ag_manager ();
  GList *ag_ids = NULL;
  GList *ag_id;

  toggle_mute ();

  ag_ids = ag_manager_list_by_service_type (ag_manager, "IM");
  g_debug ("%d accounts in SSO", g_list_length (ag_ids));

  for (ag_id = ag_ids; ag_id != NULL; ag_id = g_list_next (ag_id))
    {
      AgAccountId id = GPOINTER_TO_UINT (ag_id->data);
      AgAccount *account = ag_manager_get_account (ag_manager, id);

      if (account != NULL)
        {
          GValue value = G_VALUE_INIT;
          AgSettingSource source = AG_SETTING_SOURCE_NONE;

          g_value_init (&value, G_TYPE_STRING);
          ag_account_select_service (account, NULL);

          source = ag_account_get_value (account, MC_IDENTITY_KEY, &value);

          if (source != AG_SETTING_SOURCE_NONE)
            {
              if (g_str_equal (g_value_get_string (&value), mc_account))
                {
                  ag_account = g_object_ref (account);
                  ag_id = NULL;
                }

              g_value_unset (&value);
            }

          g_object_unref (account);
        }
    }

  ag_manager_list_free (ag_ids);

  toggle_mute ();

  return ag_account;
}

static gboolean
_ag_account_select_default_im_service (AgAccount *account)
{
  gboolean have_im_service = FALSE;
  GList *first = ag_account_list_services_by_type (account, "IM");

  if (first != NULL && first->data != NULL)
    {
      have_im_service = TRUE;
      ag_account_select_service (account, first->data);
    }

  ag_service_list_free (first);

  return have_im_service;
}

/* enabled is actually a tri-state<->boolean mapping */
static gboolean _sso_account_enabled (AgAccount *account, AgService *service)
{
  gboolean local  = FALSE;
  gboolean global = FALSE;
  AgService *original = ag_account_get_selected_service (account);

  if (service == NULL)
    {
      _ag_account_select_default_im_service (account);
      local = ag_account_get_enabled (account);
    }
  else
    {
      if (original != service)
        ag_account_select_service (account, service);

      local = ag_account_get_enabled (account);
    }

  ag_account_select_service (account, NULL);
  global = ag_account_get_enabled (account);

  ag_account_select_service (account, original);

  g_debug ("_sso_account_enabled: global:%d && local:%d", global, local);

  return local && global;
}

static void _sso_account_enable (AgAccount *account,
    AgService *service,
    gboolean on)
{
  AgService *original = ag_account_get_selected_service (account);

  /* turn the local enabled flag on/off as required */
  if (service != NULL)
    ag_account_select_service (account, service);
  else
    _ag_account_select_default_im_service (account);

  ag_account_set_enabled (account, on);

  /* if we are turning the account on, the global flag must also be set *
   * NOTE: this isn't needed when turning the account off               */
  if (on)
    {
      ag_account_select_service (account, NULL);
      ag_account_set_enabled (account, on);
    }

  ag_account_select_service (account, original);
}

/* saving settings other than the enabled tri-state */
static void
save_setting (AgAccount *account,
    const Setting *setting,
    const gchar *val)
{
  AgService *service = ag_account_get_selected_service (account);

  if (!setting->writable)
    return;

  if (setting->global)
    ag_account_select_service (account, NULL);
  else if (service == NULL)
    _ag_account_select_default_im_service (account);

  if (val != NULL)
    {
      GValue value = G_VALUE_INIT;

      g_value_init (&value, G_TYPE_STRING);
      g_value_set_string (&value, val);
      ag_account_set_value (account, setting->ag_name, &value);
      g_value_unset (&value);
    }
  else
    {
      ag_account_set_value (account, setting->ag_name, NULL);
    }

  /* leave the selected service as we found it: */
  ag_account_select_service (account, service);
}

gchar *
libaccounts_get (const gchar *mc_account, const gchar *key)
{
  gchar *rval = NULL;
  AgAccount *ag_account = get_ag_account (mc_account);
  Setting *setting = setting_data (key, SETTING_MC);

  toggle_mute ();

  if (ag_account != NULL)
    {

      if (setting == NULL)
        {
          g_debug ("setting %s is unknown/unmapped, aborting update", key);
          rval = g_strdup ("");
          goto done;
        }

      g_debug ("MC key %s -> AG key %s", key, setting->ag_name);

      if (g_str_equal (setting->ag_name, AG_ENABLED_KEY))
        {
          gboolean on = _sso_account_enabled (ag_account, NULL);

          rval = g_strdup (on ? "true" : "false");
          goto done;
        }
      else
        {
          GValue value = G_VALUE_INIT;
          AgSettingSource source = AG_SETTING_SOURCE_NONE;

          g_value_init (&value, G_TYPE_STRING);

          /* the 'account' parameter is a special case for historical reasons */
          if (g_str_equal (key, MCPP MC_ACCOUNT_KEY))
            {
              _ag_account_select_default_im_service (ag_account);
              source =
                ag_account_get_value (ag_account, AG_ACCOUNT_ALT_KEY, &value);

              if (source != AG_SETTING_SOURCE_NONE)
                goto found;
            }

          if (setting->global)
            ag_account_select_service (ag_account, NULL);
          else
            _ag_account_select_default_im_service (ag_account);

          source = ag_account_get_value (ag_account, setting->ag_name, &value);

        found:
          if (source != AG_SETTING_SOURCE_NONE)
            {
              rval = _gvalue_to_string (&value);
              g_value_unset (&value);
            }
        }
    }

 done:
  toggle_mute ();

  if (ag_account)
    g_object_unref (ag_account);

  clear_setting_data (setting);

  return rval;
}

gboolean
libaccounts_set (const gchar *mc_account,
    const gchar *key,
    const gchar *value)
{
  gboolean done = FALSE;
  AgAccount *ag_account = get_ag_account (mc_account);
  Setting *setting = setting_data (key, SETTING_MC);

  toggle_mute ();

  if (ag_account != NULL)
    {
      if (setting == NULL)
        {
          g_debug ("setting %s is unknown/unmapped, aborting update", key);
          goto done;
        }

      if (g_str_equal (setting->ag_name, MC_ENABLED_KEY))
        {
          gboolean on = g_str_equal (value, "true");

          _sso_account_enable (ag_account, NULL, on);
          done = TRUE;
          goto done;
        }
      else
        {
          save_setting (ag_account, setting, value);
          done = TRUE;
        }

      if (done)
        ag_account_store (ag_account, NULL, NULL);

    }

 done:
  toggle_mute ();

  if (ag_account)
    g_object_unref (ag_account);

  clear_setting_data (setting);

  return done;
}

gboolean
libaccounts_delete (const gchar *mc_account)
{
  gboolean done = FALSE;
  AgAccount *ag_account = get_ag_account (mc_account);

  toggle_mute ();

  if(ag_account != NULL)
    {
      ag_account_delete (ag_account);
      ag_account_store (ag_account, NULL, NULL);
      g_object_unref (ag_account);
      done = TRUE;
    }

  toggle_mute ();

  return done;
}

gboolean
libaccounts_exists (const gchar *mc_account)
{
  gboolean exists = FALSE;
  AgAccount *ag_account = get_ag_account (mc_account);

  toggle_mute ();

  if (ag_account != NULL)
    {
      exists = TRUE;
      g_object_unref (ag_account);
    }

  toggle_mute ();

  return exists;
}

GStrv
libaccounts_list (void)
{
  AgManager *ag_manager = get_ag_manager ();
  GList *ag_ids = ag_manager_list_by_service_type (ag_manager, "IM");
  guint len = g_list_length (ag_ids);
  GStrv rval = NULL;
  GList *id;
  guint i = 0;
  Setting *setting = setting_data (MC_IDENTITY_KEY, SETTING_AG);

  if (len == 0)
    goto done;

  rval = g_new (gchar *, len + 1);
  rval[len] = NULL;

  for (id = ag_ids; id && i < len; id = g_list_next (id))
    {
      GValue value = G_VALUE_INIT;
      AgAccountId uid = GPOINTER_TO_UINT (id->data);
      AgAccount *ag_account = ag_manager_get_account (ag_manager, uid);
      AgSettingSource source = AG_SETTING_SOURCE_NONE;

      if (ag_account)
        {
          if (setting->global)
            ag_account_select_service (ag_account, NULL);
          else
            _ag_account_select_default_im_service (ag_account);

          source = ag_account_get_value (ag_account, setting->ag_name, &value);
        }

      if (source != AG_SETTING_SOURCE_NONE)
        {
          rval[i++] = _gvalue_to_string (&value);
          g_value_unset (&value);
        }
      else
        {
          GValue cmanager = G_VALUE_INIT;
          GValue protocol = G_VALUE_INIT;
          GValue account  = G_VALUE_INIT;
          const gchar *acct = NULL;
          const gchar *cman = NULL;
          const gchar *proto = NULL;

          g_value_init (&cmanager, G_TYPE_STRING);
          g_value_init (&protocol, G_TYPE_STRING);
          g_value_init (&account,  G_TYPE_STRING);

          _ag_account_select_default_im_service (ag_account);

          ag_account_get_value (ag_account, MC_CMANAGER_KEY, &cmanager);
          cman = g_value_get_string (&cmanager);

          ag_account_get_value (ag_account, MC_PROTOCOL_KEY, &protocol);
          proto = g_value_get_string (&protocol);

          ag_account_select_service (ag_account, NULL);
          ag_account_get_value (ag_account, AG_ACCOUNT_KEY, &account);
          acct = g_value_get_string (&account);

          rval[i++] = g_strdup_printf ("unnamed account #%u (%s/%s/%s)",
              uid, cman, proto, acct);

          g_value_unset (&cmanager);
          g_value_unset (&protocol);
          g_value_unset (&account);
        }
    }

 done:
  g_list_free (ag_ids);
  clear_setting_data (setting);

  return rval;
}
