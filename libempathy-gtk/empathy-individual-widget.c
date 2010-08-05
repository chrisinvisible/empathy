/*
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
 *
 * Authors: Philip Withnall <philip.withnall@collabora.co.uk>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <telepathy-glib/util.h>

#include <libempathy/empathy-utils.h>

#include <folks/folks-telepathy.h>

#include "empathy-individual-widget.h"
#include "empathy-gtk-enum-types.h"

/**
 * SECTION:empathy-individual-widget
 * @title:EmpathyIndividualWidget
 * @short_description: A widget used to display and edit details about an
 * individual
 * @include: libempathy-empathy-individual-widget.h
 *
 * #EmpathyIndividualWidget is a widget which displays appropriate widgets
 * with details about an individual, also allowing changing these details,
 * if desired.
 */

/**
 * EmpathyIndividualWidget:
 * @parent: parent object
 *
 * Widget which displays appropriate widgets with details about an individual,
 * also allowing changing these details, if desired.
 *
 * Currently, it's just a thin wrapper around #EmpathyContactWidget, and
 * displays the details of the first eligible persona found in the individual.
 */

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyIndividualWidget)

typedef struct {
  FolksIndividual *individual;
  EmpathyIndividualWidgetFlags flags;
} EmpathyIndividualWidgetPriv;

G_DEFINE_TYPE (EmpathyIndividualWidget, empathy_individual_widget,
    GTK_TYPE_BOX);

enum {
  PROP_INDIVIDUAL = 1,
  PROP_FLAGS
};

static void
empathy_individual_widget_init (EmpathyIndividualWidget *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_INDIVIDUAL_WIDGET, EmpathyIndividualWidgetPriv);

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self),
      GTK_ORIENTATION_VERTICAL);
}

static void
get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyIndividualWidgetPriv *priv = GET_PRIV (object);

  switch (param_id)
    {
      case PROP_INDIVIDUAL:
        g_value_set_object (value, priv->individual);
        break;
      case PROP_FLAGS:
        g_value_set_flags (value, priv->flags);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
set_property (GObject *object,
    guint param_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyIndividualWidgetPriv *priv = GET_PRIV (object);

  switch (param_id)
    {
      case PROP_INDIVIDUAL:
        empathy_individual_widget_set_individual (
            EMPATHY_INDIVIDUAL_WIDGET (object), g_value_get_object (value));
        break;
      case PROP_FLAGS:
        priv->flags = g_value_get_flags (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
  EmpathyIndividualWidgetPriv *priv = GET_PRIV (object);

  tp_clear_object (&priv->individual);

  G_OBJECT_CLASS (empathy_individual_widget_parent_class)->dispose (object);
}

static void
empathy_individual_widget_class_init (EmpathyIndividualWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = get_property;
  object_class->set_property = set_property;
  object_class->dispose = dispose;

  /**
   * EmpathyIndividualWidget:individual:
   *
   * The #FolksIndividual to display in the widget.
   */
  g_object_class_install_property (object_class, PROP_INDIVIDUAL,
      g_param_spec_object ("individual",
          "Individual",
          "The #FolksIndividual to display in the widget.",
          FOLKS_TYPE_INDIVIDUAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * EmpathyIndividualWidget:flags:
   *
   * A set of flags which affect the widget's behaviour.
   */
  g_object_class_install_property (object_class, PROP_FLAGS,
      g_param_spec_flags ("flags",
          "Flags",
          "A set of flags which affect the widget's behaviour.",
          EMPATHY_TYPE_INDIVIDUAL_WIDGET_FLAGS,
          EMPATHY_INDIVIDUAL_WIDGET_EDIT_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY));

  g_type_class_add_private (object_class, sizeof (EmpathyIndividualWidgetPriv));
}

/**
 * empathy_individual_widget_new:
 * @individual: the #FolksIndividual to display
 * @flags: flags affecting how the widget behaves and what it displays
 *
 * Creates a new #EmpathyIndividualWidget.
 *
 * Return value: a new #EmpathyIndividualWidget
 */
GtkWidget *
empathy_individual_widget_new (FolksIndividual *individual,
    EmpathyIndividualWidgetFlags flags)
{
  g_return_val_if_fail (individual == NULL || FOLKS_IS_INDIVIDUAL (individual),
      NULL);

  return g_object_new (EMPATHY_TYPE_INDIVIDUAL_WIDGET,
      "individual", individual, "flags", flags, NULL);
}

FolksIndividual *
empathy_individual_widget_get_individual (EmpathyIndividualWidget *self)
{
  g_return_val_if_fail (EMPATHY_IS_INDIVIDUAL_WIDGET (self), NULL);

  return GET_PRIV (self)->individual;
}

void
empathy_individual_widget_set_individual (EmpathyIndividualWidget *self,
    FolksIndividual *individual)
{
  EmpathyIndividualWidgetPriv *priv;
  GList *personas = NULL, *l;

  g_return_if_fail (EMPATHY_IS_INDIVIDUAL_WIDGET (self));
  g_return_if_fail (individual == NULL || FOLKS_IS_INDIVIDUAL (individual));

  priv = GET_PRIV (self);

  /* Out with the old… */
  gtk_container_foreach (GTK_CONTAINER (self), (GtkCallback) gtk_widget_destroy,
      NULL);
  tp_clear_object (&priv->individual);

  /* …and in with the new. */
  priv->individual = individual;
  if (individual != NULL)
    {
      g_object_ref (individual);
      personas = folks_individual_get_personas (individual);
    }

  for (l = personas; l != NULL; l = l->next)
    {
      GtkWidget *contact_widget;
      TpContact *tp_contact;
      EmpathyContact *contact;
      TpfPersona *persona = l->data;

      if (!TPF_IS_PERSONA (persona))
        continue;

      tp_contact = tpf_persona_get_contact (persona);
      contact = empathy_contact_dup_from_tp_contact (tp_contact);

      /* Contact info widget */
      contact_widget = empathy_contact_widget_new (contact, priv->flags);
      gtk_container_set_border_width (GTK_CONTAINER (contact_widget), 8);
      gtk_box_pack_start (GTK_BOX (self), contact_widget, TRUE, TRUE, 0);
      gtk_widget_show (contact_widget);

      g_object_unref (contact);

      /* If we're not meant to display all of the personas, bail after the first
       * one. */
      if (!(priv->flags & EMPATHY_INDIVIDUAL_WIDGET_SHOW_PERSONAS))
        break;
    }
}
