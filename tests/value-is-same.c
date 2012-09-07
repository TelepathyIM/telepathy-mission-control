/* vi: set et sw=4 ts=8 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 8 -*- */
/*
 * Regression test for value_is_same()
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009 Collabora Ltd.
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

/* Yes, this is a hack */
#include "mcd-account.c"

static inline void
assert_and_unset (GValue *value, GValue *same, GValue *different)
{
  g_assert (value_is_same (value, same));
  g_assert (value_is_same (same, value));
  g_assert (value_is_same (value, value));
  g_assert (!value_is_same (value, different));
  g_assert (!value_is_same (different, value));

  g_value_unset (value);
  g_value_unset (same);
  g_value_unset (different);
}

static inline void
assert_and_reset (GValue *value, GValue *same, GValue *different)
{
  g_assert (value_is_same (value, same));
  g_assert (value_is_same (same, value));
  g_assert (value_is_same (value, value));
  g_assert (!value_is_same (value, different));
  g_assert (!value_is_same (different, value));

  g_value_reset (value);
  g_value_reset (same);
  g_value_reset (different);
}

static void
test_numeric (void)
{
  GValue value = G_VALUE_INIT;
  GValue same = G_VALUE_INIT;
  GValue different = G_VALUE_INIT;

  g_value_set_int (g_value_init (&value, G_TYPE_INT), -42);
  g_value_set_int (g_value_init (&same, G_TYPE_INT), -42);
  g_value_set_int (g_value_init (&different, G_TYPE_INT), -23);
  assert_and_unset (&value, &same, &different);

  g_value_set_uint (g_value_init (&value, G_TYPE_UINT), 42);
  g_value_set_uint (g_value_init (&same, G_TYPE_UINT), 42);
  g_value_set_uint (g_value_init (&different, G_TYPE_UINT), 23);
  assert_and_unset (&value, &same, &different);

  g_value_set_int64 (g_value_init (&value, G_TYPE_INT64), -42);
  g_value_set_int64 (g_value_init (&same, G_TYPE_INT64), -42);
  g_value_set_int64 (g_value_init (&different, G_TYPE_INT64), -23);
  assert_and_unset (&value, &same, &different);

  g_value_set_uint64 (g_value_init (&value, G_TYPE_UINT64), -42);
  g_value_set_uint64 (g_value_init (&same, G_TYPE_UINT64), -42);
  g_value_set_uint64 (g_value_init (&different, G_TYPE_UINT64), -23);
  assert_and_unset (&value, &same, &different);

  g_value_set_uchar (g_value_init (&value, G_TYPE_UCHAR), 42);
  g_value_set_uchar (g_value_init (&same, G_TYPE_UCHAR), 42);
  g_value_set_uchar (g_value_init (&different, G_TYPE_UCHAR), 23);
  assert_and_unset (&value, &same, &different);

  g_value_set_double (g_value_init (&value, G_TYPE_DOUBLE), 4.5);
  g_value_set_double (g_value_init (&same, G_TYPE_DOUBLE), 4.5);
  g_value_set_double (g_value_init (&different, G_TYPE_DOUBLE), -1.25);
  assert_and_unset (&value, &same, &different);

  g_value_set_boolean (g_value_init (&value, G_TYPE_BOOLEAN), TRUE);
  g_value_set_boolean (g_value_init (&same, G_TYPE_BOOLEAN), TRUE);
  g_value_set_boolean (g_value_init (&different, G_TYPE_BOOLEAN), FALSE);
  assert_and_unset (&value, &same, &different);

  g_value_set_boolean (g_value_init (&value, G_TYPE_BOOLEAN), FALSE);
  g_value_set_boolean (g_value_init (&same, G_TYPE_BOOLEAN), FALSE);
  g_value_set_boolean (g_value_init (&different, G_TYPE_BOOLEAN), TRUE);
  assert_and_unset (&value, &same, &different);
}

static void
test_string (void)
{
  GValue value = G_VALUE_INIT;
  GValue same = G_VALUE_INIT;
  GValue different = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_STRING);
  g_value_init (&same, G_TYPE_STRING);
  g_value_init (&different, G_TYPE_STRING);

  g_value_set_static_string (&value, NULL);
  g_value_set_static_string (&same, NULL);
  g_value_set_static_string (&different, "");
  assert_and_reset (&value, &same, &different);

  g_value_set_static_string (&value, "");
  g_value_set_static_string (&same, "");
  g_value_set_static_string (&different, NULL);
  assert_and_reset (&value, &same, &different);

  g_value_set_static_string (&value, "foo");
  g_value_take_string (&same, g_strdup ("foo"));
  g_value_set_static_string (&different, "bar");
  assert_and_reset (&value, &same, &different);

  g_value_unset (&value);
  g_value_unset (&same);
  g_value_unset (&different);
}

static void
test_object_path (void)
{
  GValue value = G_VALUE_INIT;
  GValue same = G_VALUE_INIT;
  GValue different = G_VALUE_INIT;

  g_value_init (&value, DBUS_TYPE_G_OBJECT_PATH);
  g_value_init (&same, DBUS_TYPE_G_OBJECT_PATH);
  g_value_init (&different, DBUS_TYPE_G_OBJECT_PATH);

  g_value_set_static_boxed (&value, "/foo");
  g_value_take_boxed (&same, g_strdup ("/foo"));
  g_value_set_static_boxed (&different, "/bar");
  assert_and_reset (&value, &same, &different);

  g_value_unset (&value);
  g_value_unset (&same);
  g_value_unset (&different);
}

static void
test_strv (void)
{
  const gchar * const empty[] = { NULL };
  const gchar * const small[] = { "foo", "bar", NULL };
  const gchar * const large[] = { "foo", "bar", "baz", NULL };
  GValue value = G_VALUE_INIT;
  GValue same = G_VALUE_INIT;
  GValue different = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_STRV);
  g_value_init (&same, G_TYPE_STRV);
  g_value_init (&different, G_TYPE_STRV);

  g_value_set_static_boxed (&value, (GStrv) small);
  g_value_take_boxed (&same, g_strdupv ((GStrv) small));
  g_value_set_static_boxed (&different, (GStrv) large);
  assert_and_reset (&value, &same, &different);

  g_value_set_static_boxed (&value, (GStrv) large);
  g_value_take_boxed (&same, g_strdupv ((GStrv) large));
  g_value_set_static_boxed (&different, (GStrv) small);
  assert_and_reset (&value, &same, &different);

  g_value_set_static_boxed (&value, NULL);
  g_value_set_static_boxed (&same, (GStrv) empty);
  g_value_set_static_boxed (&different, (GStrv) small);
  assert_and_reset (&value, &same, &different);

  g_value_set_static_boxed (&value, (GStrv) empty);
  g_value_set_static_boxed (&same, NULL);
  g_value_set_static_boxed (&different, (GStrv) large);
  assert_and_reset (&value, &same, &different);

  g_value_unset (&value);
  g_value_unset (&same);
  g_value_unset (&different);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("http://bugs.freedesktop.org/show_bug.cgi?id=");

  g_type_init ();

  g_test_add_func ("/value-is-same/numeric", test_numeric);
  g_test_add_func ("/value-is-same/string", test_string);
  g_test_add_func ("/value-is-same/object-path", test_object_path);
  g_test_add_func ("/value-is-same/strv", test_strv);

  return g_test_run ();
}
