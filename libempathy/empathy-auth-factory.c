/*
 * empathy-auth-factory.c - Source for EmpathyAuthFactory
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
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

#include "empathy-auth-factory.h"

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/simple-handler.h>

#define DEBUG_FLAG EMPATHY_DEBUG_TLS
#include "empathy-debug.h"
#include "empathy-server-tls-handler.h"
#include "empathy-utils.h"

#include "extensions/extensions.h"

G_DEFINE_TYPE (EmpathyAuthFactory, empathy_auth_factory, G_TYPE_OBJECT);

typedef struct {
  TpBaseClient *handler;
} EmpathyAuthFactoryPriv;

enum {
  NEW_SERVER_TLS_HANDLER,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0, };

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyAuthFactory)

static EmpathyAuthFactory *auth_factory_singleton = NULL;

static void
server_tls_handler_ready_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  EmpathyAuthFactory *self = user_data;
  GError *error = NULL;
  EmpathyServerTLSHandler *handler;

  handler = empathy_server_tls_handler_new_finish (res, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to create a server TLS handler; error %s",
          error->message);
      g_error_free (error);
    }
  else
    {
      g_signal_emit (self, signals[NEW_SERVER_TLS_HANDLER], 0,
          handler);
      g_object_unref (handler);
    }
}

static void
handle_channels_cb (TpSimpleHandler *handler,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    GList *requests_satisfied,
    gint64 user_action_time,
    TpHandleChannelsContext *context,
    gpointer user_data)
{
  TpChannel *channel;
  EmpathyAuthFactory *self = user_data;

  DEBUG ("Handle TLS carrier channels.");

  /* there can't be more than one ServerTLSConnection channels
   * at the same time, for the same connection/account.
   */
  g_assert (g_list_length (channels) == 1);

  channel = channels->data;

  if (tp_proxy_get_invalidated (channel) != NULL)
    goto out;

  if (tp_channel_get_channel_type_id (channel) !=
      EMP_IFACE_QUARK_CHANNEL_TYPE_SERVER_TLS_CONNECTION)
    goto out;

  /* create a handler */
  empathy_server_tls_handler_new_async (channel, server_tls_handler_ready_cb,
      self);

 out:
  tp_handle_channels_context_accept (context);
}

static GObject *
empathy_auth_factory_constructor (GType type,
    guint n_params,
    GObjectConstructParam *params)
{
  GObject *retval;

  if (auth_factory_singleton != NULL)
    {
      retval = g_object_ref (auth_factory_singleton);
    }
  else
    {
      retval = G_OBJECT_CLASS (empathy_auth_factory_parent_class)->constructor
        (type, n_params, params);

      auth_factory_singleton = EMPATHY_AUTH_FACTORY (retval);
      g_object_add_weak_pointer (retval, (gpointer *) &auth_factory_singleton);
    }

  return retval;
}

static void
empathy_auth_factory_init (EmpathyAuthFactory *self)
{
  EmpathyAuthFactoryPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_AUTH_FACTORY, EmpathyAuthFactoryPriv);
  TpDBusDaemon *bus;
  GError *error = NULL;

  self->priv = priv;

  bus = tp_dbus_daemon_dup (&error);
  if (error != NULL)
    {
      g_critical ("Failed to get TpDBusDaemon: %s", error->message);
      g_error_free (error);
      return;
    }

  priv->handler = tp_simple_handler_new (bus, FALSE, FALSE, "Empathy.Auth",
      FALSE, handle_channels_cb, self, NULL);

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
          TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          EMP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION,
          TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          TP_HANDLE_TYPE_NONE, NULL));

  g_object_unref (bus);
}

static void
empathy_auth_factory_finalize (GObject *object)
{
  EmpathyAuthFactoryPriv *priv = GET_PRIV (object);

  if (priv->handler != NULL)
    g_object_unref (priv->handler);

  G_OBJECT_CLASS (empathy_auth_factory_parent_class)->finalize (object);
}

static void
empathy_auth_factory_class_init (EmpathyAuthFactoryClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->constructor = empathy_auth_factory_constructor;
  oclass->finalize = empathy_auth_factory_finalize;

  g_type_class_add_private (klass, sizeof (EmpathyAuthFactoryPriv));

  signals[NEW_SERVER_TLS_HANDLER] =
    g_signal_new ("new-server-tls-handler",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1, EMPATHY_TYPE_SERVER_TLS_HANDLER);
}

EmpathyAuthFactory *
empathy_auth_factory_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_AUTH_FACTORY, NULL);
}

gboolean
empathy_auth_factory_register (EmpathyAuthFactory *self,
    GError **error)
{
  EmpathyAuthFactoryPriv *priv = GET_PRIV (self);

  return tp_base_client_register (priv->handler, error);
}
