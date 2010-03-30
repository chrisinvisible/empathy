/*
 * empathy-chat-manager.c - Source for EmpathyChatManager
 * Copyright (C) 2010 Collabora Ltd.
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

#include <libempathy/empathy-dispatcher.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#include "empathy-chat-manager.h"

enum {
  CHATS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE(EmpathyChatManager, empathy_chat_manager, G_TYPE_OBJECT)

/* private structure */
typedef struct _EmpathyChatManagerPriv EmpathyChatManagerPriv;

struct _EmpathyChatManagerPriv
{
  GQueue *queue;
};

#define GET_PRIV(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMPATHY_TYPE_CHAT_MANAGER, \
    EmpathyChatManagerPriv))

static EmpathyChatManager *chat_manager_singleton = NULL;

typedef struct
{
  TpAccount *account;
  gchar *id;
  gboolean room;
} ChatData;

static ChatData *
chat_data_new (EmpathyChat *chat)
{
  ChatData *data = NULL;

  data = g_slice_new0 (ChatData);

  data->account = g_object_ref (empathy_chat_get_account (chat));
  data->id = g_strdup (empathy_chat_get_id (chat));
  data->room = empathy_chat_is_room (chat);

  return data;
}

static void
chat_data_free (ChatData *data)
{
  if (data->account != NULL)
    {
      g_object_unref (data->account);
      data->account = NULL;
    }

  if (data->id != NULL)
    {
      g_free (data->id);
      data->id = NULL;
    }

  g_slice_free (ChatData, data);
}

static void
empathy_chat_manager_init (EmpathyChatManager *self)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);

  priv->queue = g_queue_new ();
}

static void
empathy_chat_manager_finalize (GObject *object)
{
  EmpathyChatManager *self = EMPATHY_CHAT_MANAGER (object);
  EmpathyChatManagerPriv *priv = GET_PRIV (self);

  if (priv->queue != NULL)
    {
      g_queue_foreach (priv->queue, (GFunc) chat_data_free, NULL);
      g_queue_free (priv->queue);
      priv->queue = NULL;
    }

  G_OBJECT_CLASS (empathy_chat_manager_parent_class)->finalize (object);
}

static GObject *
empathy_chat_manager_constructor (GType type,
    guint n_construct_params,
    GObjectConstructParam *construct_params)
{
  GObject *retval;

  if (!chat_manager_singleton)
    {
      retval = G_OBJECT_CLASS (empathy_chat_manager_parent_class)->constructor
        (type, n_construct_params, construct_params);

      chat_manager_singleton = EMPATHY_CHAT_MANAGER (retval);
      g_object_add_weak_pointer (retval, (gpointer) &chat_manager_singleton);
    }
  else
    {
      retval = g_object_ref (chat_manager_singleton);
    }

  return retval;
}

static void
empathy_chat_manager_class_init (
  EmpathyChatManagerClass *empathy_chat_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (empathy_chat_manager_class);

  object_class->finalize = empathy_chat_manager_finalize;
  object_class->constructor = empathy_chat_manager_constructor;

  signals[CHATS_CHANGED] =
    g_signal_new ("chats-changed",
        G_TYPE_FROM_CLASS (object_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__UINT,
        G_TYPE_NONE,
        1, G_TYPE_UINT, NULL);

  g_type_class_add_private (empathy_chat_manager_class,
    sizeof (EmpathyChatManagerPriv));
}

EmpathyChatManager *
empathy_chat_manager_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_CHAT_MANAGER, NULL);
}

void
empathy_chat_manager_closed_chat (EmpathyChatManager *self,
    EmpathyChat *chat)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);
  ChatData *data;

  data = chat_data_new (chat);

  DEBUG ("Adding %s to queue: %s", data->room ? "room" : "contact", data->id);

  g_queue_push_tail (priv->queue, data);

  g_signal_emit (self, signals[CHATS_CHANGED], 0,
      g_queue_get_length (priv->queue));
}

static void
connection_ready_cb (TpConnection *connection,
    const GError *error,
    gpointer user_data)
{
  ChatData *data = user_data;
  EmpathyChatManager *self = chat_manager_singleton;
  EmpathyChatManagerPriv *priv;

  /* Extremely unlikely to happen, but I don't really want to keep refs to the
   * chat manager in the ChatData structs as it'll then prevent the manager
   * from being finalized. */
  if (G_UNLIKELY (self == NULL))
    goto out;

  priv = GET_PRIV (self);

  if (error == NULL)
    {
      if (data->room)
        empathy_dispatcher_join_muc (connection, data->id, NULL, NULL);
      else
        empathy_dispatcher_chat_with_contact_id (connection, data->id,
            NULL, NULL);

      g_signal_emit (self, signals[CHATS_CHANGED], 0,
          g_queue_get_length (priv->queue));
    }
  else
    {
      DEBUG ("Error readying connection, no chat: %s", error->message);
    }

out:
  chat_data_free (data);
}

void
empathy_chat_manager_undo_closed_chat (EmpathyChatManager *self)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);
  ChatData *data;
  TpConnection *connection;

  data = g_queue_pop_tail (priv->queue);

  if (data == NULL)
    return;

  DEBUG ("Removing %s from queue and starting a chat with: %s",
      data->room ? "room" : "contact", data->id);

  connection = tp_account_get_connection (data->account);

  if (connection != NULL)
    {
      tp_connection_call_when_ready (connection, connection_ready_cb, data);
    }
  else
    {
      DEBUG ("No connection, no chat.");
      chat_data_free (data);
    }
}

guint
empathy_chat_manager_get_num_chats (EmpathyChatManager *self)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);

  return g_queue_get_length (priv->queue);
}
