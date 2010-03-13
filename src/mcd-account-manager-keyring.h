/*
 * The gnome keyring account manager keyfile storage pseudo-plugin
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

#ifndef __MCD_ACCOUNT_MANAGER_KEYRING_H__
#define __MCD_ACCOUNT_MANAGER_KEYRING_H__

G_BEGIN_DECLS

#define MCD_TYPE_ACCOUNT_MANAGER_KEYRING \
  (mcd_account_manager_keyring_get_type ())

#define MCD_ACCOUNT_MANAGER_KEYRING(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_ACCOUNT_MANAGER_KEYRING,   \
      McdAccountManagerKeyring))

#define MCD_ACCOUNT_MANAGER_KEYRING_CLASS(k)     \
    (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_ACCOUNT_MANAGER_KEYRING, \
        McdAccountManagerClass))

#define MCD_IS_ACCOUNT_MANAGER_KEYRING(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_ACCOUNT_MANAGER_KEYRING))

#define MCD_IS_ACCOUNT_MANAGER_KEYRING_CLASS(k)  \
  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_ACCOUNT_MANAGER_KEYRING))

#define MCD_ACCOUNT_MANAGER_KEYRING_GET_CLASS(o) \
    (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_ACCOUNT_MANAGER_KEYRING, \
        McdAccountManagerKeyringClass))

typedef struct {
  GObject parent;
  GKeyFile *keyfile;
  GKeyFile *removed;
  GHashTable *removed_accounts;
  gboolean save;
  gboolean loaded;
} _McdAccountManagerKeyring;

typedef struct {
  GObjectClass parent_class;
} _McdAccountManagerKeyringClass;

typedef _McdAccountManagerKeyring McdAccountManagerKeyring;
typedef _McdAccountManagerKeyringClass McdAccountManagerKeyringClass;



GType mcd_account_manager_keyring_get_type (void) G_GNUC_CONST;

McdAccountManagerKeyring *mcd_account_manager_keyring_new (void);

#endif
