/*
 * slacker.h - header for McdSlacker
 * Copyright ©2010 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef MCD_SLACKER_H
#define MCD_SLACKER_H

#include <glib-object.h>

typedef struct _McdSlacker McdSlacker;
typedef struct _McdSlackerClass McdSlackerClass;
typedef struct _McdSlackerPrivate McdSlackerPrivate;

struct _McdSlackerClass {
    GObjectClass parent_class;
};

struct _McdSlacker {
    GObject parent;

    McdSlackerPrivate *priv;
};

GType mcd_slacker_get_type (void);

McdSlacker *mcd_slacker_new (void);
gboolean mcd_slacker_is_inactive (McdSlacker *self);

/* TYPE MACROS */
#define MCD_TYPE_SLACKER \
  (mcd_slacker_get_type ())
#define MCD_SLACKER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), MCD_TYPE_SLACKER, McdSlacker))
#define MCD_SLACKER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), MCD_TYPE_SLACKER,\
                           McdSlackerClass))
#define MCD_IS_SLACKER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), MCD_TYPE_SLACKER))
#define MCD_IS_SLACKER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), MCD_TYPE_SLACKER))
#define MCD_SLACKER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_SLACKER, \
                              McdSlackerClass))

#endif /* MCD_SLACKER_H */
