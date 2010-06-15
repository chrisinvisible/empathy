/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007-2010 Collabora Ltd.
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
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#include <config.h>

#include <string.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/util.h>

#include <folks/folks.h>

#include <extensions/extensions.h>

#include "empathy-individual-manager.h"
#include "empathy-contact-manager.h"
#include "empathy-contact-list.h"
#include "empathy-marshal.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include "empathy-debug.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyIndividualManager)
typedef struct
{
  FolksIndividualAggregator *aggregator;
  EmpathyContactManager *contact_manager;
  TpProxy *logger;
  /* account object path (gchar *) => GHashTable containing favorite contacts
   * (contact ID (gchar *) => TRUE) */
  GHashTable *favourites;
  TpProxySignalConnection *favourite_contacts_changed_signal;
} EmpathyIndividualManagerPriv;

G_DEFINE_TYPE (EmpathyIndividualManager, empathy_individual_manager,
    G_TYPE_OBJECT);

static EmpathyIndividualManager *manager_singleton = NULL;

static void
individual_group_changed_cb (FolksIndividual *individual,
    gchar *group,
    gboolean is_member,
    EmpathyIndividualManager *self)
{
  g_signal_emit_by_name (self, "groups-changed", individual, group,
      is_member);
}

static void
aggregator_individuals_added_cb (FolksIndividualAggregator *aggregator,
    GList *individuals,
    EmpathyIndividualManager *self)
{
  GList *l;

  for (l = individuals; l; l = l->next)
    {
      g_signal_connect (l->data, "group-changed",
          G_CALLBACK (individual_group_changed_cb), self);
    }

  /* TODO: don't hard-code the reason or message */
  g_signal_emit_by_name (self, "members-changed",
      "individual(s) added", individuals, NULL,
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE, TRUE);
}

static void
aggregator_individuals_removed_cb (FolksIndividualAggregator *aggregator,
    GList *individuals,
    EmpathyIndividualManager *self)
{
  GList *l;

  for (l = individuals; l; l = l->next)
    {
      g_signal_handlers_disconnect_by_func (l->data,
          individual_group_changed_cb, self);
    }

  /* TODO: don't hard-code the reason or message */
  g_signal_emit_by_name (self, "members-changed",
      "individual(s) removed", NULL, individuals,
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE, TRUE);
}

static void
individual_manager_finalize (GObject *object)
{
  EmpathyIndividualManagerPriv *priv = GET_PRIV (object);

  tp_proxy_signal_connection_disconnect (
      priv->favourite_contacts_changed_signal);

  if (priv->logger != NULL)
    g_object_unref (priv->logger);

  if (priv->contact_manager != NULL)
    g_object_unref (priv->contact_manager);

  if (priv->aggregator != NULL)
    g_object_unref (priv->aggregator);

  g_hash_table_destroy (priv->favourites);
}

static GObject *
individual_manager_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *retval;

  if (manager_singleton)
    {
      retval = g_object_ref (manager_singleton);
    }
  else
    {
      retval =
          G_OBJECT_CLASS (empathy_individual_manager_parent_class)->
          constructor (type, n_props, props);

      manager_singleton = EMPATHY_INDIVIDUAL_MANAGER (retval);
      g_object_add_weak_pointer (retval, (gpointer) & manager_singleton);
    }

  return retval;
}

/**
 * empathy_individual_manager_initialized:
 *
 * Reports whether or not the singleton has already been created.
 *
 * There can be instances where you want to access the #EmpathyIndividualManager
 * only if it has been set up for this process.
 *
 * Returns: %TRUE if the #EmpathyIndividualManager singleton has previously
 * been initialized.
 */
gboolean
empathy_individual_manager_initialized (void)
{
  return (manager_singleton != NULL);
}

static void
empathy_individual_manager_class_init (EmpathyIndividualManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = individual_manager_finalize;
  object_class->constructor = individual_manager_constructor;

  g_signal_new ("groups-changed",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      _empathy_marshal_VOID__OBJECT_STRING_BOOLEAN,
      G_TYPE_NONE, 3, FOLKS_TYPE_INDIVIDUAL, G_TYPE_STRING, G_TYPE_BOOLEAN);

  g_signal_new ("members-changed",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      _empathy_marshal_VOID__STRING_OBJECT_OBJECT_UINT,
      G_TYPE_NONE,
      4, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_UINT);

  g_type_class_add_private (object_class,
      sizeof (EmpathyIndividualManagerPriv));
}

static void
empathy_individual_manager_init (EmpathyIndividualManager *self)
{
  EmpathyIndividualManagerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_INDIVIDUAL_MANAGER, EmpathyIndividualManagerPriv);
  TpDBusDaemon *bus;
  GError *error = NULL;

  self->priv = priv;
  priv->contact_manager = empathy_contact_manager_dup_singleton ();

  priv->favourites = g_hash_table_new_full (g_str_hash, g_str_equal,
      (GDestroyNotify) g_free, (GDestroyNotify) g_hash_table_unref);

  priv->aggregator = folks_individual_aggregator_new ();
  if (error == NULL)
    {
      g_signal_connect (priv->aggregator, "individuals-added",
          G_CALLBACK (aggregator_individuals_added_cb), self);
      g_signal_connect (priv->aggregator, "individuals-removed",
          G_CALLBACK (aggregator_individuals_removed_cb), self);
    }
  else
    {
      DEBUG ("Failed to get individual aggregator: %s", error->message);
      g_clear_error (&error);
    }

  bus = tp_dbus_daemon_dup (&error);

  if (error == NULL)
    {
      priv->logger = g_object_new (TP_TYPE_PROXY,
          "bus-name", "org.freedesktop.Telepathy.Logger",
          "object-path",
          "/org/freedesktop/Telepathy/Logger", "dbus-daemon", bus, NULL);
      g_object_unref (bus);

      tp_proxy_add_interface_by_id (priv->logger, EMP_IFACE_QUARK_LOGGER);
    }
  else
    {
      DEBUG ("Failed to get telepathy-logger proxy: %s", error->message);
      g_clear_error (&error);
    }
}

EmpathyIndividualManager *
empathy_individual_manager_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_INDIVIDUAL_MANAGER, NULL);
}

/* TODO: support adding and removing Individuals */

GList *
empathy_individual_manager_get_members (EmpathyIndividualManager *self)
{
  EmpathyIndividualManagerPriv *priv = GET_PRIV (self);
  GHashTable *individuals;

  g_return_val_if_fail (EMPATHY_IS_INDIVIDUAL_MANAGER (self), NULL);

  individuals = folks_individual_aggregator_get_individuals (priv->aggregator);
  return individuals ? g_hash_table_get_values (individuals) : NULL;
}

FolksIndividual *
empathy_individual_manager_lookup_member (EmpathyIndividualManager *self,
    const gchar *id)
{
  EmpathyIndividualManagerPriv *priv = GET_PRIV (self);
  GHashTable *individuals;

  g_return_val_if_fail (EMPATHY_IS_INDIVIDUAL_MANAGER (self), NULL);

  individuals = folks_individual_aggregator_get_individuals (priv->aggregator);
  if (individuals != NULL)
    return g_hash_table_lookup (individuals, id);

  return NULL;
}

void
empathy_individual_manager_remove (EmpathyIndividualManager *self,
    FolksIndividual *individual,
    const gchar *message)
{
  /* TODO: implement */
  DEBUG (G_STRLOC ": individual removal not implemented");
}

EmpathyIndividualManagerFlags
empathy_individual_manager_get_flags_for_connection (
    EmpathyIndividualManager *self,
    TpConnection *connection)
{
  EmpathyIndividualManagerPriv *priv;
  EmpathyContactListFlags list_flags;
  EmpathyIndividualManagerFlags flags;

  g_return_val_if_fail (EMPATHY_IS_INDIVIDUAL_MANAGER (self),
      EMPATHY_INDIVIDUAL_MANAGER_NO_FLAGS);

  priv = GET_PRIV (self);

  list_flags = empathy_contact_manager_get_flags_for_connection (
    priv->contact_manager, connection);

  flags = EMPATHY_INDIVIDUAL_MANAGER_NO_FLAGS;
  if (list_flags & EMPATHY_CONTACT_LIST_CAN_ADD)
    flags |= EMPATHY_INDIVIDUAL_MANAGER_CAN_ADD;
  if (list_flags & EMPATHY_CONTACT_LIST_CAN_REMOVE)
    flags |= EMPATHY_INDIVIDUAL_MANAGER_CAN_REMOVE;
  if (list_flags & EMPATHY_CONTACT_LIST_CAN_ALIAS)
    flags |= EMPATHY_INDIVIDUAL_MANAGER_CAN_ALIAS;
  if (list_flags & EMPATHY_CONTACT_LIST_CAN_GROUP)
    flags |= EMPATHY_INDIVIDUAL_MANAGER_CAN_GROUP;

  return flags;
}