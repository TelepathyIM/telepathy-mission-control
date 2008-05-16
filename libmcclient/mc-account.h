/*
 * mc-account.h - the Telepathy Account D-Bus interface
 * (client side)
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
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

#ifndef __LIBMCCLIENT_ACCOUNT_H__
#define __LIBMCCLIENT_ACCOUNT_H__

#include <telepathy-glib/proxy.h>

G_BEGIN_DECLS

typedef struct _McAccount McAccount;
typedef struct _McAccountClass McAccountClass;
typedef struct _McAccountPrivate McAccountPrivate;

struct _McAccount {
    TpProxy parent;
    gchar *unique_name;
    gchar *manager_name;
    gchar *protocol_name;
    /*<private>*/
    McAccountPrivate *priv;
};

GType mc_account_get_type (void);

#define MC_TYPE_ACCOUNT (mc_account_get_type ())
#define MC_ACCOUNT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MC_TYPE_ACCOUNT, \
                               McAccount))
#define MC_ACCOUNT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MC_TYPE_ACCOUNT, \
                            McAccountClass))
#define MC_IS_ACCOUNT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MC_TYPE_ACCOUNT))
#define MC_IS_ACCOUNT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MC_TYPE_ACCOUNT))
#define MC_ACCOUNT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MC_TYPE_ACCOUNT, \
                              McAccountClass))

McAccount *mc_account_new (TpDBusDaemon *dbus, const gchar *object_path);

gboolean mc_account_watch_interface (McAccount *account, GQuark interface);
const GValue *mc_account_get_property (McAccount *account, GQuark interface,
				       const gchar *name);
void mc_account_get (McAccount *account, GQuark interface,
		     const gchar *first_prop, ...);

G_END_DECLS

/* auto-generated stubs */
#include <libmcclient/_gen/cli-Account.h>

#endif
