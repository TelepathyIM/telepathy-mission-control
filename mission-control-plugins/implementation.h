/* Mission Control plugin API - internals, for MC to use
 *
 * Copyright © 2009-2010 Nokia Corporation
 * Copyright © 2009-2010 Collabora Ltd.
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

#ifndef MCP_IMPLEMENTATION_H
#define MCP_IMPLEMENTATION_H

#include <mission-control-plugins/mission-control-plugins.h>

G_BEGIN_DECLS

struct _McpRequestIface {
    GTypeInterface parent;

    /* Account */
    const gchar * (*get_account_path) (McpRequest *self);
    const gchar * (*get_protocol) (McpRequest *self);
    const gchar * (*get_cm_name) (McpRequest *self);

    gint64 (*get_user_action_time) (McpRequest *self);
    guint (*get_n_requests) (McpRequest *self);
    GHashTable * (*ref_nth_request) (McpRequest *self, guint n);

    void (*deny) (McpRequest *self, GQuark domain, gint code,
        const gchar *message);

    /* Delay the request */
    McpRequestDelay * (*start_delay) (McpRequest *self);
    void (*end_delay) (McpRequest *self,
        McpRequestDelay *delay);
};

struct _McpDispatchOperationIface {
    GTypeInterface parent;

    /* Account and Connection */
    const gchar * (*get_account_path) (McpDispatchOperation *self);
    const gchar * (*get_connection_path) (McpDispatchOperation *self);
    const gchar * (*get_protocol) (McpDispatchOperation *self);
    const gchar * (*get_cm_name) (McpDispatchOperation *self);

    /* Channels */
    guint (*get_n_channels) (McpDispatchOperation *self);
    const gchar * (*get_nth_channel_path) (McpDispatchOperation *self,
        guint n);
    GHashTable * (*ref_nth_channel_properties) (McpDispatchOperation *self,
        guint n);

    /* Delay the dispatch */
    McpDispatchOperationDelay * (*start_delay) (McpDispatchOperation *self);
    void (*end_delay) (McpDispatchOperation *self,
        McpDispatchOperationDelay *delay);

    /* Close */
    void (*leave_channels) (McpDispatchOperation *self,
        gboolean wait_for_observers, TpChannelGroupChangeReason reason,
        const gchar *message);
    void (*close_channels) (McpDispatchOperation *self,
        gboolean wait_for_observers);
    void (*destroy_channels) (McpDispatchOperation *self,
        gboolean wait_for_observers);
};

struct _McpAccountManagerIface {
  GTypeInterface parent;

  void (*set_value) (const McpAccountManager *ma,
      const gchar *acct,
      const gchar *key,
      const gchar *value);

  gchar * (*get_value) (const McpAccountManager *ma,
      const gchar *acct,
      const gchar *key);

  gboolean (*is_secret) (const McpAccountManager *ma,
      const gchar *acct,
      const gchar *key);

  void (* make_secret) (const McpAccountManager *ma,
      const gchar *acct,
      const gchar *key);

  gchar * (* unique_name) (const McpAccountManager *ma,
      const gchar *manager,
      const gchar *protocol,
      const GHashTable *params);

  GStrv (* list_keys) (const McpAccountManager *ma,
      const gchar *acct);

  gchar * (* escape_value_for_keyfile) (const McpAccountManager *mcpa,
      const GValue *value);

  gboolean (* unescape_value_from_keyfile) (const McpAccountManager *mcpa,
      const gchar *escaped,
      GValue *value,
      GError **error);

  gboolean (* init_value_for_attribute) (const McpAccountManager *mcpa,
      GValue *value,
      const gchar *attribute);

  gchar * (* escape_variant_for_keyfile) (const McpAccountManager *mcpa,
      GVariant *variant);

  void (* set_attribute) (const McpAccountManager *mcpa,
      const gchar *account,
      const gchar *attribute,
      GVariant *value,
      McpAttributeFlags flags);

  void (* set_parameter) (const McpAccountManager *mcpa,
      const gchar *account,
      const gchar *parameter,
      GVariant *value,
      McpParameterFlags flags);
};

G_END_DECLS

#endif
