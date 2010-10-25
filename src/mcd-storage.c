/* Mission Control storage API - interface which provides access to account
 * parameter/setting storage
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

#include "mcd-storage-priv.h"
#include "mcd-master.h"
#include "mcd-account-manager-priv.h"

GType
mcd_storage_get_type (void)
{
  static gsize once = 0;
  static GType type = 0;

  if (g_once_init_enter (&once))
    {
      static const GTypeInfo info = {
          sizeof (McdStorageIface),
          NULL, /* base_init */
          NULL, /* base_finalize */
          NULL, /* class_init */
          NULL, /* class_finalize */
          NULL, /* class_data */
          0, /* instance_size */
          0, /* n_preallocs */
          NULL, /* instance_init */
          NULL /* value_table */
      };

      type = g_type_register_static (G_TYPE_INTERFACE, "McdStorage", &info, 0);
      g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
      g_once_init_leave (&once, 1);
    }

  return type;
}

/**
 * mcd_storage_set_string:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 * @key: the key (name) of the parameter or setting
 * @value: the value to be stored (or %NULL to erase it)
 * @secret: whether the value is confidential (might get stored in the
 * keyring, for example)
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
mcd_storage_set_string (McdStorage *storage,
    const gchar *account,
    const gchar *key,
    const gchar *value,
    gboolean secret)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (iface->set_string != NULL, FALSE);

  return iface->set_string (storage, account, key, value, secret);
}

/**
 * mcd_storage_set_value:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 * @key: the key (name) of the parameter or setting
 * @value: the value to be stored (or %NULL to erase it)
 * @secret: whether the value is confidential (might get stored in the
 * keyring, for example)
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
mcd_storage_set_value (McdStorage *storage,
    const gchar *account,
    const gchar *key,
    const GValue *value,
    gboolean secret)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (iface->set_value != NULL, FALSE);

  return iface->set_value (storage, account, key, value, secret);
}

/**
 * mcd_storage_set_strv:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 * @key: the key (name) of the parameter or setting
 * @strv: the string vector to be stored (where %NULL is treated as equivalent
 * to an empty vector)
 * @secret: whether the value is confidential (might get stored in the
 * keyring, for example)
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
    const gchar *key,
    const gchar * const *strv,
    gboolean secret)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);
  GValue v = { 0, };
  static const gchar * const *empty = { NULL };
  gboolean ret;

  g_assert (iface != NULL);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (iface->set_value != NULL, FALSE);

  g_value_init (&v, G_TYPE_STRV);
  g_value_set_static_boxed (&v, strv == NULL ? empty : strv);
  ret = iface->set_value (storage, account, key, &v, secret);
  g_value_unset (&v);
  return ret;
}

/**
 * mcd_storage_commit:
 * @storage: An object implementing the #McdStorage interface
 * @account: the unique name of an account
 *
 * Sync the long term storage (whatever it might be) with the current
 * state of our internal cache.
 */
void
mcd_storage_commit (McdStorage *storage, const gchar *account)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_return_if_fail (iface->commit != NULL);

  iface->commit (storage, account);
}

/**
 * mcd_storage_load:
 * @storage: An object implementing the #McdStorage interface
 *
 * Load the long term account settings storage into our internal cache.
 * Should only really be called during startup, ie before our DBus names
 * have been claimed and other people might be relying on responses from us.
 */
void
mcd_storage_load (McdStorage *storage)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_return_if_fail (iface->load != NULL);

  iface->load (storage);
}

/**
 * mcd_storage_dup_accounts:
 * @storage: An object implementing the #McdStorage interface
 * @n: place for the number of accounts to be written to (or %NULL)
 *
 * Returns: a newly allocated GStrv containing the unique account names,
 * which must be freed by the caller with g_strfreev().
 **/
GStrv
mcd_storage_dup_accounts (McdStorage *storage, gsize *n)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_return_val_if_fail (iface->dup_accounts != NULL, NULL);

  return iface->dup_accounts (storage, n);
}

/**
 * mcd_storage_dup_settings:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @n: place for the number of settings to be written to (or %NULL)
 *
 * Returns: a newly allocated GStrv containing the names of all the
 * settings or parameters currently stored for @account. Must be
 * freed by the caller with g_strfreev().
 **/
GStrv
mcd_storage_dup_settings (McdStorage *storage, const gchar *account, gsize *n)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_return_val_if_fail (account != NULL, NULL);
  g_return_val_if_fail (iface->dup_settings != NULL, NULL);

  return iface->dup_settings (storage, account, n);
}

/**
 * mcd_storage_dup_string:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @key: name of the setting to be retrieved
 *
 * Returns: a newly allocated gchar * which must be freed with g_free().
 **/
gchar *
mcd_storage_dup_string (McdStorage *storage,
    const gchar *account,
    const gchar *key)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_assert (iface->dup_string != NULL);
  g_return_val_if_fail (account != NULL, NULL);

  return iface->dup_string (storage, account, key);
}

/**
 * mcd_storage_dup_value:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @key: name of the setting to be retrieved
 * @type: the type of #GValue to retrieve
 * @error: a place to store any #GError<!-- -->s that occur
 *
 * Returns: a newly allocated #GValue of type @type, whihc should be freed
 * with tp_g_value_slice_free() or g_slice_free() depending on whether the
 * the value itself should be freed (the former frees everything, the latter
 * only the #GValue container.
 *
 * If @error is set, but a non-%NULL value was returned, this indicates
 * that no value for the @key was found for @account, and the default
 * value for @type has been returned.
 **/
GValue *
mcd_storage_dup_value (McdStorage *storage,
    const gchar *account,
    const gchar *key,
    GType type,
    GError **error)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_assert (iface->dup_value != NULL);
  g_return_val_if_fail (account != NULL, NULL);

  return iface->dup_value (storage, account, key, type, error);
}

/**
 * mcd_storage_get_boolean:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @key: name of the setting to be retrieved
 *
 * Returns: a #gboolean. Unset/unparseable values are returned as %FALSE
 **/
gboolean
mcd_storage_get_boolean (McdStorage *storage,
    const gchar *account,
    const gchar *key)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_assert (iface->get_boolean != NULL);
  g_return_val_if_fail (account != NULL, FALSE);

  return iface->get_boolean (storage, account, key);
}

/**
 * mcd_storage_get_integer:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @key: name of the setting to be retrieved
 *
 * Returns: a #gint. Unset or non-numeric values are returned as 0
 **/
gint
mcd_storage_get_integer (McdStorage *storage,
    const gchar *account,
    const gchar *key)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_assert (iface->get_integer != NULL);
  g_return_val_if_fail (account != NULL, 0);

  return iface->get_integer (storage, account, key);
}


/**
 * mcd_storage_has_value:
 * @storage: An object implementing the #McdStorage interface
 * @account: unique name of the account
 * @key: name of the setting to be retrieved
 *
 * Returns: a #gboolean: %TRUE if the setting is present in the store,
 * %FALSE otherwise.
 **/
gboolean
mcd_storage_has_value (McdStorage *storage,
    const gchar *account,
    const gchar *key)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_assert (iface->has_value != NULL);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  return iface->has_value (storage, account, key);
}

/**
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
 **/
McpAccountStorage *
mcd_storage_get_plugin (McdStorage *storage, const gchar *account)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_assert (iface->get_storage_plugin != NULL);
  g_return_val_if_fail (account != NULL, NULL);

  return iface->get_storage_plugin (storage, account);
}

/**
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
mcd_storage_delete_account (McdStorage *storage, const gchar *account)
{
  McdStorageIface *iface = MCD_STORAGE_GET_IFACE (storage);

  g_assert (iface != NULL);
  g_assert (iface->delete_account != NULL);
  g_return_if_fail (account != NULL);

  iface->delete_account (storage, account);
}

void
_mcd_storage_store_connections (McdStorage *storage)
{
  McdMaster *master = mcd_master_get_default ();
  McdAccountManager *account_manager = NULL;

  g_object_get (master, "account-manager", &account_manager, NULL);

  if (account_manager != NULL)
    {
      _mcd_account_manager_store_account_connections (account_manager);
      g_object_unref (account_manager);
    }
}
