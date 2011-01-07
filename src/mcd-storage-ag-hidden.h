/*
 * storage-ag-hidden.h - account backend for "magic" hidden accounts using
 *                       accounts-glib
 * Copyright ©2011 Collabora Ltd.
 * Copyright ©2011 Nokia Corporation
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

#ifndef MCD_STORAGE_AG_HIDDEN_H
#define MCD_STORAGE_AG_HIDDEN_H

#include <glib-object.h>
#include "mcd-account-manager-sso.h"

typedef struct _McdStorageAgHidden McdStorageAgHidden;
typedef struct _McdStorageAgHiddenClass McdStorageAgHiddenClass;

struct _McdStorageAgHiddenClass {
    McdAccountManagerSsoClass parent_class;
};

struct _McdStorageAgHidden {
    McdAccountManagerSso parent;
};

GType mcd_storage_ag_hidden_get_type (void);

McdStorageAgHidden *mcd_storage_ag_hidden_new (void);

/* TYPE MACROS */
#define MCD_TYPE_STORAGE_AG_HIDDEN \
  (mcd_storage_ag_hidden_get_type ())
#define MCD_STORAGE_AG_HIDDEN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), MCD_TYPE_STORAGE_AG_HIDDEN, McdStorageAgHidden))
#define MCD_STORAGE_AG_HIDDEN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), MCD_TYPE_STORAGE_AG_HIDDEN,\
                           McdStorageAgHiddenClass))
#define MCD_IS_STORAGE_AG_HIDDEN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), MCD_TYPE_STORAGE_AG_HIDDEN))
#define MCD_IS_STORAGE_AG_HIDDEN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), MCD_TYPE_STORAGE_AG_HIDDEN))
#define MCD_STORAGE_AG_HIDDEN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), MCD_TYPE_STORAGE_AG_HIDDEN, \
                              McdStorageAgHiddenClass))

#endif /* MCD_STORAGE_AG_HIDDEN_H */
