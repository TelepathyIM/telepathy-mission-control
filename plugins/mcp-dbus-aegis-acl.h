/*
 * A pseudo-plugin that checks the caller's Aegis permission tokens
 *
 * Copyright © 2010-2011 Nokia Corporation
 * Copyright © 2010-2011 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef AEGIS_ACL_H
#define AEGIS_ACL_H

#include <mission-control-plugins/mission-control-plugins.h>
#include <sys/types.h>
#include <sys/creds.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct {
  GObject parent;
} _AegisAcl;

typedef struct {
  GObjectClass parent_class;
  creds_value_t token;
  creds_type_t token_type;
} _AegisAclClass;

typedef _AegisAcl AegisAcl;
typedef _AegisAclClass AegisAclClass;

AegisAcl *aegis_acl_new (void);

G_END_DECLS

#endif
