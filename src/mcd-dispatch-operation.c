/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright © 2008-2011 Nokia Corporation.
 * Copyright © 2009-2011 Collabora Ltd.
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

#include <glib/gstdio.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "channel-utils.h"
#include "mcd-channel-priv.h"
#include "mcd-dbusprop.h"
#include "mcd-master-priv.h"
#include "mcd-misc.h"
#include "plugin-dispatch-operation.h"
#include "plugin-loader.h"

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
    { NULL, }
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

    g_free (approval->client_bus_name);
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

    /* Owned McdChannel we're dispatching */
    McdChannel *channel;
    /* If non-NULL, we have lost the McdChannel but can't emit
     * ChannelLost yet */
    McdChannel *lost_channel;

    /* If TRUE, either the channel being dispatched was requested, or it
     * was pre-approved by being returned as a response to another request,
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

    /* The number of observers that are pending which have
     * DelayApprovers=TRUE. This is used to know if
     * AddDispatchOperation can be called yet. */
    gsize delay_approver_observers_pending;

    /* The number of approvers that have not yet returned from
     * AddDispatchOperation. Until they have done so, we can't allow the
     * dispatch operation to finish. This is a client lock.
     *
     * A reference is held for each pending approver. */
    gsize ado_pending;

    /* The number of plugins whose decision we're waiting for,
     * regarding whether a handler is in fact suitable. */
    gsize handler_suitable_pending;

    /* If non-NULL, a plugin has decided the selected handler is unsuitable. */
    GError *handler_unsuitable;

    /* If TRUE, we're dispatching a channel request and it was cancelled */
    gboolean cancelled;

    /* if TRUE, this channel was requested "behind our back", so stop
     * after observers */
    gboolean observe_only;

    /* If non-NULL, we're in the middle of asking plugins whether we may call
     * HandleChannels, or doing so. This is a client lock. */
    McdClientProxy *trying_handler;

    /* If TRUE, we've tried all the BypassApproval handlers, which happens
     * before we run approvers. */
    gboolean tried_handlers_before_approval;

    McdPluginDispatchOperation *plugin_api;
    gsize plugins_pending;
    gboolean did_post_observer_actions;
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
_mcd_dispatch_operation_inc_observers_pending (McdDispatchOperation *self,
    McdClientProxy *client)
{
    g_return_if_fail (self->priv->result == NULL);

    g_object_ref (self);

    DEBUG ("%" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
           self->priv->observers_pending,
           self->priv->observers_pending + 1);
    self->priv->observers_pending++;

    if (_mcd_client_proxy_get_delay_approvers (client))
      self->priv->delay_approver_observers_pending++;
}

static void
_mcd_dispatch_operation_dec_observers_pending (McdDispatchOperation *self,
    McdClientProxy *client)
{
    DEBUG ("%" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
           self->priv->observers_pending,
           self->priv->observers_pending - 1);
    g_return_if_fail (self->priv->observers_pending > 0);
    self->priv->observers_pending--;

    if (_mcd_client_proxy_get_delay_approvers (client))
      self->priv->delay_approver_observers_pending--;

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
        DEBUG ("No approver accepted the channel; considering it to be "
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

gboolean
_mcd_dispatch_operation_is_internal (McdDispatchOperation *self)
{
    GStrv handlers = self->priv->possible_handlers;

    return (handlers != NULL && !tp_strdiff (CDO_INTERNAL_HANDLER, *handlers));
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
    McdDispatchOperation *self, McdChannel *channel, const gchar *unique_name,
    const gchar *well_known_name);
static gboolean _mcd_dispatch_operation_handlers_can_bypass_approval (
    McdDispatchOperation *self);
static gboolean _mcd_dispatch_operation_handlers_can_bypass_observers (
    McdDispatchOperation *self);

static void
_mcd_dispatch_operation_check_client_locks (McdDispatchOperation *self)
{
    Approval *approval;
    guint approver_event_id = 0;

    if (!self->priv->invoked_observers_if_needed)
    {
        DEBUG ("waiting for Observers to be called");
        return;
    }

    if (self->priv->plugins_pending > 0)
    {
        DEBUG ("waiting for plugins to stop delaying");
        return;
    }

    /* Check whether plugins' requests to close channels later should be
     * honoured. We want to do this before running Approvers (if any). */
    if (self->priv->observers_pending == 0 &&
        !self->priv->did_post_observer_actions)
    {
        _mcd_plugin_dispatch_operation_observers_finished (
            self->priv->plugin_api);
        self->priv->did_post_observer_actions = TRUE;
    }

    /* If nobody is bypassing approval, then we want to run approvers as soon
     * as possible, without waiting for observers, to improve responsiveness.
     * (The regression test dispatcher/exploding-bundles.py asserts that we
     * do this.)
     *
     * However, if a handler bypasses approval, we must wait til the observers
     * return, then run that handler, then proceed with the other handlers. */
    if (!self->priv->tried_handlers_before_approval &&
        !_mcd_dispatch_operation_handlers_can_bypass_approval (self)
        && self->priv->delay_approver_observers_pending == 0
        && self->priv->channel != NULL &&
        ! _mcd_plugin_dispatch_operation_will_terminate (
            self->priv->plugin_api))
    {
        self->priv->tried_handlers_before_approval = TRUE;

        approver_event_id = g_idle_add_full (G_PRIORITY_HIGH,
                         mcd_dispatch_operation_idle_run_approvers,
                         g_object_ref (self), g_object_unref);
    }

    /* we may not continue until we've called all the Observers, and they've
     * all replied "I'm ready" */
    if (self->priv->observers_pending > 0)
    {
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
    if (self->priv->trying_handler != NULL)
    {
        DEBUG ("waiting for handler_is_suitable or HandleChannels to return");
        return;
    }

    /* if a handler has claimed or accepted the channel, we have nothing to
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

    if (_mcd_dispatch_operation_is_internal (self))
    {
        DEBUG ("Invoking internal handlers for requests");
        if (self->priv->channel != NULL)
        {
            McdChannel *channel = self->priv->channel;
            McdRequest *request = _mcd_channel_get_request (channel);

            if (request != NULL)
            {
                DEBUG ("Internal handler for request channel");
                _mcd_handler_map_set_channel_handled_internally (
                    self->priv->handler_map,
                    mcd_channel_get_tp_channel (channel),
                    _mcd_dispatch_operation_get_account_path (self));
                _mcd_request_handle_internally (request, channel, TRUE);
            }
        }

        /* The rest of this function deals with externally handled requests: *
         * Since these requests were internal, we need not trouble ourselves *
         * further (and infact would probably trigger errors if we tried)    */
        return;
    }

    /* If there are no potential handlers, the story ends here: we don't
     * want to run approvers in this case */
    if (self->priv->possible_handlers == NULL)
    {
        GError incapable = { TP_ERROR, TP_ERROR_NOT_CAPABLE,
            "No possible handlers, giving up" };

        DEBUG ("%s", incapable.message);
        _mcd_dispatch_operation_close_as_undispatchable (self, &incapable);
        return;
    }

    approval = g_queue_peek_head (self->priv->approvals);

    /* if we've been claimed, respond, then do not call HandleChannels */
    if (approval != NULL && approval->type == APPROVAL_TYPE_CLAIM)
    {
        gchar *caller = dbus_g_method_get_sender (approval->context);

        /* remove this approval from the list, so it won't be treated as a
         * failure */
        g_queue_pop_head (self->priv->approvals);

        if (self->priv->channel != NULL)
        {
            McdChannel *channel = self->priv->channel;

            mcd_dispatch_operation_set_channel_handled_by (self, channel,
                caller, NULL);
        }

        DEBUG ("Replying to Claim call from %s", caller);

        tp_svc_channel_dispatch_operation_return_from_claim (
            approval->context);
        approval->context = NULL;

        _mcd_dispatch_operation_finish (self, TP_ERROR, TP_ERROR_NOT_YOURS,
                                        "Channel successfully claimed by %s",
                                        caller);
        g_free (caller);

        if (approver_event_id > 0)
        {
            DEBUG ("Cancelling call to approvers as dispatch operation has been Claimed");
            g_source_remove (approver_event_id);
        }

        approval_free (approval);
        return;
    }
    else if (approval != NULL && approval->type == APPROVAL_TYPE_HANDLE_WITH)
    {
        /* We set this to TRUE so that the handlers are called. */
        self->priv->invoked_approvers_if_needed = TRUE;

        if (approver_event_id > 0)
        {
            DEBUG ("Cancelling call to approvers as dispatch operation has been HandledWith'd");
            g_source_remove (approver_event_id);
        }
    }

    if (self->priv->invoked_approvers_if_needed)
    {
        if (_mcd_dispatch_operation_is_approved (self))
        {
            DEBUG ("trying next handler");

            if (!_mcd_dispatch_operation_try_next_handler (self))
            {
                GError incapable = { TP_ERROR, TP_ERROR_NOT_CAPABLE,
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
    PROP_CHANNEL,
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
const gchar *
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

const gchar *
_mcd_dispatch_operation_get_cm_name (McdDispatchOperation *self)
{
    const gchar *ret;

    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), NULL);
    g_return_val_if_fail (self->priv->account != NULL, NULL);
    ret = mcd_account_get_manager_name (self->priv->account);
    g_return_val_if_fail (ret != NULL, NULL);
    return ret;
}

const gchar *
_mcd_dispatch_operation_get_protocol (McdDispatchOperation *self)
{
    const gchar *ret;

    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), NULL);
    g_return_val_if_fail (self->priv->account != NULL, NULL);
    ret = mcd_account_get_protocol_name (self->priv->account);
    g_return_val_if_fail (ret != NULL, NULL);
    return ret;
}

/*
 * _mcd_dispatch_operation_get_account_path:
 * @operation: the #McdDispatchOperation.
 *
 * Returns: the D-Bus object path of the Account associated with @operation,
 *    or "/" if none.
 */
const gchar *
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

    if (self->priv->channel == NULL)
    {
        g_value_take_boxed (value, g_ptr_array_sized_new (0));
        return;
    }

    g_value_take_boxed (value,
        _mcd_tp_channel_details_build_from_tp_chan (
            mcd_channel_get_tp_channel (self->priv->channel)));
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
                                               const gchar *unique_name,
                                               const gchar *well_known_name)
{
    TpChannel *tp_channel;

    g_assert (unique_name != NULL);

    tp_channel = mcd_channel_get_tp_channel (channel);
    g_return_if_fail (tp_channel != NULL);

    _mcd_channel_set_status (channel, MCD_CHANNEL_STATUS_DISPATCHED);

    _mcd_handler_map_set_channel_handled (self->priv->handler_map,
        tp_channel, unique_name, well_known_name,
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
    priv->result = g_error_new_valist (domain, code, format, ap);
    va_end (ap);
    DEBUG ("Result: %s", priv->result->message);

    for (approval = g_queue_pop_head (priv->approvals);
         approval != NULL;
         approval = g_queue_pop_head (priv->approvals))
    {
        gchar *caller;

        switch (approval->type)
        {
            case APPROVAL_TYPE_CLAIM:
                /* someone else got it - either another Claim() or a handler */
                g_assert (approval->context != NULL);
                caller = dbus_g_method_get_sender (approval->context);
                DEBUG ("denying Claim call from %s", caller);
                g_free (caller);
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

                        /* HandleWith and HandleWithTime both return void, so
                         * it's OK to not distinguish */
                        tp_svc_channel_dispatch_operation_return_from_handle_with (
                            approval->context);
                        approval->context = NULL;
                    }
                    else
                    {
                        DEBUG ("HandleWith -> NotYours: wanted %s but "
                               "%s got it instead", approval->client_bus_name,
                               successful_handler);
                        dbus_g_method_return_error (approval->context,
                                                    priv->result);
                        approval->context = NULL;
                    }
                }
                else
                {
                    /* Handling finished for some other reason: perhaps the
                     * channel was claimed, or perhaps it closed or we were
                     * told to forget about it.
                     */
                    DEBUG ("HandleWith -> error: %s %d: %s",
                           g_quark_to_string (priv->result->domain),
                           priv->result->code, priv->result->message);
                    dbus_g_method_return_error (approval->context, priv->result);
                    approval->context = NULL;
                }

                break;

            default:
                {   /* there shouldn't be a dbus context for these: */
                    g_assert (approval->context == NULL);
                }
        }

        approval_free (approval);
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
dispatch_operation_handle_with_time (TpSvcChannelDispatchOperation *cdo,
    const gchar *handler_name,
    gint64 user_action_timestamp,
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

    self->priv->handle_with_time = user_action_timestamp;

    g_queue_push_tail (self->priv->approvals,
                       approval_new_handle_with (handler_name, context));
    _mcd_dispatch_operation_check_client_locks (self);
}

static void
dispatch_operation_handle_with (TpSvcChannelDispatchOperation *cdo,
    const gchar *handler_name,
    DBusGMethodInvocation *context)
{
    dispatch_operation_handle_with_time (cdo, handler_name,
                                         TP_USER_ACTION_TIME_NOT_USER_ACTION,
                                         context);
}

typedef struct {
    McdDispatchOperation *self;
    DBusGMethodInvocation *context;
    gsize handler_suitable_pending;
} ClaimAttempt;

static void
claim_attempt_resolve (ClaimAttempt *claim_attempt)
{
    if (claim_attempt->context != NULL)
    {
        g_queue_push_tail (claim_attempt->self->priv->approvals,
                           approval_new_claim (claim_attempt->context));
        _mcd_dispatch_operation_check_client_locks (claim_attempt->self);
    }

    g_object_unref (claim_attempt->self);
    g_slice_free (ClaimAttempt, claim_attempt);
}

static void
claim_attempt_suitability_cb (GObject *source,
                              GAsyncResult *res,
                              gpointer user_data)
{
    ClaimAttempt *claim_attempt = user_data;
    GError *error = NULL;

    if (!mcp_dispatch_operation_policy_handler_is_suitable_finish (
            MCP_DISPATCH_OPERATION_POLICY (source), res, &error))
    {
        if (claim_attempt->context != NULL)
            dbus_g_method_return_error (claim_attempt->context, error);

        claim_attempt->context = NULL;
        g_error_free (error);
    }

    if (--claim_attempt->handler_suitable_pending == 0)
    {
        DEBUG ("all plugins have finished, resolving claim attempt");
        claim_attempt_resolve (claim_attempt);
    }
}

static void
dispatch_operation_claim (TpSvcChannelDispatchOperation *cdo,
                          DBusGMethodInvocation *context)
{
    McdDispatchOperation *self = MCD_DISPATCH_OPERATION (cdo);
    ClaimAttempt *claim_attempt;
    gchar *sender = dbus_g_method_get_sender (context);
    McpDispatchOperation *plugin_api = MCP_DISPATCH_OPERATION (
        self->priv->plugin_api);
    const GList *p;

    if (self->priv->result != NULL)
    {

        DEBUG ("Giving error to %s: %s", sender, self->priv->result->message);
        dbus_g_method_return_error (context, self->priv->result);
        goto finally;
    }

    claim_attempt = g_slice_new0 (ClaimAttempt);
    claim_attempt->self = g_object_ref (self);
    claim_attempt->context = context;
    claim_attempt->handler_suitable_pending = 0;

    for (p = mcp_list_objects (); p != NULL; p = g_list_next (p))
    {
        if (MCP_IS_DISPATCH_OPERATION_POLICY (p->data))
        {
            McpDispatchOperationPolicy *plugin = p->data;

            DEBUG ("%s: checking policy for %s",
                G_OBJECT_TYPE_NAME (plugin), sender);

            claim_attempt->handler_suitable_pending++;
            mcp_dispatch_operation_policy_handler_is_suitable_async (plugin,
                    NULL, sender, plugin_api,
                    claim_attempt_suitability_cb,
                    claim_attempt);
        }
    }

    if (claim_attempt->handler_suitable_pending == 0)
        claim_attempt_resolve (claim_attempt);

finally:
    g_free (sender);
}

static void
dispatch_operation_iface_init (TpSvcChannelDispatchOperationClass *iface,
                               gpointer iface_data)
{
#define IMPLEMENT(x) tp_svc_channel_dispatch_operation_implement_##x (\
    iface, dispatch_operation_##x)
    IMPLEMENT(handle_with);
    IMPLEMENT(claim);
    IMPLEMENT(handle_with_time);
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

    if (!priv->client_registry || !priv->handler_map || !priv->channel)
        goto error;

    if (priv->needs_approval && priv->observe_only)
    {
        g_critical ("observe_only => needs_approval must not be TRUE");
        goto error;
    }

    create_object_path (priv);

    DEBUG ("%s/%p: needs_approval=%c", priv->unique_name, object,
           priv->needs_approval ? 'T' : 'F');

    if (priv->channel != NULL)
    {
        DEBUG ("Channel: %s",
               mcd_channel_get_object_path (priv->channel));
        g_assert (mcd_channel_get_account (priv->channel) ==
                  priv->account);
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

    priv->plugin_api = _mcd_plugin_dispatch_operation_new (operation);

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

    if (self->priv->channel == NULL)
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

    case PROP_CHANNEL:
        /* because this is construct-only, we can assert that: */
        g_assert (priv->channel == NULL);
        g_assert (g_queue_is_empty (priv->approvals));

        priv->channel = g_value_dup_object (val);

        if (G_LIKELY (priv->channel))
        {
            const gchar *preferred_handler;

            priv->connection = (McdConnection *)
                mcd_mission_get_parent (MCD_MISSION (priv->channel));

            if (G_LIKELY (priv->connection))
            {
                g_object_ref (priv->connection);
            }
            else
            {
                /* shouldn't happen? */
                g_warning ("Channel has no Connection?!");
            }

            /* if the channel is actually a channel request, get the
             * preferred handler from it */
            preferred_handler =
                _mcd_channel_get_request_preferred_handler (priv->channel);

            if (preferred_handler != NULL &&
                g_str_has_prefix (preferred_handler, TP_CLIENT_BUS_NAME_BASE) &&
                tp_dbus_check_valid_bus_name (preferred_handler,
                                              TP_DBUS_NAME_TYPE_WELL_KNOWN,
                                              NULL))
            {
                DEBUG ("Extracted preferred handler: %s",
                       preferred_handler);
                g_queue_push_tail (priv->approvals,
                                   approval_new_requested (preferred_handler));
            }

            priv->account = mcd_channel_get_account (priv->channel);

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

            /* connect to its signals */
            g_signal_connect_after (priv->channel, "abort",
                G_CALLBACK (mcd_dispatch_operation_channel_aborted_cb),
                operation);
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

    tp_clear_pointer (&priv->possible_handlers, g_strfreev);
    tp_clear_pointer (&priv->properties, g_hash_table_unref);
    tp_clear_pointer (&priv->failed_handlers, g_hash_table_unref);
    g_clear_error (&priv->result);
    g_free (priv->object_path);

    G_OBJECT_CLASS (_mcd_dispatch_operation_parent_class)->finalize (object);
}

static void
mcd_dispatch_operation_dispose (GObject *object)
{
    McdDispatchOperationPrivate *priv = MCD_DISPATCH_OPERATION_PRIV (object);

    tp_clear_object (&priv->plugin_api);
    tp_clear_object (&priv->successful_handler);

    if (priv->channel != NULL)
    {
        g_signal_handlers_disconnect_by_func (priv->channel,
            mcd_dispatch_operation_channel_aborted_cb, object);
    }

    tp_clear_object (&priv->channel);
    tp_clear_object (&priv->lost_channel);
    tp_clear_object (&priv->account);
    tp_clear_object (&priv->handler_map);
    tp_clear_object (&priv->client_registry);

    if (priv->approvals != NULL)
    {
        g_queue_foreach (priv->approvals, (GFunc) approval_free, NULL);
        tp_clear_pointer (&priv->approvals, g_queue_free);
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

    g_object_class_install_property (object_class, PROP_CHANNEL,
        g_param_spec_object ("channel", "Channel", "the channel to dispatch",
                             MCD_TYPE_CHANNEL,
                             G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS));

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
 * @channel: the channel to dispatch
 * @possible_handlers: the bus names of possible handlers for this channel
 *
 * Creates a #McdDispatchOperation.
 */
McdDispatchOperation *
_mcd_dispatch_operation_new (McdClientRegistry *client_registry,
                             McdHandlerMap *handler_map,
                             gboolean needs_approval,
                             gboolean observe_only,
                             McdChannel *channel,
                             const gchar * const *possible_handlers)
{
    gpointer *obj;

    /* If we're only observing, then the channel was requested "behind MC's
     * back", so they can't need approval (i.e. observe_only implies
     * !needs_approval) */
    g_return_val_if_fail (!observe_only || !needs_approval, NULL);
    g_return_val_if_fail (MCD_IS_CHANNEL (channel), NULL);

    obj = g_object_new (MCD_TYPE_DISPATCH_OPERATION,
                        "client-registry", client_registry,
                        "handler-map", handler_map,
                        "channel", channel,
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
        g_set_error (error, TP_ERROR, TP_ERROR_NOT_YOURS,
                     "CDO already finished or approved");
        return FALSE;
    }

    if (handler_name == NULL || handler_name[0] == '\0')
    {
        /* no handler name given */
        return TRUE;
    }

    if (!g_str_has_prefix (handler_name, TP_CLIENT_BUS_NAME_BASE) ||
        !tp_dbus_check_valid_bus_name (handler_name,
                                       TP_DBUS_NAME_TYPE_WELL_KNOWN, NULL))
    {
        DEBUG ("InvalidArgument: handler name %s is bad", handler_name);
        g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
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

    if (!g_str_has_prefix (preferred_handler, TP_CLIENT_BUS_NAME_BASE) ||
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
    const gchar *object_path;
    const GError *error = NULL;

    if (G_UNLIKELY (channel != self->priv->channel))
    {
        g_warning ("%s: apparently lost %p but my channel is %p",
                   G_STRFUNC, channel, self->priv->channel);
        return;
    }

    /* steal the reference */
    self->priv->channel = NULL;

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
        g_assert (self->priv->lost_channel == NULL);
        self->priv->lost_channel = g_object_ref (channel);
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

    /* We previously stole this ref from self->priv->channel - drop it */
    g_object_unref (channel);

    /* no channels left, so the CDO finishes (if it hasn't already) */
    _mcd_dispatch_operation_finish (self, error->domain, error->code,
                                    "%s", error->message);
}

static void
_mcd_dispatch_operation_check_finished (McdDispatchOperation *self)
{
    if (mcd_dispatch_operation_may_signal_finished (self))
    {
        McdChannel *lost_channel = self->priv->lost_channel;

        /* steal it */
        self->priv->lost_channel = NULL;

        if (lost_channel != NULL)
        {
            const gchar *object_path = mcd_channel_get_object_path (
                lost_channel);

            if (object_path == NULL)
            {
                /* This shouldn't happen, but McdChannel is twisty enough
                 * that I can't be sure */
                g_critical ("McdChannel has already lost its TpChannel: %p",
                    lost_channel);
            }
            else
            {
                const GError *error = mcd_channel_get_error (lost_channel);
                gchar *error_name = _mcd_build_error_string (error);

                DEBUG ("%s/%p losing channel %s: %s: %s",
                       self->priv->unique_name, self, object_path, error_name,
                       error->message);
                tp_svc_channel_dispatch_operation_emit_channel_lost (self,
                    object_path, error_name, error->message);
                g_free (error_name);
            }

            g_object_unref (lost_channel);
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

    /* special case: internally handled request, not subject to approval */
    if (_mcd_dispatch_operation_is_internal (self))
        return TRUE;

    /* special case: we don't have any handlers at all, so we don't want
     * approval - we're just going to fail */
    if (self->priv->possible_handlers == NULL)
        return TRUE;

    for (iter = self->priv->possible_handlers;
         *iter != NULL;
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
     * happens we're basically doomed anyway. (unless this is an internal
     * request, in which case we should be ok) */
    return FALSE;
}

/* this is analogous to *_can_bypass_handlers() method above */
static gboolean
_mcd_dispatch_operation_handlers_can_bypass_observers (
    McdDispatchOperation *self)
{
    gchar **iter;

    for (iter = self->priv->possible_handlers;
         iter != NULL && *iter != NULL;
         iter++)
    {
        McdClientProxy *handler = _mcd_client_registry_lookup (
            self->priv->client_registry, *iter);

        if (handler != NULL)
        {
            gboolean bypass = _mcd_client_proxy_get_bypass_observers (
                handler);

            DEBUG ("%s has BypassObservers=%c", *iter, bypass ? 'T' : 'F');
            return bypass;
        }
    }

    return FALSE;
}


gboolean
_mcd_dispatch_operation_has_channel (McdDispatchOperation *self,
                                     McdChannel *channel)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), FALSE);

    return (self->priv->channel != NULL &&
            self->priv->channel == channel);
}

McdChannel *
_mcd_dispatch_operation_peek_channel (McdDispatchOperation *self)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), NULL);

    return self->priv->channel;
}

McdChannel *
_mcd_dispatch_operation_dup_channel (McdDispatchOperation *self)
{
    g_return_val_if_fail (MCD_IS_DISPATCH_OPERATION (self), NULL);

    if (self->priv->channel != NULL)
        return g_object_ref (self->priv->channel);

    return NULL;
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
        /* FIXME: can channel ever be NULL here? */
        if (self->priv->channel != NULL)
        {
            McdChannel *channel = MCD_CHANNEL (self->priv->channel);
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
            }
            else
            {
                mcd_dispatch_operation_set_channel_handled_by (self, channel,
                    unique_name, tp_proxy_get_bus_name (client));
            }
        }

        /* emit Finished, if we haven't already; but first make a note of the
         * handler we used, so we can reply to all the HandleWith calls with
         * success or failure */
        self->priv->successful_handler = g_object_ref (client);
        _mcd_dispatch_operation_finish (self, TP_ERROR, TP_ERROR_NOT_YOURS,
                                        "Channel successfully handled by %s",
                                        tp_proxy_get_bus_name (client));
    }

    tp_clear_object (&self->priv->trying_handler);
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

    _mcd_dispatch_operation_dec_observers_pending (self, MCD_CLIENT_PROXY (proxy));
}

/*
 * @paths_out: (out) (transfer container) (element-type utf8):
 *  Requests_Satisfied
 * @props_out: (out) (transfer container) (element-type utf8 GHashTable):
 *  request-properties for Observer_Info or Handler_Info
 */
static void
collect_satisfied_requests (McdChannel *channel,
    GPtrArray **paths_out,
    GHashTable **props_out)
{
    GHashTableIter it;
    gpointer path, value;
    GPtrArray *satisfied_requests;
    GHashTable *request_properties;
    GHashTable *reqs;

    reqs = _mcd_channel_get_satisfied_requests (channel, NULL);

    satisfied_requests = g_ptr_array_sized_new (g_hash_table_size (reqs));
    g_ptr_array_set_free_func (satisfied_requests, g_free);

    request_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, (GDestroyNotify) g_hash_table_unref);

    g_hash_table_iter_init (&it, reqs);

    while (g_hash_table_iter_next (&it, &path, &value))
    {
        GHashTable *props;

        g_ptr_array_add (satisfied_requests, g_strdup (path));
        props = _mcd_request_dup_immutable_properties (value);
        g_assert (props != NULL);
        g_hash_table_insert (request_properties, g_strdup (path), props);
    }

    g_hash_table_unref (reqs);

    if (paths_out != NULL)
        *paths_out = satisfied_requests;
    else
        g_ptr_array_unref (satisfied_requests);

    if (props_out != NULL)
        *props_out = request_properties;
    else
        g_hash_table_unref (request_properties);
}

static void
_mcd_dispatch_operation_run_observers (McdDispatchOperation *self)
{
    const gchar *dispatch_operation_path = "/";
    GHashTable *observer_info;
    GHashTableIter iter;
    gpointer client_p;

    observer_info = tp_asv_new (NULL, NULL);

    _mcd_client_registry_init_hash_iter (self->priv->client_registry, &iter);

    while (g_hash_table_iter_next (&iter, NULL, &client_p))
    {
        McdClientProxy *client = MCD_CLIENT_PROXY (client_p);
        gboolean observed = FALSE;
        const gchar *account_path, *connection_path;
        GPtrArray *channels_array, *satisfied_requests;
        GHashTable *request_properties;

        if (!tp_proxy_has_interface_by_id (client,
                                           TP_IFACE_QUARK_CLIENT_OBSERVER))
            continue;

        if (self->priv->channel != NULL)
        {
            McdChannel *channel = MCD_CHANNEL (self->priv->channel);
            GVariant *properties;

            properties = mcd_channel_dup_immutable_properties (channel);
            g_assert (properties != NULL);

            if (_mcd_client_match_filters (properties,
                _mcd_client_proxy_get_observer_filters (client),
                FALSE))
                observed = TRUE;

            g_variant_unref (properties);
        }

        /* in particular this happens if there is no channel at all */
        if (!observed) continue;

        /* build up the parameters and invoke the observer */

        connection_path = _mcd_dispatch_operation_get_connection_path (self);
        account_path = _mcd_dispatch_operation_get_account_path (self);

        /* TODO: there's room for optimization here: reuse the channels_array,
         * if the observed list is the same */
        channels_array = _mcd_tp_channel_details_build_from_tp_chan (
            mcd_channel_get_tp_channel (self->priv->channel));

        collect_satisfied_requests (self->priv->channel, &satisfied_requests,
                                    &request_properties);

        /* transfer ownership into observer_info */
        tp_asv_take_boxed (observer_info, "request-properties",
            TP_HASH_TYPE_OBJECT_IMMUTABLE_PROPERTIES_MAP,
            request_properties);
        request_properties = NULL;

        if (_mcd_dispatch_operation_needs_approval (self))
        {
            dispatch_operation_path = _mcd_dispatch_operation_get_path (self);
        }

        _mcd_dispatch_operation_inc_observers_pending (self, client);

        DEBUG ("calling ObserveChannels on %s for CDO %p",
               tp_proxy_get_bus_name (client), self);
        tp_cli_client_observer_call_observe_channels (
            (TpClient *) client, -1,
            account_path, connection_path, channels_array,
            dispatch_operation_path, satisfied_requests, observer_info,
            observe_channels_cb,
            g_object_ref (self), g_object_unref, NULL);

        g_ptr_array_unref (satisfied_requests);

        _mcd_tp_channel_details_free (channels_array);
    }

    g_hash_table_unref (observer_info);
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

        if (self->priv->channel != NULL)
        {
            McdChannel *channel = MCD_CHANNEL (self->priv->channel);
            GVariant *channel_properties;

            channel_properties = mcd_channel_dup_immutable_properties (channel);
            g_assert (channel_properties != NULL);

            if (_mcd_client_match_filters (channel_properties,
                _mcd_client_proxy_get_approver_filters (client),
                FALSE))
            {
                matched = TRUE;
            }

            g_variant_unref (channel_properties);
        }

        /* in particular, after this point, self->priv->channel can't
         * be NULL */
        if (!matched) continue;

        dispatch_operation = _mcd_dispatch_operation_get_path (self);
        properties = _mcd_dispatch_operation_get_properties (self);
        channel_details = _mcd_tp_channel_details_build_from_tp_chan (
            mcd_channel_get_tp_channel (self->priv->channel));

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

    if (self->priv->channel != NULL)
    {
        const GList *mini_plugins;

        if (_mcd_dispatch_operation_handlers_can_bypass_observers (self))
        {
            DEBUG ("Bypassing observers");
        }
        else
        {
            DEBUG ("Running observers");
            _mcd_dispatch_operation_run_observers (self);
        }

        for (mini_plugins = mcp_list_objects ();
             mini_plugins != NULL;
             mini_plugins = mini_plugins->next)
        {
            if (MCP_IS_DISPATCH_OPERATION_POLICY (mini_plugins->data))
            {
                mcp_dispatch_operation_policy_check (mini_plugins->data,
                    MCP_DISPATCH_OPERATION (self->priv->plugin_api));
            }
        }
    }

    DEBUG ("All necessary observers invoked");
    self->priv->invoked_observers_if_needed = TRUE;

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
mcd_dispatch_operation_handle_channels (McdDispatchOperation *self)
{
    GList *channels = NULL;
    GHashTable *handler_info;
    GHashTable *request_properties;

    g_assert (self->priv->trying_handler != NULL);

    if (self->priv->handler_unsuitable != NULL)
    {
        GError *tmp = self->priv->handler_unsuitable;

        /* move the error out of the way first, in case the callback
         * tries a different handler which will also want to check
         * handler_unsuitable */
        self->priv->handler_unsuitable = NULL;

        _mcd_dispatch_operation_handle_channels_cb (
            (TpClient *) self->priv->trying_handler,
            tmp, self, NULL);
        g_error_free (tmp);

        return;
    }

    /* FIXME: it shouldn't be possible to get here without a channel */
    if (self->priv->channel != NULL)
    {
        collect_satisfied_requests (self->priv->channel, NULL,
                                    &request_properties);
        channels = g_list_prepend (NULL, self->priv->channel);
    }
    else
    {
        request_properties = g_hash_table_new_full (g_str_hash,
            g_str_equal, g_free, (GDestroyNotify) g_hash_table_unref);
    }

    handler_info = tp_asv_new (NULL, NULL);
    tp_asv_take_boxed (handler_info, "request-properties",
        TP_HASH_TYPE_OBJECT_IMMUTABLE_PROPERTIES_MAP, request_properties);
    request_properties = NULL;

    _mcd_client_proxy_handle_channels (self->priv->trying_handler,
        -1, channels, self->priv->handle_with_time,
        handler_info, _mcd_dispatch_operation_handle_channels_cb,
        g_object_ref (self), g_object_unref, NULL);

    g_hash_table_unref (handler_info);
    g_list_free (channels);
}

static void
mcd_dispatch_operation_handler_decision_cb (GObject *source,
                                            GAsyncResult *res,
                                            gpointer user_data)
{
    McdDispatchOperation *self = user_data;
    GError *error = NULL;

    if (!mcp_dispatch_operation_policy_handler_is_suitable_finish (
            MCP_DISPATCH_OPERATION_POLICY (source), res, &error))
    {
        /* ignore any errors after the first */
        if (self->priv->handler_unsuitable == NULL)
            g_propagate_error (&self->priv->handler_unsuitable, error);
        else
            g_error_free (error);
    }

    if (--self->priv->handler_suitable_pending == 0)
    {
        mcd_dispatch_operation_handle_channels (self);
    }

    g_object_unref (self);
}

static void
mcd_dispatch_operation_try_handler (McdDispatchOperation *self,
                                    McdClientProxy *handler)
{
    TpClient *handler_client = (TpClient *) handler;
    const GList *p;
    McpDispatchOperation *plugin_api = MCP_DISPATCH_OPERATION (
        self->priv->plugin_api);

    g_assert (self->priv->trying_handler == NULL);
    self->priv->trying_handler = g_object_ref (handler);

    self->priv->handler_suitable_pending = 0;

    DEBUG ("%s: channel ACL verification", self->priv->unique_name);

    for (p = mcp_list_objects (); p != NULL; p = g_list_next (p))
    {
        if (MCP_IS_DISPATCH_OPERATION_POLICY (p->data))
        {
            McpDispatchOperationPolicy *plugin = p->data;

            DEBUG ("%s: checking policy for %s",
                G_OBJECT_TYPE_NAME (plugin),
                tp_proxy_get_object_path (handler));

            self->priv->handler_suitable_pending++;
            mcp_dispatch_operation_policy_handler_is_suitable_async (plugin,
                    handler_client,
                    _mcd_client_proxy_get_unique_name (handler),
                    plugin_api,
                    mcd_dispatch_operation_handler_decision_cb,
                    g_object_ref (self));
        }
    }

    if (self->priv->handler_suitable_pending == 0)
    {
        mcd_dispatch_operation_handle_channels (self);
    }
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
            mcd_dispatch_operation_try_handler (self, handler);
            return TRUE;
        }

        /* If the Handler has disappeared, a HandleWith call should fail,
         * but a request (for which the client_bus_name is merely advisory)
         * can legitimately try more handlers. */
        if (approval->type == APPROVAL_TYPE_HANDLE_WITH)
        {
            GError gone = { TP_ERROR,
                TP_ERROR_NOT_IMPLEMENTED,
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
            mcd_dispatch_operation_try_handler (self, handler);
            return TRUE;
        }
    }

    return FALSE;
}

static void
_mcd_dispatch_operation_close_as_undispatchable (McdDispatchOperation *self,
                                                 const GError *error)
{
    /* All of the usable handlers vanished while we were thinking about it
     * (this can only happen if non-activatable handlers exit after we
     * include them in the list of possible handlers, but before we .
     * We should recover in some better way, perhaps by asking all the
     * approvers again (?), but for now we'll just close all the channels. */

    DEBUG ("%s", error->message);
    _mcd_dispatch_operation_finish (self, error->domain, error->code,
                                    "%s", error->message);

    if (self->priv->channel != NULL)
    {
        McdChannel *channel = MCD_CHANNEL (self->priv->channel);
        GError e = { TP_ERROR, TP_ERROR_NOT_AVAILABLE,
            "Handler no longer available" };

        g_object_ref (channel);
        mcd_channel_take_error (channel, g_error_copy (&e));
        _mcd_channel_undispatchable (channel);
        g_object_unref (channel);
    }
}

void
_mcd_dispatch_operation_start_plugin_delay (McdDispatchOperation *self)
{
    g_object_ref (self);
    DEBUG ("%" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
           self->priv->plugins_pending,
           self->priv->plugins_pending + 1);
    self->priv->plugins_pending++;
}

void
_mcd_dispatch_operation_end_plugin_delay (McdDispatchOperation *self)
{
    DEBUG ("%" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT,
           self->priv->plugins_pending,
           self->priv->plugins_pending - 1);
    g_return_if_fail (self->priv->plugins_pending > 0);
    self->priv->plugins_pending--;

    _mcd_dispatch_operation_check_client_locks (self);
    g_object_unref (self);
}

void
_mcd_dispatch_operation_forget_channels (McdDispatchOperation *self)
{
    if (self->priv->channel != NULL)
    {
        /* Take a temporary copy, because self->priv->channels is going
         * to be modified as a result of mcd_mission_abort() */
        McdChannel *channel = g_object_ref (self->priv->channel);

        mcd_mission_abort (MCD_MISSION (channel));
        g_object_unref (channel);
    }

    /* There should now be no channel left (it was aborted) */
    g_return_if_fail (self->priv->channel == NULL);
}

void
_mcd_dispatch_operation_leave_channels (McdDispatchOperation *self,
                                        TpChannelGroupChangeReason reason,
                                        const gchar *message)
{
    if (message == NULL)
    {
        message = "";
    }

    if (self->priv->channel != NULL)
    {
        /* Take a temporary copy, because self->priv->channels could
         * be modified as a result */
        McdChannel *channel = g_object_ref (self->priv->channel);

        _mcd_channel_depart (channel, reason, message);
        g_object_unref (channel);
    }

    _mcd_dispatch_operation_forget_channels (self);
}

void
_mcd_dispatch_operation_close_channels (McdDispatchOperation *self)
{
    if (self->priv->channel != NULL)
    {
        /* Take a temporary copy, because self->priv->channels could
         * be modified as a result */
        McdChannel *channel = g_object_ref (self->priv->channel);

        _mcd_channel_close (channel);
        g_object_unref (channel);
    }

    _mcd_dispatch_operation_forget_channels (self);
}

void
_mcd_dispatch_operation_destroy_channels (McdDispatchOperation *self)
{
    if (self->priv->channel != NULL)
    {
        /* Take a temporary copy, because self->priv->channels could
         * be modified as a result */
        McdChannel *channel = g_object_ref (self->priv->channel);

        _mcd_channel_undispatchable (channel);
        g_object_unref (channel);
    }

    _mcd_dispatch_operation_forget_channels (self);
}

/* This should really be called ..._has_invoked_observers_if_needed,
 * but that name would be ridiculous. */
gboolean
_mcd_dispatch_operation_has_invoked_observers (McdDispatchOperation *self)
{
    return self->priv->invoked_observers_if_needed;
}
