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

#include <gmodule.h>

#include "mc.h"

#define LIBRARY_FILE G_STRINGIFY(LIBDIR) "/libmissioncontrol-config.so." G_STRINGIFY(LIBVERSION)

/**
 * mc_make_resident:
 *
 * This function is a workaround for problems with mc getting loaded twice
 * into the same process, such as when the control panel loads a plugin which
 * uses mc after it has already been loaded and unloaded. In order to
 * prevent g_type_register_static being called twice, this function can be
 * called to make mc be redident in memory for the lifetime of the process.
 */

void
mc_make_resident (void)
{
  GModule *module = g_module_open (LIBRARY_FILE, 0);
  if (NULL == module)
    {
      g_critical("%s: g_module_open() failed: %s", G_STRFUNC, g_module_error());
    }
  g_module_make_resident (module);
}

