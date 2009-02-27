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

