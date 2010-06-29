/*
 * empathy-dispatch-operation.c - Source for EmpathyDispatchOperation
 * Copyright (C) 2008 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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


#include <stdio.h>
#include <stdlib.h>

#include <telepathy-glib/interfaces.h>

#include "empathy-dispatch-operation.h"
#include <libempathy/empathy-enum-types.h>
#include <libempathy/empathy-tp-contact-factory.h>

#include "empathy-marshal.h"

#include "extensions/extensions.h"

#define DEBUG_FLAG EMPATHY_DEBUG_DISPATCHER
#include <libempathy/empathy-debug.h>

G_DEFINE_TYPE(EmpathyDispatchOperation, empathy_dispatch_operation,
  G_TYPE_OBJECT)

static void empathy_dispatch_operation_set_status (
  EmpathyDispatchOperation *self, EmpathyDispatchOperationState status);
static void empathy_dispatch_operation_channel_ready_cb (TpChannel *channel,
  const GError *error, gpointer user_data);

/* signal enum */
enum
{
    /* Ready for dispatching */
    READY,
    /* Claimed by a handler */
    CLAIMED,
    /* Error, channel went away, inspecting it failed etc */
    INVALIDATED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum {
  PROP_CONNECTION = 1,
  PROP_CHANNEL,
  PROP_CHANNEL_WRAPPER,
  PROP_CONTACT,
  PROP_INCOMING,
  PROP_STATUS,
  PROP_USER_ACTION_TIME,
};

/* private structure */
typedef struct _EmpathyDispatchOperationPriv \
  EmpathyDispatchOperationPriv;

struct _EmpathyDispatchOperationPriv
{
  gboolean dispose_has_run;
  TpConnection *connection;
  TpChannel *channel;
  GObject *channel_wrapper;
  EmpathyContact *contact;
  EmpathyDispatchOperationState status;
  gboolean incoming;
  gint64 user_action_time;
  gulong invalidated_handler;
  gulong ready_handler;
};

#define GET_PRIV(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMPATHY_TYPE_DISPATCH_OPERATION, \
    EmpathyDispatchOperationPriv))

static void
empathy_dispatch_operation_init (EmpathyDispatchOperation *obj)
{
  //EmpathyDispatchOperationPriv *priv =
  //  GET_PRIV (obj);

  /* allocate any data required by the object here */
}

static void empathy_dispatch_operation_dispose (GObject *object);
static void empathy_dispatch_operation_finalize (GObject *object);

static void
empathy_dispatch_operation_set_property (GObject *object,
  guint property_id, const GValue *value, GParamSpec *pspec)
{
  EmpathyDispatchOperation *operation = EMPATHY_DISPATCH_OPERATION (object);
  EmpathyDispatchOperationPriv *priv = GET_PRIV (operation);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->connection = g_value_dup_object (value);
        break;
      case PROP_CHANNEL:
        priv->channel = g_value_dup_object (value);
        break;
      case PROP_CHANNEL_WRAPPER:
        priv->channel_wrapper = g_value_dup_object (value);
        break;
      case PROP_CONTACT:
        if (priv->contact != NULL)
          g_object_unref (priv->contact);
        priv->contact = g_value_dup_object (value);
        break;
      case PROP_INCOMING:
        priv->incoming = g_value_get_boolean (value);
        break;
      case PROP_USER_ACTION_TIME:
        priv->user_action_time = g_value_get_int64 (value);
        break;
      default:
        g_assert_not_reached ();
    }
}

static void
empathy_dispatch_operation_get_property (GObject *object,
  guint property_id, GValue *value, GParamSpec *pspec)
{
  EmpathyDispatchOperation *operation = EMPATHY_DISPATCH_OPERATION (object);
  EmpathyDispatchOperationPriv *priv = GET_PRIV (operation);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
        break;
      case PROP_CHANNEL:
        g_value_set_object (value, priv->channel);
        break;
      case PROP_CHANNEL_WRAPPER:
        g_value_set_object (value, priv->channel_wrapper);
        break;
      case PROP_CONTACT:
        g_value_set_object (value, priv->contact);
        break;
      case PROP_INCOMING:
        g_value_set_boolean (value, priv->incoming);
        break;
      case PROP_STATUS:
        g_value_set_enum (value, priv->status);
        break;
      case PROP_USER_ACTION_TIME:
        g_value_set_int64 (value, priv->user_action_time);
        break;
      default:
        g_assert_not_reached ();
    }
}

static void
empathy_dispatch_operation_invalidated (TpProxy *proxy, guint domain,
  gint code, char *message, EmpathyDispatchOperation *self)
{
  empathy_dispatch_operation_set_status (self,
    EMPATHY_DISPATCHER_OPERATION_STATE_INVALIDATED);

  g_signal_emit (self, signals[INVALIDATED], 0, domain, code, message);
}

static void
dispatcher_operation_got_contact_cb (TpConnection *connection,
                                     EmpathyContact *contact,
                                     const GError *error,
                                     gpointer user_data,
                                     GObject *self)
{
  EmpathyDispatchOperationPriv *priv = GET_PRIV (self);

  if (error)
    {
      /* FIXME: We should cancel the operation */
      DEBUG ("Error: %s", error->message);
      return;
    }

  if (priv->contact != NULL)
    g_object_unref (priv->contact);
  priv->contact = g_object_ref (contact);
  g_object_notify (G_OBJECT (self), "contact");

  /* Ensure to keep the self object alive while the call_when_ready is
   * running */
  g_object_ref (self);
  tp_channel_call_when_ready (priv->channel,
    empathy_dispatch_operation_channel_ready_cb, self);
}

static void
dispatch_operation_connection_ready (TpConnection *connection,
    const GError *error,
    gpointer user_data)
{
  EmpathyDispatchOperation *self = EMPATHY_DISPATCH_OPERATION (user_data);
  EmpathyDispatchOperationPriv *priv = GET_PRIV (self);
  TpHandle handle;
  TpHandleType handle_type;

  if (error != NULL)
    goto out;

  if (priv->status >= EMPATHY_DISPATCHER_OPERATION_STATE_CLAIMED)
    /* no point to get more information */
    goto out;

  handle = tp_channel_get_handle (priv->channel, &handle_type);
  if (handle_type == TP_HANDLE_TYPE_CONTACT && priv->contact == NULL)
    {
      empathy_tp_contact_factory_get_from_handle (priv->connection, handle,
        dispatcher_operation_got_contact_cb, NULL, NULL, G_OBJECT (self));
    }
  else
    {
      g_object_ref (self);
      tp_channel_call_when_ready (priv->channel,
          empathy_dispatch_operation_channel_ready_cb, self);
    }

out:
  g_object_unref (self);
}

static void
empathy_dispatch_operation_constructed (GObject *object)
{
  EmpathyDispatchOperation *self = EMPATHY_DISPATCH_OPERATION (object);
  EmpathyDispatchOperationPriv *priv = GET_PRIV (self);

  empathy_dispatch_operation_set_status (self,
    EMPATHY_DISPATCHER_OPERATION_STATE_PREPARING);

  priv->invalidated_handler =
    g_signal_connect (priv->channel, "invalidated",
      G_CALLBACK (empathy_dispatch_operation_invalidated), self);

  g_object_ref (self);
  tp_connection_call_when_ready (priv->connection,
          dispatch_operation_connection_ready, object);
}

static void
empathy_dispatch_operation_class_init (
  EmpathyDispatchOperationClass *empathy_dispatch_operation_class)
{
  GObjectClass *object_class =
    G_OBJECT_CLASS (empathy_dispatch_operation_class);
  GParamSpec *param_spec;

  g_type_class_add_private (empathy_dispatch_operation_class,
    sizeof (EmpathyDispatchOperationPriv));

  object_class->set_property = empathy_dispatch_operation_set_property;
  object_class->get_property = empathy_dispatch_operation_get_property;

  object_class->dispose = empathy_dispatch_operation_dispose;
  object_class->finalize = empathy_dispatch_operation_finalize;
  object_class->constructed = empathy_dispatch_operation_constructed;

  signals[READY] = g_signal_new ("ready",
    G_OBJECT_CLASS_TYPE(empathy_dispatch_operation_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  signals[CLAIMED] = g_signal_new ("claimed",
    G_OBJECT_CLASS_TYPE(empathy_dispatch_operation_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  signals[INVALIDATED] = g_signal_new ("invalidated",
    G_OBJECT_CLASS_TYPE(empathy_dispatch_operation_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      _empathy_marshal_VOID__UINT_INT_STRING,
      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_INT, G_TYPE_STRING);

  param_spec = g_param_spec_object ("connection",
    "connection", "The telepathy connection",
    TP_TYPE_CONNECTION,
    G_PARAM_CONSTRUCT_ONLY |
    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION,
                                  param_spec);

  param_spec = g_param_spec_object ("channel",
    "channel", "The telepathy channel",
    TP_TYPE_CHANNEL,
    G_PARAM_CONSTRUCT_ONLY |
    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CHANNEL,
                                  param_spec);

  param_spec = g_param_spec_object ("channel-wrapper",
    "channel wrapper", "The empathy specific channel wrapper",
    G_TYPE_OBJECT,
    G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CHANNEL_WRAPPER,
                                  param_spec);

  param_spec = g_param_spec_object ("contact",
    "contact", "The empathy contact",
    EMPATHY_TYPE_CONTACT,
    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTACT,
                                  param_spec);

  param_spec = g_param_spec_boolean ("incoming",
    "incoming", "Whether or not the channel is incoming",
    FALSE,
    G_PARAM_CONSTRUCT_ONLY |
    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INCOMING,
                                  param_spec);

  param_spec = g_param_spec_enum ("status",
    "status", "Status of the dispatch operation",
    EMPATHY_TYPE_DISPATCH_OPERATION_STATE, 0,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STATUS, param_spec);

  param_spec = g_param_spec_int64 ("user-action-time",
    "user action time", "The user action time of the operation",
    G_MININT64, G_MAXINT64, 0,
    G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_USER_ACTION_TIME,
      param_spec);
}

void
empathy_dispatch_operation_dispose (GObject *object)
{
  EmpathyDispatchOperation *self = EMPATHY_DISPATCH_OPERATION (object);
  EmpathyDispatchOperationPriv *priv =
    GET_PRIV (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_object_unref (priv->connection);

  if (priv->ready_handler != 0)
    g_signal_handler_disconnect (priv->channel_wrapper,
      priv->ready_handler);

  if (priv->channel_wrapper != NULL)
    g_object_unref (priv->channel_wrapper);

  g_signal_handler_disconnect (priv->channel, priv->invalidated_handler);
  g_object_unref (priv->channel);

  if (priv->contact != NULL)
    g_object_unref (priv->contact);

  if (G_OBJECT_CLASS (empathy_dispatch_operation_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_dispatch_operation_parent_class)->dispose (object);
}

void
empathy_dispatch_operation_finalize (GObject *object)
{
  /* free any data held directly by the object here */
  G_OBJECT_CLASS (empathy_dispatch_operation_parent_class)->finalize (object);
}

static void
empathy_dispatch_operation_set_status (EmpathyDispatchOperation *self,
  EmpathyDispatchOperationState status)
{
  EmpathyDispatchOperationPriv *priv = GET_PRIV (self);

  g_assert (status >= priv->status);


  if (priv->status != status)
    {
      DEBUG ("Dispatch operation %s status: %d -> %d",
        empathy_dispatch_operation_get_object_path (self),
        priv->status, status);

      priv->status = status;
      g_object_notify (G_OBJECT (self), "status");

      if (status == EMPATHY_DISPATCHER_OPERATION_STATE_PENDING)
        g_signal_emit (self, signals[READY], 0);
    }
}

static void
empathy_dispatch_operation_channel_ready_cb (TpChannel *channel,
  const GError *error, gpointer user_data)
{
  EmpathyDispatchOperation *self = EMPATHY_DISPATCH_OPERATION (user_data);
  EmpathyDispatchOperationPriv *priv = GET_PRIV (self);

  /* FIXME: remove */

  /* The error will be handled in empathy_dispatch_operation_invalidated */
  if (error != NULL)
    goto out;

  g_assert (channel == priv->channel);

  if (priv->status >= EMPATHY_DISPATCHER_OPERATION_STATE_CLAIMED)
    /* no point to get more information */
    goto out;

  /* If the channel wrapper is defined, we assume it's ready */
  if (priv->channel_wrapper != NULL)
    goto ready;

ready:
  empathy_dispatch_operation_set_status (self,
    EMPATHY_DISPATCHER_OPERATION_STATE_PENDING);
out:
  g_object_unref (self);
}

EmpathyDispatchOperation *
empathy_dispatch_operation_new (TpConnection *connection,
    TpChannel *channel,
    EmpathyContact *contact,
    gboolean incoming,
    gint64 user_action_time)
{
  g_return_val_if_fail (connection != NULL, NULL);
  g_return_val_if_fail (channel != NULL, NULL);

  return g_object_new (EMPATHY_TYPE_DISPATCH_OPERATION,
      "connection", connection,
      "channel", channel,
      "contact", contact,
      "incoming", incoming,
      "user-action-time", user_action_time,
      NULL);
}

void
empathy_dispatch_operation_start (EmpathyDispatchOperation *operation)
{
  EmpathyDispatchOperationPriv *priv;

  g_return_if_fail (EMPATHY_IS_DISPATCH_OPERATION (operation));

  priv = GET_PRIV (operation);

  g_return_if_fail (
    priv->status == EMPATHY_DISPATCHER_OPERATION_STATE_PENDING);

  empathy_dispatch_operation_set_status (operation,
      EMPATHY_DISPATCHER_OPERATION_STATE_DISPATCHING);
}

/* Returns whether or not the operation was successfully claimed */
gboolean
empathy_dispatch_operation_claim (EmpathyDispatchOperation *operation)
{
  EmpathyDispatchOperationPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_DISPATCH_OPERATION (operation), FALSE);

  priv = GET_PRIV (operation);

  if (priv->status == EMPATHY_DISPATCHER_OPERATION_STATE_CLAIMED)
    return FALSE;

  empathy_dispatch_operation_set_status (operation,
    EMPATHY_DISPATCHER_OPERATION_STATE_CLAIMED);

  g_signal_emit (operation, signals[CLAIMED], 0);

  return TRUE;
}

TpConnection *
empathy_dispatch_operation_get_tp_connection (
  EmpathyDispatchOperation *operation)
{
  EmpathyDispatchOperationPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_DISPATCH_OPERATION (operation), NULL);

  priv = GET_PRIV (operation);

  return priv->connection;
}

TpChannel *
empathy_dispatch_operation_get_channel (EmpathyDispatchOperation *operation)
{
  EmpathyDispatchOperationPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_DISPATCH_OPERATION (operation), NULL);

  priv = GET_PRIV (operation);

  return priv->channel;
}

GObject *
empathy_dispatch_operation_get_channel_wrapper (
  EmpathyDispatchOperation *operation)
{
  EmpathyDispatchOperationPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_DISPATCH_OPERATION (operation), NULL);

  priv = GET_PRIV (operation);

  return priv->channel_wrapper;
}

const gchar *
empathy_dispatch_operation_get_channel_type (
  EmpathyDispatchOperation *operation)
{
  EmpathyDispatchOperationPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_DISPATCH_OPERATION (operation), NULL);

  priv = GET_PRIV (operation);

  return tp_channel_get_channel_type (priv->channel);
}

GQuark
empathy_dispatch_operation_get_channel_type_id (
  EmpathyDispatchOperation *operation)
{
  EmpathyDispatchOperationPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_DISPATCH_OPERATION (operation), 0);

  priv = GET_PRIV (operation);

  return tp_channel_get_channel_type_id (priv->channel);
}

const gchar *
empathy_dispatch_operation_get_object_path (
  EmpathyDispatchOperation *operation)
{
  EmpathyDispatchOperationPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_DISPATCH_OPERATION (operation), NULL);

  priv = GET_PRIV (operation);

  return tp_proxy_get_object_path (TP_PROXY (priv->channel));
}

EmpathyDispatchOperationState
empathy_dispatch_operation_get_status (EmpathyDispatchOperation *operation)
{
  EmpathyDispatchOperationPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_DISPATCH_OPERATION (operation),
    EMPATHY_DISPATCHER_OPERATION_STATE_PREPARING);

  priv = GET_PRIV (operation);

  return priv->status;
}

gboolean
empathy_dispatch_operation_is_incoming (EmpathyDispatchOperation *operation)
{
  EmpathyDispatchOperationPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_DISPATCH_OPERATION (operation), FALSE);

  priv = GET_PRIV (operation);

  return priv->incoming;
}

void
empathy_dispatch_operation_set_user_action_time (
    EmpathyDispatchOperation *self,
    gint64 user_action_time)
{
  EmpathyDispatchOperationPriv *priv = GET_PRIV (self);

  priv->user_action_time = user_action_time;
}

gint64
empathy_dispatch_operation_get_user_action_time (EmpathyDispatchOperation *self)
{
  EmpathyDispatchOperationPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_DISPATCH_OPERATION (self), FALSE);

  priv = GET_PRIV (self);

  return priv->user_action_time;
}
