/*
 * The SSO/libaccounts-glib manager keyfile storage pseudo-plugin
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
#include <libaccounts-glib/ag-manager.h>

#ifndef __MCD_ACCOUNT_MANAGER_SSO_H__
#define __MCD_ACCOUNT_MANAGER_SSO_H__

G_BEGIN_DECLS

#define MCD_TYPE_ACCOUNT_MANAGER_SSO \
  (mcd_account_manager_sso_get_type ())

#define MCD_ACCOUNT_MANAGER_SSO(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), MCD_TYPE_ACCOUNT_MANAGER_SSO,   \
      McdAccountManagerSso))

#define MCD_ACCOUNT_MANAGER_SSO_CLASS(k)     \
    (G_TYPE_CHECK_CLASS_CAST((k), MCD_TYPE_ACCOUNT_MANAGER_SSO, \
        McdAccountManagerSsoClass))

#define MCD_IS_ACCOUNT_MANAGER_SSO(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), MCD_TYPE_ACCOUNT_MANAGER_SSO))

#define MCD_IS_ACCOUNT_MANAGER_SSO_CLASS(k)  \
  (G_TYPE_CHECK_CLASS_TYPE ((k), MCD_TYPE_ACCOUNT_MANAGER_SSO))

#define MCD_ACCOUNT_MANAGER_SSO_GET_CLASS(o) \
    (G_TYPE_INSTANCE_GET_CLASS ((o), MCD_TYPE_ACCOUNT_MANAGER_SSO, \
        McdAccountManagerSsoClass))

typedef struct {
  GObject parent;
  GHashTable *accounts;
  GHashTable *id_name_map;
  GHashTable *watches;
  GQueue *pending_signals;
  AgManager *ag_manager;
  McpAccountManager *manager_interface;
  gboolean ready;
  gboolean save;
  gboolean loaded;
  guint commit_source;
} _McdAccountManagerSso;

typedef struct {
  GObjectClass parent_class;

  /* In the libaccounts model, each account has a number of associated
   * 'services'; for example, you might have a Google account with Google Talk,
   * Google Mail, Google Calendar, etc. services. Each service is of a
   * particular service type; for instance, the service named "google-talk" is
   * of type "IM".
   *
   * Typically we care about the "IM" service type for Telepathy purposes; but
   * we allow for the possibility of a subclass which cares about some other
   * service type.
   */
  const gchar *service_type;
} _McdAccountManagerSsoClass;

typedef _McdAccountManagerSso McdAccountManagerSso;
typedef _McdAccountManagerSsoClass McdAccountManagerSsoClass;

GType mcd_account_manager_sso_get_type (void) G_GNUC_CONST;

McdAccountManagerSso *mcd_account_manager_sso_new (void);

/* FIXME: we shouldn't need to expose this. Subclasses should be able to chain
 * up to the parent class's implementation of the interface method, but they
 * can't because McpAccountStorageIface isn't exposed. See
 * <https://bugs.freedesktop.org//show_bug.cgi?id=32914>.
 */
gboolean _mcd_account_manager_sso_get (
    const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *account_suffix,
    const gchar *key);

G_END_DECLS

#endif
