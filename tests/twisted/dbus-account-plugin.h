/*
 * A demonstration plugin that diverts account storage to D-Bus, where the
 * regression tests can manipulate it.
 *
 * Copyright © 2010 Nokia Corporation
 * Copyright © 2010-2012 Collabora Ltd.
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

#ifndef TEST_DBUS_ACCOUNT_PLUGIN_H
#define TEST_DBUS_ACCOUNT_PLUGIN_H

#include <mission-control-plugins/mission-control-plugins.h>

typedef struct _TestDBusAccountPlugin TestDBusAccountPlugin;
typedef struct _TestDBusAccountPluginClass TestDBusAccountPluginClass;
typedef struct _TestDBusAccountPluginPrivate TestDBusAccountPluginPrivate;

GType test_dbus_account_plugin_get_type (void);

#define TEST_TYPE_DBUS_ACCOUNT_PLUGIN \
  (test_dbus_account_plugin_get_type ())
#define TEST_DBUS_ACCOUNT_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TEST_TYPE_DBUS_ACCOUNT_PLUGIN, \
                               TestDBusAccountPlugin))
#define TEST_DBUS_ACCOUNT_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TEST_TYPE_DBUS_ACCOUNT_PLUGIN, \
                            TestDBusAccountPluginClass))
#define TEST_IS_DBUS_ACCOUNT_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TEST_TYPE_DBUS_ACCOUNT_PLUGIN))
#define TEST_IS_DBUS_ACCOUNT_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TEST_TYPE_DBUS_ACCOUNT_PLUGIN))
#define TEST_DBUS_ACCOUNT_PLUGIN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_DBUS_ACCOUNT_PLUGIN, \
                              TestDBusAccountPluginClass))

#endif
