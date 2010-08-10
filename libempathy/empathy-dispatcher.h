/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 */

#ifndef __EMPATHY_DISPATCHER_H__
#define __EMPATHY_DISPATCHER_H__

#include <glib.h>
#include <gio/gio.h>

#include <telepathy-glib/channel.h>

#include "empathy-contact.h"
#include "empathy-dispatch-operation.h"

G_BEGIN_DECLS

#define EMPATHY_TYPE_DISPATCHER         (empathy_dispatcher_get_type ())
#define EMPATHY_DISPATCHER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_DISPATCHER, EmpathyDispatcher))
#define EMPATHY_DISPATCHER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_DISPATCHER, EmpathyDispatcherClass))
#define EMPATHY_IS_DISPATCHER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_DISPATCHER))
#define EMPATHY_IS_DISPATCHER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_DISPATCHER))
#define EMPATHY_DISPATCHER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_DISPATCHER, EmpathyDispatcherClass))

#define EMPATHY_DISPATCHER_NON_USER_ACTION  (G_GINT64_CONSTANT (0))
#define EMPATHY_DISPATCHER_CURRENT_TIME  G_MAXINT64

typedef struct _EmpathyDispatcher      EmpathyDispatcher;
typedef struct _EmpathyDispatcherClass EmpathyDispatcherClass;

struct _EmpathyDispatcher
{
  GObject parent;
  gpointer priv;
};

struct _EmpathyDispatcherClass
{
 GObjectClass parent_class;
};

typedef void (EmpathyDispatcherFindChannelClassCb) (
  GList *channel_classes, gpointer user_data);

GType empathy_dispatcher_get_type (void) G_GNUC_CONST;

/* Requesting 1 to 1 text channels */
void empathy_dispatcher_chat_with_contact_id (TpAccount *account,
  const gchar *contact_id,
  gint64 timestamp);

void  empathy_dispatcher_chat_with_contact (EmpathyContact *contact,
  gint64 timestamp);

/* Request a muc channel */
void empathy_dispatcher_join_muc (TpAccount *account,
  const gchar *roomname,
  gint64 timestamp);

void empathy_dispatcher_find_requestable_channel_classes_async
    (EmpathyDispatcher *dispatcher, TpConnection *connection,
     const gchar *channel_type, guint handle_type,
     EmpathyDispatcherFindChannelClassCb callback, gpointer user_data,
     const char *first_property_name, ...);

GList * empathy_dispatcher_find_requestable_channel_classes
    (EmpathyDispatcher *dispatcher, TpConnection *connection,
     const gchar *channel_type, guint handle_type,
     const char *first_property_name, ...);

/* Get the dispatcher singleton */
EmpathyDispatcher *    empathy_dispatcher_dup_singleton (void);

G_END_DECLS

#endif /* __EMPATHY_DISPATCHER_H__ */
