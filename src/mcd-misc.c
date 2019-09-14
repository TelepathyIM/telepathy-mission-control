/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2005-2009 Nokia Corporation.
 * Copyright (C) 2005-2009 Collabora Ltd.
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

#include "config.h"

#include "mcd-misc.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <telepathy-glib/telepathy-glib.h>

#include "mcd-debug.h"

#include "_gen/register-dbus-glib-marshallers-body.h"

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

/*
 * Miscellaneus functions
 */

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

    if (error->domain == TP_ERROR)
    {
        klass = g_type_class_ref (TP_TYPE_ERROR);
        prefix = TP_ERROR_PREFIX;
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
        GError error = { TP_ERROR, TP_ERROR_CANCELLED, "Object disposed" };
        mcd_object_invoke_ready_callbacks (rd, &error);
    }
    g_slice_free (McdReadyData, rd);
}

void
_mcd_object_call_when_ready (gpointer object, GQuark quark, McdReadyCb callback,
                             gpointer user_data)
{
    _mcd_object_call_on_struct_when_ready (object, object, quark, callback,
                                           user_data);
}

void
_mcd_object_call_on_struct_when_ready (gpointer object, gpointer strukt,
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
_mcd_object_ready (gpointer object, GQuark quark, const GError *error)
{
    McdReadyData *rd;

    /* steal the qdata so the callbacks won't be invoked again, even if the
     * object becomes ready or is finalized while still invoking them */
    rd = g_object_steal_qdata ((GObject *)object, quark);
    if (!rd) return;

    g_object_ref (object);

    mcd_object_invoke_ready_callbacks (rd, error);
    rd->strukt = NULL; /* so the callbacks won't be invoked again */
    mcd_ready_data_free (rd);

    g_object_unref (object);
}

gboolean
mcd_ensure_directory (const gchar *dir,
                      GError **error)
{
    DEBUG ("%s", dir);

    if (g_mkdir_with_parents (dir, 0700) != 0)
    {
        g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
                     "Unable to create directory '%s': %s",
                     dir, g_strerror (errno));
        return FALSE;
    }

    return TRUE;
}

int
_mcd_chmod_private (const gchar *filename)
{
    struct stat buf;
    int ret;

    ret = g_stat (filename, &buf);

    if (ret < 0)
    {
        DEBUG ("g_stat(%s): %s", filename, g_strerror (errno));
        return ret;
    }

    if ((buf.st_mode & 0077) != 0)
    {
        DEBUG ("chmod go-rwx %s", filename);
        ret = g_chmod (filename, (buf.st_mode & ~0077));

        if (ret < 0)
        {
            DEBUG ("g_chmod: %s", g_strerror (errno));
        }
    }

    return ret;
}

gboolean
mcd_nullable_variant_equal (GVariant *a,
                            GVariant *b)
{
    if (a == b)
        return TRUE;

    if (a == NULL || b == NULL)
        return FALSE;

    return g_variant_equal (a, b);
}
