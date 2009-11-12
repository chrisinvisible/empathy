/* * Copyright (C) 2009 Collabora Ltd.
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
 * Authors: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 */

#include <config.h>
#include <string.h>

#include <libnotify/notification.h>
#include <libnotify/notify.h>

#include <libempathy/empathy-utils.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#include "empathy-notify-manager.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyNotifyManager)

typedef struct
{
  /* owned (gchar *) => TRUE */
  GHashTable *capabilities;

  gboolean dispose_has_run;
} EmpathyNotifyManagerPriv;

G_DEFINE_TYPE (EmpathyNotifyManager, empathy_notify_manager, G_TYPE_OBJECT);

static EmpathyNotifyManager *notify_manager = NULL;

static GObject *
notify_manager_constructor (GType type,
    guint n_construct_params,
    GObjectConstructParam *construct_params)
{
  GObject *retval;
  EmpathyNotifyManagerPriv *priv;
  GList *list, *l;

  if (notify_manager != NULL)
    return g_object_ref (notify_manager);

  retval = G_OBJECT_CLASS (empathy_notify_manager_parent_class)->constructor
    (type, n_construct_params, construct_params);

  notify_manager = EMPATHY_NOTIFY_MANAGER (retval);
  g_object_add_weak_pointer (retval, (gpointer) &notify_manager);

  priv = GET_PRIV (notify_manager);

  /* fetch capabilities */
  list = notify_get_server_caps ();
  for (l = list; l != NULL; l = g_list_next (l))
    {
      gchar *capa = l->data;

      DEBUG ("add capability: %s", capa);
      /* owernship of the string is transfered to the hash table */
      g_hash_table_insert (priv->capabilities, capa, GUINT_TO_POINTER (TRUE));
    }
  g_list_free (list);

  return retval;
}

static void
notify_manager_dispose (GObject *object)
{
  EmpathyNotifyManagerPriv *priv = GET_PRIV (object);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  G_OBJECT_CLASS (empathy_notify_manager_parent_class)->dispose (object);
}

static void
notify_manager_finalize (GObject *object)
{
  EmpathyNotifyManagerPriv *priv = GET_PRIV (object);

  g_hash_table_destroy (priv->capabilities);

  G_OBJECT_CLASS (empathy_notify_manager_parent_class)->finalize (object);
}

static void
empathy_notify_manager_class_init (EmpathyNotifyManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = notify_manager_dispose;
  object_class->finalize = notify_manager_finalize;
  object_class->constructor = notify_manager_constructor;

  g_type_class_add_private (object_class, sizeof (EmpathyNotifyManagerPriv));
}

static void
empathy_notify_manager_init (EmpathyNotifyManager *self)
{
  EmpathyNotifyManagerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_NOTIFY_MANAGER, EmpathyNotifyManagerPriv);

  self->priv = priv;

  priv->capabilities = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      NULL);
}

EmpathyNotifyManager *
empathy_notify_manager_dup_singleton (void)
{
  return EMPATHY_NOTIFY_MANAGER (g_object_new (EMPATHY_TYPE_NOTIFY_MANAGER,
        NULL));
}
