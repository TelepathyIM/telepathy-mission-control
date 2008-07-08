/*
 * dbus-api.c - Mission Control D-Bus API strings, enums etc.
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
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

#include "dbus-api.h"
#include <string.h>

/* auto-generated stubs */
#include "_gen/gtypes-body.h"
#include "_gen/interfaces-body.h"

#define MC_IFACE_IS_READY(iface_data) (*(iface_data->props_data_ptr) != NULL)

typedef struct _McIfaceStatus McIfaceStatus;

struct _CallWhenReadyContext {
    McIfaceWhenReadyCb callback;
    gpointer user_data;
    GDestroyNotify destroy;
    GObject *weak_object;
    McIfaceStatus *iface_status;
};

struct _McIfaceStatus {
    GQuark iface_quark;
    GSList *contexts;
    GSList *removed_contexts;
    McIfaceCreateProps create_props;
};

inline void
_mc_gvalue_stolen (GValue *value)
{
    GType type;

    /* HACK: clear the GValue so that the contents will not be freed */
    type = G_VALUE_TYPE (value); 
    memset (value, 0, sizeof (GValue)); 
    g_value_init (value, type); 
}

static void
lost_weak_ref (gpointer data, GObject *dead)
{
    CallWhenReadyContext *ctx = data;

    g_assert (ctx->weak_object == dead);
    ctx->weak_object = NULL;
    _mc_iface_cancel_callback (ctx);
}

static void
call_when_ready_context_free (CallWhenReadyContext *ctx)
{
    if (ctx->weak_object)
	g_object_weak_unref (ctx->weak_object, lost_weak_ref, ctx);
    if (ctx->destroy)
	ctx->destroy (ctx->user_data);
    g_slice_free (CallWhenReadyContext, ctx);
}

static void
_mc_iface_status_free (gpointer ptr)
{
    McIfaceStatus *iface_status = ptr;
    GSList *list;

    for (list = iface_status->contexts; list; list = list->next)
    {
	if (g_slist_find (iface_status->removed_contexts, list->data))
	    continue;
	call_when_ready_context_free (list->data);
    }
    g_slist_free (iface_status->contexts);
    g_slist_free (iface_status->removed_contexts);
    g_slice_free (McIfaceStatus, iface_status);
}

static void
properties_get_all_cb (TpProxy *proxy, GHashTable *props, 
		       const GError *error, gpointer user_data, 
		       GObject *weak_object) 
{
    McIfaceStatus *iface_status = user_data;
    CallWhenReadyContext *ctx;
    GSList *list;

    if (error == NULL)
	iface_status->create_props (proxy, props);

    for (list = iface_status->contexts; list; list = list->next)
    {
	ctx = list->data;
	if (g_slist_find (iface_status->removed_contexts, ctx))
	    continue;
	ctx->callback (proxy, error, ctx->user_data, ctx->weak_object);
    }
    g_object_set_qdata ((GObject *)proxy, iface_status->iface_quark, NULL);
}

gboolean
_mc_iface_call_when_ready_int (TpProxy *proxy,
			       McIfaceWhenReadyCb callback,
			       gpointer user_data,
			       McIfaceData *iface_data)
{
    return _mc_iface_call_when_ready_object_int (proxy, callback, user_data,
						 NULL, NULL, iface_data);
}

gboolean
_mc_iface_call_when_ready_object_int (TpProxy *proxy,
				      McIfaceWhenReadyCb callback,
				      gpointer user_data,
				      GDestroyNotify destroy,
				      GObject *weak_object,
				      McIfaceData *iface_data)
{
    gboolean first_invocation = FALSE;

    g_return_val_if_fail (callback != NULL, FALSE);

    if (MC_IFACE_IS_READY (iface_data) || proxy->invalidated)
    {
	callback (proxy, proxy->invalidated, user_data, weak_object);
	if (destroy)
	    destroy (user_data);
    }
    else
    {
	CallWhenReadyContext *ctx = g_slice_new (CallWhenReadyContext);
	GObject *object = (GObject *) proxy;
	McIfaceStatus *iface_status;

	ctx->callback = callback;
	ctx->user_data = user_data;
	ctx->destroy = destroy;
	ctx->weak_object = weak_object;
	if (weak_object != NULL)
	    g_object_weak_ref (weak_object, lost_weak_ref, ctx);


	iface_status = g_object_get_qdata (object, iface_data->id);
	if (!iface_status)
	{
	    /* it's the first time we are interested in this interface:
	     * setup the struct and call the GetAll method */
	    iface_status = g_slice_new (McIfaceStatus);
	    iface_status->contexts = NULL;
	    iface_status->removed_contexts = NULL;
	    iface_status->iface_quark = iface_data->id;
	    iface_status->create_props = iface_data->create_props;
	    g_object_set_qdata_full (object, iface_data->id,
				     iface_status, _mc_iface_status_free);

	    const gchar *name = g_quark_to_string (iface_data->id);
	    tp_cli_dbus_properties_call_get_all (proxy, -1, name, 
						 properties_get_all_cb, 
						 iface_status,
						 NULL,
						 NULL);
	    first_invocation = TRUE;
	}

	ctx->iface_status = iface_status;
	iface_status->contexts = g_slist_prepend (iface_status->contexts, ctx);
    }

    return first_invocation;
}

void
_mc_iface_cancel_callback (CallWhenReadyContext *ctx)
{
    if (G_UNLIKELY (ctx == NULL)) return;

    ctx->iface_status->removed_contexts =
       	g_slist_prepend (ctx->iface_status->removed_contexts, ctx);
    call_when_ready_context_free (ctx);
}

gboolean
_mc_iface_is_ready (gpointer object, GQuark iface)
{
    /* The interface is ready when the struct has been deleted */
    return g_object_get_qdata (object, iface) ? FALSE : TRUE;
}

