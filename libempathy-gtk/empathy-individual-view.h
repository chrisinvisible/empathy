/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2010 Collabora Ltd.
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
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#ifndef __EMPATHY_INDIVIDUAL_VIEW_H__
#define __EMPATHY_INDIVIDUAL_VIEW_H__

#include <gtk/gtk.h>

#include <folks/folks.h>

#include <libempathy/empathy-enum-types.h>

#include "empathy-live-search.h"
#include "empathy-individual-menu.h"
#include "empathy-individual-store.h"

G_BEGIN_DECLS
#define EMPATHY_TYPE_INDIVIDUAL_VIEW         (empathy_individual_view_get_type ())
#define EMPATHY_INDIVIDUAL_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_INDIVIDUAL_VIEW, EmpathyIndividualView))
#define EMPATHY_INDIVIDUAL_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_INDIVIDUAL_VIEW, EmpathyIndividualViewClass))
#define EMPATHY_IS_INDIVIDUAL_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_INDIVIDUAL_VIEW))
#define EMPATHY_IS_INDIVIDUAL_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_INDIVIDUAL_VIEW))
#define EMPATHY_INDIVIDUAL_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_INDIVIDUAL_VIEW, EmpathyIndividualViewClass))
typedef struct _EmpathyIndividualView EmpathyIndividualView;
typedef struct _EmpathyIndividualViewClass EmpathyIndividualViewClass;

typedef enum
{
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_NONE = 0,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_GROUPS_SAVE = 1 << 0,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_GROUPS_RENAME = 1 << 1,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_GROUPS_REMOVE = 1 << 2,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_CONTACT_REMOVE = 1 << 3,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_CONTACT_DROP = 1 << 4,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_CONTACT_DRAG = 1 << 5,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_CONTACT_TOOLTIP = 1 << 6,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_ALL = (1 << 7) - 1,
} EmpathyIndividualViewFeatureFlags;

struct _EmpathyIndividualView
{
  GtkTreeView parent;
  gpointer priv;
};

struct _EmpathyIndividualViewClass
{
  GtkTreeViewClass parent_class;
};

GType empathy_individual_view_get_type (void) G_GNUC_CONST;

EmpathyIndividualView *empathy_individual_view_new (
    EmpathyIndividualStore *store,
    EmpathyIndividualViewFeatureFlags view_features,
    EmpathyIndividualFeatureFlags individual_features);

FolksIndividual *empathy_individual_view_dup_selected (
    EmpathyIndividualView *view);

EmpathyIndividualManagerFlags empathy_individual_view_get_flags (
    EmpathyIndividualView *view);

gchar *empathy_individual_view_get_selected_group (EmpathyIndividualView *view,
    gboolean * is_fake_group);

GtkWidget *empathy_individual_view_get_individual_menu (
    EmpathyIndividualView *view);

GtkWidget *empathy_individual_view_get_group_menu (EmpathyIndividualView *view);

void empathy_individual_view_set_live_search (EmpathyIndividualView *view,
    EmpathyLiveSearch *search);

gboolean empathy_individual_view_get_show_offline (
    EmpathyIndividualView *view);

void empathy_individual_view_set_show_offline (
    EmpathyIndividualView *view,
    gboolean show_offline);

G_END_DECLS
#endif /* __EMPATHY_INDIVIDUAL_VIEW_H__ */
