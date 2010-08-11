/* * Copyright (C) 2007-2009 Collabora Ltd.
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
 *
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

#define DISPATCHER_BUS_NAME TP_CLIENT_BUS_NAME_BASE "Empathy"
#define DISPATCHER_OBJECT_PATH TP_CLIENT_OBJECT_PATH_BASE "Empathy"

#include <config.h>

#include <string.h>

#include <glib/gi18n-lib.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/channel-dispatcher.h>
#include <telepathy-glib/channel-request.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/svc-client.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/interfaces.h>

#include <extensions/extensions.h>

#include "empathy-dispatcher.h"
#include "empathy-handler.h"
#include "empathy-utils.h"
#include "empathy-tp-contact-factory.h"
#include "empathy-chatroom-manager.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_DISPATCHER
#include <libempathy/empathy-debug.h>

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyDispatcher)
typedef struct
{
  gboolean dispose_has_run;

  TpAccountManager *account_manager;
  /* connection to connection data mapping */
  GHashTable *connections;
  GHashTable *outstanding_classes_requests;
  gpointer token;

  /* channels which the dispatcher is listening "invalidated" */
  GList *channels;
  GPtrArray *array;

  /* main handler */
  EmpathyHandler *handler;

  /* extra handlers */
  GList *handlers;

  GHashTable *request_channel_class_async_ids;
  /* reffed (TpAccount *) => gulong
   * Signal handler ID of the "status-changed" signal */
  GHashTable *status_changed_handlers;

  TpChannelDispatcher *channel_dispatcher;
  TpDBusDaemon *dbus;
} EmpathyDispatcherPriv;

static GList *
empathy_dispatcher_get_channels (EmpathyHandler *handler,
    gpointer user_data);

static gboolean
empathy_dispatcher_handle_channels (EmpathyHandler *handler,
    const gchar *account_path,
    const gchar *connection_path,
    const GPtrArray *channels,
    const GPtrArray *requests_satisfied,
    guint64 timestamp,
    GHashTable *handler_info,
    gpointer user_data,
    GError **error);

G_DEFINE_TYPE (EmpathyDispatcher, empathy_dispatcher, G_TYPE_OBJECT);

enum
{
  PROP_INTERFACES = 1,
  PROP_HANDLER,
};

enum
{
  OBSERVE,
  DISPATCH,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];
static EmpathyDispatcher *dispatcher = NULL;

static void dispatcher_init_connection_if_needed (
    EmpathyDispatcher *dispatcher,
    TpConnection *connection);

static GList * empathy_dispatcher_find_channel_classes
  (EmpathyDispatcher *dispatcher, TpConnection *connection,
   const gchar *channel_type, guint handle_type, GArray *fixed_properties);


typedef struct
{
  EmpathyDispatcher *dispatcher;
  EmpathyDispatchOperation *operation;
  TpConnection *connection;
  gboolean should_ensure;
  gchar *channel_type;
  guint handle_type;
  guint handle;
  EmpathyContact *contact;
  TpProxyPendingCall *pending_call;

  /* Properties to pass to the channel when requesting it */
  GHashTable *request;
  gint64 timestamp;
  EmpathyDispatcherRequestCb *cb;
  gpointer user_data;
  gpointer *request_data;

  TpChannelRequest *channel_request;
} DispatcherRequestData;

typedef struct
{
  TpChannel *channel;
  /* Channel type specific wrapper object */
  GObject *channel_wrapper;
} DispatchData;

typedef struct
{
  /* ObjectPath => DispatchData.. */
  GHashTable *dispatched_channels;
  /* ObjectPath -> EmpathyDispatchOperations */
  GHashTable *dispatching_channels;

  /* List of DispatcherRequestData */
  GList *outstanding_requests;
  /* List of requestable channel classes */
  GPtrArray *requestable_channels;
} ConnectionData;

typedef struct
{
  EmpathyDispatcher *dispatcher;
  TpConnection *connection;
  char *channel_type;
  guint handle_type;
  GArray *properties;
  EmpathyDispatcherFindChannelClassCb *callback;
  gpointer user_data;
} FindChannelRequest;

static void
empathy_dispatcher_call_create_or_ensure_channel (
    EmpathyDispatcher *dispatcher,
    DispatcherRequestData *request_data);

static void
dispatcher_request_failed (EmpathyDispatcher *dispatcher,
    DispatcherRequestData *request_data,
    const GError *error);

static DispatchData *
new_dispatch_data (TpChannel *channel,
                   GObject *channel_wrapper)
{
  DispatchData *d = g_slice_new0 (DispatchData);
  d->channel = g_object_ref (channel);
  if (channel_wrapper != NULL)
    d->channel_wrapper = g_object_ref (channel_wrapper);

  return d;
}

static void
free_dispatch_data (DispatchData *data)
{
  g_object_unref (data->channel);
  if (data->channel_wrapper != NULL)
    g_object_unref (data->channel_wrapper);

  g_slice_free (DispatchData, data);
}

static DispatcherRequestData *
new_dispatcher_request_data (EmpathyDispatcher *self,
                             TpConnection *connection,
                             const gchar *channel_type,
                             guint handle_type,
                             guint handle,
                             GHashTable *request,
                             gint64 timestamp,
                             EmpathyContact *contact,
                             EmpathyDispatcherRequestCb *cb,
                             gpointer user_data)
{
  DispatcherRequestData *result = g_slice_new0 (DispatcherRequestData);

  result->dispatcher = g_object_ref (self);
  result->connection = connection;

  result->should_ensure = FALSE;

  result->channel_type = g_strdup (channel_type);
  result->handle_type = handle_type;
  result->handle = handle;
  result->request = request;
  result->timestamp = timestamp;

  if (contact != NULL)
    result->contact = g_object_ref (contact);

  result->cb = cb;
  result->user_data = user_data;

  return result;
}

static void
free_dispatcher_request_data (DispatcherRequestData *r)
{
  g_free (r->channel_type);

  if (r->dispatcher != NULL)
    g_object_unref (r->dispatcher);

  if (r->contact != NULL)
    g_object_unref (r->contact);

  if (r->request != NULL)
    g_hash_table_unref (r->request);


  if (r->pending_call != NULL)
    tp_proxy_pending_call_cancel (r->pending_call);

  if (r->channel_request != NULL)
    g_object_unref (r->channel_request);

  g_slice_free (DispatcherRequestData, r);
}

static ConnectionData *
new_connection_data (void)
{
  ConnectionData *cd = g_slice_new0 (ConnectionData);

  cd->dispatched_channels = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) free_dispatch_data);

  cd->dispatching_channels = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);

  return cd;
}

static void
free_connection_data (ConnectionData *cd)
{
  GList *l;
  guint i;

  g_hash_table_destroy (cd->dispatched_channels);
  g_hash_table_destroy (cd->dispatching_channels);

  for (l = cd->outstanding_requests ; l != NULL; l = g_list_delete_link (l,l))
    {
      free_dispatcher_request_data (l->data);
    }

  if (cd->requestable_channels  != NULL)
    {
      for (i = 0 ; i < cd->requestable_channels->len ; i++)
          g_value_array_free (
            g_ptr_array_index (cd->requestable_channels, i));
      g_ptr_array_free (cd->requestable_channels, TRUE);
    }
}

static void
free_find_channel_request (FindChannelRequest *r)
{
  guint idx;
  char *str;

  g_object_unref (r->dispatcher);
  g_free (r->channel_type);

  if (r->properties != NULL)
    {
      for (idx = 0; idx < r->properties->len ; idx++)
        {
          str = g_array_index (r->properties, char *, idx);
          g_free (str);
        }

      g_array_free (r->properties, TRUE);
    }

  g_slice_free (FindChannelRequest, r);
}

static void
dispatcher_connection_invalidated_cb (TpConnection *connection,
                                      guint domain,
                                      gint code,
                                      gchar *message,
                                      EmpathyDispatcher *self)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  ConnectionData *connection_data;

  DEBUG ("Error: %s", message);

  /* Terminate pending requests, if any */
  connection_data = g_hash_table_lookup (priv->connections, connection);
  if (connection_data != NULL)
    {
      GList *l;
      GError *error = NULL;

      error = g_error_new_literal (domain, code, message);

      for (l = connection_data->outstanding_requests; l != NULL;
          l = g_list_next (l))
        {
          DispatcherRequestData *request_data = l->data;

          if (request_data->cb != NULL)
            request_data->cb (NULL, error, request_data->user_data);
        }

      g_error_free (error);
    }

  g_hash_table_remove (priv->connections, connection);
}

static void
dispatch_operation_flush_requests (EmpathyDispatcher *self,
                                   EmpathyDispatchOperation *operation,
                                   GError *error,
                                   ConnectionData *cd)
{
  GList *l;

  l = cd->outstanding_requests;
  while (l != NULL)
    {
      DispatcherRequestData *d = (DispatcherRequestData *) l->data;
      GList *lt = l;

      l = g_list_next (l);

      if (d->operation == operation)
        {
          if (d->cb != NULL)
            {
              if (error != NULL)
                d->cb (NULL, error, d->user_data);
              else
                d->cb (operation, NULL, d->user_data);
            }

          cd->outstanding_requests = g_list_delete_link
            (cd->outstanding_requests, lt);

          free_dispatcher_request_data (d);
        }
    }
}

static void
dispatcher_channel_invalidated_cb (TpProxy *proxy,
                                   guint domain,
                                   gint code,
                                   gchar *message,
                                   EmpathyDispatcher *self)
{
  /* Channel went away... */
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  TpConnection *connection;
  ConnectionData *cd;
  const gchar *object_path;

  connection = tp_channel_borrow_connection (TP_CHANNEL (proxy));

  priv->channels = g_list_remove (priv->channels, proxy);

  cd = g_hash_table_lookup (priv->connections, connection);
  /* Connection itself invalidated? */
  if (cd == NULL)
    return;

  object_path = tp_proxy_get_object_path (proxy);

  DEBUG ("Channel %s invalidated", object_path);

  g_hash_table_remove (cd->dispatched_channels, object_path);
  g_hash_table_remove (cd->dispatching_channels, object_path);
}

static void
dispatch_operation_claimed_cb (EmpathyDispatchOperation *operation,
                               EmpathyDispatcher *self)
{
  /* Our job is done, remove the dispatch operation and mark the channel as
   * dispatched */
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  TpConnection *connection;
  ConnectionData *cd;
  const gchar *object_path;

  connection = empathy_dispatch_operation_get_tp_connection (operation);
  cd = g_hash_table_lookup (priv->connections, connection);
  g_assert (cd != NULL);

  object_path = empathy_dispatch_operation_get_object_path (operation);

  if (g_hash_table_lookup (cd->dispatched_channels, object_path) == NULL)
    {
      DispatchData *d;
      d = new_dispatch_data (
        empathy_dispatch_operation_get_channel (operation),
        empathy_dispatch_operation_get_channel_wrapper (operation));
      g_hash_table_insert (cd->dispatched_channels,
        g_strdup (object_path), d);
    }
  g_hash_table_remove (cd->dispatching_channels, object_path);

  DEBUG ("Channel claimed: %s", object_path);
}

static void
dispatch_operation_ready_cb (EmpathyDispatchOperation *operation,
                             EmpathyDispatcher *self)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  TpConnection *connection;
  ConnectionData *cd;
  EmpathyDispatchOperationState status;

  g_signal_connect (operation, "claimed",
    G_CALLBACK (dispatch_operation_claimed_cb), self);

  /* Signal the observers */
  DEBUG ("Send to observers: %s",
    empathy_dispatch_operation_get_object_path (operation));
  g_signal_emit (self, signals[OBSERVE], 0, operation);

  empathy_dispatch_operation_start (operation);

  /* Signal potential requestors */
  connection =  empathy_dispatch_operation_get_tp_connection (operation);
  cd = g_hash_table_lookup (priv->connections, connection);
  g_assert (cd != NULL);

  g_object_ref (operation);
  g_object_ref (self);

  dispatch_operation_flush_requests (self, operation, NULL, cd);
  status = empathy_dispatch_operation_get_status (operation);
  g_object_unref (operation);

  if (status == EMPATHY_DISPATCHER_OPERATION_STATE_CLAIMED)
    return;

  g_assert (status == EMPATHY_DISPATCHER_OPERATION_STATE_DISPATCHING);
  DEBUG ("Send of for dispatching: %s",
      empathy_dispatch_operation_get_object_path (operation));
  g_signal_emit (self, signals[DISPATCH], 0, operation);

  g_object_unref (self);
}

static void
dispatcher_start_dispatching (EmpathyDispatcher *self,
                              EmpathyDispatchOperation *operation,
                              ConnectionData *cd)
{
  const gchar *object_path =
    empathy_dispatch_operation_get_object_path (operation);

  DEBUG ("Dispatching process started for %s", object_path);

  if (g_hash_table_lookup (cd->dispatching_channels, object_path) == NULL)
    {
      g_hash_table_insert (cd->dispatching_channels,
        g_strdup (object_path), operation);

      switch (empathy_dispatch_operation_get_status (operation))
        {
          case EMPATHY_DISPATCHER_OPERATION_STATE_PREPARING:
            g_signal_connect (operation, "ready",
              G_CALLBACK (dispatch_operation_ready_cb), self);
            break;
          case EMPATHY_DISPATCHER_OPERATION_STATE_PENDING:
            dispatch_operation_ready_cb (operation, self);
            break;
          case EMPATHY_DISPATCHER_OPERATION_STATE_DISPATCHING:
          case EMPATHY_DISPATCHER_OPERATION_STATE_CLAIMED:
          case EMPATHY_DISPATCHER_OPERATION_STATE_INVALIDATED:
          default:
            g_assert_not_reached ();
        }

    }
  else if (empathy_dispatch_operation_get_status (operation) >=
      EMPATHY_DISPATCHER_OPERATION_STATE_PENDING)
    {
      /* Already dispatching and the operation is pending, thus the observers
       * have seen it (if applicable), so we can flush the request right away.
       */
      dispatch_operation_flush_requests (self, operation, NULL, cd);
    }
}

static void
dispatcher_connection_new_channel (EmpathyDispatcher *self,
                                   TpConnection *connection,
                                   const gchar *object_path,
                                   const gchar *channel_type,
                                   guint handle_type,
                                   guint handle,
                                   GHashTable *properties,
                                   gboolean incoming,
                                   const GPtrArray *requests_satisfied,
                                   gint64 timestamp)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  TpChannel         *channel;
  ConnectionData *cd;
  EmpathyDispatchOperation *operation;
  int i;
  /* Channel types we never want to dispatch because they're either deprecated
   * or can't sensibly be dispatch (e.g. channels that should always be
   * requested) */
  const char *blacklist[] = {
    TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
    TP_IFACE_CHANNEL_TYPE_TUBES,
    TP_IFACE_CHANNEL_TYPE_ROOM_LIST,
    NULL
  };

  dispatcher_init_connection_if_needed (self, connection);

  cd = g_hash_table_lookup (priv->connections, connection);

  for (i = 0 ; blacklist[i] != NULL; i++)
    {
      if (!tp_strdiff (channel_type, blacklist[i]))
        {
          DEBUG ("Ignoring blacklisted channel type %s on %s",
            channel_type, object_path);
          return;
        }
    }

  DEBUG ("%s channel of type %s on %s", incoming ? "incoming" : "outgoing",
    channel_type, object_path);

  if (properties == NULL)
    channel = tp_channel_new (connection, object_path, channel_type,
      handle_type, handle, NULL);
  else
    channel = tp_channel_new_from_properties (connection, object_path,
      properties, NULL);

  g_signal_connect (channel, "invalidated",
    G_CALLBACK (dispatcher_channel_invalidated_cb),
    self);

  priv->channels = g_list_prepend (priv->channels, channel);

  operation = empathy_dispatch_operation_new (connection, channel, NULL,
    incoming, timestamp);

  g_object_unref (channel);

  if (!incoming && requests_satisfied != NULL)
    {
      GList *l;
      gboolean found = FALSE;

      l = cd->outstanding_requests;

      while (l != NULL)
        {
          DispatcherRequestData *d = (DispatcherRequestData *) l->data;
          guint n;
          const gchar *path;

          l = g_list_next (l);
          if (d->request == NULL)
            continue;

          if (d->operation != NULL)
            continue;

          path = tp_proxy_get_object_path (d->channel_request);
          for (n = 0; n < requests_satisfied->len ; n++)
            {
              const gchar *p = g_ptr_array_index (requests_satisfied, n);
              if (!tp_strdiff (p, path))
                {
                  DEBUG ("Channel satified request %s"
                   "(already dispatched: %d)", p, found);
                  if (!found)
                    {
                      d->operation = operation;
                      found = TRUE;
                      continue;
                    }
                  else
                    {
                      GError err = { TP_ERRORS, TP_ERROR_NOT_YOURS,
                        "Not yours!" };
                      dispatcher_request_failed (dispatcher, d, &err);
                    }
                }
            }
        }
    }

  dispatcher_start_dispatching (dispatcher, operation, cd);
}

static void
dispatcher_connection_new_channel_with_properties (
    EmpathyDispatcher *self,
    TpConnection *connection,
    const gchar *object_path,
    GHashTable *properties,
    const GPtrArray *requests_satisfied,
    gint64 timestamp)
{
  const gchar *channel_type;
  guint handle_type;
  guint handle;
  gboolean requested;
  gboolean valid;

  channel_type = tp_asv_get_string (properties,
    TP_IFACE_CHANNEL ".ChannelType");
  if (channel_type == NULL)
    {
      g_message ("%s had an invalid ChannelType property", object_path);
      return;
    }

  handle_type = tp_asv_get_uint32 (properties,
    TP_IFACE_CHANNEL ".TargetHandleType", &valid);
  if (!valid)
    {
      g_message ("%s had an invalid TargetHandleType property", object_path);
      return;
    }

  handle = tp_asv_get_uint32 (properties,
    TP_IFACE_CHANNEL ".TargetHandle", &valid);
  if (!valid)
    {
      g_message ("%s had an invalid TargetHandle property", object_path);
      return;
    }

  /* We assume there is no channel dispather, so we're the only one dispatching
   * it. Which means that a requested channel it is outgoing one */
  requested = tp_asv_get_boolean (properties,
    TP_IFACE_CHANNEL ".Requested", &valid);
  if (!valid)
    {
      g_message ("%s had an invalid Requested property", object_path);
      requested = FALSE;
    }

  dispatcher_connection_new_channel (self, connection,
    object_path, channel_type, handle_type, handle, properties, !requested,
    requests_satisfied, timestamp);
}

static void
dispatcher_connection_got_all (TpProxy *proxy,
                               GHashTable *properties,
                               const GError *error,
                               gpointer user_data,
                               GObject *object)
{
  EmpathyDispatcher *self = EMPATHY_DISPATCHER (object);
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  GPtrArray *requestable_channels;

  if (error) {
    DEBUG ("Error: %s", error->message);
    return;
  }

  requestable_channels = tp_asv_get_boxed (properties,
    "RequestableChannelClasses", TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST);

  if (requestable_channels == NULL)
    DEBUG ("No RequestableChannelClasses property !?! on connection");
  else
    {
      ConnectionData *cd;
      GList *requests, *l;
      FindChannelRequest *request;
      GList *retval;

      cd = g_hash_table_lookup (priv->connections, proxy);
      g_assert (cd != NULL);

      cd->requestable_channels = g_boxed_copy (
        TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST, requestable_channels);

      requests = g_hash_table_lookup (priv->outstanding_classes_requests,
          proxy);

      for (l = requests; l != NULL; l = l->next)
        {
          request = l->data;

          retval = empathy_dispatcher_find_channel_classes (self,
              TP_CONNECTION (proxy), request->channel_type,
              request->handle_type, request->properties);
          request->callback (retval, request->user_data);

          free_find_channel_request (request);
          g_list_free (retval);
        }

      g_list_free (requests);

      g_hash_table_remove (priv->outstanding_classes_requests, proxy);
    }
}

static void
connection_ready_cb (TpConnection *connection,
    const GError *error,
    gpointer user_data)
{
  EmpathyDispatcher *self = EMPATHY_DISPATCHER (user_data);

  if (error != NULL)
    {
      DEBUG ("Error: %s", error->message);
      goto out;
    }

  tp_cli_dbus_properties_call_get_all (connection, -1,
      TP_IFACE_CONNECTION_INTERFACE_REQUESTS, dispatcher_connection_got_all,
      NULL, NULL, G_OBJECT (self));

out:
  g_object_unref (self);
}

static void
dispatcher_init_connection_if_needed (EmpathyDispatcher *self,
    TpConnection *connection)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (self);

  if (g_hash_table_lookup (priv->connections, connection) != NULL)
    return;

  g_hash_table_insert (priv->connections, g_object_ref (connection),
    new_connection_data ());

  g_signal_connect (connection, "invalidated",
    G_CALLBACK (dispatcher_connection_invalidated_cb), self);

  /* Ensure to keep the self object alive while the call_when_ready is
   * running */
  g_object_ref (self);
  tp_connection_call_when_ready (connection, connection_ready_cb, self);
}

static void
dispatcher_status_changed_cb (TpAccount *account,
                              guint old_status,
                              guint new_status,
                              guint reason,
                              gchar *dbus_error_name,
                              GHashTable *details,
                              EmpathyDispatcher *self)
{
  TpConnection *conn = tp_account_get_connection (account);

  if (conn != NULL)
    dispatcher_init_connection_if_needed (self, conn);
}

static void
remove_idle_handlers (gpointer key,
                      gpointer value,
                      gpointer user_data)
{
  guint source_id;

  source_id = GPOINTER_TO_UINT (value);
  g_source_remove (source_id);
}

static GObject *
dispatcher_constructor (GType type,
                        guint n_construct_params,
                        GObjectConstructParam *construct_params)
{
  GObject *retval;
  EmpathyDispatcherPriv *priv;

  if (dispatcher != NULL)
    return g_object_ref (dispatcher);

  retval = G_OBJECT_CLASS (empathy_dispatcher_parent_class)->constructor
    (type, n_construct_params, construct_params);

  dispatcher = EMPATHY_DISPATCHER (retval);
  g_object_add_weak_pointer (retval, (gpointer) &dispatcher);

  priv = GET_PRIV (dispatcher);

  if (priv->handler == NULL)
    priv->handler = empathy_handler_new (NULL, NULL, NULL);

  empathy_handler_set_handle_channels_func (priv->handler,
    empathy_dispatcher_handle_channels,
    dispatcher);

  empathy_handler_set_channels_func (priv->handler,
    empathy_dispatcher_get_channels,
    dispatcher);

  return retval;
}

static void
dispatcher_dispose (GObject *object)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (object);
  GHashTableIter iter;
  gpointer connection;
  GList *l;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  for (l = priv->handlers ; l != NULL; l = g_list_next (l))
    g_object_unref (G_OBJECT (l->data));

  g_list_free (priv->handlers);
  priv->handlers = NULL;

  if (priv->handler != NULL)
    g_object_unref (priv->handler);
  priv->handler = NULL;

  g_hash_table_iter_init (&iter, priv->connections);
  while (g_hash_table_iter_next (&iter, &connection, NULL))
    {
      g_signal_handlers_disconnect_by_func (connection,
          dispatcher_connection_invalidated_cb, object);
    }

  g_hash_table_destroy (priv->connections);
  priv->connections = NULL;

  if (priv->channel_dispatcher != NULL)
    g_object_unref (priv->channel_dispatcher);
  priv->channel_dispatcher = NULL;

  if (priv->dbus != NULL)
    g_object_unref (priv->dbus);
  priv->dbus = NULL;

  G_OBJECT_CLASS (empathy_dispatcher_parent_class)->dispose (object);
}

static void
dispatcher_finalize (GObject *object)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (object);
  GList *l;
  GHashTableIter iter;
  gpointer connection;
  GList *list;
  gpointer account, id;

  if (priv->request_channel_class_async_ids != NULL)
    {
      g_hash_table_foreach (priv->request_channel_class_async_ids,
        remove_idle_handlers, NULL);
      g_hash_table_destroy (priv->request_channel_class_async_ids);
    }

  for (l = priv->channels; l; l = l->next)
    {
      g_signal_handlers_disconnect_by_func (l->data,
          dispatcher_channel_invalidated_cb, object);
    }

  g_list_free (priv->channels);

  g_hash_table_iter_init (&iter, priv->outstanding_classes_requests);
  while (g_hash_table_iter_next (&iter, &connection, (gpointer *) &list))
    {
      g_list_foreach (list, (GFunc) free_find_channel_request, NULL);
      g_list_free (list);
    }

  g_hash_table_iter_init (&iter, priv->status_changed_handlers);
  while (g_hash_table_iter_next (&iter, &account, &id))
    {
      g_signal_handler_disconnect (account, GPOINTER_TO_UINT (id));
    }
  g_hash_table_destroy (priv->status_changed_handlers);

  g_object_unref (priv->account_manager);

  g_hash_table_destroy (priv->outstanding_classes_requests);
}

static void
dispatcher_set_property (GObject *object,
  guint property_id,
  const GValue *value,
  GParamSpec *pspec)
{
  EmpathyDispatcher *self = EMPATHY_DISPATCHER (object);
  EmpathyDispatcherPriv *priv = GET_PRIV (self);

  switch (property_id)
    {
      case PROP_HANDLER:
        priv->handler = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
dispatcher_get_property (GObject *object,
  guint property_id,
  GValue *value,
  GParamSpec *pspec)
{
  EmpathyDispatcher *self = EMPATHY_DISPATCHER (object);
  EmpathyDispatcherPriv *priv = GET_PRIV (self);

  switch (property_id)
    {
      case PROP_HANDLER:
        g_value_set_object (value, priv->handler);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
empathy_dispatcher_class_init (EmpathyDispatcherClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  object_class->dispose = dispatcher_dispose;
  object_class->finalize = dispatcher_finalize;
  object_class->constructor = dispatcher_constructor;

  object_class->get_property = dispatcher_get_property;
  object_class->set_property = dispatcher_set_property;

  param_spec = g_param_spec_object ("handler", "handler",
    "The main Telepathy Client Hander object",
    EMPATHY_TYPE_HANDLER,
    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class,
    PROP_HANDLER, param_spec);

  signals[OBSERVE] =
    g_signal_new ("observe",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1, EMPATHY_TYPE_DISPATCH_OPERATION);

  signals[DISPATCH] =
    g_signal_new ("dispatch",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1, EMPATHY_TYPE_DISPATCH_OPERATION);


  g_type_class_add_private (object_class, sizeof (EmpathyDispatcherPriv));
}

static void
connect_account (EmpathyDispatcher *self,
    TpAccount *account)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  TpConnection *conn = tp_account_get_connection (account);
  gulong id;

  id = GPOINTER_TO_UINT (g_hash_table_lookup (priv->status_changed_handlers,
        account));

  if (id != 0)
    return;

  if (conn != NULL)
    dispatcher_status_changed_cb (account, 0, 0, 0, NULL, NULL, self);

  id = g_signal_connect (account, "status-changed",
      G_CALLBACK (dispatcher_status_changed_cb), self);

  g_hash_table_insert (priv->status_changed_handlers, g_object_ref (account),
      GUINT_TO_POINTER (id));
}

static void
account_manager_prepared_cb (GObject *source_object,
                             GAsyncResult *result,
                             gpointer user_data)
{
  GList *accounts, *l;
  EmpathyDispatcher *self = user_data;
  TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (source_object);
  GError *error = NULL;

  if (!tp_account_manager_prepare_finish (account_manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  accounts = tp_account_manager_get_valid_accounts (account_manager);
  for (l = accounts; l; l = l->next)
    {
      TpAccount *a = l->data;

      connect_account (self, a);
    }

  g_list_foreach (accounts, (GFunc) g_object_ref, NULL);
  g_list_free (accounts);
}

static void
account_prepare_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyDispatcher *self = user_data;
  TpAccount *account = TP_ACCOUNT (source_object);
  GError *error = NULL;

  if (!tp_account_prepare_finish (account, result, &error))
    {
      DEBUG ("Failed to prepare account: %s", error->message);
      g_error_free (error);
      return;
    }

  connect_account (self, account);
}

static void
account_validity_changed_cb (TpAccountManager *manager,
    TpAccount *account,
    gboolean valid,
    gpointer user_data)
{
  if (!valid)
    return;

  tp_account_prepare_async (account, NULL, account_prepare_cb, user_data);
}

static void
empathy_dispatcher_init (EmpathyDispatcher *self)
{
  EmpathyDispatcherPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_DISPATCHER, EmpathyDispatcherPriv);

  self->priv = priv;
  priv->account_manager = tp_account_manager_dup ();

  priv->connections = g_hash_table_new_full (g_direct_hash, g_direct_equal,
    g_object_unref, (GDestroyNotify) free_connection_data);

  priv->outstanding_classes_requests = g_hash_table_new_full (g_direct_hash,
    g_direct_equal, g_object_unref, NULL);

  priv->channels = NULL;

  tp_account_manager_prepare_async (priv->account_manager, NULL,
      account_manager_prepared_cb, self);

  tp_g_signal_connect_object (priv->account_manager,
      "account-validity-changed", G_CALLBACK (account_validity_changed_cb),
      self, 0);

  priv->request_channel_class_async_ids = g_hash_table_new (g_direct_hash,
    g_direct_equal);
  priv->status_changed_handlers = g_hash_table_new_full (NULL, NULL,
      (GDestroyNotify) g_object_unref, NULL);

  priv->dbus = tp_dbus_daemon_dup (NULL);
  priv->channel_dispatcher = tp_channel_dispatcher_new (priv->dbus);
}

EmpathyDispatcher *
empathy_dispatcher_new (const gchar *name,
  GPtrArray *filters,
  GStrv capabilities)
{
  EmpathyHandler *handler;
  EmpathyDispatcher *ret;

  g_assert (dispatcher == NULL);
  handler = empathy_handler_new (name, filters, capabilities);

  ret = EMPATHY_DISPATCHER (
    g_object_new (EMPATHY_TYPE_DISPATCHER,
      "handler", handler,
      NULL));
  g_object_unref (handler);

  return ret;
}

EmpathyDispatcher *
empathy_dispatcher_dup_singleton (void)
{
  return EMPATHY_DISPATCHER (g_object_new (EMPATHY_TYPE_DISPATCHER, NULL));
}

static void
dispatcher_request_failed (EmpathyDispatcher *self,
                           DispatcherRequestData *request_data,
                           const GError *error)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  ConnectionData *conn_data;

  conn_data = g_hash_table_lookup (priv->connections,
      request_data->connection);
  if (request_data->cb != NULL)
    request_data->cb (NULL, error, request_data->user_data);

  if (conn_data != NULL)
    {
      conn_data->outstanding_requests =
          g_list_remove (conn_data->outstanding_requests, request_data);
    }
  /* else Connection has been invalidated */

  free_dispatcher_request_data (request_data);
}

void
empathy_dispatcher_chat_with_contact (EmpathyContact *contact,
    gint64 timestamp)
{
  empathy_dispatcher_chat_with_contact_id (
      empathy_contact_get_account (contact), empathy_contact_get_id (contact),
      timestamp);
}

static void
ensure_text_channel_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GError *error = NULL;

  if (!tp_account_channel_request_ensure_channel_finish (
        TP_ACCOUNT_CHANNEL_REQUEST (source), result, &error))
    {
      DEBUG ("Failed to ensure text channel: %s", error->message);
      g_error_free (error);
    }
}

void
empathy_dispatcher_chat_with_contact_id (TpAccount *account,
    const gchar *contact_id,
    gint64 timestamp)
{
  GHashTable *request;
  TpAccountChannelRequest *req;

  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING, contact_id,
      NULL);

  req = tp_account_channel_request_new (account, request, timestamp);

  tp_account_channel_request_ensure_channel_async (req, NULL, NULL,
      ensure_text_channel_cb, NULL);

  g_hash_table_unref (request);
  g_object_unref (req);
}

void
empathy_dispatcher_join_muc (TpAccount *account,
    const gchar *room_name,
    gint64 timestamp)
{
  GHashTable *request;
  TpAccountChannelRequest *req;

  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_ROOM,
      TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING, room_name,
      NULL);

  req = tp_account_channel_request_new (account, request, timestamp);

  tp_account_channel_request_ensure_channel_async (req, NULL, NULL,
      ensure_text_channel_cb, NULL);

  g_hash_table_unref (request);
  g_object_unref (req);
}

static void
dispatcher_channel_request_failed_cb (TpChannelRequest *request,
  const gchar *error,
  const gchar *message,
  gpointer user_data,
  GObject *weak_object)
{
  DispatcherRequestData *request_data = (DispatcherRequestData *) user_data;
  EmpathyDispatcher *self =
      EMPATHY_DISPATCHER (request_data->dispatcher);
  GError *err = NULL;

  request_data->pending_call = NULL;

  DEBUG ("Request failed: %s - %s %s",
    tp_proxy_get_object_path (request),
    error, message);

  tp_proxy_dbus_error_to_gerror (TP_PROXY (request),
    error, message, &err);

  dispatcher_request_failed (self, request_data, err);

  g_error_free (err);
}

static void
dispatcher_channel_request_succeeded_cb (TpChannelRequest *request,
  gpointer user_data,
  GObject *weak_object)
{
  EmpathyDispatcher *self = EMPATHY_DISPATCHER (weak_object);
  EmpathyDispatcherPriv *priv = GET_PRIV (dispatcher);
  DispatcherRequestData *request_data = (DispatcherRequestData *) user_data;
  ConnectionData *conn_data;

  conn_data = g_hash_table_lookup (priv->connections,
    request_data->connection);

  DEBUG ("Request succeeded: %s", tp_proxy_get_object_path (request));

  /* When success gets called the internal request should have been satisfied,
   * if it's still in outstanding_requests and doesn't have an operation
   * assigned to it, the channel got handled by someone else.. */

  if (g_list_find (conn_data->outstanding_requests, request_data) == NULL)
    return;

  if (request_data->operation == NULL)
    {
      GError err = { TP_ERRORS, TP_ERROR_NOT_YOURS, "Not yours!" };
      dispatcher_request_failed (self, request_data, &err);
    }
}

static void
dispatcher_channel_request_proceed_cb (TpChannelRequest *request,
  const GError *error,
  gpointer user_data,
  GObject *weak_object)
{
  EmpathyDispatcher *self = EMPATHY_DISPATCHER (weak_object);
  DispatcherRequestData *request_data = (DispatcherRequestData *) user_data;

  request_data->pending_call = NULL;

  if (error != NULL)
    dispatcher_request_failed (self, request_data, error);
}

static void
dispatcher_create_channel_cb (TpChannelDispatcher *proxy,
    const gchar *request_path,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  DispatcherRequestData *request_data = (DispatcherRequestData *) user_data;
  EmpathyDispatcher *self = EMPATHY_DISPATCHER (request_data->dispatcher);
  EmpathyDispatcherPriv *priv = GET_PRIV (dispatcher);
  TpChannelRequest *request;
  GError *err = NULL;
  TpProxyPendingCall *call;

  request_data->pending_call = NULL;

  if (error != NULL)
    {
      dispatcher_request_failed (self, request_data, error);
      return;
    }

  request = tp_channel_request_new (priv->dbus, request_path, NULL, NULL);
  request_data->channel_request = request;

  if (tp_cli_channel_request_connect_to_failed (request,
      dispatcher_channel_request_failed_cb, request_data,
      NULL, G_OBJECT (self), &err) == NULL)
    {
      dispatcher_request_failed (self, request_data, err);
      g_error_free (err);
      return;
    }

  if (tp_cli_channel_request_connect_to_succeeded (request,
      dispatcher_channel_request_succeeded_cb, request_data,
      NULL, G_OBJECT (self), &err) == NULL)
    {
      dispatcher_request_failed (self, request_data, err);
      g_error_free (err);
      return;
    }

  call = tp_cli_channel_request_call_proceed (request,
    -1, dispatcher_channel_request_proceed_cb,
    request_data, NULL, G_OBJECT (self));

  if (call != NULL)
    request_data->pending_call = call;
}

static void
empathy_dispatcher_call_create_or_ensure_channel (
    EmpathyDispatcher *self,
    DispatcherRequestData *request_data)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (dispatcher);
  TpAccount *account;
  const gchar *handler = "";
  TpProxyPendingCall *call;

  account = empathy_get_account_for_connection (request_data->connection);

  g_assert (account != NULL);

  if (request_data->cb)
    handler = empathy_handler_get_busname (priv->handler);

  if (request_data->should_ensure)
    {
      call = tp_cli_channel_dispatcher_call_ensure_channel (
          priv->channel_dispatcher,
          -1, tp_proxy_get_object_path (TP_PROXY (account)),
          request_data->request, request_data->timestamp, handler,
          dispatcher_create_channel_cb, request_data, NULL, NULL);
    }
  else
    {
      call = tp_cli_channel_dispatcher_call_create_channel (
          priv->channel_dispatcher,
          -1, tp_proxy_get_object_path (TP_PROXY (account)),
          request_data->request, request_data->timestamp, handler,
          dispatcher_create_channel_cb, request_data, NULL,
          G_OBJECT (dispatcher));
    }

  if (call != NULL)
    request_data->pending_call = call;
}

/**
 * empathy_dispatcher_create_channel:
 * @self: the EmpathyDispatcher
 * @connection: the Connection to dispatch on
 * @request: an a{sv} map of properties for the request, i.e. using tp_asv_new()
 * @callback: a callback for when the channel arrives (or NULL)
 * @user_data: optional user data (or NULL)
 *
 * When calling this function, #EmpathyDispatcher takes ownership of your
 * reference to @request. DO NOT unref or destroy @request. When the request is
 * done, @request will be unreferenced. Take another reference if you want to
 * keep it around.
 */
void
empathy_dispatcher_create_channel (EmpathyDispatcher *self,
                                   TpConnection *connection,
                                   GHashTable *request,
                                   gint64 timestamp,
                                   EmpathyDispatcherRequestCb *callback,
                                   gpointer user_data)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  ConnectionData *connection_data;
  DispatcherRequestData *request_data;
  const gchar *channel_type;
  guint handle_type;
  guint handle;
  gboolean valid;

  g_return_if_fail (EMPATHY_IS_DISPATCHER (self));
  g_return_if_fail (TP_IS_CONNECTION (connection));
  g_return_if_fail (request != NULL);

  connection_data = g_hash_table_lookup (priv->connections, connection);
  g_assert (connection_data != NULL);

  channel_type = tp_asv_get_string (request, TP_IFACE_CHANNEL ".ChannelType");

  handle_type = tp_asv_get_uint32 (request,
    TP_IFACE_CHANNEL ".TargetHandleType", &valid);
  if (!valid)
    handle_type = TP_UNKNOWN_HANDLE_TYPE;

  handle = tp_asv_get_uint32 (request, TP_IFACE_CHANNEL ".TargetHandle", NULL);

  request_data  = new_dispatcher_request_data (self, connection,
    channel_type, handle_type, handle, request, timestamp,
    NULL, callback, user_data);

  connection_data->outstanding_requests = g_list_prepend
    (connection_data->outstanding_requests, request_data);

  empathy_dispatcher_call_create_or_ensure_channel (self, request_data);
}

static gboolean
channel_class_matches (GValueArray *class,
                       const char *channel_type,
                       guint handle_type,
                       GArray *fixed_properties)
{
  GHashTable *fprops;
  GValue *v;
  const char *c_type;
  guint h_type;
  gboolean valid;

  v = g_value_array_get_nth (class, 0);

  /* if the class doesn't match channel type discard it. */
  fprops = g_value_get_boxed (v);
  c_type = tp_asv_get_string (fprops, TP_IFACE_CHANNEL ".ChannelType");

  if (tp_strdiff (channel_type, c_type))
    return FALSE;

  /* we have the right channel type, see if the handle type matches */
  h_type = tp_asv_get_uint32 (fprops,
                              TP_IFACE_CHANNEL ".TargetHandleType", &valid);

  if (!valid || (handle_type != h_type && handle_type != TP_UNKNOWN_HANDLE_TYPE))
    return FALSE;

  if (fixed_properties != NULL)
    {
      gpointer h_key, h_val;
      guint idx;
      GHashTableIter iter;
      gboolean found;

      g_hash_table_iter_init (&iter, fprops);

      while (g_hash_table_iter_next (&iter, &h_key, &h_val))
        {
          /* discard ChannelType and TargetHandleType, as we already
           * checked them.
           */
          if (!tp_strdiff ((char *) h_key, TP_IFACE_CHANNEL ".ChannelType") ||
              !tp_strdiff
                ((char *) h_key, TP_IFACE_CHANNEL ".TargetHandleType"))
            continue;

          found = FALSE;

          for (idx = 0; idx < fixed_properties->len; idx++)
            {
              /* if |key| doesn't exist in |fixed_properties|, discard
               * the class.
               */
              if (!tp_strdiff
                    ((char *) h_key,
                     g_array_index (fixed_properties, char *, idx)))
                {
                  found = TRUE;
                  /* exit the for() loop */
                  break;
                }
            }

          if (!found)
            return FALSE;
        }
    }
  else
    {
      /* if no fixed_properties are specified, discard the classes
       * with some fixed properties other than the two we already
       * checked.
       */
      if (g_hash_table_size (fprops) > 2)
        return FALSE;
    }

  return TRUE;
}

static GList *
empathy_dispatcher_find_channel_classes (EmpathyDispatcher *self,
                                         TpConnection *connection,
                                         const gchar *channel_type,
                                         guint handle_type,
                                         GArray *fixed_properties)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  GValueArray *class;
  GPtrArray *classes;
  GList *matching_classes;
  guint i;
  ConnectionData *cd;

  g_return_val_if_fail (channel_type != NULL, NULL);

  cd = g_hash_table_lookup (priv->connections, connection);

  if (cd == NULL)
    return NULL;

  classes = cd->requestable_channels;
  if (classes == NULL)
    return NULL;

  matching_classes = NULL;

  for (i = 0; i < classes->len; i++)
    {
      class = g_ptr_array_index (classes, i);

      if (!channel_class_matches
          (class, channel_type, handle_type, fixed_properties))
        continue;

      matching_classes = g_list_prepend (matching_classes, class);
    }

  return matching_classes;
}

static gboolean
find_channel_class_idle_cb (gpointer user_data)
{
  GList *retval;
  GList *requests;
  FindChannelRequest *request = user_data;
  ConnectionData *cd;
  gboolean is_ready = TRUE;
  EmpathyDispatcherPriv *priv = GET_PRIV (request->dispatcher);

  g_hash_table_remove (priv->request_channel_class_async_ids, request);

  cd = g_hash_table_lookup (priv->connections, request->connection);

  if (cd == NULL)
    is_ready = FALSE;
  else if (cd->requestable_channels == NULL)
    is_ready = FALSE;

  if (is_ready)
    {
      retval = empathy_dispatcher_find_channel_classes (request->dispatcher,
          request->connection, request->channel_type, request->handle_type,
          request->properties);

      request->callback (retval, request->user_data);
      free_find_channel_request (request);
      g_list_free (retval);

      return FALSE;
    }

  requests = g_hash_table_lookup (priv->outstanding_classes_requests,
      request->connection);
  requests = g_list_prepend (requests, request);

  g_hash_table_insert (priv->outstanding_classes_requests,
      request->connection, requests);

  return FALSE;
}

static GArray *
setup_varargs (va_list var_args,
               const char *channel_namespace,
               const char *first_property_name)
{
  const char *name;
  char *name_full;
  GArray *properties;

  if (first_property_name == NULL)
    return NULL;

  name = first_property_name;
  properties = g_array_new (TRUE, TRUE, sizeof (char *));

  while (name != NULL)
    {
      name_full = g_strdup (name);
      properties = g_array_append_val (properties, name_full);
      name = va_arg (var_args, char *);
    }

  return properties;
}

/**
 * empathy_dispatcher_find_requestable_channel_classes:
 * @dispatcher: an #EmpathyDispatcher
 * @connection: a #TpConnection
 * @channel_type: a string identifying the type of the channel to lookup
 * @handle_type: the handle type for the channel, or %TP_UNKNOWN_HANDLE_TYPE
 *               if you don't care about the channel's target handle type
 * @first_property_name: %NULL, or the name of the first fixed property,
 * followed optionally by more names, followed by %NULL.
 *
 * Returns all the channel classes that a client can request for the connection
 * @connection, of the type identified by @channel_type, @handle_type and the
 * fixed properties list.
 * The classes which are compatible with a fixed properties list (i.e. those
 * that will be returned by this function) are intended as those that do not
 * contain any fixed property other than those in the list; note that this
 * doesn't guarantee that all the classes compatible with the list will contain
 * all the requested fixed properties, so the clients will have to filter
 * the returned list themselves.
 * If @first_property_name is %NULL, only the classes with no other fixed
 * properties than ChannelType and TargetHandleType will be returned.
 * Note that this function may return %NULL without performing any lookup if
 * @connection is not ready. To ensure that @connection is always ready,
 * use the empathy_dispatcher_find_requestable_channel_classes_async() variant.
 *
 * Return value: a #GList of #GValueArray objects, where the first element in
 * the array is a #GHashTable of the fixed properties, and the second is
 * a #GStrv of the allowed properties for the class. The list should be free'd
 * with g_list_free() when done, but the objects inside the list are owned
 * by the #EmpathyDispatcher and must not be modified.
 */
GList *
empathy_dispatcher_find_requestable_channel_classes
                                 (EmpathyDispatcher *self,
                                  TpConnection *connection,
                                  const gchar *channel_type,
                                  guint handle_type,
                                  const char *first_property_name,
                                  ...)
{
  va_list var_args;
  GArray *properties;
  EmpathyDispatcherPriv *priv;
  GList *retval;
  guint idx;
  char *str;

  g_return_val_if_fail (EMPATHY_IS_DISPATCHER (self), NULL);
  g_return_val_if_fail (TP_IS_CONNECTION (connection), NULL);
  g_return_val_if_fail (channel_type != NULL, NULL);

  priv = GET_PRIV (self);

  va_start (var_args, first_property_name);

  properties = setup_varargs (var_args, channel_type, first_property_name);

  va_end (var_args);

  retval = empathy_dispatcher_find_channel_classes (self, connection,
    channel_type, handle_type, properties);

  if (properties != NULL)
    {
      /* free the properties array */
      for (idx = 0; idx < properties->len ; idx++)
        {
          str = g_array_index (properties, char *, idx);
          g_free (str);
        }

      g_array_free (properties, TRUE);
    }

  return retval;
}

/**
 * empathy_dispatcher_find_requestable_channel_classes_async:
 * @dispatcher: an #EmpathyDispatcher
 * @connection: a #TpConnection
 * @channel_type: a string identifying the type of the channel to lookup
 * @handle_type: the handle type for the channel
 * @callback: the callback to call when @connection is ready
 * @user_data: the user data to pass to @callback
 * @first_property_name: %NULL, or the name of the first fixed property,
 * followed optionally by more names, followed by %NULL.
 *
 * Please see the documentation of
 * empathy_dispatcher_find_requestable_channel_classes() for a detailed
 * description of this function.
 */
void
empathy_dispatcher_find_requestable_channel_classes_async
                                 (EmpathyDispatcher *self,
                                  TpConnection *connection,
                                  const gchar *channel_type,
                                  guint handle_type,
                                  EmpathyDispatcherFindChannelClassCb callback,
                                  gpointer user_data,
                                  const char *first_property_name,
                                  ...)
{
  va_list var_args;
  GArray *properties;
  FindChannelRequest *request;
  EmpathyDispatcherPriv *priv;
  guint source_id;

  g_return_if_fail (EMPATHY_IS_DISPATCHER (self));
  g_return_if_fail (TP_IS_CONNECTION (connection));
  g_return_if_fail (channel_type != NULL);
  g_return_if_fail (handle_type != 0);

  priv = GET_PRIV (self);

  va_start (var_args, first_property_name);

  properties = setup_varargs (var_args, channel_type, first_property_name);

  va_end (var_args);

  /* append another request for this connection */
  request = g_slice_new0 (FindChannelRequest);
  request->dispatcher = g_object_ref (self);
  request->channel_type = g_strdup (channel_type);
  request->handle_type = handle_type;
  request->connection = connection;
  request->callback = callback;
  request->user_data = user_data;
  request->properties = properties;

  source_id = g_idle_add (find_channel_class_idle_cb, request);

  g_hash_table_insert (priv->request_channel_class_async_ids,
    request, GUINT_TO_POINTER (source_id));
}

static GList *
empathy_dispatcher_get_channels (EmpathyHandler *handler,
  gpointer user_data)
{
  EmpathyDispatcher *self = EMPATHY_DISPATCHER (user_data);
  EmpathyDispatcherPriv *priv = GET_PRIV (self);

  return priv->channels;
}

static gboolean
empathy_dispatcher_handle_channels (EmpathyHandler *handler,
    const gchar *account_path,
    const gchar *connection_path,
    const GPtrArray *channels,
    const GPtrArray *requests_satisfied,
    guint64 timestamp,
    GHashTable *handler_info,
    gpointer user_data,
    GError **error)
{
  EmpathyDispatcher *self = EMPATHY_DISPATCHER (user_data);
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  guint i;
  TpAccount *account;
  TpConnection *connection;

  /* FIXME: should probably find out whether the account manager is prepared
   * before ensuring. See bug #600111. */
  account = tp_account_manager_ensure_account (priv->account_manager,
    account_path);
  g_assert (account != NULL);

  connection = tp_account_ensure_connection (account, connection_path);
  if (connection == NULL)
    {
      g_set_error_literal (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
        "Invalid connection argument");
      return FALSE;
    }

  for (i = 0; i < channels->len ; i++)
    {
      GValueArray *arr = g_ptr_array_index (channels, i);
      const gchar *object_path;
      GHashTable *properties;

      object_path = g_value_get_boxed (g_value_array_get_nth (arr, 0));
      properties = g_value_get_boxed (g_value_array_get_nth (arr, 1));

      dispatcher_connection_new_channel_with_properties (self,
        connection, object_path, properties, requests_satisfied, timestamp);
    }

  return TRUE;
}


EmpathyHandler *
empathy_dispatcher_add_handler (EmpathyDispatcher *self,
    const gchar *name,
    GPtrArray *filters,
    GStrv capabilities)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  EmpathyHandler *handler;

  handler = empathy_handler_new (name, filters, capabilities);
  priv->handlers = g_list_prepend (priv->handlers, handler);

  /* Only set the handle_channels function, the Channel property on the main
   * handler will always report all dispatched channels even if they came from
   * a different Handler */
  empathy_handler_set_handle_channels_func (handler,
    empathy_dispatcher_handle_channels, self);

  return handler;
}

void
empathy_dispatcher_remove_handler (EmpathyDispatcher *self,
  EmpathyHandler *handler)
{
  EmpathyDispatcherPriv *priv = GET_PRIV (self);
  GList *h;

  h = g_list_find (priv->handlers, handler);
  g_return_if_fail (h != NULL);

  priv->handlers = g_list_delete_link (priv->handlers, h);

  g_object_unref (handler);
}
