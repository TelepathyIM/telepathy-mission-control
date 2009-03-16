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
#include <errno.h>
#define __USE_POSIX
#include <glib/gstdio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/util.h>
#include <libmcclient/mc-errors.h>

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
        g_clear_error (&error);
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

GHashTable *
_mcd_deepcopy_asv (GHashTable *asv)
{
    GHashTable *copy;

    copy = g_hash_table_new_full (g_str_hash, g_str_equal,
                                  g_free,
                                  (GDestroyNotify)tp_g_value_slice_free);
    tp_g_hash_table_update (copy, asv, (GBoxedCopyFunc) g_strdup,
                            (GBoxedCopyFunc) tp_g_value_slice_dup);
    return copy;
}

gchar *
_mcd_build_error_string (const GError *error)
{
    GEnumValue *value;
    GEnumClass *klass;
    const gchar *prefix;

    if (error->domain == TP_ERRORS)
    {
        klass = g_type_class_ref (TP_TYPE_ERROR);
        prefix = TP_ERROR_PREFIX;
    }
    else if (error->domain == MC_ERROR)
    {
        klass = g_type_class_ref (MC_TYPE_ERROR);
        prefix = MC_ERROR_PREFIX;
    }
    else
        return NULL;
    value = g_enum_get_value (klass, error->code);
    g_type_class_unref (klass);

    if (G_LIKELY (value && value->value_nick))
        return g_strconcat (prefix, ".", value->value_nick, NULL);
    else
        return NULL;
}

GType
_mcd_type_dbus_ao (void)
{
  static GType t = 0;

  if (G_UNLIKELY (t == 0))
    t = dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH);
  return t;
}

typedef struct
{
    McdReadyCb callback;
    gpointer user_data;
} McdReadyCbData;

typedef struct
{
    gpointer strukt;
    GSList *callbacks;
} McdReadyData;

static void
mcd_object_invoke_ready_callbacks (McdReadyData *rd, const GError *error)
{
    GSList *list;

    for (list = rd->callbacks; list != NULL; list = list->next)
    {
        McdReadyCbData *cb = list->data;

        cb->callback (rd->strukt, error, cb->user_data);
        g_slice_free (McdReadyCbData, cb);
    }
    g_slist_free (rd->callbacks);
}

static void
mcd_ready_data_free (McdReadyData *rd)
{
    if (rd->strukt)
    {
        GError error = { TP_ERRORS, TP_ERROR_CANCELLED, "Object disposed" };
        mcd_object_invoke_ready_callbacks (rd, &error);
    }
    g_slice_free (McdReadyData, rd);
}

void
mcd_object_call_when_ready (gpointer object, GQuark quark, McdReadyCb callback,
                            gpointer user_data)
{
    mcd_object_call_on_struct_when_ready (object, object, quark, callback,
                                          user_data);
}

void
mcd_object_call_on_struct_when_ready (gpointer object, gpointer strukt,
                                      GQuark quark, McdReadyCb callback,
                                      gpointer user_data)
{
    McdReadyData *rd;
    McdReadyCbData *cb;

    g_return_if_fail (G_IS_OBJECT (object));
    g_return_if_fail (quark != 0);
    g_return_if_fail (callback != NULL);

    cb = g_slice_new (McdReadyCbData);
    cb->callback = callback;
    cb->user_data = user_data;

    rd = g_object_get_qdata ((GObject *)object, quark);
    if (!rd)
    {
        rd = g_slice_new (McdReadyData);
        rd->strukt = strukt;
        rd->callbacks = NULL;
        g_object_set_qdata_full ((GObject *)object, quark, rd,
                                 (GDestroyNotify)mcd_ready_data_free);
    }
    rd->callbacks = g_slist_prepend (rd->callbacks, cb);
}

void
mcd_object_ready (gpointer object, GQuark quark, const GError *error)
{
    McdReadyData *rd;

    rd = g_object_get_qdata ((GObject *)object, quark);
    if (!rd) return;

    mcd_object_invoke_ready_callbacks (rd, error);
    rd->strukt = NULL; /* so the callbacks won't be invoked again */
    g_object_set_qdata ((GObject *)object, quark, NULL);
}


static gint
create_temp_file (gchar *tmpl, 
                  int    permissions)
{
    char *XXXXXX;
    int count, fd;
    static const char letters[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static const int NLETTERS = sizeof (letters) - 1;
    glong value;
    GTimeVal tv;
    static int counter = 0;

    /* find the last occurrence of "XXXXXX" */
    XXXXXX = g_strrstr (tmpl, "XXXXXX");

    if (!XXXXXX || strncmp (XXXXXX, "XXXXXX", 6))
    {
        errno = EINVAL;
        return -1;
    }

    /* Get some more or less random data.  */
    g_get_current_time (&tv);
    value = (tv.tv_usec ^ tv.tv_sec) + counter++;

    for (count = 0; count < 100; value += 7777, ++count)
    {
        glong v = value;

        /* Fill in the random bits.  */
        XXXXXX[0] = letters[v % NLETTERS];
        v /= NLETTERS;
        XXXXXX[1] = letters[v % NLETTERS];
        v /= NLETTERS;
        XXXXXX[2] = letters[v % NLETTERS];
        v /= NLETTERS;
        XXXXXX[3] = letters[v % NLETTERS];
        v /= NLETTERS;
        XXXXXX[4] = letters[v % NLETTERS];
        v /= NLETTERS;
        XXXXXX[5] = letters[v % NLETTERS];

#ifndef O_BINARY
#define O_BINARY 0
#endif

        /* tmpl is in UTF-8 on Windows, thus use g_open() */
        fd = g_open (tmpl, O_RDWR | O_CREAT | O_EXCL | O_BINARY, permissions);

        if (fd >= 0)
            return fd;
        else if (errno != EEXIST)
            /* Any other error will apply also to other names we might
             *  try, and there are 2^32 or so of them, so give up now.
             */
            return -1;
    }

    /* We got out of the loop because we ran out of combinations to try.  */
    errno = EEXIST;
    return -1;
}

static gboolean
rename_file (const char *old_name,
             const char *new_name,
             GError **err)
{
    errno = 0;
    if (g_rename (old_name, new_name) == -1)
    {
        int save_errno = errno;
        gchar *display_old_name = g_filename_display_name (old_name);
        gchar *display_new_name = g_filename_display_name (new_name);

        g_set_error (err,
                     G_FILE_ERROR,
                     g_file_error_from_errno (save_errno),
                     "Failed to rename file '%s' to '%s': g_rename() failed: %s",
                     display_old_name,
                     display_new_name,
                     g_strerror (save_errno));

        g_free (display_old_name);
        g_free (display_new_name);

        return FALSE;
    }

    return TRUE;
}

static gchar *
write_to_temp_file (const gchar *contents,
                    gssize length,
                    const gchar *template,
                    GError **err)
{
    gchar *tmp_name;
    gchar *display_name;
    gchar *retval;
    FILE *file;
    gint fd;
    int save_errno;

    retval = NULL;

    tmp_name = g_strdup_printf ("%s.XXXXXX", template);

    errno = 0;
    fd = create_temp_file (tmp_name, 0666);
    save_errno = errno;

    display_name = g_filename_display_name (tmp_name);

    if (fd == -1)
    {
        g_set_error (err,
                     G_FILE_ERROR,
                     g_file_error_from_errno (save_errno),
                     "Failed to create file '%s': %s",
                     display_name, g_strerror (save_errno));

        goto out;
    }

    errno = 0;
    file = fdopen (fd, "wb");
    if (!file)
    {
        save_errno = errno;
        g_set_error (err,
                     G_FILE_ERROR,
                     g_file_error_from_errno (save_errno),
                     "Failed to open file '%s' for writing: fdopen() failed: %s",
                     display_name,
                     g_strerror (save_errno));

        close (fd);
        g_unlink (tmp_name);

        goto out;
    }

    if (length > 0)
    {
        gsize n_written;

        errno = 0;

        n_written = fwrite (contents, 1, length, file);

        if (n_written < length)
        {
            save_errno = errno;

            g_set_error (err,
                         G_FILE_ERROR,
                         g_file_error_from_errno (save_errno),
                         "Failed to write file '%s': fwrite() failed: %s",
                         display_name,
                         g_strerror (save_errno));

            fclose (file);
            g_unlink (tmp_name);

            goto out;
        }
    }

    errno = 0;
    if (fclose (file) == EOF)
    { 
        save_errno = 0;

        g_set_error (err,
                     G_FILE_ERROR,
                     g_file_error_from_errno (save_errno),
                     "Failed to close file '%s': fclose() failed: %s",
                     display_name, 
                     g_strerror (save_errno));

        g_unlink (tmp_name);

        goto out;
    }

    retval = g_strdup (tmp_name);

out:
    g_free (tmp_name);
    g_free (display_name);

    return retval;
}

gboolean
_mcd_file_set_contents (const gchar *filename, const gchar *contents,
                        gssize length, GError **error)
{
    gchar *tmp_filename;
    gboolean retval;
    GError *rename_error = NULL;

    g_return_val_if_fail (filename != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
    g_return_val_if_fail (contents != NULL || length == 0, FALSE);
    g_return_val_if_fail (length >= -1, FALSE);

    if (length == -1)
        length = strlen (contents);

    tmp_filename = write_to_temp_file (contents, length, filename, error);

    if (!tmp_filename)
    {
        retval = FALSE;
        goto out;
    }

    if (!rename_file (tmp_filename, filename, &rename_error))
    {
        g_unlink (tmp_filename);
        g_propagate_error (error, rename_error);
        retval = FALSE;
        goto out;
    }

    retval = TRUE;

out:
    g_free (tmp_filename);
    return retval;
}

