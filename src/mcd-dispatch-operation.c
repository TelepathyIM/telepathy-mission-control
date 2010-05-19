/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008-2009 Nokia Corporation.
 * Copyright (C) 2009 Collabora Ltd.
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
#include "mcd-dispatch-operation-priv.h"

#include <stdio.h>
#include <string.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <telepathy-glib/defs.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel-dispatch-operation.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>

#include "channel-utils.h"
#include "mcd-channel-priv.h"
#include "mcd-dbusprop.h"
#include "mcd-misc.h"

#include <libmcclient/mc-errors.h>

#define MCD_CLIENT_BASE_NAME "org.freedesktop.Telepathy.Client."
#define MCD_CLIENT_BASE_NAME_LEN (sizeof (MCD_CLIENT_BASE_NAME) - 1)

#define MCD_DISPATCH_OPERATION_PRIV(operation) (MCD_DISPATCH_OPERATION (operation)->priv)

static void
dispatch_operation_iface_init (TpSvcChannelDispatchOperationClass *iface,
                               gpointer iface_data);
static void properties_iface_init (TpSvcDBusPropertiesClass *iface,
                                   gpointer iface_data);

static const McdDBusProp dispatch_operation_properties[];

static const McdInterfaceData dispatch_operation_interfaces[] = {
    MCD_IMPLEMENT_IFACE (tp_svc_channel_dispatch_operation_get_type,
                         dispatch_operation,
                         TP_IFACE_CHANNEL_DISPATCH_OPERATION),
    { G_TYPE_INVALID, }
};

G_DEFINE_TYPE_WITH_CODE (McdDispatchOperation, _mcd_dispatch_operation,
                         G_TYPE_OBJECT,
    MCD_DBUS_INIT_INTERFACES (dispatch_operation_interfaces);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES, properties_iface_init);
    )

typedef enum {
    APPROVAL_TYPE_REQUESTED,
    APPROVAL_TYPE_HANDLE_WITH,
    APPROVAL_TYPE_CLAIM,
    APPROVAL_TYPE_CHANNELS_LOST,
    APPROVAL_TYPE_NO_APPROVERS
} ApprovalType;

typedef struct {
    ApprovalType type;
    /* NULL unless type is REQUESTED or HANDLE_WITH; may be NULL even in those
     * cases, to signify "any handler will do" */
    gchar *client_bus_name;
    /* NULL unless type is CLAIM or HANDLE_WITH */
    DBusGMethodInvocation *context;
} Approval;

static Approval *
approval_new_handle_with (const gchar *client_bus_name,
                          DBusGMethodInvocation *context)
{
    Approval *approval = g_slice_new0 (Approval);

    g_assert (context != NULL);

    if (client_bus_name != NULL && client_bus_name[0] != '\0')
        approval->client_bus_name = g_strdup (client_bus_name);

    approval->type = APPROVAL_TYPE_HANDLE_WITH;
    approval->context = context;
    return approval;
}

static Approval *
approval_new_claim (DBusGMethodInvocation *context)
{
    Approval *approval = g_slice_new0 (Approval);

    g_assert (context != NULL);
    approval->type = APPROVAL_TYPE_CLAIM;
    approval->context = context;
    return approval;
}

static Approval *
approval_new_requested (const gchar *preferred_bus_name)
{
    Approval *approval = g_slice_new0 (Approval);

    if (preferred_bus_name != NULL && preferred_bus_name[0] != '\0')
        approval->client_bus_name = g_strdup (preferred_bus_name);

    approval->type = APPROVAL_TYPE_REQUESTED;
    return approval;
}

static Approval *
approval_new (ApprovalType type)
{
    Approval *approval = g_slice_new0 (Approval);

    switch (type)
    {
        case APPROVAL_TYPE_CLAIM:
        case APPROVAL_TYPE_HANDLE_WITH:
        case APPROVAL_TYPE_REQUESTED:
            g_assert_not_reached ();
        default:
            {} /* do nothing */
    }

    approval->type = type;
    return approval;
}

static void
approval_free (Approval *approval)
{
    /* we should have replied to the method call by now */
    g_assert (approval->context == NULL);

    g_slice_free (Approval, approval);
}

struct _McdDispatchOperationPrivate
{
    const gchar *unique_name;   /* borrowed from object_path */
    gchar *object_path;
    GStrv possible_handlers;
    GHashTable *properties;

    /* If FALSE, we're not actually on D-Bus; an object path is reserved,
     * but we're inaccessible. */
    guint needs_approval : 1;

    /* set of handlers we already tried
     * dup'd bus name (string) => dummy non-NULL pointer */
    GHashTable *failed_handlers;

    /* if non-NULL, we will emit finished as soon as we can; on success,
     * this is NotYours, and on failure, it's something else */
    GError *result;

    /* The time of the latest call to HandleWith(), for focus-stealing
     * prevention.
     *
     * This is shared between calls, so if the user makes contradictory
     * choices, like HandleWith("...Empathy") and HandleWith("...Kopete") in
     * quick succession, the channel will be handled with Empathy, but the
     * timestamp for focus-stealing purposes will be that of the call that
     * wanted Kopete; we consider this to be reasonable, since the user did
     * expect *something* to happen at the time of the second call. */
    gint64 handle_with_time;

    /* queue of Approval */
    GQueue *approvals;
    /* if not NULL, the handler that accepted it */
    TpClient *successful_handler;

    /* Reference to a global handler map */
    McdHandlerMap *handler_map;

    /* Reference to a global registry of clients */
    McdClientRegistry *client_registry;

    McdAccount *account;
    McdConnection *connection;

    /* Owned McdChannels we're dispatching */
    GList *channels;
    /* Owned McdChannels for which we can't emit ChannelLost yet, in
     * reverse chronological order */
    GList *lost_channels;

    /* If TRUE, either the channels being dispatched were requested, or they
     * were pre-approved by being returned as a response to another request,
     * or a client approved processing with arbitrary handlers */
    gboolean approved;

    /* If TRUE, at least one Approver accepted this dispatch operation, and
     * we're waiting for one of them to call HandleWith or Claim. */
    gboolean accepted_by_an_approver;

    /* If FALSE, we're still working out what Observers and/or Approvers to
     * run. These are temporary client locks.
     */
    gboolean invoked_observers_if_needed;
    gboolean invoked_approvers_if_needed;

    /* The number of observers that have not yet returned from ObserveChannels.
     * Until they have done so, we can't allow the dispatch operation to
     * finish. This is a client lock.
     *
     * A reference is held for each pending observer. */
    gsize observers_pending;

    /* The number of approvers that have not yet returned from
     * AddDispatchOperation. Until they have done so, we can't allow the
     * dispatch operation to finish. This is a client lock.
     *
     * A reference is held for each pending approver. */
    gsize ado_pending;

    /* If TRUE, we're dispatching a channel request and it was cancelled */
    gboolean cancelled;

    /* if TRUE, these channels were requested "behind our back", so stop
     * after observers */
    gboolean observe_only;

    /* If TRUE, we're in the middle of calling HandleChannels. This is a
     * client lock. */
    gboolean calling_handle_channels;

    /* If TRUE, we've tried all the BypassApproval handlers, which happens
     * before we run approvers. */
    gboolean tried_handlers_before_approval;
};

static void _mcd_dispatch_operation_check_finished (
    McdDispatchOperation *self);
static void _mcd_dispatch_operation_finish (McdDispatchOperation *,
    GQuark domain, gint code, const gchar *format, ...) G_GNUC_PRINTF (4, 5);

static void _mcd_dispatch_operation_check_client_locks (
    McdDispatchOperation *self);

/* To give clients time to connect to our "destructive" signals (ChannelLost
 * and Finished), we guarantee not to emit them if we have called methods on
 * an observer or approver, but they have not returned.
 *
 * Returns: TRUE if we may emit Finished or ChannelLost */
static inline gboolean
mcd_dispatch_operation_may_signal_finished (McdDispatchOperation *self)
{
    return (self->priv->invoked_observers_if_needed &&
            self->priv->observers_pending == 0 &&
            self->priv->ado_pending == 0);
}

static void
_mcd_dispatch_operation_inc_observers_pending (McdDispatchOperation *self)
{
    g_return_if_fail (self->priv->result == NULL);

    g_object_ref (self);

    DEBUG ("%" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
           self->priv->observers_pending,
           self->priv->observers_pending + 1);
    self->priv->observers_pending++;
}

static void
_mcd_dispatch_operation_dec_observers_pending (McdDispatchOperation *self)
{
    DEBUG ("%" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
           self->priv->observers_pending,
           self->priv->observers_pending - 1);
    g_return_if_fail (self->priv->observers_pending > 0);
    self->priv->observers_pending--;

    _mcd_dispatch_operation_check_finished (self);
    _mcd_dispatch_operation_check_client_locks (self);
    g_object_unref (self);
}

static void
_mcd_dispatch_operation_inc_ado_pending (McdDispatchOperation *self)
{
    g_return_if_fail (self->priv->result == NULL);

    g_object_ref (self);

    DEBUG ("%" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
           self->priv->ado_pending,
           self->priv->ado_pending + 1);
    self->priv->ado_pending++;
}

static void
_mcd_dispatch_operation_dec_ado_pending (McdDispatchOperation *self)
{
    DEBUG ("%" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
           self->priv->ado_pending,
           self->priv->ado_pending - 1);
    g_return_if_fail (self->priv->ado_pending > 0);
    self->priv->ado_pending--;

    _mcd_dispatch_operation_check_finished (self);

    if (self->priv->ado_pending == 0 && !self->priv->accepted_by_an_approver)
    {
        DEBUG ("No approver accepted the channels; considering them to be "
               "approved");
        g_queue_push_tail (self->priv->approvals,
                           approval_new (APPROVAL_TYPE_NO_APPROVERS));
    }

    _mcd_dispatch_operation_check_client_locks (self);

    g_object_unref (self);
}

gboolean
_mcd_dispatch_operation_get_cancelled (McdDispatchOperation *self)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), FALSE);
    return self->priv->cancelled;
}

static inline gboolean
_mcd_dispatch_operation_is_approved (McdDispatchOperation *self)
{
    return (!self->priv->needs_approval ||
            !g_queue_is_empty (self->priv->approvals));
}

static gboolean _mcd_dispatch_operation_try_next_handler (
    McdDispatchOperation *self);
static void _mcd_dispatch_operation_close_as_undispatchable (
    McdDispatchOperation *self, const GError *error);
static gboolean mcd_dispatch_operation_idle_run_approvers (gpointer p);
static void mcd_dispatch_operation_set_channel_handled_by (
    McdDispatchOperation *self, McdChannel *channel, const gchar *unique_name);

static void
_mcd_dispatch_operation_check_client_locks (McdDispatchOperation *self)
{
    Approval *approval;

    /* we may not continue until we've called all the Observers, and they've
     * all replied "I'm ready" */
    if (!self->priv->invoked_observers_if_needed ||
        self->priv->observers_pending > 0)
    {
        DEBUG ("waiting for Observers");
        return;
    }

    /* if we've called the first Approver, we may not continue until we've
     * called them all, and they all replied "I'm ready" */
    if (self->priv->ado_pending > 0)
    {
        DEBUG ("waiting for AddDispatchOperation to return");
        return;
    }

    /* if we've called one Handler, we may not continue until it responds
     * with an error */
    if (self->priv->calling_handle_channels)
    {
        DEBUG ("waiting for HandleChannels to return");
        return;
    }

    /* if a handler has claimed or accepted the channels, we have nothing to
     * do */
    if (self->priv->result != NULL)
    {
        DEBUG ("already finished (or finishing): %s",
               self->priv->result->message);
        return;
    }

    /* If we're only meant to be observing, do nothing */
    if (self->priv->observe_only)
    {
        DEBUG ("only observing");
        return;
    }

    approval = g_queue_peek_head (self->priv->approvals);

    /* if we've been claimed, respond, then do not call HandleChannels */
    if (approval != NULL && approval->type == APPROVAL_TYPE_CLAIM)
    {
        const GList *list;
        /* this needs to be copied because we don't use it til after we've
         * freed approval->context */
        gchar *caller = g_strdup (dbus_g_method_get_sender (
            approval->context));

        /* remove this approval from the list, so it won't be treated as a
         * failure */
        g_queue_pop_head (self->priv->approvals);

        for (list = self->priv->channels; list != NULL; list = list->next)
        {
            McdChannel *channel = MCD_CHANNEL (list->data);

            mcd_dispatch_operation_set_channel_handled_by (self, channel,
                caller);
        }

        DEBUG ("Replying to Claim call from %s", caller);

        tp_svc_channel_dispatch_operation_return_from_claim (
            approval->context);
        approval->context = NULL;

        _mcd_dispatch_operation_finish (self, TP_ERRORS, TP_ERROR_NOT_YOURS,
                                        "Channel successfully claimed by %s",
                                        caller);
        g_free (caller);

        return;
    }

    if (self->priv->invoked_approvers_if_needed)
    {
        if (_mcd_dispatch_operation_is_approved (self))
        {
            DEBUG ("trying next handler");

            if (!_mcd_dispatch_operation_try_next_handler (self))
            {
                GError incapable = { TP_ERRORS, TP_ERROR_NOT_CAPABLE,
                    "No possible handler still exists, giving up" };

                DEBUG ("ran out of handlers");
                _mcd_dispatch_operation_close_as_undispatchable (self,
                                                                 &incapable);
            }
        }
        else
        {
            DEBUG ("waiting for approval");
        }
    }
    else if (!self->priv->tried_handlers_before_approval)
    {
        DEBUG ("trying next pre-approval handler");

        if (!_mcd_dispatch_operation_try_next_handler (self))
        {
            DEBUG ("ran out of pre-approval handlers");

            self->priv->tried_handlers_before_approval = TRUE;

            g_idle_add_full (G_PRIORITY_HIGH,
                             mcd_dispatch_operation_idle_run_approvers,
                             g_object_ref (self), g_object_unref);
        }
    }
}

enum
{
    PROP_0,
    PROP_CHANNELS,
    PROP_CLIENT_REGISTRY,
    PROP_HANDLER_MAP,
    PROP_POSSIBLE_HANDLERS,
    PROP_NEEDS_APPROVAL,
    PROP_OBSERVE_ONLY,
};

/*
 * _mcd_dispatch_operation_get_connection_path:
 * @self: the #McdDispatchOperation.
 *
 * Returns: the D-Bus object path of the Connection associated with @self,
 *    or "/" if none.
 */
static const gchar *
_mcd_dispatch_operation_get_connection_path (McdDispatchOperation *self)
{
    const gchar *path;

    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), "/");

    if (self->priv->connection == NULL)
        return "/";

    path = mcd_connection_get_object_path (self->priv->connection);

    g_return_val_if_fail (path != NULL, "/");

    return path;
}

static void
get_connection (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (self);

    DEBUG ("called for %s", priv->unique_name);
    g_value_init (value, DBUS_TYPE_G_OBJECT_PATH);
    g_value_set_boxed (value,
        _mcd_dispatch_operation_get_connection_path (
            MCD_DISPATCH_OPERATION (self)));
}

/*
 * _mcd_dispatch_operation_get_account_path:
 * @operation: the #McdDispatchOperation.
 *
 * Returns: the D-Bus object path of the Account associated with @operation,
 *    or "/" if none.
 */
static const gchar *
_mcd_dispatch_operation_get_account_path (McdDispatchOperation *self)
{
    const gchar *path;

    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), "/");

    if (self->priv->account == NULL)
        return "/";

    path = mcd_account_get_object_path (self->priv->account);

    g_return_val_if_fail (path != NULL, "/");

    return path;
}

static void
get_account (TpSvcDBusProperties *self, const gchar *name, GValue *value)
{
    g_value_init (value, DBUS_TYPE_G_OBJECT_PATH);
    g_value_set_boxed (value,
        _mcd_dispatch_operation_get_account_path
            (MCD_DISPATCH_OPERATION (self)));
}

static void
get_channels (TpSvcDBusProperties *iface, const gchar *name, GValue *value)
{
    McdDispatchOperation *self = MCD_DISPATCH_OPERATION (iface);

    DEBUG ("called for %s", self->priv->unique_name);

    g_value_init (value, TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST);
    g_value_take_boxed (value,
        _mcd_tp_channel_details_build_from_list (self->priv->channels));
}

static void
get_possible_handlers (TpSvcDBusProperties *self, const gchar *name,
                       GValue *value)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (self);

    DEBUG ("called for %s", priv->unique_name);
    g_value_init (value, G_TYPE_STRV);
    g_value_set_boxed (value, priv->possible_handlers);
}


static const McdDBusProp dispatch_operation_properties[] = {
    { "Interfaces", NULL, mcd_dbus_get_interfaces },
    { "Connection", NULL, get_connection },
    { "Account", NULL, get_account },
    { "Channels", NULL, get_channels },
    { "PossibleHandlers", NULL, get_possible_handlers },
    { 0 },
};

static void
properties_iface_init (TpSvcDBusPropertiesClass *iface, gpointer iface_data)
{
#define IMPLEMENT(x) tp_svc_dbus_properties_implement_##x (\
    iface, dbusprop_##x)
    IMPLEMENT(set);
    IMPLEMENT(get);
    IMPLEMENT(get_all);
#undef IMPLEMENT
}

static void
mcd_dispatch_operation_set_channel_handled_by (McdDispatchOperation *self,
                                               McdChannel *channel,
                                               const gchar *unique_name)
{
    const gchar *path;
    TpChannel *tp_channel;

    g_assert (unique_name != NULL);

    path = mcd_channel_get_object_path (channel);
    tp_channel = mcd_channel_get_tp_channel (channel);
    g_return_if_fail (tp_channel != NULL);

    _mcd_channel_set_status (channel, MCD_CHANNEL_STATUS_DISPATCHED);

    _mcd_handler_map_set_channel_handled (self->priv->handler_map,
        tp_channel, unique_name,
        _mcd_dispatch_operation_get_account_path (self));
}

static void
mcd_dispatch_operation_actually_finish (McdDispatchOperation *self)
{
    g_object_ref (self);

    DEBUG ("%s/%p: finished", self->priv->unique_name, self);
    tp_svc_channel_dispatch_operation_emit_finished (self);

    _mcd_dispatch_operation_check_client_locks (self);

    g_object_unref (self);
}

static void
_mcd_dispatch_operation_finish (McdDispatchOperation *operation,
                                GQuark domain, gint code,
                                const gchar *format, ...)
{
    McdDispatchOperationPrivate *priv = operation->priv;
    Approval *approval;
    const gchar *successful_handler = NULL;
    va_list ap;

    if (priv->successful_handler != NULL)
    {
        successful_handler = tp_proxy_get_bus_name (priv->successful_handler);
    }

    if (priv->result != NULL)
    {
        DEBUG ("already finished (or about to): %s", priv->result->message);
        return;
    }

    va_start (ap, format);
    priv->result = _mcd_g_error_new_valist (domain, code, format, ap);
    va_end (ap);
    DEBUG ("Result: %s", priv->result->message);

    for (approval = g_queue_pop_head (priv->approvals);
         approval != NULL;
         approval = g_queue_pop_head (priv->approvals))
    {
        switch (approval->type)
        {
            case APPROVAL_TYPE_CLAIM:
                /* someone else got it - either another Claim() or a handler */
                g_assert (approval->context != NULL);
                DEBUG ("denying Claim call from %s",
                       dbus_g_method_get_sender (approval->context));
                dbus_g_method_return_error (approval->context, priv->result);
                approval->context = NULL;
                break;

            case APPROVAL_TYPE_HANDLE_WITH:
                g_assert (approval->context != NULL);

                if (successful_handler != NULL)
                {
                    /* Some Handler got it. If this Approver would have been
                     * happy with that Handler, then it succeeds, otherwise,
                     * it loses. */
                    if (approval->client_bus_name == NULL ||
                        !tp_strdiff (approval->client_bus_name,
                                     successful_handler))
                    {
                        DEBUG ("successful HandleWith, channel went to %s",
                               successful_handler);
                        tp_svc_channel_dispatch_operation_return_from_handle_with (
                            approval->context);
                    }
                    else
                    {
                        DEBUG ("HandleWith -> NotYours: wanted %s but "
                               "%s got it instead", approval->client_bus_name,
                               successful_handler);
                        dbus_g_method_return_error (approval->context,
                                                    priv->result);
                    }
                }
                else
                {
                    /* Handling finished for some other reason: perhaps the
                     * channel was claimed, or perhaps we ran out of channels.
                     */
                    DEBUG ("HandleWith -> error: %s %d: %s",
                           g_quark_to_string (priv->result->domain),
                           priv->result->code, priv->result->message);
                    dbus_g_method_return_error (approval->context, priv->result);
                }

                break;

            default:
                {} /* do nothing */
        }
    }

    if (mcd_dispatch_operation_may_signal_finished (operation))
    {
        DEBUG ("%s/%p has finished", priv->unique_name, operation);
        mcd_dispatch_operation_actually_finish (operation);
    }
    else
    {
        DEBUG ("%s/%p not finishing just yet: "
               "waiting for %" G_GSIZE_FORMAT " observers, "
               "%" G_GSIZE_FORMAT " approvers",
               priv->unique_name, operation,
               priv->observers_pending, priv->ado_pending);
    }
}

static gboolean mcd_dispatch_operation_check_handle_with (
    McdDispatchOperation *self, const gchar *handler_name, GError **error);

static void
dispatch_operation_handle_with (TpSvcChannelDispatchOperation *cdo,
                                const gchar *handler_name,
                                DBusGMethodInvocation *context)
{
    GError *error = NULL;
    McdDispatchOperation *self = MCD_DISPATCH_OPERATION (cdo);

    DEBUG ("%s/%p", self->priv->unique_name, self);

    if (!mcd_dispatch_operation_check_handle_with (self, handler_name, &error))
    {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    /* 0 is a special case for 'no user action' */
    self->priv->handle_with_time = 0;

    g_queue_push_tail (self->priv->approvals,
                       approval_new_handle_with (handler_name, context));
    _mcd_dispatch_operation_check_client_locks (self);
}

static void
dispatch_operation_claim (TpSvcChannelDispatchOperation *cdo,
                          DBusGMethodInvocation *context)
{
    McdDispatchOperation *self = MCD_DISPATCH_OPERATION (cdo);
    McdDispatchOperationPrivate *priv = self->priv;

    if (self->priv->result != NULL)
    {
        DEBUG ("Giving error to %s: %s", dbus_g_method_get_sender (context),
               self->priv->result->message);
        dbus_g_method_return_error (context, self->priv->result);
        return;
    }

    g_queue_push_tail (priv->approvals, approval_new_claim (context));
    _mcd_dispatch_operation_check_client_locks (self);
}

static void
dispatch_operation_iface_init (TpSvcChannelDispatchOperationClass *iface,
                               gpointer iface_data)
{
#define IMPLEMENT(x) tp_svc_channel_dispatch_operation_implement_##x (\
    iface, dispatch_operation_##x)
    IMPLEMENT(handle_with);
    IMPLEMENT(claim);
#undef IMPLEMENT
}

static void
create_object_path (McdDispatchOperationPrivate *priv)
{
    static guint cpt = 0;
    priv->object_path =
        g_strdup_printf (MC_DISPATCH_OPERATION_DBUS_OBJECT_BASE "do%u",
                         cpt++);
    priv->unique_name = priv->object_path +
        (sizeof (MC_DISPATCH_OPERATION_DBUS_OBJECT_BASE) - 1);
}

static GObject *
mcd_dispatch_operation_constructor (GType type, guint n_params,
                                    GObjectConstructParam *params)
{
    GObjectClass *object_class =
        (GObjectClass *)_mcd_dispatch_operation_parent_class;
    GObject *object;
    McdDispatchOperation *operation;
    McdDispatchOperationPrivate *priv;

    object = object_class->constructor (type, n_params, params);
    operation = MCD_DISPATCH_OPERATION (object);

    g_return_val_if_fail (operation != NULL, NULL);
    priv = operation->priv;

    if (!priv->client_registry || !priv->handler_map)
        goto error;

    if (priv->possible_handlers == NULL && !priv->observe_only)
    {
        g_critical ("!observe_only => possible_handlers must not be NULL");
        goto error;
    }

    if (priv->needs_approval && priv->observe_only)
    {
        g_critical ("observe_only => needs_approval must not be TRUE");
        goto error;
    }

    create_object_path (priv);

    DEBUG ("%s/%p: needs_approval=%c", priv->unique_name, object,
           priv->needs_approval ? 'T' : 'F');

    if (DEBUGGING)
    {
        GList *list;

        for (list = priv->channels; list != NULL; list = list->next)
        {
            DEBUG ("Channel: %s", mcd_channel_get_object_path (list->data));
        }
    }

    /* If approval is not needed, we don't appear on D-Bus (and approvers
     * don't run) */
    if (priv->needs_approval)
    {
        TpDBusDaemon *dbus_daemon;
        DBusGConnection *dbus_connection;

        g_object_get (priv->client_registry,
                      "dbus-daemon", &dbus_daemon,
                      NULL);

        /* can be NULL if we have fallen off the bus (in the real MC libdbus
         * would exit in this situation, but in the debug build, we stay
         * active briefly) */
        dbus_connection = tp_proxy_get_dbus_connection (dbus_daemon);

        if (G_LIKELY (dbus_connection != NULL))
            dbus_g_connection_register_g_object (dbus_connection,
                                                 priv->object_path, object);

        g_object_unref (dbus_daemon);
    }

    return object;
error:
    g_object_unref (object);
    g_return_val_if_reached (NULL);
}

static void _mcd_dispatch_operation_lose_channel (McdDispatchOperation *self,
                                                  McdChannel *channel);

static void
mcd_dispatch_operation_channel_aborted_cb (McdChannel *channel,
                                           McdDispatchOperation *self)
{
    const GError *error;

    g_object_ref (self);    /* FIXME: use a GObject closure or something */

    DEBUG ("Channel %p aborted while in a dispatch operation", channel);

    /* if it was a channel request, and it was cancelled, then the whole
     * dispatch operation should be aborted, closing any related channels */
    error = mcd_channel_get_error (channel);
    if (error && error->code == TP_ERROR_CANCELLED)
        self->priv->cancelled = TRUE;

    _mcd_dispatch_operation_lose_channel (self, channel);

    if (_mcd_dispatch_operation_peek_channels (self) == NULL)
    {
        DEBUG ("Nothing left in this context");
    }

    g_object_unref (self);
}

static void
mcd_dispatch_operation_set_property (GObject *obj, guint prop_id,
                                     const GValue *val, GParamSpec *pspec)
{
    McdDispatchOperation *operation = MCD_DISPATCH_OPERATION (obj);
    McdDispatchOperationPrivate *priv = operation->priv;
    GList *list;

    switch (prop_id)
    {
    case PROP_CLIENT_REGISTRY:
        g_assert (priv->client_registry == NULL); /* construct-only */
        priv->client_registry = MCD_CLIENT_REGISTRY (g_value_dup_object (val));
        break;

    case PROP_HANDLER_MAP:
        g_assert (priv->handler_map == NULL); /* construct-only */
        priv->handler_map = MCD_HANDLER_MAP (g_value_dup_object (val));
        break;

    case PROP_CHANNELS:
        /* because this is construct-only, we can assert that: */
        g_assert (priv->channels == NULL);
        g_assert (g_queue_is_empty (priv->approvals));

        priv->channels = g_list_copy (g_value_get_pointer (val));

        if (G_LIKELY (priv->channels))
        {
            /* get the connection and account from the first channel */
            McdChannel *channel = MCD_CHANNEL (priv->channels->data);
            const gchar *preferred_handler;

            priv->connection = (McdConnection *)
                mcd_mission_get_parent (MCD_MISSION (channel));

            if (G_LIKELY (priv->connection))
            {
                g_object_ref (priv->connection);
            }
            else
            {
                /* shouldn't happen? */
                g_warning ("Channel has no Connection?!");
            }

            /* if the first channel is actually a channel request, get the
             * preferred handler from it */
            preferred_handler =
                _mcd_channel_get_request_preferred_handler (channel);

            if (preferred_handler != NULL &&
                g_str_has_prefix (preferred_handler, MCD_CLIENT_BASE_NAME) &&
                tp_dbus_check_valid_bus_name (preferred_handler,
                                              TP_DBUS_NAME_TYPE_WELL_KNOWN,
                                              NULL))
            {
                DEBUG ("Extracted preferred handler: %s",
                       preferred_handler);
                g_queue_push_tail (priv->approvals,
                                   approval_new_requested (preferred_handler));
            }

            priv->account = mcd_channel_get_account (channel);

            if (G_LIKELY (priv->account != NULL))
            {
                g_object_ref (priv->account);
            }
            else
            {
                /* shouldn't happen? */
                g_warning ("Channel given to McdDispatchOperation has no "
                           "Account?!");
            }

            /* reference the channels and connect to their signals */
            for (list = priv->channels; list != NULL; list = list->next)
            {
                g_object_ref (list->data);

                g_signal_connect_after (list->data, "abort",
                    G_CALLBACK (mcd_dispatch_operation_channel_aborted_cb),
                    operation);

            }
        }
        break;

    case PROP_POSSIBLE_HANDLERS:
        g_assert (priv->possible_handlers == NULL);
        priv->possible_handlers = g_value_dup_boxed (val);
        break;

    case PROP_NEEDS_APPROVAL:
        priv->needs_approval = g_value_get_boolean (val);
        break;

    case PROP_OBSERVE_ONLY:
        priv->observe_only = g_value_get_boolean (val);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
        break;
    }
}

static void
mcd_dispatch_operation_get_property (GObject *obj, guint prop_id,
                                     GValue *val, GParamSpec *pspec)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (obj);

    switch (prop_id)
    {
    case PROP_CLIENT_REGISTRY:
        g_value_set_object (val, priv->client_registry);
        break;

    case PROP_HANDLER_MAP:
        g_value_set_object (val, priv->handler_map);
        break;

    case PROP_POSSIBLE_HANDLERS:
        g_value_set_boxed (val, priv->possible_handlers);
        break;

    case PROP_NEEDS_APPROVAL:
        g_value_set_boolean (val, priv->needs_approval);
        break;

    case PROP_OBSERVE_ONLY:
        g_value_set_boolean (val, priv->observe_only);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
        break;
    }
}

static void
mcd_dispatch_operation_finalize (GObject *object)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (object);

    g_strfreev (priv->possible_handlers);
    priv->possible_handlers = NULL;

    if (priv->properties)
        g_hash_table_unref (priv->properties);

    if (priv->failed_handlers != NULL)
    {
        g_hash_table_unref (priv->failed_handlers);
    }

    if (priv->result != NULL)
    {
        g_error_free (priv->result);
        priv->result = NULL;
    }

    g_free (priv->object_path);

    G_OBJECT_CLASS (_mcd_dispatch_operation_parent_class)->finalize (object);
}

static void
mcd_dispatch_operation_dispose (GObject *object)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (object);
    GList *list;

    if (priv->successful_handler != NULL)
    {
        g_object_unref (priv->successful_handler);
        priv->successful_handler = NULL;
    }

    if (priv->channels)
    {
        for (list = priv->channels; list != NULL; list = list->next)
        {
            g_signal_handlers_disconnect_by_func (list->data,
                mcd_dispatch_operation_channel_aborted_cb, object);
            g_object_unref (list->data);
        }

        g_list_free (priv->channels);
        priv->channels = NULL;
    }

    if (priv->lost_channels != NULL)
    {
        for (list = priv->lost_channels; list != NULL; list = list->next)
            g_object_unref (list->data);
        g_list_free (priv->lost_channels);
        priv->lost_channels = NULL;
    }

    if (priv->connection)
    {
        g_object_unref (priv->connection);
        priv->connection = NULL;
    }

    if (priv->account != NULL)
    {
        g_object_unref (priv->account);
        priv->account = NULL;
    }

    if (priv->handler_map != NULL)
    {
        g_object_unref (priv->handler_map);
        priv->handler_map = NULL;
    }

    if (priv->client_registry != NULL)
    {
        g_object_unref (priv->client_registry);
        priv->client_registry = NULL;
    }

    if (priv->approvals != NULL)
    {
        g_queue_foreach (priv->approvals, (GFunc) approval_free, NULL);
        g_queue_free (priv->approvals);
        priv->approvals = NULL;
    }

    G_OBJECT_CLASS (_mcd_dispatch_operation_parent_class)->dispose (object);
}

static void
_mcd_dispatch_operation_class_init (McdDispatchOperationClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (object_class,
                              sizeof (McdDispatchOperationPrivate));

    object_class->constructor = mcd_dispatch_operation_constructor;
    object_class->dispose = mcd_dispatch_operation_dispose;
    object_class->finalize = mcd_dispatch_operation_finalize;
    object_class->set_property = mcd_dispatch_operation_set_property;
    object_class->get_property = mcd_dispatch_operation_get_property;

    g_object_class_install_property (object_class, PROP_CLIENT_REGISTRY,
        g_param_spec_object ("client-registry", "Client registry",
            "Reference to a global registry of Telepathy clients",
            MCD_TYPE_CLIENT_REGISTRY,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
            G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class, PROP_HANDLER_MAP,
        g_param_spec_object ("handler-map", "Handler map",
            "Reference to a global map from handled channels to handlers",
            MCD_TYPE_HANDLER_MAP,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
            G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class, PROP_CHANNELS,
        g_param_spec_pointer ("channels", "channels", "channels",
                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (object_class, PROP_POSSIBLE_HANDLERS,
        g_param_spec_boxed ("possible-handlers", "Possible handlers",
                            "Well-known bus names of possible handlers",
                            G_TYPE_STRV,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                            G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class, PROP_NEEDS_APPROVAL,
        g_param_spec_boolean ("needs-approval", "Needs approval?",
                              "TRUE if this CDO should run Approvers and "
                              "appear on D-Bus", FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                              G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class, PROP_OBSERVE_ONLY,
        g_param_spec_boolean ("observe-only", "Observe only?",
                              "TRUE if this CDO should stop dispatching "
                              "as soon as Observers have been run",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                              G_PARAM_STATIC_STRINGS));
}

static void
_mcd_dispatch_operation_init (McdDispatchOperation *operation)
{
    McdDispatchOperationPrivate *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE ((operation),
                                        MCD_TYPE_DISPATCH_OPERATION,
                                        McdDispatchOperationPrivate);
    operation->priv = priv;
    operation->priv->approvals = g_queue_new ();

    /* initializes the interfaces */
    mcd_dbus_init_interfaces_instances (operation);
}

/*
 * _mcd_dispatch_operation_new:
 * @client_registry: the client registry.
 * @handler_map: the handler map
 * @channels: a #GList of #McdChannel elements to dispatch.
 * @possible_handlers: the bus names of possible handlers for these channels.
 *
 * Creates a #McdDispatchOperation. The #GList @channels will be no longer
 * valid after this function has been called.
 */
McdDispatchOperation *
_mcd_dispatch_operation_new (McdClientRegistry *client_registry,
                             McdHandlerMap *handler_map,
                             gboolean needs_approval,
                             gboolean observe_only,
                             GList *channels,
                             const gchar * const *possible_handlers)
{
    gpointer *obj;

    /* possible-handlers is only allowed to be NULL if we're only observing */
    g_return_val_if_fail (possible_handlers != NULL || observe_only, NULL);

    /* If we're only observing, then the channels were requested "behind MC's
     * back", so they can't need approval (i.e. observe_only implies
     * !needs_approval) */
    g_return_val_if_fail (!observe_only || !needs_approval, NULL);

    obj = g_object_new (MCD_TYPE_DISPATCH_OPERATION,
                        "client-registry", client_registry,
                        "handler-map", handler_map,
                        "channels", channels,
                        "possible-handlers", possible_handlers,
                        "needs-approval", needs_approval,
                        "observe-only", observe_only,
                        NULL);

    return MCD_DISPATCH_OPERATION (obj);
}

/*
 * _mcd_dispatch_operation_get_path:
 * @operation: the #McdDispatchOperation.
 *
 * Returns: the D-Bus object path of @operation.
 */
const gchar *
_mcd_dispatch_operation_get_path (McdDispatchOperation *operation)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (operation), NULL);
    return operation->priv->object_path;
}

/*
 * _mcd_dispatch_operation_get_properties:
 * @operation: the #McdDispatchOperation.
 *
 * Gets the immutable properties of @operation.
 *
 * Returns: a #GHashTable with the operation properties. The reference count is
 * not incremented.
 */
GHashTable *
_mcd_dispatch_operation_get_properties (McdDispatchOperation *operation)
{
    McdDispatchOperationPrivate *priv;

    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (operation), NULL);
    priv = operation->priv;
    if (!priv->properties)
    {
        const McdDBusProp *property;

        priv->properties =
            g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify)tp_g_value_slice_free);

        for (property = dispatch_operation_properties;
             property->name != NULL;
             property++)
        {
            GValue *value;
            gchar *name;

            if (!property->getprop) continue;

            /* The Channels property is mutable, so cannot be returned
             * here */
            if (!tp_strdiff (property->name, "Channels")) continue;

            value = g_slice_new0 (GValue);
            property->getprop ((TpSvcDBusProperties *)operation,
                               property->name, value);
            name = g_strconcat (TP_IFACE_CHANNEL_DISPATCH_OPERATION, ".",
                                property->name, NULL);
            g_hash_table_insert (priv->properties, name, value);
        }
    }
    return priv->properties;
}

gboolean
_mcd_dispatch_operation_needs_approval (McdDispatchOperation *self)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), FALSE);

    return self->priv->needs_approval;
}

/*
 * _mcd_dispatch_operation_is_finished:
 * @self: the #McdDispatchOperation.
 *
 * Returns: %TRUE if the operation has finished, %FALSE otherwise.
 */
gboolean
_mcd_dispatch_operation_is_finished (McdDispatchOperation *self)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), FALSE);
    /* if we want to finish, and we can, then we have */
    return (self->priv->result != NULL &&
            mcd_dispatch_operation_may_signal_finished (self));
}

static gboolean
mcd_dispatch_operation_check_handle_with (McdDispatchOperation *self,
                                          const gchar *handler_name,
                                          GError **error)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), FALSE);

    if (self->priv->result != NULL)
    {
        DEBUG ("already finished, %s", self->priv->result->message);
        if (error != NULL)
            *error = g_error_copy (self->priv->result);
        return FALSE;
    }

    if (!g_queue_is_empty (self->priv->approvals))
    {
        DEBUG ("NotYours: already finished or approved");
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_YOURS,
                     "CDO already finished or approved");
        return FALSE;
    }

    if (handler_name == NULL || handler_name[0] == '\0')
    {
        /* no handler name given */
        return TRUE;
    }

    if (!g_str_has_prefix (handler_name, MCD_CLIENT_BASE_NAME) ||
        !tp_dbus_check_valid_bus_name (handler_name,
                                       TP_DBUS_NAME_TYPE_WELL_KNOWN, NULL))
    {
        DEBUG ("InvalidArgument: handler name %s is bad", handler_name);
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "Invalid handler name");
        return FALSE;
    }

    return TRUE;
}

void
_mcd_dispatch_operation_approve (McdDispatchOperation *self,
                                 const gchar *preferred_handler)
{
    g_return_if_fail (MCD_IS_DISPATCH_OPERATION (self));

    /* NULL-safety: treat both NULL and "" as "unspecified" */
    if (preferred_handler == NULL)
        preferred_handler = "";

    DEBUG ("%s/%p (preferred handler: '%s')", self->priv->unique_name, self,
           preferred_handler);

    if (!g_str_has_prefix (preferred_handler, MCD_CLIENT_BASE_NAME) ||
        !tp_dbus_check_valid_bus_name (preferred_handler,
                                       TP_DBUS_NAME_TYPE_WELL_KNOWN, NULL))
    {
        DEBUG ("preferred handler name '%s' is bad, treating as unspecified",
               preferred_handler);
        preferred_handler = "";
    }

    g_queue_push_tail (self->priv->approvals,
                       approval_new_requested (preferred_handler));

    _mcd_dispatch_operation_check_client_locks (self);
}

static void
_mcd_dispatch_operation_lose_channel (McdDispatchOperation *self,
                                      McdChannel *channel)
{
    GList *li = g_list_find (self->priv->channels, channel);
    const gchar *object_path;
    const GError *error = NULL;

    if (li == NULL)
    {
        return;
    }

    self->priv->channels = g_list_delete_link (self->priv->channels, li);

    object_path = mcd_channel_get_object_path (channel);
    error = mcd_channel_get_error (channel);

    if (object_path == NULL)
    {
        /* This shouldn't happen, but McdChannel is twisty enough that I
         * can't be sure */
        g_critical ("McdChannel has already lost its TpChannel: %p",
            channel);
    }
    else if (!mcd_dispatch_operation_may_signal_finished (self))
    {
        /* We're still invoking approvers, so we're not allowed to talk
         * about it right now. Instead, save the signal for later. */
        DEBUG ("%s/%p not losing channel %s just yet: "
               "waiting for %" G_GSIZE_FORMAT " observers, "
               "%" G_GSIZE_FORMAT " approvers",
               self->priv->unique_name, self, object_path,
               self->priv->observers_pending, self->priv->ado_pending);
        self->priv->lost_channels =
            g_list_prepend (self->priv->lost_channels,
                            g_object_ref (channel));
    }
    else
    {
        gchar *error_name = _mcd_build_error_string (error);

        DEBUG ("%s/%p losing channel %s: %s: %s",
               self->priv->unique_name, self, object_path, error_name,
               error->message);
        tp_svc_channel_dispatch_operation_emit_channel_lost (self, object_path,
                                                             error_name,
                                                             error->message);
        g_free (error_name);
    }

    /* We previously had a ref in the linked list - drop it */
    g_object_unref (channel);

    if (self->priv->channels == NULL)
    {
        /* no channels left, so the CDO finishes (if it hasn't already) */
        _mcd_dispatch_operation_finish (self, error->domain, error->code,
                                        "%s", error->message);
    }
}

static void
_mcd_dispatch_operation_check_finished (McdDispatchOperation *self)
{
    if (mcd_dispatch_operation_may_signal_finished (self))
    {
        GList *lost_channels;

        /* get the lost channels into chronological order, and steal them from
         * the object*/
        lost_channels = g_list_reverse (self->priv->lost_channels);
        self->priv->lost_channels = NULL;

        while (lost_channels != NULL)
        {
            McdChannel *channel = lost_channels->data;
            const gchar *object_path = mcd_channel_get_object_path (channel);

            if (object_path == NULL)
            {
                /* This shouldn't happen, but McdChannel is twisty enough
                 * that I can't be sure */
                g_critical ("McdChannel has already lost its TpChannel: %p",
                    channel);
            }
            else
            {
                const GError *error = mcd_channel_get_error (channel);
                gchar *error_name = _mcd_build_error_string (error);

                DEBUG ("%s/%p losing channel %s: %s: %s",
                       self->priv->unique_name, self, object_path, error_name,
                       error->message);
                tp_svc_channel_dispatch_operation_emit_channel_lost (self,
                    object_path, error_name, error->message);
                g_free (error_name);
            }

            g_object_unref (channel);
            lost_channels = g_list_delete_link (lost_channels, lost_channels);
        }

        if (self->priv->result != NULL)
        {
            DEBUG ("%s/%p finished", self->priv->unique_name, self);
            mcd_dispatch_operation_actually_finish (self);
        }
    }
    else if (self->priv->result != NULL)
    {
        DEBUG ("%s/%p still unable to finish: "
               "waiting for %" G_GSIZE_FORMAT " observers, "
               "%" G_GSIZE_FORMAT " approvers",
               self->priv->unique_name, self,
               self->priv->observers_pending, self->priv->ado_pending);
    }
}

static void
_mcd_dispatch_operation_set_handler_failed (McdDispatchOperation *self,
                                            const gchar *bus_name,
                                            const GError *error)
{
    GList *iter, *next;
    gchar **handler;

    if (self->priv->failed_handlers == NULL)
    {
        self->priv->failed_handlers = g_hash_table_new_full (g_str_hash,
                                                             g_str_equal,
                                                             g_free, NULL);
    }

    /* the value is an arbitrary non-NULL pointer - the hash table itself
     * will do nicely */
    g_hash_table_insert (self->priv->failed_handlers, g_strdup (bus_name),
                         self->priv->failed_handlers);

    for (iter = g_queue_peek_head_link (self->priv->approvals);
         iter != NULL;
         iter = next)
    {
        Approval *approval = iter->data;

        /* do this before we potentially free the list element */
        next = iter->next;

        /* If this approval wanted the same handler that just failed, then
         * we can assume that's not going to happen. */
        if (approval->type == APPROVAL_TYPE_HANDLE_WITH &&
            !tp_strdiff (approval->client_bus_name, bus_name))
        {
            dbus_g_method_return_error (approval->context, (GError *) error);
            approval->context = NULL;
            approval_free (approval);
            g_queue_delete_link (self->priv->approvals, iter);
        }
    }

    for (handler = self->priv->possible_handlers;
         handler != NULL && *handler != NULL;
         handler++)
    {
        if (g_hash_table_lookup (self->priv->failed_handlers, *handler)
            == NULL)
        {
            /* we'll try this one soon */
            return;
        }
    }

    DEBUG ("All possible handlers failed: failing with the last error");
    _mcd_dispatch_operation_close_as_undispatchable (self, error);
}

static gboolean
_mcd_dispatch_operation_get_handler_failed (McdDispatchOperation *self,
                                            const gchar *bus_name)
{
    g_assert (MCD_IS_DISPATCH_OPERATION (self));
    g_assert (bus_name != NULL);

    if (self->priv->failed_handlers == NULL)
        return FALSE;

    return (g_hash_table_lookup (self->priv->failed_handlers, bus_name)
            != NULL);
}

static gboolean
_mcd_dispatch_operation_handlers_can_bypass_approval (
    McdDispatchOperation *self)
{
    gchar **iter;

    for (iter = self->priv->possible_handlers;
         iter != NULL && *iter != NULL;
         iter++)
    {
        McdClientProxy *handler = _mcd_client_registry_lookup (
            self->priv->client_registry, *iter);

        /* If the best handler that still exists bypasses approval, then
         * we're going to bypass approval.
         *
         * Also, because handlers are sorted with the best ones first, and
         * handlers with BypassApproval are "better", we can be sure that if
         * we've found a handler that still exists and does not bypass
         * approval, no handler bypasses approval. */
        if (handler != NULL)
        {
            gboolean bypass = _mcd_client_proxy_get_bypass_approval (
                handler);

            DEBUG ("%s has BypassApproval=%c", *iter, bypass ? 'T' : 'F');
            return bypass;
        }
    }

    /* If no handler still exists, we don't bypass approval, although if that
     * happens we're basically doomed anyway. */
    return FALSE;
}

gboolean
_mcd_dispatch_operation_has_channel (McdDispatchOperation *self,
                                     McdChannel *channel)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), FALSE);
    return (g_list_find (self->priv->channels, channel) != NULL);
}

const GList *
_mcd_dispatch_operation_peek_channels (McdDispatchOperation *self)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), NULL);
    return self->priv->channels;
}

GList *
_mcd_dispatch_operation_dup_channels (McdDispatchOperation *self)
{
    GList *copy;

    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), NULL);
    copy = g_list_copy (self->priv->channels);
    g_list_foreach (copy, (GFunc) g_object_ref, NULL);
    return copy;
}

static void
_mcd_dispatch_operation_handle_channels_cb (TpClient *client,
                                            const GError *error,
                                            gpointer user_data,
                                            GObject *weak G_GNUC_UNUSED)
{
    McdDispatchOperation *self = user_data;

    if (error)
    {
        DEBUG ("error: %s", error->message);

        _mcd_dispatch_operation_set_handler_failed (self,
            tp_proxy_get_bus_name (client), error);
    }
    else
    {
        const GList *list;

        for (list = self->priv->channels; list != NULL; list = list->next)
        {
            McdChannel *channel = list->data;
            const gchar *unique_name;

            unique_name = _mcd_client_proxy_get_unique_name (MCD_CLIENT_PROXY (client));

            /* This should always be false in practice - either we already know
             * the handler's unique name (because active handlers' unique names
             * are discovered before their handler filters), or the handler
             * is activatable and was not running, the handler filter came
             * from a .client file, and the bus daemon activated the handler
             * as a side-effect of HandleChannels (in which case
             * NameOwnerChanged should have already been emitted by the time
             * we got a reply to HandleChannels).
             *
             * We recover by whining to stderr and closing the channels, in the
             * interests of at least failing visibly.
             *
             * If dbus-glib exposed more of the details of the D-Bus message
             * passing system, then we could just look at the sender of the
             * reply and bypass this rubbish...
             */
            if (G_UNLIKELY (unique_name == NULL || unique_name[0] == '\0'))
            {
                g_warning ("Client %s returned successfully but doesn't "
                           "exist? dbus-daemon bug suspected",
                           tp_proxy_get_bus_name (client));
                g_warning ("Closing channel %s as a result",
                           mcd_channel_get_object_path (channel));
                _mcd_channel_undispatchable (channel);
                continue;
            }

            mcd_dispatch_operation_set_channel_handled_by (self, channel,
                                                           unique_name);
        }

        /* emit Finished, if we haven't already; but first make a note of the
         * handler we used, so we can reply to all the HandleWith calls with
         * success or failure */
        self->priv->successful_handler = g_object_ref (client);
        _mcd_dispatch_operation_finish (self, TP_ERRORS, TP_ERROR_NOT_YOURS,
                                        "Channel successfully handled by %s",
                                        tp_proxy_get_bus_name (client));
    }

    self->priv->calling_handle_channels = FALSE;
    _mcd_dispatch_operation_check_client_locks (self);
}

static void
observe_channels_cb (TpClient *proxy, const GError *error,
                     gpointer user_data, GObject *weak_object)
{
    McdDispatchOperation *self = user_data;

    /* we display the error just for debugging, but we don't really care */
    if (error)
        DEBUG ("Observer %s returned error: %s",
               tp_proxy_get_object_path (proxy), error->message);
    else
        DEBUG ("success from %s", tp_proxy_get_object_path (proxy));

    _mcd_dispatch_operation_dec_observers_pending (self);
}

/* The returned GPtrArray is allocated, but the contents are borrowed. */
static GPtrArray *
collect_satisfied_requests (GList *channels)
{
    const GList *c, *r;
    GHashTable *set = g_hash_table_new (g_str_hash, g_str_equal);
    GHashTableIter iter;
    gpointer path;
    GPtrArray *ret;

    /* collect object paths into a hash table, to drop duplicates
     * FIXME (fd.o #24763): this shouldn't be necessary, because there should
     * never be duplicates, unless my analysis is wrong? */
    for (c = channels; c != NULL; c = c->next)
    {
        const GList *reqs = _mcd_channel_get_satisfied_requests (c->data,
                                                                 NULL);

        for (r = reqs; r != NULL; r = r->next)
        {
            g_hash_table_insert (set, r->data, r->data);
        }
    }

    /* serialize them into a pointer array, which is what dbus-glib wants */
    ret = g_ptr_array_sized_new (g_hash_table_size (set));

    g_hash_table_iter_init (&iter, set);

    while (g_hash_table_iter_next (&iter, &path, NULL))
    {
        g_ptr_array_add (ret, path);
    }

    g_hash_table_destroy (set);

    return ret;
}

static void
_mcd_dispatch_operation_run_observers (McdDispatchOperation *self)
{
    const GList *cl;
    const gchar *dispatch_operation_path = "/";
    GHashTable *observer_info;
    GHashTableIter iter;
    gpointer client_p;

    observer_info = g_hash_table_new (g_str_hash, g_str_equal);

    _mcd_client_registry_init_hash_iter (self->priv->client_registry, &iter);

    while (g_hash_table_iter_next (&iter, NULL, &client_p))
    {
        McdClientProxy *client = MCD_CLIENT_PROXY (client_p);
        GList *observed = NULL;
        const gchar *account_path, *connection_path;
        GPtrArray *channels_array, *satisfied_requests;

        if (!tp_proxy_has_interface_by_id (client,
                                           TP_IFACE_QUARK_CLIENT_OBSERVER))
            continue;

        for (cl = self->priv->channels; cl != NULL; cl = cl->next)
        {
            McdChannel *channel = MCD_CHANNEL (cl->data);
            GHashTable *properties;

            properties = _mcd_channel_get_immutable_properties (channel);
            g_assert (properties != NULL);

            if (_mcd_client_match_filters (properties,
                _mcd_client_proxy_get_observer_filters (client),
                FALSE))
                observed = g_list_prepend (observed, channel);
        }
        if (!observed) continue;

        /* build up the parameters and invoke the observer */

        connection_path = _mcd_dispatch_operation_get_connection_path (self);
        account_path = _mcd_dispatch_operation_get_account_path (self);

        /* TODO: there's room for optimization here: reuse the channels_array,
         * if the observed list is the same */
        channels_array = _mcd_tp_channel_details_build_from_list (observed);

        satisfied_requests = collect_satisfied_requests (observed);

        if (_mcd_dispatch_operation_needs_approval (self))
        {
            dispatch_operation_path = _mcd_dispatch_operation_get_path (self);
        }

        _mcd_dispatch_operation_inc_observers_pending (self);

        DEBUG ("calling ObserveChannels on %s for CDO %p",
               tp_proxy_get_bus_name (client), self);
        tp_cli_client_observer_call_observe_channels (
            (TpClient *) client, -1,
            account_path, connection_path, channels_array,
            dispatch_operation_path, satisfied_requests, observer_info,
            observe_channels_cb,
            g_object_ref (self), g_object_unref, NULL);

        /* don't free the individual object paths, which are borrowed from the
         * McdChannel objects */
        g_ptr_array_free (satisfied_requests, TRUE);

        _mcd_tp_channel_details_free (channels_array);

        g_list_free (observed);
    }

    g_hash_table_destroy (observer_info);
}

static void
add_dispatch_operation_cb (TpClient *proxy,
                           const GError *error,
                           gpointer user_data,
                           GObject *weak_object)
{
    McdDispatchOperation *self = user_data;

    if (error)
    {
        DEBUG ("AddDispatchOperation %s (%p) on approver %s failed: "
               "%s",
               _mcd_dispatch_operation_get_path (self), self,
               tp_proxy_get_object_path (proxy), error->message);
    }
    else
    {
        DEBUG ("Approver %s accepted AddDispatchOperation %s (%p)",
               tp_proxy_get_object_path (proxy),
               _mcd_dispatch_operation_get_path (self), self);

        if (!self->priv->accepted_by_an_approver)
        {
            self->priv->accepted_by_an_approver = TRUE;
        }
    }

    /* If all approvers fail to add the DO, then we behave as if no
     * approver was registered: i.e., we continue dispatching. If at least
     * one approver accepted it, then we can still continue dispatching,
     * since it will be stalled until an approval is received. */
    _mcd_dispatch_operation_dec_ado_pending (self);
}

static void
_mcd_dispatch_operation_run_approvers (McdDispatchOperation *self)
{
    const GList *cl;
    GHashTableIter iter;
    gpointer client_p;

    /* we temporarily increment this count and decrement it at the end of the
     * function, to make sure it won't become 0 while we are still invoking
     * approvers */
    _mcd_dispatch_operation_inc_ado_pending (self);

    _mcd_client_registry_init_hash_iter (self->priv->client_registry, &iter);
    while (g_hash_table_iter_next (&iter, NULL, &client_p))
    {
        McdClientProxy *client = MCD_CLIENT_PROXY (client_p);
        GPtrArray *channel_details;
        const gchar *dispatch_operation;
        GHashTable *properties;
        gboolean matched = FALSE;

        if (!tp_proxy_has_interface_by_id (client,
                                           TP_IFACE_QUARK_CLIENT_APPROVER))
            continue;

        for (cl = self->priv->channels; cl != NULL; cl = cl->next)
        {
            McdChannel *channel = MCD_CHANNEL (cl->data);
            GHashTable *channel_properties;

            channel_properties = _mcd_channel_get_immutable_properties (channel);
            g_assert (channel_properties != NULL);

            if (_mcd_client_match_filters (channel_properties,
                _mcd_client_proxy_get_approver_filters (client),
                FALSE))
            {
                matched = TRUE;
                break;
            }
        }
        if (!matched) continue;

        dispatch_operation = _mcd_dispatch_operation_get_path (self);
        properties = _mcd_dispatch_operation_get_properties (self);
        channel_details =
            _mcd_tp_channel_details_build_from_list (self->priv->channels);

        DEBUG ("Calling AddDispatchOperation on approver %s for CDO %s @ %p",
               tp_proxy_get_bus_name (client), dispatch_operation, self);

        _mcd_dispatch_operation_inc_ado_pending (self);

        tp_cli_client_approver_call_add_dispatch_operation (
            (TpClient *) client, -1,
            channel_details, dispatch_operation, properties,
            add_dispatch_operation_cb,
            g_object_ref (self), g_object_unref, NULL);

        g_boxed_free (TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST, channel_details);
    }

    /* This matches the approvers count set to 1 at the beginning of the
     * function */
    _mcd_dispatch_operation_dec_ado_pending (self);
}

static gboolean
mcd_dispatch_operation_idle_run_approvers (gpointer p)
{
    McdDispatchOperation *self = p;

    if (_mcd_dispatch_operation_needs_approval (self))
    {
        if (!_mcd_dispatch_operation_is_approved (self))
            _mcd_dispatch_operation_run_approvers (self);
    }

    self->priv->invoked_approvers_if_needed = TRUE;
    _mcd_dispatch_operation_check_client_locks (self);

    return FALSE;
}

/* After this function is called, the McdDispatchOperation takes over its
 * own life-cycle, and the caller needn't hold an explicit reference to it. */
void
_mcd_dispatch_operation_run_clients (McdDispatchOperation *self)
{
    g_object_ref (self);
    DEBUG ("%s %p", self->priv->unique_name, self);

    if (self->priv->channels != NULL)
    {
        _mcd_dispatch_operation_run_observers (self);
    }

    DEBUG ("All necessary observers invoked");
    self->priv->invoked_observers_if_needed = TRUE;
    /* we call check_finished before returning */

    /* If nobody is bypassing approval, then we want to run approvers as soon
     * as possible, without waiting for observers, to improve responsiveness.
     * (The regression test dispatcher/exploding-bundles.py asserts that we
     * do this.)
     *
     * However, if a handler bypasses approval, we must wait til the observers
     * return, then run that handler, then proceed with the other handlers. */
    if (!_mcd_dispatch_operation_handlers_can_bypass_approval (self)
        && self->priv->channels != NULL)
    {
        self->priv->tried_handlers_before_approval = TRUE;

        g_idle_add_full (G_PRIORITY_HIGH,
                         mcd_dispatch_operation_idle_run_approvers,
                         g_object_ref (self), g_object_unref);
    }

    DEBUG ("Checking finished/locks");
    _mcd_dispatch_operation_check_finished (self);
    _mcd_dispatch_operation_check_client_locks (self);

    g_object_unref (self);
}

/*
 * mcd_dispatch_operation_handle_channels:
 * @self: the dispatch operation
 * @handler: the selected handler
 *
 * Invoke the handler for the given channels.
 */
static void
mcd_dispatch_operation_handle_channels (McdDispatchOperation *self,
                                        McdClientProxy *handler)
{
    g_assert (!self->priv->calling_handle_channels);
    self->priv->calling_handle_channels = TRUE;

    _mcd_client_proxy_handle_channels (handler,
        -1, self->priv->channels, self->priv->handle_with_time,
        NULL, _mcd_dispatch_operation_handle_channels_cb,
        g_object_ref (self), g_object_unref, NULL);
}

static gboolean
_mcd_dispatch_operation_try_next_handler (McdDispatchOperation *self)
{
    gchar **iter;
    gboolean is_approved = _mcd_dispatch_operation_is_approved (self);
    Approval *approval = g_queue_peek_head (self->priv->approvals);

    /* If there is a preferred Handler chosen by the first Approver or
     * request, it's the first one we'll consider. We'll even consider
     * it even if its filter doesn't match.
     *
     * In the case of an Approver calling HandleWith, we'll also try again
     * even if it already failed - perhaps the Approver is feeling lucky. */
    if (approval != NULL && approval->client_bus_name != NULL)
    {
        McdClientProxy *handler = _mcd_client_registry_lookup (
            self->priv->client_registry, approval->client_bus_name);
        gboolean failed = _mcd_dispatch_operation_get_handler_failed (self,
            approval->client_bus_name);

        DEBUG ("Approved handler is %s (still exists: %c, "
               "already failed: %c)", approval->client_bus_name,
               handler != NULL ? 'Y' : 'N',
               failed ? 'Y' : 'N');

        /* Maybe the handler has exited since we chose it, or maybe we
         * already tried it? Otherwise, it's the right choice. */
        if (handler != NULL &&
            (approval->type == APPROVAL_TYPE_HANDLE_WITH || !failed))
        {
            mcd_dispatch_operation_handle_channels (self, handler);
            return TRUE;
        }

        /* If the Handler has disappeared, a HandleWith call should fail,
         * but a request (for which the client_bus_name is merely advisory)
         * can legitimately try more handlers. */
        if (approval->type == APPROVAL_TYPE_HANDLE_WITH)
        {
            GError gone = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
                "The requested Handler does not exist" };

            g_queue_pop_head (self->priv->approvals);
            dbus_g_method_return_error (approval->context, &gone);
            approval->context = NULL;
            approval_free (approval);
            return TRUE;
        }
    }

    for (iter = self->priv->possible_handlers;
         iter != NULL && *iter != NULL;
         iter++)
    {
        McdClientProxy *handler = _mcd_client_registry_lookup (
            self->priv->client_registry, *iter);
        gboolean failed = _mcd_dispatch_operation_get_handler_failed
            (self, *iter);

        DEBUG ("Possible handler: %s (still exists: %c, already failed: %c)",
               *iter, handler != NULL ? 'Y' : 'N', failed ? 'Y' : 'N');

        if (handler != NULL && !failed &&
            (is_approved || _mcd_client_proxy_get_bypass_approval (handler)))
        {
            mcd_dispatch_operation_handle_channels (self, handler);
            return TRUE;
        }
    }

    return FALSE;
}

static void
_mcd_dispatch_operation_close_as_undispatchable (McdDispatchOperation *self,
                                                 const GError *error)
{
    GList *channels, *list;

    /* All of the usable handlers vanished while we were thinking about it
     * (this can only happen if non-activatable handlers exit after we
     * include them in the list of possible handlers, but before we .
     * We should recover in some better way, perhaps by asking all the
     * approvers again (?), but for now we'll just close all the channels. */

    DEBUG ("%s", error->message);
    _mcd_dispatch_operation_finish (self, error->domain, error->code,
                                    "%s", error->message);

    channels = _mcd_dispatch_operation_dup_channels (self);

    for (list = channels; list != NULL; list = list->next)
    {
        McdChannel *channel = MCD_CHANNEL (list->data);
        GError e = { MC_ERROR, MC_CHANNEL_REQUEST_GENERIC_ERROR,
            "Handler no longer available" };

        mcd_channel_take_error (channel, g_error_copy (&e));
        _mcd_channel_undispatchable (channel);
        g_object_unref (channel);
    }

    g_list_free (channels);
}
