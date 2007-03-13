/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007 Nokia Corporation. 
 *
 * Contact: Naba Kumar  <naba.kumar@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
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

#ifndef __MC_PROTOCOL_H__
#define __MC_PROTOCOL_H__

#include <glib.h>
#include <glib-object.h>

#include <libmissioncontrol/mc-manager.h>

G_BEGIN_DECLS

#define MC_TYPE_PROTOCOL mc_protocol_get_type()

#define MC_PROTOCOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  MC_TYPE_PROTOCOL, McProtocol))

#define MC_PROTOCOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  MC_TYPE_PROTOCOL, McProtocolClass))

#define MC_IS_PROTOCOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  MC_TYPE_PROTOCOL))

#define MC_IS_PROTOCOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  MC_TYPE_PROTOCOL))

#define MC_PROTOCOL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  MC_TYPE_PROTOCOL, McProtocolClass))

GType mc_protocol_get_type (void);

typedef struct {
    GObject parent;
    gpointer priv;
} McProtocol;

typedef struct {
    GObjectClass parent_class;
} McProtocolClass;

/* protocols are only unique within the context of a particular manager */
McProtocol *mc_protocol_lookup (McManager *id, const gchar *protocol);
#ifndef MC_DISABLE_DEPRECATED
void mc_protocol_free (McProtocol *id);
#endif

GList *mc_protocols_list (void);
GList *mc_protocols_list_by_manager (McManager *id);
void mc_protocols_free_list (GList *list);

McManager *mc_protocol_get_manager (McProtocol *id);
const gchar *mc_protocol_get_name (McProtocol *id);

enum
{
  MC_PROTOCOL_PARAM_REQUIRED = 1 << 0,
  MC_PROTOCOL_PARAM_REGISTER = 1 << 1
};

/* return type for params */
typedef struct
{
    const gchar *name;
    const gchar *signature;
    const gchar *def;
    guint flags;
} McProtocolParam;

/* Returns list of McProtocolParam. */
GSList *mc_protocol_get_params (McProtocol *protocol);

/* Frees the lists above and all data */
void mc_protocol_free_params_list (GSList *list);

void mc_protocol_print (McProtocol *protocol);

G_END_DECLS

#endif /* __MC_PROTOCOL_H__ */
