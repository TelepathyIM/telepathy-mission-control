/* Representation of the account manager as presented to plugins. This is
 * deliberately a "smaller" API than McdAccountManager.
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010 Collabora Ltd.
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

#ifndef MCD_PLUGIN_ACCOUNT_H
#define MCD_PLUGIN_ACCOUNT_H

#include <mission-control-plugins/mission-control-plugins.h>

#include "mcd-account-manager.h"

G_BEGIN_DECLS

typedef struct _McdPluginAccount McdPluginAccount;
typedef struct _McdPluginAccountClass McdPluginAccountClass;
typedef struct _McdPluginAccountPrivate McdPluginAccountPrivate;

G_GNUC_INTERNAL GType mcd_plugin_account_get_type (void);

#define MCD_TYPE_PLUGIN_ACCOUNT (mcd_plugin_account_get_type ())

#define MCD_PLUGIN_ACCOUNT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_PLUGIN_ACCOUNT, McdPluginAccount))

#define MCD_PLUGIN_ACCOUNT_CLASS(klass)                         \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MCD_TYPE_PLUGIN_ACCOUNT,   \
      McdPluginAccountClass))

#define MCD_IS_PLUGIN_ACCOUNT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_PLUGIN_ACCOUNT))

#define MCD_IS_PLUGIN_ACCOUNT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MCD_TYPE_PLUGIN_ACCOUNT))

#define MCD_PLUGIN_ACCOUNT_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_PLUGIN_ACCOUNT, \
      McdPluginAccountClass))

McdPluginAccount *mcd_plugin_account_new (GKeyFile *m, GKeyFile *s);

G_END_DECLS

#endif
