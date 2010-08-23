/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/*
 * Copyright (C) 2007-2009 Collabora Ltd.
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

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#include <folks/folks.h>
#include <folks/folks-telepathy.h>

#ifdef HAVE_GEOCLUE
#include <geoclue/geoclue-geocode.h>
#endif

#include "empathy-contact.h"
#include "empathy-utils.h"
#include "empathy-enum-types.h"
#include "empathy-marshal.h"
#include "empathy-location.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include "empathy-debug.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyContact)
typedef struct {
  TpContact *tp_contact;
  TpAccount *account;
  FolksPersona *persona;
  gchar *id;
  gchar *alias;
  EmpathyAvatar *avatar;
  TpConnectionPresenceType presence;
  guint handle;
  EmpathyCapabilities capabilities;
  gboolean is_user;
  guint hash;
  /* Location is composed of string keys and GValues.
   * Example: a "city" key would have "Helsinki" as string GValue,
   *          a "latitude" would have 65.0 as double GValue.
   *
   * This is a super set of the location stored in TpContact as we can try add
   * more fields by searching the address using geoclue.
   */
  GHashTable *location;
  GHashTable *groups;
} EmpathyContactPriv;

static void contact_finalize (GObject *object);
static void contact_get_property (GObject *object, guint param_id,
    GValue *value, GParamSpec *pspec);
static void contact_set_property (GObject *object, guint param_id,
    const GValue *value, GParamSpec *pspec);

#ifdef HAVE_GEOCLUE
static void update_geocode (EmpathyContact *contact);
#endif

static void empathy_contact_set_location (EmpathyContact *contact,
    GHashTable *location);

static void set_capabilities_from_tp_caps (EmpathyContact *self,
    TpCapabilities *caps);

static void contact_set_avatar (EmpathyContact *contact,
    EmpathyAvatar *avatar);
static void contact_set_avatar_from_tp_contact (EmpathyContact *contact);
static gboolean contact_load_avatar_cache (EmpathyContact *contact,
    const gchar *token);

G_DEFINE_TYPE (EmpathyContact, empathy_contact, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_TP_CONTACT,
  PROP_ACCOUNT,
  PROP_PERSONA,
  PROP_ID,
  PROP_ALIAS,
  PROP_AVATAR,
  PROP_PRESENCE,
  PROP_PRESENCE_MESSAGE,
  PROP_HANDLE,
  PROP_CAPABILITIES,
  PROP_IS_USER,
  PROP_LOCATION
};

enum {
  PRESENCE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

/* TpContact* -> EmpathyContact*, both borrowed ref */
static GHashTable *contacts_table = NULL;

static void
tp_contact_notify_cb (TpContact *tp_contact,
                      GParamSpec *param,
                      GObject *contact)
{
  EmpathyContactPriv *priv = GET_PRIV (contact);

  /* Forward property notifications */
  if (!tp_strdiff (param->name, "alias"))
    g_object_notify (contact, "alias");
  else if (!tp_strdiff (param->name, "presence-type")) {
    TpConnectionPresenceType presence;

    presence = empathy_contact_get_presence (EMPATHY_CONTACT (contact));
    g_signal_emit (contact, signals[PRESENCE_CHANGED], 0, presence,
      priv->presence);
    priv->presence = presence;
    g_object_notify (contact, "presence");
  }
  else if (!tp_strdiff (param->name, "identifier"))
    g_object_notify (contact, "id");
  else if (!tp_strdiff (param->name, "handle"))
    g_object_notify (contact, "handle");
  else if (!tp_strdiff (param->name, "location"))
    {
      GHashTable *location;

      location = tp_contact_get_location (tp_contact);
      /* This will start a geoclue search to find the address if needed */
      empathy_contact_set_location (EMPATHY_CONTACT (contact), location);
    }
  else if (!tp_strdiff (param->name, "capabilities"))
    {
      set_capabilities_from_tp_caps (EMPATHY_CONTACT (contact),
          tp_contact_get_capabilities (tp_contact));
    }
  else if (!tp_strdiff (param->name, "avatar-file"))
    {
      contact_set_avatar_from_tp_contact (EMPATHY_CONTACT (contact));
    }
}

static void
folks_persona_notify_cb (FolksPersona *folks_persona,
                         GParamSpec *param,
                         GObject *contact)
{
  if (!tp_strdiff (param->name, "presence-message"))
    g_object_notify (contact, "presence-message");
}

static void
contact_dispose (GObject *object)
{
  EmpathyContactPriv *priv = GET_PRIV (object);

  if (priv->tp_contact)
    {
      g_hash_table_remove (contacts_table, priv->tp_contact);
      g_signal_handlers_disconnect_by_func (priv->tp_contact,
          tp_contact_notify_cb, object);
      g_object_unref (priv->tp_contact);
    }
  priv->tp_contact = NULL;

  if (priv->account)
    g_object_unref (priv->account);
  priv->account = NULL;

  if (priv->persona)
    {
      g_signal_handlers_disconnect_by_func (priv->persona,
          folks_persona_notify_cb, object);
      g_object_unref (priv->persona);
    }
  priv->persona = NULL;

  if (priv->avatar != NULL)
    {
      empathy_avatar_unref (priv->avatar);
      priv->avatar = NULL;
    }

  if (priv->location != NULL)
    {
      g_hash_table_unref (priv->location);
      priv->location = NULL;
    }

  G_OBJECT_CLASS (empathy_contact_parent_class)->dispose (object);
}

static void
empathy_contact_class_init (EmpathyContactClass *class)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (class);

  object_class->finalize = contact_finalize;
  object_class->dispose = contact_dispose;
  object_class->get_property = contact_get_property;
  object_class->set_property = contact_set_property;

  g_object_class_install_property (object_class,
      PROP_TP_CONTACT,
      g_param_spec_object ("tp-contact",
        "TpContact",
        "The TpContact associated with the contact",
        TP_TYPE_CONTACT,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_ACCOUNT,
      g_param_spec_object ("account",
        "The account",
        "The account associated with the contact",
        TP_TYPE_ACCOUNT,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_PERSONA,
      g_param_spec_object ("persona",
        "Persona",
        "The FolksPersona associated with the contact",
        FOLKS_TYPE_PERSONA,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_ID,
      g_param_spec_string ("id",
        "Contact id",
        "String identifying contact",
        NULL,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_ALIAS,
      g_param_spec_string ("alias",
        "Contact alias",
        "An alias for the contact",
        NULL,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_AVATAR,
      g_param_spec_boxed ("avatar",
        "Avatar image",
        "The avatar image",
        EMPATHY_TYPE_AVATAR,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_PRESENCE,
      g_param_spec_uint ("presence",
        "Contact presence",
        "Presence of contact",
        TP_CONNECTION_PRESENCE_TYPE_UNSET,
        NUM_TP_CONNECTION_PRESENCE_TYPES,
        TP_CONNECTION_PRESENCE_TYPE_UNSET,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_PRESENCE_MESSAGE,
      g_param_spec_string ("presence-message",
        "Contact presence message",
        "Presence message of contact",
        NULL,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_HANDLE,
      g_param_spec_uint ("handle",
        "Contact Handle",
        "The handle of the contact",
        0,
        G_MAXUINT,
        0,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_CAPABILITIES,
      g_param_spec_flags ("capabilities",
        "Contact Capabilities",
        "Capabilities of the contact",
        EMPATHY_TYPE_CAPABILITIES,
        EMPATHY_CAPABILITIES_UNKNOWN,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_IS_USER,
      g_param_spec_boolean ("is-user",
        "Contact is-user",
        "Is contact the user",
        FALSE,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (object_class,
      PROP_LOCATION,
      g_param_spec_boxed ("location",
        "Contact location",
        "Physical location of the contact",
        G_TYPE_HASH_TABLE,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  signals[PRESENCE_CHANGED] =
    g_signal_new ("presence-changed",
                  G_TYPE_FROM_CLASS (class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  _empathy_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE,
                  2, G_TYPE_UINT,
                  G_TYPE_UINT);

  g_type_class_add_private (object_class, sizeof (EmpathyContactPriv));
}

static void
empathy_contact_init (EmpathyContact *contact)
{
  EmpathyContactPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (contact,
    EMPATHY_TYPE_CONTACT, EmpathyContactPriv);

  contact->priv = priv;

  priv->location = NULL;
  priv->groups = NULL;
}

static void
contact_finalize (GObject *object)
{
  EmpathyContactPriv *priv;

  priv = GET_PRIV (object);

  DEBUG ("finalize: %p", object);

  if (priv->groups != NULL)
    g_hash_table_destroy (priv->groups);
  g_free (priv->alias);
  g_free (priv->id);

  G_OBJECT_CLASS (empathy_contact_parent_class)->finalize (object);
}

static void
set_tp_contact (EmpathyContact *contact,
                TpContact *tp_contact)
{
  EmpathyContactPriv *priv = GET_PRIV (contact);
  GHashTable *location;
  TpHandle self_handle;
  TpHandle handle;

  if (tp_contact == NULL)
    return;

  g_assert (priv->tp_contact == NULL);
  priv->tp_contact = g_object_ref (tp_contact);
  priv->presence = empathy_contact_get_presence (contact);

  location = tp_contact_get_location (tp_contact);
  if (location != NULL)
    empathy_contact_set_location (contact, location);

  set_capabilities_from_tp_caps (contact,
      tp_contact_get_capabilities (tp_contact));

  contact_set_avatar_from_tp_contact (contact);

  /* Set is-user property. Note that it could still be the handle is
   * different from the connection's self handle, in the case the handle
   * comes from a group interface. */
  self_handle = tp_connection_get_self_handle (
      tp_contact_get_connection (tp_contact));
  handle = tp_contact_get_handle (tp_contact);
  empathy_contact_set_is_user (contact, self_handle == handle);

  g_signal_connect (priv->tp_contact, "notify",
    G_CALLBACK (tp_contact_notify_cb), contact);
}

static void
contact_get_property (GObject *object,
                      guint param_id,
                      GValue *value,
                      GParamSpec *pspec)
{
  EmpathyContact *contact = EMPATHY_CONTACT (object);

  switch (param_id)
    {
      case PROP_TP_CONTACT:
        g_value_set_object (value, empathy_contact_get_tp_contact (contact));
        break;
      case PROP_ACCOUNT:
        g_value_set_object (value, empathy_contact_get_account (contact));
        break;
      case PROP_PERSONA:
        g_value_set_object (value, empathy_contact_get_persona (contact));
        break;
      case PROP_ID:
        g_value_set_string (value, empathy_contact_get_id (contact));
        break;
      case PROP_ALIAS:
        g_value_set_string (value, empathy_contact_get_alias (contact));
        break;
      case PROP_AVATAR:
        g_value_set_boxed (value, empathy_contact_get_avatar (contact));
        break;
      case PROP_PRESENCE:
        g_value_set_uint (value, empathy_contact_get_presence (contact));
        break;
      case PROP_PRESENCE_MESSAGE:
        g_value_set_string (value, empathy_contact_get_presence_message (contact));
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, empathy_contact_get_handle (contact));
        break;
      case PROP_CAPABILITIES:
        g_value_set_flags (value, empathy_contact_get_capabilities (contact));
        break;
      case PROP_IS_USER:
        g_value_set_boolean (value, empathy_contact_is_user (contact));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    };
}

static void
contact_set_property (GObject *object,
                      guint param_id,
                      const GValue *value,
                      GParamSpec *pspec)
{
  EmpathyContact *contact = EMPATHY_CONTACT (object);
  EmpathyContactPriv *priv = GET_PRIV (object);

  switch (param_id)
    {
      case PROP_TP_CONTACT:
        set_tp_contact (contact, g_value_get_object (value));
        break;
      case PROP_ACCOUNT:
        g_assert (priv->account == NULL);
        priv->account = g_value_dup_object (value);
        break;
      case PROP_PERSONA:
        empathy_contact_set_persona (contact, g_value_get_object (value));
        break;
      case PROP_ID:
        empathy_contact_set_id (contact, g_value_get_string (value));
        break;
      case PROP_ALIAS:
        empathy_contact_set_alias (contact, g_value_get_string (value));
        break;
      case PROP_PRESENCE:
        empathy_contact_set_presence (contact, g_value_get_uint (value));
        break;
      case PROP_PRESENCE_MESSAGE:
        empathy_contact_set_presence_message (contact, g_value_get_string (value));
        break;
      case PROP_HANDLE:
        empathy_contact_set_handle (contact, g_value_get_uint (value));
        break;
      case PROP_CAPABILITIES:
        empathy_contact_set_capabilities (contact, g_value_get_flags (value));
        break;
      case PROP_IS_USER:
        empathy_contact_set_is_user (contact, g_value_get_boolean (value));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    };
}

static EmpathyContact *
empathy_contact_new (TpContact *tp_contact)
{
  g_return_val_if_fail (TP_IS_CONTACT (tp_contact), NULL);

  return g_object_new (EMPATHY_TYPE_CONTACT,
      "tp-contact", tp_contact,
      NULL);
}

EmpathyContact *
empathy_contact_from_tpl_contact (TpAccount *account,
    TplEntity *tpl_entity)
{
  EmpathyContact *retval;
  gboolean is_user;

  g_return_val_if_fail (TPL_IS_ENTITY (tpl_entity), NULL);

  is_user = (TPL_ENTITY_SELF == tpl_entity_get_entity_type (tpl_entity));

  retval = g_object_new (EMPATHY_TYPE_CONTACT,
      "id", tpl_entity_get_alias (tpl_entity),
      "alias", tpl_entity_get_identifier (tpl_entity),
      "account", account,
      "is-user", is_user,
      NULL);

  if (!EMP_STR_EMPTY (tpl_entity_get_avatar_token (tpl_entity)))
    contact_load_avatar_cache (retval,
        tpl_entity_get_avatar_token (tpl_entity));

  return retval;
}

TpContact *
empathy_contact_get_tp_contact (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  priv = GET_PRIV (contact);

  return priv->tp_contact;
}

const gchar *
empathy_contact_get_id (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  priv = GET_PRIV (contact);

  if (priv->tp_contact != NULL)
    return tp_contact_get_identifier (priv->tp_contact);

  return priv->id;
}

void
empathy_contact_set_id (EmpathyContact *contact,
                        const gchar *id)
{
  EmpathyContactPriv *priv;

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));
  g_return_if_fail (id != NULL);

  priv = GET_PRIV (contact);

  /* We temporally ref the contact because it could be destroyed
   * during the signal emition */
  g_object_ref (contact);
  if (tp_strdiff (id, priv->id))
    {
      g_free (priv->id);
      priv->id = g_strdup (id);

      g_object_notify (G_OBJECT (contact), "id");
      if (EMP_STR_EMPTY (priv->alias))
          g_object_notify (G_OBJECT (contact), "alias");
    }

  g_object_unref (contact);
}

const gchar *
empathy_contact_get_alias (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;
  const gchar        *alias;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  priv = GET_PRIV (contact);

  if (priv->tp_contact != NULL)
    alias = tp_contact_get_alias (priv->tp_contact);
  else
    alias = priv->alias;

  if (!EMP_STR_EMPTY (alias))
    return alias;
  else
    return empathy_contact_get_id (contact);
}

void
empathy_contact_set_alias (EmpathyContact *contact,
                          const gchar *alias)
{
  EmpathyContactPriv *priv;
  FolksPersona *persona;

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  priv = GET_PRIV (contact);

  g_object_ref (contact);

  /* Set the alias on the persona if possible */
  persona = empathy_contact_get_persona (contact);
  if (persona != NULL && FOLKS_IS_ALIAS (persona))
    {
      DEBUG ("Setting alias for contact %s to %s",
          empathy_contact_get_id (contact), alias);

      folks_alias_set_alias (FOLKS_ALIAS (persona), alias);
    }

  if (tp_strdiff (alias, priv->alias))
    {
      g_free (priv->alias);
      priv->alias = g_strdup (alias);
      g_object_notify (G_OBJECT (contact), "alias");
    }

  g_object_unref (contact);
}

static void
groups_change_group_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  FolksGroups *groups = FOLKS_GROUPS (source);
  GError *error = NULL;

  folks_groups_change_group_finish (groups, result, &error);
  if (error != NULL)
    {
      g_warning ("failed to change group: %s", error->message);
      g_clear_error (&error);
    }
}

void
empathy_contact_change_group (EmpathyContact *contact, const gchar *group,
    gboolean is_member)
{
  EmpathyContactPriv *priv;
  FolksPersona *persona;

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));
  g_return_if_fail (group != NULL);

  priv = GET_PRIV (contact);

  /* Normally pass through the changes to the persona */
  persona = empathy_contact_get_persona (contact);
  if (persona != NULL)
    {
      if (FOLKS_IS_GROUPS (persona))
        folks_groups_change_group (FOLKS_GROUPS (persona), group, is_member,
          groups_change_group_cb, contact);
      return;
    }

  /* If the persona doesn't exist yet, we have to cache the changes until it
   * does */
  if (priv->groups == NULL)
    {
      priv->groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
          NULL);
    }

  g_hash_table_insert (priv->groups, g_strdup (group),
      GUINT_TO_POINTER (is_member));
}

EmpathyAvatar *
empathy_contact_get_avatar (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  priv = GET_PRIV (contact);

  return priv->avatar;
}

static void
contact_set_avatar (EmpathyContact *contact,
                    EmpathyAvatar *avatar)
{
  EmpathyContactPriv *priv;

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  priv = GET_PRIV (contact);

  if (priv->avatar == avatar)
    return;

  if (priv->avatar)
    {
      empathy_avatar_unref (priv->avatar);
      priv->avatar = NULL;
    }

  if (avatar)
      priv->avatar = empathy_avatar_ref (avatar);

  g_object_notify (G_OBJECT (contact), "avatar");
}

TpAccount *
empathy_contact_get_account (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  priv = GET_PRIV (contact);

  if (priv->account == NULL && priv->tp_contact != NULL)
    {
      TpConnection *connection;

      /* FIXME: This assume the account manager already exists */
      connection = tp_contact_get_connection (priv->tp_contact);
      priv->account =
        g_object_ref (empathy_get_account_for_connection (connection));
    }

  return priv->account;
}

FolksPersona *
empathy_contact_get_persona (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  priv = GET_PRIV (contact);

  if (priv->persona == NULL && priv->tp_contact != NULL)
    {
      /* FIXME: This is disgustingly slow */
      /* Query for the persona */
      EmpathyIndividualManager *manager;
      GList *individuals, *l;

      manager = empathy_individual_manager_dup_singleton ();
      individuals = empathy_individual_manager_get_members (manager);

      for (l = individuals; l != NULL; l = l->next)
        {
          GList *personas, *j;
          FolksIndividual *individual = FOLKS_INDIVIDUAL (l->data);

          personas = folks_individual_get_personas (individual);
          for (j = personas; j != NULL; j = j->next)
            {
              TpfPersona *persona = j->data;

              if (TPF_IS_PERSONA (persona))
                {
                  TpContact *tp_contact = tpf_persona_get_contact (persona);

                  if (tp_contact == priv->tp_contact)
                    {
                      /* Found the right persona */
                      empathy_contact_set_persona (contact,
                          (FolksPersona *) persona);
                      goto finished;
                    }
                }
            }
        }

finished:
      g_list_free (individuals);
      g_object_unref (manager);
    }

  return priv->persona;
}

void
empathy_contact_set_persona (EmpathyContact *contact,
    FolksPersona *persona)
{
  EmpathyContactPriv *priv;

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));
  g_return_if_fail (TPF_IS_PERSONA (persona));

  priv = GET_PRIV (contact);

  if (persona == priv->persona)
    return;

  if (priv->persona != NULL)
    g_object_unref (priv->persona);
  priv->persona = g_object_ref (persona);

  g_signal_connect (priv->persona, "notify",
    G_CALLBACK (folks_persona_notify_cb), contact);

  g_object_notify (G_OBJECT (contact), "persona");

  /* Set the persona's alias, since ours could've been set using
   * empathy_contact_set_alias() before we had a persona; this happens when
   * adding a contact. */
  if (priv->alias != NULL)
    empathy_contact_set_alias (contact, priv->alias);

  /* Set the persona's groups */
  if (priv->groups != NULL)
    {
      folks_groups_set_groups (FOLKS_GROUPS (persona), priv->groups);
      g_hash_table_destroy (priv->groups);
      priv->groups = NULL;
    }
}

TpConnection *
empathy_contact_get_connection (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  priv = GET_PRIV (contact);

  if (priv->tp_contact != NULL)
    return tp_contact_get_connection (priv->tp_contact);

  return NULL;
}

TpConnectionPresenceType
empathy_contact_get_presence (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact),
    TP_CONNECTION_PRESENCE_TYPE_UNSET);

  priv = GET_PRIV (contact);

  if (priv->tp_contact != NULL)
    return tp_contact_get_presence_type (priv->tp_contact);

  return priv->presence;
}

void
empathy_contact_set_presence (EmpathyContact *contact,
                              TpConnectionPresenceType presence)
{
  EmpathyContactPriv *priv;
  TpConnectionPresenceType old_presence;

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  priv = GET_PRIV (contact);

  if (presence == priv->presence)
    return;

  old_presence = priv->presence;
  priv->presence = presence;

  g_signal_emit (contact, signals[PRESENCE_CHANGED], 0, presence, old_presence);

  g_object_notify (G_OBJECT (contact), "presence");
}

const gchar *
empathy_contact_get_presence_message (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  priv = GET_PRIV (contact);

  if (priv->persona != NULL)
    return folks_presence_get_presence_message (FOLKS_PRESENCE (priv->persona));

  return NULL;
}

void
empathy_contact_set_presence_message (EmpathyContact *contact,
                                      const gchar *message)
{
  EmpathyContactPriv *priv = GET_PRIV (contact);

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  if (priv->persona != NULL)
    {
      folks_presence_set_presence_message (FOLKS_PRESENCE (priv->persona),
          message);
    }
}

guint
empathy_contact_get_handle (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), 0);

  priv = GET_PRIV (contact);

  if (priv->tp_contact != NULL)
    return tp_contact_get_handle (priv->tp_contact);

  return priv->handle;
}

void
empathy_contact_set_handle (EmpathyContact *contact,
                            guint handle)
{
  EmpathyContactPriv *priv;

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  priv = GET_PRIV (contact);

  g_object_ref (contact);
  if (handle != priv->handle)
    {
      priv->handle = handle;
      g_object_notify (G_OBJECT (contact), "handle");
    }
  g_object_unref (contact);
}

EmpathyCapabilities
empathy_contact_get_capabilities (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), 0);

  priv = GET_PRIV (contact);

  return priv->capabilities;
}

void
empathy_contact_set_capabilities (EmpathyContact *contact,
                                  EmpathyCapabilities capabilities)
{
  EmpathyContactPriv *priv;

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  priv = GET_PRIV (contact);

  if (priv->capabilities == capabilities)
    return;

  priv->capabilities = capabilities;

  g_object_notify (G_OBJECT (contact), "capabilities");
}

gboolean
empathy_contact_is_user (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), FALSE);

  priv = GET_PRIV (contact);

  return priv->is_user;
}

void
empathy_contact_set_is_user (EmpathyContact *contact,
                             gboolean is_user)
{
  EmpathyContactPriv *priv;

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  priv = GET_PRIV (contact);

  if (priv->is_user == is_user)
    return;

  priv->is_user = is_user;

  g_object_notify (G_OBJECT (contact), "is-user");
}

gboolean
empathy_contact_is_online (EmpathyContact *contact)
{
  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), FALSE);

  switch (empathy_contact_get_presence (contact))
    {
      case TP_CONNECTION_PRESENCE_TYPE_OFFLINE:
      case TP_CONNECTION_PRESENCE_TYPE_UNKNOWN:
      case TP_CONNECTION_PRESENCE_TYPE_ERROR:
        return FALSE;
      /* Contacts without presence are considered online so we can display IRC
       * contacts in rooms. */
      case TP_CONNECTION_PRESENCE_TYPE_UNSET:
      case TP_CONNECTION_PRESENCE_TYPE_AVAILABLE:
      case TP_CONNECTION_PRESENCE_TYPE_AWAY:
      case TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY:
      case TP_CONNECTION_PRESENCE_TYPE_HIDDEN:
      case TP_CONNECTION_PRESENCE_TYPE_BUSY:
      default:
        return TRUE;
    }
}

const gchar *
empathy_contact_get_status (EmpathyContact *contact)
{
  const gchar *message;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), "");

  message = empathy_contact_get_presence_message (contact);
  if (!EMP_STR_EMPTY (message))
    return message;

  return empathy_presence_get_default_message (
      empathy_contact_get_presence (contact));
}

gboolean
empathy_contact_can_voip (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), FALSE);

  priv = GET_PRIV (contact);

  return priv->capabilities & (EMPATHY_CAPABILITIES_AUDIO |
      EMPATHY_CAPABILITIES_VIDEO);
}

gboolean
empathy_contact_can_voip_audio (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), FALSE);

  priv = GET_PRIV (contact);

  return priv->capabilities & EMPATHY_CAPABILITIES_AUDIO;
}

gboolean
empathy_contact_can_voip_video (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), FALSE);

  priv = GET_PRIV (contact);

  return priv->capabilities & EMPATHY_CAPABILITIES_VIDEO;
}

gboolean
empathy_contact_can_send_files (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), FALSE);

  priv = GET_PRIV (contact);

  return priv->capabilities & EMPATHY_CAPABILITIES_FT;
}

gboolean
empathy_contact_can_use_rfb_stream_tube (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), FALSE);

  priv = GET_PRIV (contact);

  return priv->capabilities & EMPATHY_CAPABILITIES_RFB_STREAM_TUBE;
}

static gchar *
contact_get_avatar_filename (EmpathyContact *contact,
                             const gchar *token)
{
  TpAccount *account;
  gchar *avatar_path;
  gchar *avatar_file;
  gchar *token_escaped;

  if (EMP_STR_EMPTY (empathy_contact_get_id (contact)))
    return NULL;

  token_escaped = tp_escape_as_identifier (token);
  account = empathy_contact_get_account (contact);

  avatar_path = g_build_filename (g_get_user_cache_dir (),
      "telepathy",
      "avatars",
      tp_account_get_connection_manager (account),
      tp_account_get_protocol (account),
      NULL);
  g_mkdir_with_parents (avatar_path, 0700);

  avatar_file = g_build_filename (avatar_path, token_escaped, NULL);

  g_free (token_escaped);
  g_free (avatar_path);

  return avatar_file;
}

static gboolean
contact_load_avatar_cache (EmpathyContact *contact,
                           const gchar *token)
{
  EmpathyAvatar *avatar = NULL;
  gchar *filename;
  gchar *data = NULL;
  gsize len;
  GError *error = NULL;

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), FALSE);
  g_return_val_if_fail (!EMP_STR_EMPTY (token), FALSE);

  /* Load the avatar from file if it exists */
  filename = contact_get_avatar_filename (contact, token);
  if (filename && g_file_test (filename, G_FILE_TEST_EXISTS))
    {
      if (!g_file_get_contents (filename, &data, &len, &error))
        {
          DEBUG ("Failed to load avatar from cache: %s",
            error ? error->message : "No error given");
          g_clear_error (&error);
        }
    }

  if (data)
    {
      DEBUG ("Avatar loaded from %s", filename);
      avatar = empathy_avatar_new ((guchar *) data, len, NULL, g_strdup (token),
          filename);
      contact_set_avatar (contact, avatar);
      empathy_avatar_unref (avatar);
    }
  else
    {
      g_free (filename);
    }

  return data != NULL;
}

GType
empathy_avatar_get_type (void)
{
  static GType type_id = 0;

  if (!type_id)
    {
      type_id = g_boxed_type_register_static ("EmpathyAvatar",
          (GBoxedCopyFunc) empathy_avatar_ref,
          (GBoxedFreeFunc) empathy_avatar_unref);
    }

  return type_id;
}

/**
 * empathy_avatar_new:
 * @data: the avatar data
 * @len: the size of avatar data
 * @format: the mime type of the avatar image
 * @token: the token of the avatar
 * @filename: the filename where the avatar is stored in cache
 *
 * Create a #EmpathyAvatar from the provided data. This function takes the
 * ownership of @data, @format, @token and @filename.
 *
 * Returns: a new #EmpathyAvatar
 */
EmpathyAvatar *
empathy_avatar_new (guchar *data,
                    gsize len,
                    gchar *format,
                    gchar *token,
                    gchar *filename)
{
  EmpathyAvatar *avatar;

  avatar = g_slice_new0 (EmpathyAvatar);
  avatar->data = data;
  avatar->len = len;
  avatar->format = format;
  avatar->token = token;
  avatar->filename = filename;
  avatar->refcount = 1;

  return avatar;
}

void
empathy_avatar_unref (EmpathyAvatar *avatar)
{
  g_return_if_fail (avatar != NULL);

  avatar->refcount--;
  if (avatar->refcount == 0)
    {
      g_free (avatar->data);
      g_free (avatar->format);
      g_free (avatar->token);
      g_free (avatar->filename);
      g_slice_free (EmpathyAvatar, avatar);
    }
}

EmpathyAvatar *
empathy_avatar_ref (EmpathyAvatar *avatar)
{
  g_return_val_if_fail (avatar != NULL, NULL);

  avatar->refcount++;

  return avatar;
}

/**
 * empathy_avatar_save_to_file:
 * @avatar: the avatar
 * @filename: name of a file to write avatar to
 * @error: return location for a GError, or NULL
 *
 * Save the avatar to a file named filename
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
empathy_avatar_save_to_file (EmpathyAvatar *self,
                             const gchar *filename,
                             GError **error)
{
  return g_file_set_contents (filename, (const gchar *) self->data, self->len,
      error);
}

/**
 * empathy_contact_get_location:
 * @contact: an #EmpathyContact
 *
 * Returns the user's location if available.  The keys are defined in
 * empathy-location.h. If the contact doesn't have location
 * information, the GHashTable will be empthy. Use #g_hash_table_unref when
 * you are done with the #GHashTable.
 *
 * It is composed of string keys and GValues.  Keys are
 * defined in empathy-location.h such as #EMPATHY_LOCATION_COUNTRY.
 * Example: a "city" key would have "Helsinki" as string GValue,
 *          a "latitude" would have 65.0 as double GValue.
 *
 * Returns: a #GHashTable of location values
 */
GHashTable *
empathy_contact_get_location (EmpathyContact *contact)
{
  EmpathyContactPriv *priv;

  g_return_val_if_fail (EMPATHY_CONTACT (contact), NULL);

  priv = GET_PRIV (contact);

  return priv->location;
}

/**
 * empathy_contact_set_location:
 * @contact: an #EmpathyContact
 * @location: a #GHashTable of the location
 *
 * Sets the user's location based on the location #GHashTable passed.
 * It is composed of string keys and GValues.  Keys are
 * defined in empathy-location.h such as #EMPATHY_LOCATION_COUNTRY.
 * Example: a "city" key would have "Helsinki" as string GValue,
 *          a "latitude" would have 65.0 as double GValue.
 */
static void
empathy_contact_set_location (EmpathyContact *contact,
    GHashTable *location)
{
  EmpathyContactPriv *priv;

  g_return_if_fail (EMPATHY_CONTACT (contact));
  g_return_if_fail (location != NULL);

  priv = GET_PRIV (contact);

  if (priv->location != NULL)
    g_hash_table_unref (priv->location);

  priv->location = g_hash_table_ref (location);
#ifdef HAVE_GEOCLUE
  update_geocode (contact);
#endif
  g_object_notify (G_OBJECT (contact), "location");
}

/**
 * empathy_contact_equal:
 * @contact1: an #EmpathyContact
 * @contact2: an #EmpathyContact
 *
 * Returns FALSE if one of the contacts is NULL but the other is not.
 * Otherwise returns TRUE if both pointer are equal or if they bith
 * refer to the same id.
 * It's only necessary to call this function if your contact objects
 * come from logs where contacts are created dynamically and comparing
 * pointers is not enough.
 */
gboolean
empathy_contact_equal (gconstpointer contact1,
                       gconstpointer contact2)
{
  EmpathyContact *c1;
  EmpathyContact *c2;
  const gchar *id1;
  const gchar *id2;

  if ((contact1 == NULL) != (contact2 == NULL)) {
    return FALSE;
  }
  if (contact1 == contact2) {
    return TRUE;
  }
  c1 = EMPATHY_CONTACT (contact1);
  c2 = EMPATHY_CONTACT (contact2);
  id1 = empathy_contact_get_id (c1);
  id2 = empathy_contact_get_id (c2);
  if (!tp_strdiff (id1, id2)) {
    return TRUE;
  }
  return FALSE;
}

#ifdef HAVE_GEOCLUE
#define GEOCODE_SERVICE "org.freedesktop.Geoclue.Providers.Yahoo"
#define GEOCODE_PATH "/org/freedesktop/Geoclue/Providers/Yahoo"

/* This callback is called by geoclue when it found a position
 * for the given address.  A position is necessary for a contact
 * to show up on the map
 */
static void
geocode_cb (GeoclueGeocode *geocode,
    GeocluePositionFields fields,
    double latitude,
    double longitude,
    double altitude,
    GeoclueAccuracy *accuracy,
    GError *error,
    gpointer contact)
{
  EmpathyContactPriv *priv = GET_PRIV (contact);
  GHashTable *new_location;

  if (priv->location == NULL)
    goto out;

  if (error != NULL)
    {
      DEBUG ("Error geocoding location : %s", error->message);
      goto out;
    }

  /* No need to change location if we didn't find the position */
  if (!(fields & GEOCLUE_POSITION_FIELDS_LATITUDE))
    goto out;

  if (!(fields & GEOCLUE_POSITION_FIELDS_LONGITUDE))
    goto out;

  new_location = tp_asv_new (
      EMPATHY_LOCATION_LAT, G_TYPE_DOUBLE, latitude,
      EMPATHY_LOCATION_LON, G_TYPE_DOUBLE, longitude,
      NULL);

  DEBUG ("\t - Latitude: %f", latitude);
  DEBUG ("\t - Longitude: %f", longitude);

  /* Copy remaning fields. LAT and LON were not defined so we won't overwrite
   * the values we just set. */
  tp_g_hash_table_update (new_location, priv->location,
      (GBoxedCopyFunc) g_strdup, (GBoxedCopyFunc) tp_g_value_slice_dup);

  /* Set the altitude only if it wasn't defined before */
  if (fields & GEOCLUE_POSITION_FIELDS_ALTITUDE &&
      g_hash_table_lookup (new_location, EMPATHY_LOCATION_ALT) == NULL)
    {
      tp_asv_set_double (new_location, g_strdup (EMPATHY_LOCATION_ALT),
          altitude);
      DEBUG ("\t - Altitude: %f", altitude);
    }

  /* Don't change the accuracy as we used an address to get this position */
  g_hash_table_unref (priv->location);
  priv->location = new_location;
  g_object_notify (contact, "location");
out:
  g_object_unref (geocode);
  g_object_unref (contact);
}

static gchar *
get_dup_string (GHashTable *location,
    gchar *key)
{
  GValue *value;

  value = g_hash_table_lookup (location, key);
  if (value != NULL)
    return g_value_dup_string (value);

  return NULL;
}

static void
update_geocode (EmpathyContact *contact)
{
  static GeoclueGeocode *geocode;
  gchar *str;
  GHashTable *address;
  GHashTable *location;

  location = empathy_contact_get_location (contact);
  if (location == NULL)
    return;

  /* No need to search for position if contact published it */
  if (g_hash_table_lookup (location, EMPATHY_LOCATION_LAT) != NULL ||
      g_hash_table_lookup (location, EMPATHY_LOCATION_LON) != NULL)
    return;

  if (geocode == NULL)
    {
      geocode = geoclue_geocode_new (GEOCODE_SERVICE, GEOCODE_PATH);
      g_object_add_weak_pointer (G_OBJECT (geocode), (gpointer *) &geocode);
    }
  else
    {
      g_object_ref (geocode);
    }

  address = geoclue_address_details_new ();

  str = get_dup_string (location, EMPATHY_LOCATION_COUNTRY_CODE);
  if (str != NULL)
    {
      g_hash_table_insert (address,
        g_strdup (GEOCLUE_ADDRESS_KEY_COUNTRYCODE), str);
      DEBUG ("\t - countrycode: %s", str);
    }

  str = get_dup_string (location, EMPATHY_LOCATION_COUNTRY);
  if (str != NULL)
    {
      g_hash_table_insert (address,
        g_strdup (GEOCLUE_ADDRESS_KEY_COUNTRY), str);
      DEBUG ("\t - country: %s", str);
    }

  str = get_dup_string (location, EMPATHY_LOCATION_POSTAL_CODE);
  if (str != NULL)
    {
      g_hash_table_insert (address,
        g_strdup (GEOCLUE_ADDRESS_KEY_POSTALCODE), str);
      DEBUG ("\t - postalcode: %s", str);
    }

  str = get_dup_string (location, EMPATHY_LOCATION_REGION);
  if (str != NULL)
    {
      g_hash_table_insert (address,
        g_strdup (GEOCLUE_ADDRESS_KEY_REGION), str);
      DEBUG ("\t - region: %s", str);
    }

  str = get_dup_string (location, EMPATHY_LOCATION_LOCALITY);
  if (str != NULL)
    {
      g_hash_table_insert (address,
        g_strdup (GEOCLUE_ADDRESS_KEY_LOCALITY), str);
      DEBUG ("\t - locality: %s", str);
    }

  str = get_dup_string (location, EMPATHY_LOCATION_STREET);
  if (str != NULL)
    {
      g_hash_table_insert (address,
        g_strdup (GEOCLUE_ADDRESS_KEY_STREET), str);
      DEBUG ("\t - street: %s", str);
    }

  if (g_hash_table_size (address) > 0)
    {
      g_object_ref (contact);

      geoclue_geocode_address_to_position_async (geocode, address,
          geocode_cb, contact);
    }

  g_hash_table_unref (address);
}
#endif

static EmpathyCapabilities
tp_caps_to_capabilities (TpCapabilities *caps)
{
  EmpathyCapabilities capabilities = 0;
  guint i;
  GPtrArray *classes;

  classes = tp_capabilities_get_channel_classes (caps);

  for (i = 0; i < classes->len; i++)
    {
      GValueArray *class_struct;
      GHashTable *fixed_prop;
      GStrv allowed_prop;
      TpHandleType handle_type;
      const gchar *chan_type;

      class_struct = g_ptr_array_index (classes, i);
      tp_value_array_unpack (class_struct, 2,
          &fixed_prop,
          &allowed_prop);

      handle_type = tp_asv_get_uint32 (fixed_prop,
          TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL);
      if (handle_type != TP_HANDLE_TYPE_CONTACT)
        continue;

      chan_type = tp_asv_get_string (fixed_prop,
          TP_PROP_CHANNEL_CHANNEL_TYPE);

      if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER))
        {
          capabilities |= EMPATHY_CAPABILITIES_FT;
        }
      else if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
        {
          const gchar *service;

          service = tp_asv_get_string (fixed_prop,
              TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE);

          if (!tp_strdiff (service, "rfb"))
            capabilities |= EMPATHY_CAPABILITIES_RFB_STREAM_TUBE;
        }
      else if (!tp_strdiff (chan_type,
        TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
        {
          guint j;

          for (j = 0; allowed_prop[j] != NULL; j++)
            {
              if (!tp_strdiff (allowed_prop[j],
                    TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_AUDIO))
                capabilities |= EMPATHY_CAPABILITIES_AUDIO;
              else if (!tp_strdiff (allowed_prop[j],
                    TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_VIDEO))
                capabilities |= EMPATHY_CAPABILITIES_VIDEO;
            }
        }
    }

  return capabilities;
}

static void
set_capabilities_from_tp_caps (EmpathyContact *self,
    TpCapabilities *caps)
{
  EmpathyCapabilities capabilities;

  if (caps == NULL)
    return;

  capabilities = tp_caps_to_capabilities (caps);
  empathy_contact_set_capabilities (self, capabilities);
}

static void
contact_set_avatar_from_tp_contact (EmpathyContact *contact)
{
  EmpathyContactPriv *priv = GET_PRIV (contact);
  const gchar *mime;
  const gchar *token;
  GFile *file;

  token = tp_contact_get_avatar_token (priv->tp_contact);
  mime = tp_contact_get_avatar_mime_type (priv->tp_contact);
  file = tp_contact_get_avatar_file (priv->tp_contact);

  if (file != NULL)
    {
      EmpathyAvatar *avatar;
      gchar *data;
      gsize len;

      g_file_load_contents (file, NULL, &data, &len, NULL, NULL);
      avatar = empathy_avatar_new ((guchar *) data, len, g_strdup (mime), g_strdup (token),
          g_file_get_path (file));
      contact_set_avatar (contact, avatar);
      empathy_avatar_unref (avatar);
    }
  else
    {
      contact_set_avatar (contact, NULL);
    }
}

EmpathyContact *
empathy_contact_dup_from_tp_contact (TpContact *tp_contact)
{
  EmpathyContact *contact = NULL;

  g_return_val_if_fail (TP_IS_CONTACT (tp_contact), NULL);

  if (contacts_table == NULL)
    contacts_table = g_hash_table_new (g_direct_hash, g_direct_equal);
  else
    contact = g_hash_table_lookup (contacts_table, tp_contact);

  if (contact == NULL)
    {
      contact = empathy_contact_new (tp_contact);

      /* The hash table does not keep any ref.
       * contact keeps a ref to tp_contact, and is removed from the table in
       * contact_dispose() */
      g_hash_table_insert (contacts_table, tp_contact, contact);
    }
  else
    {
      g_object_ref (contact);
    }

  return contact;
}

