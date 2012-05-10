/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2007-2009 Nokia Corporation.
 * Copyright (C) 2008 Collabora Ltd.
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

/**
 * SECTION:mcd-debug
 * @title: Debugging
 * @short_description: Debugging utilities
 * @see_also: 
 * @stability: Unstable
 * @include: mcd-debug.h
 * 
 * FIXME
 */

#include <config.h>

#include <stdlib.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <mission-control-plugins/mission-control-plugins.h>

#include "mcd-debug.h"
#include "mcd-operation.h"

gint mcd_debug_level = 0;

static void
mcd_debug_print_tree_real (gpointer object, gint level)
{
    GString *indent_str;
    gchar *indent = "    ";
    gint i;
    
    indent_str = g_string_new ("");
    
    for (i = 0; i < level; i++)
    {
	g_string_append (indent_str, indent);
    }
    
    g_debug ("%s%s (%p): %d", indent_str->str,
	     G_OBJECT_TYPE_NAME(object), object, G_OBJECT (object)->ref_count);
    
    if (MCD_IS_OPERATION (object))
    {
	const GList *missions = mcd_operation_get_missions (MCD_OPERATION (object));
	const GList *node = missions;
	while (node)
	{
	    mcd_debug_print_tree_real (node->data, level + 1);
	    node = g_list_next (node);
	}
    }
    g_string_free (indent_str, TRUE);
}

/* We don't really have debug categories yet */

typedef enum {
    MCD_DEBUG_MISC = 1 << 0,
    MCD_DEBUG_TREES = 1 << 1
} McdDebugCategory;

static GDebugKey const keys[] = {
    { "misc", MCD_DEBUG_MISC },
    { "trees", MCD_DEBUG_TREES },
    { NULL, 0 }
};

static McdDebugCategory categories = 0;

void
mcd_debug_print_tree (gpointer object)
{
    g_return_if_fail (MCD_IS_MISSION (object));

    if (categories & MCD_DEBUG_TREES)
    {
	g_debug ("Object Hierarchy of object %p", object);
	g_debug ("[");
	mcd_debug_print_tree_real (object, 1);
	g_debug ("]");
    }
}

void mcd_debug_init ()
{
    gchar *mc_debug_str;
    guint level;

    mc_debug_str = getenv ("MC_DEBUG");

    if (mc_debug_str)
    {
        /* historically, MC_DEBUG was an integer; try that first */
        level = atoi (mc_debug_str);

        /* if it wasn't an integer; try interpreting it as a
         * telepathy-glib-style flags-word */
        if (level == 0)
        {
            categories = g_parse_debug_string (mc_debug_str, keys,
                                               G_N_ELEMENTS (keys) - 1);
            tp_debug_set_flags (mc_debug_str);

            /* mcd-debug.h uses the value of mcd_debug_level directly, so
             * we need to set it nonzero to get uncategorized messages */
            if ((categories & MCD_DEBUG_MISC) != 0 && mcd_debug_level == 0)
            {
                mcd_debug_level = 1;
            }
        }
        else
        {
            /* this is API, and will also try to set up categories from the
             * level */
            mcd_debug_set_level (level);
        }
    }

    mcp_set_debug ((mcd_debug_level >= 1));
    mcp_debug_init ();

    tp_debug_divert_messages (g_getenv ("MC_LOGFILE"));

    if (mcd_debug_level >= 1)
        g_debug ("%s version %s", PACKAGE, VERSION);
}

void
mcd_debug_set_level (gint level)
{
    mcd_debug_level = level;

    mcp_set_debug ((mcd_debug_level >= 1));

    if (level >= 1)
    {
        categories |= MCD_DEBUG_MISC;
    }
    else
    {
        categories = 0;
    }

    if (level >= 2)
    {
        categories |= MCD_DEBUG_TREES;
    }
}

void
mcd_debug (const gchar *format, ...)
{
  gchar *message = NULL;
  gchar **formatted = NULL;
  TpDebugSender *dbg = tp_debug_sender_dup ();
  va_list args;

  if (_mcd_debug_get_level () > 0)
    formatted = &message;

  va_start (args, format);
  tp_debug_sender_add_message_vprintf (dbg, NULL, formatted,
      G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
  va_end (args);

  if (!tp_str_empty (message))
    {
      g_debug ("%s", message);
      g_free (message);
    }

  /* NOTE: the sender must be cached elsewhere, or this gets EXPENSIVE: */
  g_object_unref (dbg);
}
