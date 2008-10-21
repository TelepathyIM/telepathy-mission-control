/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008 Nokia Corporation. 
 *
 * Contact: Alberto Mardegan  <alberto.mardegan@nokia.com>
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

#include "mcd-misc.h"

/*
 * Miscellaneus functions
 */

static gboolean
scan_data_subdir (const gchar *dirname, McdXdgDataSubdirFunc callback,
                  gpointer user_data)
{
    const gchar *filename;
    GError *error = NULL;
    gboolean proceed = TRUE;
    GDir *dir;

    if (!g_file_test (dirname, G_FILE_TEST_IS_DIR)) return TRUE;

    if ((dir = g_dir_open (dirname, 0, &error)) == NULL)
    {
        g_warning ("Error opening directory %s: %s", dirname,
                   error->message);
    }

    while ((filename = g_dir_read_name (dir)) != NULL)
    {
        gchar *absolute_filepath;

        absolute_filepath = g_build_filename(dirname, filename, NULL);
        proceed = callback (absolute_filepath, filename, user_data);
        g_free(absolute_filepath);
        if (!proceed) break;
    }
    g_dir_close(dir);
    return proceed;
}

/* utility function to scan XDG_DATA_DIRS subdirectories */
void
_mcd_xdg_data_subdir_foreach (const gchar *subdir,
                              McdXdgDataSubdirFunc callback,
                              gpointer user_data)
{
    const gchar *dirname;
    const gchar * const *dirs;
    gboolean proceed = TRUE;
    gchar *dir;

    dirs = g_get_system_data_dirs();
    for (dirname = *dirs; dirname != NULL; dirs++, dirname = *dirs)
    {
        dir = g_build_filename (dirname, subdir, NULL);
        proceed = scan_data_subdir (dir, callback, user_data);
        g_free (dir);
        if (!proceed) break;
    }

    if (proceed)
    {
        dir = g_build_filename (g_get_user_data_dir(), subdir, NULL);
        scan_data_subdir (dir, callback, user_data);
        g_free (dir);
    }
}

void
_mcd_prop_value_free (gpointer data)
{
  GValue *value = (GValue *) data;
  g_value_unset (value);
  g_slice_free (GValue, value);
}

