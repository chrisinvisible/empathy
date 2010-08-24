/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2008, 2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 *
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 *          Philip Withnall <philip.withnall@collabora.co.uk>
 *
 * Based off EmpathyContactListView.
 */

#ifndef __EMPATHY_PERSONA_VIEW_H__
#define __EMPATHY_PERSONA_VIEW_H__

#include <gtk/gtk.h>

#include <folks/folks.h>

#include "empathy-persona-store.h"

G_BEGIN_DECLS

typedef enum
{
  EMPATHY_PERSONA_VIEW_FEATURE_NONE = 0,
  EMPATHY_PERSONA_VIEW_FEATURE_PERSONA_DRAG = 1 << 0,
  EMPATHY_PERSONA_VIEW_FEATURE_PERSONA_DROP = 1 << 1,
  EMPATHY_PERSONA_VIEW_FEATURE_ALL = (1 << 2) - 1,
} EmpathyPersonaViewFeatureFlags;

#define EMPATHY_TYPE_PERSONA_VIEW (empathy_persona_view_get_type ())
#define EMPATHY_PERSONA_VIEW(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), \
    EMPATHY_TYPE_PERSONA_VIEW, EmpathyPersonaView))
#define EMPATHY_PERSONA_VIEW_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), \
    EMPATHY_TYPE_PERSONA_VIEW, EmpathyPersonaViewClass))
#define EMPATHY_IS_PERSONA_VIEW(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
    EMPATHY_TYPE_PERSONA_VIEW))
#define EMPATHY_IS_PERSONA_VIEW_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), \
    EMPATHY_TYPE_PERSONA_VIEW))
#define EMPATHY_PERSONA_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), \
    EMPATHY_TYPE_PERSONA_VIEW, EmpathyPersonaViewClass))

typedef struct
{
  GtkTreeView parent;
  gpointer priv;
} EmpathyPersonaView;

typedef struct
{
  GtkTreeViewClass parent_class;

  void (* drag_individual_received) (EmpathyPersonaView *self,
      GdkDragAction action,
      FolksIndividual *individual);
} EmpathyPersonaViewClass;

GType empathy_persona_view_get_type (void) G_GNUC_CONST;

EmpathyPersonaView *empathy_persona_view_new (EmpathyPersonaStore *store,
    EmpathyPersonaViewFeatureFlags features);

FolksPersona *empathy_persona_view_dup_selected (EmpathyPersonaView *self);

gboolean empathy_persona_view_get_show_offline (EmpathyPersonaView *self);
void empathy_persona_view_set_show_offline (EmpathyPersonaView *self,
    gboolean show_offline);

G_END_DECLS

#endif /* __EMPATHY_PERSONA_VIEW_H__ */
