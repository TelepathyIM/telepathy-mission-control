/*
 * Regression test for keyfile (un)escaping
 *
 * Copyright © 2012 Collabora Ltd.
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

#include "config.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "mcd-storage.h"

typedef enum {
    FAILS = 1,
    NOT_NORMALIZED = 2
} Flags;

#define SIMPLE_TEST(NAME, GTYPE, CMP, GET) \
static void \
test_ ## NAME (void) \
{ \
  guint i; \
\
  for (i = 0; NAME ## _tests[i].escaped != NULL; i++) \
    { \
      gboolean success; \
      gchar *escaped; \
      GValue unescaped = G_VALUE_INIT; \
      GError *error = NULL; \
\
      g_value_init (&unescaped, GTYPE); \
\
      success = mcd_keyfile_unescape_value (NAME ## _tests[i].escaped, \
          &unescaped, &error); \
\
      if (NAME ## _tests[i].flags & FAILS) \
        { \
          if (success || error == NULL) \
            g_error ("Interpreting '%s' as %s was meant to fail", \
                NAME ## _tests[i].escaped, g_type_name (GTYPE)); \
\
          g_error_free (error); \
        } \
      else \
        { \
          if (error != NULL) \
            g_error ("Interpreting '%s' as %s was meant to succeed: %s", \
                NAME ## _tests[i].escaped, g_type_name (GTYPE), error->message); \
\
          if (!success) \
            g_error ("Interpreting '%s' as %s was meant to succeed", \
                NAME ## _tests[i].escaped, g_type_name (GTYPE)); \
\
          g_assert (success); \
          CMP (GET (&unescaped), ==, \
              NAME ## _tests[i].unescaped); \
\
          escaped = mcd_keyfile_escape_value (&unescaped); \
\
          if (NAME ## _tests[i].flags & NOT_NORMALIZED) \
            g_assert (escaped != NULL); \
          else \
            g_assert_cmpstr (escaped, ==, NAME ## _tests[i].escaped); \
\
          g_free (escaped); \
        } \
\
      g_value_unset (&unescaped); \
    } \
}

struct {
    const gchar *escaped;
    gint32 unescaped;
    Flags flags;
} int32_tests[] = {
    { "-2147483649", 0, FAILS },
    { "-2147483648", G_MININT32, 0 },
    { "-2147483647", -2147483647, 0 },
    { "-1", -1, 0 },
    { "x", 0, FAILS },
    { "0", 0, 0 },
    { "000", 0, NOT_NORMALIZED },
    { "1", 1, 0 },
    { "001", 1, NOT_NORMALIZED },
    { "042", 42, NOT_NORMALIZED },
    { "2147483647", 2147483647, 0 },
    { "2147483648", 0, FAILS },
    { NULL, 0, 0 }
};

struct {
    const gchar *escaped;
    guint32 unescaped;
    Flags flags;
} uint32_tests[] = {
    { "-1", 0, FAILS },
    { "x", 0, FAILS },
    { "0", 0, 0 },
    { "000", 0, NOT_NORMALIZED },
    { "1", 1, 0 },
    { "001", 1, NOT_NORMALIZED },
    { "042", 42, NOT_NORMALIZED },
    { "2147483647", 2147483647, 0 },
    { "2147483648", 2147483648u, 0 },
    { "4294967295", 4294967295u, 0 },
    { "4294967296", 0, FAILS },
    { NULL, 0, 0 }
};

struct {
    const gchar *escaped;
    gint64 unescaped;
    Flags flags;
} int64_tests[] = {
#if 0
    /* This actually "succeeds". tp_g_key_file_get_int64() and _uint64(),
     * and the copy of them that was merged in GLib, don't detect overflow */
    { "-9223372036854775809", 0, FAILS },
#endif
    { "-9223372036854775808", G_MININT64, 0 },
    { "-1", -1, 0 },
    { "0", 0, 0 },
    { "1", 1, 0 },
    { "9223372036854775807", G_GINT64_CONSTANT (9223372036854775807), 0 },
#if 0
    { "9223372036854775808", 0, FAILS },
#endif
    { "x", 0, FAILS },
    { NULL, 0, 0 }
};

struct {
    const gchar *escaped;
    guint64 unescaped;
    Flags flags;
} uint64_tests[] = {
#if 0
    { "-1", 0, FAILS },
#endif
    { "0", 0, 0 },
    { "1", 1, 0 },
    { "9223372036854775807", G_GUINT64_CONSTANT (9223372036854775807), 0 },
    { "9223372036854775808", G_GUINT64_CONSTANT (9223372036854775808), 0 },
    { "18446744073709551615", G_GUINT64_CONSTANT (18446744073709551615), 0 },
#if 0
    { "18446744073709551616", 0, FAILS },
#endif
    { "x", 0, FAILS },
    { NULL, 0, 0 }
};

struct {
    const gchar *escaped;
    guchar unescaped;
    Flags flags;
} byte_tests[] = {
    { "-1", 0, FAILS },
    { "x", 0, FAILS },
    { "0", 0, 0 },
    { "1", 1, 0 },
    { "255", 255, 0 },
    { "256", 0, FAILS },
    { NULL, 0, 0 }
};

struct {
    const gchar *escaped;
    gboolean unescaped;
    Flags flags;
} boolean_tests[] = {
    { "true", TRUE, 0 },
    { "false", FALSE, 0 },
    { "0", FALSE, NOT_NORMALIZED },
    { "1", TRUE, NOT_NORMALIZED },
    { "2", 0, FAILS },
    { "", 0, FAILS },
    { NULL, 0, 0 }
};

struct {
    const gchar *escaped;
    const gchar *unescaped;
    Flags flags;
} string_tests[] = {
    { "lol", "lol", 0 },
    { "\\s", " ", 0 },
    { "\\s ", "  ", NOT_NORMALIZED },
    { "\\t", "\t", 0 },
    { NULL, NULL, 0 }
};

struct {
    const gchar *escaped;
    const gchar *unescaped;
    Flags flags;
} path_tests[] = {
    { "/", "/", 0 },
    { "/foo", "/foo", 0 },
    { "x", NULL, FAILS },
    { NULL, NULL, 0 }
};

struct {
    const gchar *escaped;
    double unescaped;
    Flags flags;
} double_tests[] = {
    { "0", 0.0, 0 },
    { "0.5", 0.5, 0 },
    { "x", 0.0, FAILS },
    { NULL, 0.0, 0 }
};

SIMPLE_TEST (int32, G_TYPE_INT, g_assert_cmpint, g_value_get_int)
SIMPLE_TEST (uint32, G_TYPE_UINT, g_assert_cmpuint, g_value_get_uint)
SIMPLE_TEST (int64, G_TYPE_INT64, g_assert_cmpint, g_value_get_int64)
SIMPLE_TEST (uint64, G_TYPE_UINT64, g_assert_cmpuint, g_value_get_uint64)
SIMPLE_TEST (byte, G_TYPE_UCHAR, g_assert_cmpuint, g_value_get_uchar)
SIMPLE_TEST (boolean, G_TYPE_BOOLEAN, g_assert_cmpuint, g_value_get_boolean)
SIMPLE_TEST (string, G_TYPE_STRING, g_assert_cmpstr, g_value_get_string)
SIMPLE_TEST (path, DBUS_TYPE_G_OBJECT_PATH, g_assert_cmpstr, g_value_get_boxed)
SIMPLE_TEST (double, G_TYPE_DOUBLE, g_assert_cmpfloat, g_value_get_double)

static void
test_strv (void)
{
  gboolean success;
  gchar *escaped;
  GValue unescaped = G_VALUE_INIT;
  GError *error = NULL;
  gchar **unescaped_strv;

  g_value_init (&unescaped, G_TYPE_STRV);

  success = mcd_keyfile_unescape_value ("x;\\t;z;", &unescaped, &error);

  g_assert_no_error (error);
  g_assert (success);
  unescaped_strv = g_value_get_boxed (&unescaped);
  g_assert_cmpstr (unescaped_strv[0], ==, "x");
  g_assert_cmpstr (unescaped_strv[1], ==, "\t");
  g_assert_cmpstr (unescaped_strv[2], ==, "z");
  g_assert_cmpstr (unescaped_strv[3], ==, NULL);

  escaped = mcd_keyfile_escape_value (&unescaped);
  g_assert_cmpstr (escaped, ==, "x;\\t;z;");
  g_free (escaped);

  g_value_unset (&unescaped);
}

static void
test_ao (void)
{
  gboolean success;
  gchar *escaped;
  GValue unescaped = G_VALUE_INIT;
  GError *error = NULL;
  GPtrArray *unescaped_pa;

  g_value_init (&unescaped, TP_ARRAY_TYPE_OBJECT_PATH_LIST);

  success = mcd_keyfile_unescape_value ("/x;/;", &unescaped, &error);

  g_assert_no_error (error);
  g_assert (success);
  unescaped_pa = g_value_get_boxed (&unescaped);
  g_assert_cmpuint (unescaped_pa->len, ==, 2);
  g_assert_cmpstr (g_ptr_array_index (unescaped_pa, 0), ==, "/x");
  g_assert_cmpstr (g_ptr_array_index (unescaped_pa, 1), ==, "/");

  escaped = mcd_keyfile_escape_value (&unescaped);
  g_assert_cmpstr (escaped, ==, "/x;/;");
  g_free (escaped);

  g_value_unset (&unescaped);
}

static void
test_uss (void)
{
  gboolean success;
  gchar *escaped;
  GValue unescaped = G_VALUE_INIT;
  GError *error = NULL;
  GValueArray *unescaped_va;

  g_value_init (&unescaped, TP_STRUCT_TYPE_SIMPLE_PRESENCE);

  success = mcd_keyfile_unescape_value ("2;available;\\;;",
      &unescaped, &error);

  g_assert_no_error (error);
  g_assert (success);
  unescaped_va = g_value_get_boxed (&unescaped);
  g_assert_cmpuint (g_value_get_uint (unescaped_va->values + 0), ==, 2);
  g_assert_cmpstr (g_value_get_string (unescaped_va->values + 1), ==,
        "available");
  g_assert_cmpstr (g_value_get_string (unescaped_va->values + 2), ==,
        ";");

  escaped = mcd_keyfile_escape_value (&unescaped);
  g_assert_cmpstr (escaped, ==, "2;available;\\;;");
  g_free (escaped);

  g_value_unset (&unescaped);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("http://bugs.freedesktop.org/show_bug.cgi?id=");

  g_type_init ();

  g_test_add_func ("/keyfile/int32", test_int32);
  g_test_add_func ("/keyfile/uint32", test_uint32);
  g_test_add_func ("/keyfile/int64", test_int64);
  g_test_add_func ("/keyfile/uint64", test_uint64);
  g_test_add_func ("/keyfile/string", test_string);
  g_test_add_func ("/keyfile/byte", test_byte);
  g_test_add_func ("/keyfile/boolean", test_boolean);
  g_test_add_func ("/keyfile/double", test_double);
  g_test_add_func ("/keyfile/path", test_path);

  g_test_add_func ("/keyfile/strv", test_strv);
  g_test_add_func ("/keyfile/ao", test_ao);
  g_test_add_func ("/keyfile/uss", test_uss);

  return g_test_run ();
}
