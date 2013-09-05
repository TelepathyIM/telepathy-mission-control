/*
 * The default account manager keyfile storage pseudo-plugin
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

#ifndef __MCD_ACCOUNT_MANAGER_DEFAULT_H__
#define __MCD_ACCOUNT_MANAGER_DEFAULT_H__

G_BEGIN_DECLS

#define MCD_TYPE_ACCOUNT_MANAGER_DEFAULT \
  (mcd_account_manager_default_get_type ())

#define MCD_ACCOUNT_MANAGER_DEFAULT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_ACCOUNT_MANAGER_DEFAULT,   \
      McdAccountManagerDefault))

#define MCD_ACCOUNT_MANAGER_DEFAULT_CLASS(k)     \
    (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_ACCOUNT_MANAGER_DEFAULT, \
        McdAccountManagerClass))

#define MCD_IS_ACCOUNT_MANAGER_DEFAULT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_ACCOUNT_MANAGER_DEFAULT))

#define MCD_IS_ACCOUNT_MANAGER_DEFAULT_CLASS(k)  \
  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_ACCOUNT_MANAGER_DEFAULT))

#define MCD_ACCOUNT_MANAGER_DEFAULT_GET_CLASS(o) \
    (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_ACCOUNT_MANAGER_DEFAULT, \
        McdAccountManagerDefaultClass))

typedef struct {
  GObject parent;
  GKeyFile *keyfile;
  GKeyFile *removed;
  GHashTable *removed_accounts;
  gchar *filename;
  gboolean save;
  gboolean loaded;
} _McdAccountManagerDefault;

typedef struct {
  GObjectClass parent_class;
} _McdAccountManagerDefaultClass;

typedef _McdAccountManagerDefault McdAccountManagerDefault;
typedef _McdAccountManagerDefaultClass McdAccountManagerDefaultClass;



GType mcd_account_manager_default_get_type (void) G_GNUC_CONST;

McdAccountManagerDefault *mcd_account_manager_default_new (void);

G_END_DECLS

#endif
