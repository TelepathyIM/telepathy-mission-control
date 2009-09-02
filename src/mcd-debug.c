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
#include "mcd-debug.h"
#include "mcd-operation.h"

gint mcd_debug_level = 0;

gpointer
mcd_debug_ref (gpointer obj, const gchar *filename, gint linenum)
{
    /* Function reference untouchable by macro processing */
    gpointer (*untouchable_ref) (gpointer object);
    
    untouchable_ref = g_object_ref;
    if (mcd_debug_level >= 2)
	g_debug ("[%s:%d]: Referencing (%d) object %p of type %s",
		 filename, linenum, G_OBJECT (obj)->ref_count,
		 obj, G_OBJECT_TYPE_NAME(obj));
    return untouchable_ref (obj);
}

void
mcd_debug_unref (gpointer obj, const gchar *filename, gint linenum)
{
    void (*untouchable_unref) (gpointer object);
    
    untouchable_unref = g_object_unref;
    if (mcd_debug_level >= 2)
	g_debug ("[%s:%d]: Unreferencing (%d) object %p of type %s",
		 filename, linenum, G_OBJECT (obj)->ref_count, obj,
		 G_OBJECT_TYPE_NAME(obj));
    untouchable_unref (obj);
}

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

void
mcd_debug_print_tree (gpointer object)
{
    g_return_if_fail (MCD_IS_MISSION (object));

    if (mcd_debug_level >= 2)
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

    mc_debug_str = getenv ("MC_DEBUG");

    if (mc_debug_str)
	mcd_debug_level = atoi (mc_debug_str);

    if (mcd_debug_level >= 1)
        g_debug ("%s version %s", PACKAGE, VERSION);
}

void
mcd_debug_set_level (gint level)
{
    mcd_debug_level = level;
}

