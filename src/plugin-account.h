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

#ifndef MCD_PLUGIN_ACCOUNT_MANAGER_H
#define MCD_PLUGIN_ACCOUNT_MANAGER_H

#include <mission-control-plugins/mission-control-plugins.h>

#include "mcd-account-manager.h"

G_BEGIN_DECLS

typedef struct {
  GObject parent;
  TpDBusDaemon *dbusd;
  GKeyFile *keyfile;
  GKeyFile *secrets;
} McdPluginAccountManager;

typedef struct _McdPluginAccountManagerClass McdPluginAccountManagerClass;
typedef struct _McdPluginAccountManagerPrivate McdPluginAccountManagerPrivate;

G_GNUC_INTERNAL GType mcd_plugin_account_manager_get_type (void);

#define MCD_TYPE_PLUGIN_ACCOUNT_MANAGER (mcd_plugin_account_manager_get_type ())

#define MCD_PLUGIN_ACCOUNT_MANAGER(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_PLUGIN_ACCOUNT_MANAGER, \
      McdPluginAccountManager))

#define MCD_PLUGIN_ACCOUNT_MANAGER_CLASS(klass)                         \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MCD_TYPE_PLUGIN_ACCOUNT_MANAGER,   \
      McdPluginAccountManagerClass))

#define MCD_IS_PLUGIN_ACCOUNT_MANAGER(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_PLUGIN_ACCOUNT_MANAGER))

#define MCD_IS_PLUGIN_ACCOUNT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MCD_TYPE_PLUGIN_ACCOUNT_MANAGER))

#define MCD_PLUGIN_ACCOUNT_MANAGER_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_PLUGIN_ACCOUNT_MANAGER, \
      McdPluginAccountManagerClass))

McdPluginAccountManager *mcd_plugin_account_manager_new (void);

G_GNUC_INTERNAL
void _mcd_plugin_account_manager_set_dbus_daemon (McdPluginAccountManager *self,
    TpDBusDaemon *dbusd);

G_GNUC_INTERNAL
void _mcd_plugin_account_manager_ready (McdPluginAccountManager *self);

G_GNUC_INTERNAL
void _mcd_plugin_account_manager_connect_signal (const gchar *signal,
    GCallback func,
    gpointer user_data);

G_END_DECLS

#endif
