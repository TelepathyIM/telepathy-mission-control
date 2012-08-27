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

#include "config.h"

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
  GValue v = { 0, };
  static const gchar * const *empty = { NULL };
  gboolean ret;

  g_return_val_if_fail (MCD_IS_STORAGE (storage), FALSE);
  g_return_val_if_fail (account != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  g_value_init (&v, G_TYPE_STRV);
  g_value_set_static_boxed (&v, strv == NULL ? empty : strv);
  ret = mcd_storage_set_value (storage, account, key, &v, secret);
  g_value_unset (&v);
  return ret;
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
