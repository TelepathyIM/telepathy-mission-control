/* Representation of a channel request as presented to plugins. This is
 * deliberately a "smaller" API than McdChannel.
 *
 * Copyright © 2009 Nokia Corporation.
 * Copyright © 2009-2010 Collabora Ltd.
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

#ifndef MCD_PLUGIN_REQUEST_H
#define MCD_PLUGIN_REQUEST_H

#include <mission-control-plugins/mission-control-plugins.h>

#include "mcd-account.h"
#include "request.h"

G_BEGIN_DECLS

typedef struct _McdPluginRequest McdPluginRequest;
typedef struct _McdPluginRequestClass McdPluginRequestClass;
typedef struct _McdPluginRequestPrivate McdPluginRequestPrivate;

G_GNUC_INTERNAL GType _mcd_plugin_request_get_type (void);

#define MCD_TYPE_PLUGIN_REQUEST \
  (_mcd_plugin_request_get_type ())
#define MCD_PLUGIN_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MCD_TYPE_PLUGIN_REQUEST, \
                               McdPluginRequest))
#define MCD_PLUGIN_REQUEST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), MCD_TYPE_PLUGIN_REQUEST, \
                            McdPluginRequestClass))
#define MCD_IS_PLUGIN_REQUEST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCD_TYPE_PLUGIN_REQUEST))
#define MCD_IS_PLUGIN_REQUEST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), MCD_TYPE_PLUGIN_REQUEST))
#define MCD_PLUGIN_REQUEST_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_PLUGIN_REQUEST, \
                              McdPluginRequestClass))

G_GNUC_INTERNAL McdPluginRequest *_mcd_plugin_request_new (McdAccount *account,
    McdRequest *real_request);

G_END_DECLS

#endif
