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

/* This class doesn't store or ref any of the individuals, since they're already
 * stored and referenced in the aggregator.
 *
 * This class merely forwards along signals from the aggregator and individuals
 * and wraps aggregator functions for other client code. */
typedef struct
{
  FolksIndividualAggregator *aggregator;
  EmpathyContactManager *contact_manager;
} EmpathyIndividualManagerPriv;

enum
{
  FAVOURITES_CHANGED,
  GROUPS_CHANGED,
  MEMBERS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (EmpathyIndividualManager, empathy_individual_manager,
    G_TYPE_OBJECT);

static EmpathyIndividualManager *manager_singleton = NULL;

static void
individual_group_changed_cb (FolksIndividual *individual,
    gchar *group,
    gboolean is_member,
    EmpathyIndividualManager *self)
{
  g_signal_emit (self, signals[GROUPS_CHANGED], 0, individual, group,
      is_member);
}

static void
individual_notify_is_favourite_cb (FolksIndividual *individual,
    GParamSpec *pspec,
    EmpathyIndividualManager *self)
{
  gboolean is_favourite = folks_favourite_get_is_favourite (
      FOLKS_FAVOURITE (individual));
  g_signal_emit (self, signals[FAVOURITES_CHANGED], 0, individual,
      is_favourite);
}

static void
aggregator_individuals_changed_cb (FolksIndividualAggregator *aggregator,
    GList *added,
    GList *removed,
    const char *message,
    FolksPersona *actor,
    guint reason,
    EmpathyIndividualManager *self)
{
  GList *l;

  for (l = added; l; l = l->next)
    {
      g_signal_connect (l->data, "group-changed",
          G_CALLBACK (individual_group_changed_cb), self);
      g_signal_connect (l->data, "notify::is-favourite",
          G_CALLBACK (individual_notify_is_favourite_cb), self);
    }

  for (l = removed; l; l = l->next)
    {
      g_signal_handlers_disconnect_by_func (l->data,
          individual_group_changed_cb, self);
      g_signal_handlers_disconnect_by_func (l->data,
          individual_notify_is_favourite_cb, self);
    }

  g_signal_emit (self, signals[MEMBERS_CHANGED], 0, message,
      added, removed,
      tp_chanel_group_change_reason_from_folks_groups_change_reason (reason),
      TRUE);
}

static void
individual_manager_dispose (GObject *object)
{
  EmpathyIndividualManagerPriv *priv = GET_PRIV (object);

  tp_clear_object (&priv->contact_manager);
  tp_clear_object (&priv->aggregator);

  G_OBJECT_CLASS (empathy_individual_manager_parent_class)->dispose (object);
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

  object_class->dispose = individual_manager_dispose;
  object_class->constructor = individual_manager_constructor;

  signals[GROUPS_CHANGED] =
      g_signal_new ("groups-changed",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          _empathy_marshal_VOID__OBJECT_STRING_BOOLEAN,
          G_TYPE_NONE, 3, FOLKS_TYPE_INDIVIDUAL, G_TYPE_STRING, G_TYPE_BOOLEAN);

  signals[FAVOURITES_CHANGED] =
      g_signal_new ("favourites-changed",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          _empathy_marshal_VOID__OBJECT_BOOLEAN,
          G_TYPE_NONE, 2, FOLKS_TYPE_INDIVIDUAL, G_TYPE_BOOLEAN);

  signals[MEMBERS_CHANGED] =
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

  self->priv = priv;
  priv->contact_manager = empathy_contact_manager_dup_singleton ();

  priv->aggregator = folks_individual_aggregator_new ();
  g_signal_connect (priv->aggregator, "individuals-changed",
      G_CALLBACK (aggregator_individuals_changed_cb), self);
}

EmpathyIndividualManager *
empathy_individual_manager_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_INDIVIDUAL_MANAGER, NULL);
}

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

static void
aggregator_add_persona_from_details_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  FolksIndividualAggregator *aggregator = FOLKS_INDIVIDUAL_AGGREGATOR (source);
  EmpathyContact *contact = EMPATHY_CONTACT (user_data);
  FolksPersona *persona;
  GError *error = NULL;

  persona = folks_individual_aggregator_add_persona_from_details_finish (
      aggregator, result, &error);
  if (error != NULL)
    {
      g_warning ("failed to add individual from contact: %s", error->message);
      g_clear_error (&error);
    }

  /* Set the contact's persona */
  empathy_contact_set_persona (contact, persona);

  /* We can unref the contact now */
  g_object_unref (contact);
  g_object_unref (persona);
}

void
empathy_individual_manager_add_from_contact (EmpathyIndividualManager *self,
    EmpathyContact *contact)
{
  EmpathyIndividualManagerPriv *priv;
  GHashTable* details;
  TpAccount *account;
  const gchar *store_id;

  g_return_if_fail (EMPATHY_IS_INDIVIDUAL_MANAGER (self));
  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  priv = GET_PRIV (self);

  /* We need to ref the contact since otherwise its linked TpHandle will be
   * destroyed. */
  g_object_ref (contact);

  DEBUG ("adding individual from contact %s (%s)",
      empathy_contact_get_id (contact), empathy_contact_get_name (contact));

  account = empathy_contact_get_account (contact);
  store_id = tp_proxy_get_object_path (TP_PROXY (account));

  details = tp_asv_new (
      "contact", G_TYPE_STRING, empathy_contact_get_id (contact),
      NULL);

  folks_individual_aggregator_add_persona_from_details (
      priv->aggregator, NULL, "telepathy", store_id, details,
      aggregator_add_persona_from_details_cb, contact);

  g_hash_table_destroy (details);
}

/**
 * Removes the inner contact from the server (and thus the Individual). Not
 * meant for de-shelling inner personas from an Individual.
 */
void
empathy_individual_manager_remove (EmpathyIndividualManager *self,
    FolksIndividual *individual,
    const gchar *message)
{
  EmpathyIndividualManagerPriv *priv;

  g_return_if_fail (EMPATHY_IS_INDIVIDUAL_MANAGER (self));
  g_return_if_fail (FOLKS_IS_INDIVIDUAL (individual));

  priv = GET_PRIV (self);

  DEBUG ("removing individual %s (%s)",
      folks_individual_get_id (individual),
      folks_individual_get_alias (individual));

  folks_individual_aggregator_remove_individual (priv->aggregator, individual);
}

static void
remove_group_cb (const gchar *id,
    FolksIndividual *individual,
    const gchar *group)
{
  folks_groups_change_group (FOLKS_GROUPS (individual), group, FALSE);
}

void
empathy_individual_manager_remove_group (EmpathyIndividualManager *manager,
    const gchar *group)
{
  EmpathyIndividualManagerPriv *priv;
  GHashTable *individuals;

  g_return_if_fail (EMPATHY_IS_INDIVIDUAL_MANAGER (manager));
  g_return_if_fail (group != NULL);

  priv = GET_PRIV (manager);

  DEBUG ("removing group %s", group);

  /* Remove every individual from the group */
  individuals = folks_individual_aggregator_get_individuals (priv->aggregator);
  g_hash_table_foreach (individuals, (GHFunc) remove_group_cb,
      (gpointer) group);
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
