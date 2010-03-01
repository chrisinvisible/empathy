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
      g_queue_foreach (priv->queue, (GFunc) g_object_unref, NULL);
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
    EmpathyContact *contact)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);

  DEBUG ("Adding contact to queue: %s", empathy_contact_get_id (contact));

  g_queue_push_tail (priv->queue, g_object_ref (contact));

  g_signal_emit (self, signals[CHATS_CHANGED], 0,
      g_queue_get_length (priv->queue));
}

void
empathy_chat_manager_undo_closed_chat (EmpathyChatManager *self)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);
  EmpathyContact *contact;

  contact = g_queue_pop_tail (priv->queue);

  if (contact == NULL)
    return;

  DEBUG ("Removing contact from queue and starting a chat with them: %s",
      empathy_contact_get_id (contact));

  empathy_dispatcher_chat_with_contact (contact, NULL, NULL);

  g_object_unref (contact);

  g_signal_emit (self, signals[CHATS_CHANGED], 0,
      g_queue_get_length (priv->queue));
}

guint
empathy_chat_manager_get_num_chats (EmpathyChatManager *self)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);

  return g_queue_get_length (priv->queue);
}
