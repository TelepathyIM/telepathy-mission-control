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

#include <string.h>
#include <libaccounts-glib/ag-manager.h>
#include <libaccounts-glib/ag-account.h>
#include <glib.h>

#define PARAM_PREFIX_MC "param-"
#define PARAM_PREFIX    "parameters/"
#define LIBACCT_ID_KEY  "libacct-uid"

#define AG_ACCOUNT_KEY "username"
#define MC_ACCOUNT_KEY "account"
#define PASSWORD_KEY   "password"
#define ENABLED_KEY    "Enabled"

#define MC_CMANAGER_KEY "manager"
#define MC_PROTOCOL_KEY "protocol"
#define MC_IDENTITY_KEY "tmc-uid"

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "account-store-libaccounts"

static void
_g_log_handler (const gchar   *log_domain,
    GLogLevelFlags log_level,
    const gchar   *message,
    gpointer	      unused_data)
{
  if (log_domain != G_LOG_DOMAIN)
    return;

  g_log_default_handler (log_domain, log_level, message, unused_data);
}

/* libaccounts is incredibly chatty */
static void
toggle_mute ()
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
          GValue value = { 0 };
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

static gchar *
mc_to_ag_key (const gchar *mc_key)
{
  if (g_str_has_prefix (mc_key, PARAM_PREFIX_MC))
    {
      const gchar *pkey = mc_key + strlen (PARAM_PREFIX_MC);

      if (g_str_equal (pkey, MC_ACCOUNT_KEY))
        return g_strdup (AG_ACCOUNT_KEY);

      if (g_str_equal (pkey, PASSWORD_KEY))
        return g_strdup (PASSWORD_KEY);

      return g_strdup_printf (PARAM_PREFIX "%s", pkey);
    }

  return g_strdup (mc_key);
}

static gchar *
ag_to_mc_key (const gchar *ag_key)
{
  /* these two are parameters in MC but not in AG */
  if (g_str_equal (ag_key, AG_ACCOUNT_KEY))
    return g_strdup (PARAM_PREFIX_MC MC_ACCOUNT_KEY);

  if (g_str_equal (ag_key, PASSWORD_KEY))
    return g_strdup (PARAM_PREFIX_MC PASSWORD_KEY);

  /* now check for regular params */
  if (g_str_has_prefix (ag_key, PARAM_PREFIX))
    return
      g_strdup_printf (PARAM_PREFIX_MC "%s", ag_key + strlen (PARAM_PREFIX));

  return g_strdup (ag_key);
}

static gboolean key_is_global (const char *ag_key)
{
  /* parameters and MC_IDENTITY_KEY are service specific */
  if (g_str_has_prefix (ag_key, PARAM_PREFIX))
    return FALSE;

  if (g_str_equal (ag_key, MC_IDENTITY_KEY))
    return FALSE;

  /* anything else is global */
  return TRUE;
}

gchar *
libaccounts_get (const gchar *mc_account,
    const gchar *key)
{
  gchar *rval = NULL;
  AgAccount *ag_account = get_ag_account (mc_account);

  toggle_mute ();

  if (ag_account != NULL)
    {
      gchar *ag_key = mc_to_ag_key (key);

      g_debug ("MC key %s -> AG key %s", key, ag_key);

      if (g_str_equal (ag_key, ENABLED_KEY))
        {
          gboolean on = FALSE;

          ag_account_select_service (ag_account, NULL);
          on = ag_account_get_enabled (ag_account);

          if (on)
            {
              _ag_account_select_default_im_service (ag_account);
              on = ag_account_get_enabled (ag_account);
            }

          rval = on ? g_strdup ("true") : g_strdup ("false");
        }
      else
        {
          GValue value = { 0 };
          AgSettingSource source = AG_SETTING_SOURCE_NONE;

          g_value_init (&value, G_TYPE_STRING);

          if (key_is_global (ag_key))
            ag_account_select_service (ag_account, NULL);
          else
            _ag_account_select_default_im_service (ag_account);

          source = ag_account_get_value (ag_account, ag_key, &value);

          if (source != AG_SETTING_SOURCE_NONE)
            {
              rval = _gvalue_to_string (&value);
              g_value_unset (&value);
            }
        }

      g_free (ag_key);
      g_object_unref (ag_account);
    }

  toggle_mute ();

  return rval;
}

gboolean
libaccounts_set (const gchar *mc_account,
    const gchar *key,
    const gchar *value)
{
  gboolean done = FALSE;
  AgAccount *ag_account = get_ag_account (mc_account);

  toggle_mute ();

  if (ag_account != NULL)
    {
      gchar *ag_key = mc_to_ag_key (key);

      if (g_str_equal (ag_key, ENABLED_KEY))
        {
          gboolean on = g_str_equal (value, "true");

          ag_account_select_service (ag_account, NULL);
          ag_account_set_enabled (ag_account, on);
          _ag_account_select_default_im_service (ag_account);
          ag_account_set_enabled (ag_account, on);

          done = TRUE;
        }
      else
        {
          GValue val = { 0 };

          g_value_init (&val, G_TYPE_STRING);
          g_value_set_string (&val, value);
          ag_account_set_value (ag_account, ag_key, &val);
          g_value_unset (&val);

          done = TRUE;
        }

      if (done)
        ag_account_store (ag_account, NULL, NULL);

      g_free (ag_key);
      g_object_unref (ag_account);
    }

  toggle_mute ();

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
