/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Copyright (C) 2010 Thomas Meire <blackskad@gmail.com>
 *
 * The code contained in this file is free software; you can redistribute
 * it and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either version
 * 2.1 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this code; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include <libempathy/empathy-utils.h>

#include "empathy-chat-view.h"
#include "empathy-search-bar.h"
#include "empathy-ui-utils.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathySearchBar)

G_DEFINE_TYPE (EmpathySearchBar, empathy_search_bar, GTK_TYPE_BIN);

typedef struct _EmpathySearchBarPriv EmpathySearchBarPriv;
struct _EmpathySearchBarPriv
{
  EmpathyChatView *chat_view;

  GtkWidget *search_entry;
  gchar *last_search;

  GtkWidget *search_close;
  GtkWidget *search_previous;
  GtkWidget *search_next;
  GtkWidget *search_not_found;
};

GtkWidget *
empathy_search_bar_new (EmpathyChatView *view)
{
  EmpathySearchBar *self = g_object_new (EMPATHY_TYPE_SEARCH_BAR, NULL);

  GET_PRIV (self)->chat_view = view;

  return GTK_WIDGET (self);
}

static void
empathy_search_bar_size_request (GtkWidget *widget,
    GtkRequisition *requisition)
{
  GtkBin *bin;
  GtkWidget *child;

  bin = GTK_BIN (widget);
  child = gtk_bin_get_child (bin);

  if (child && gtk_widget_get_visible (child))
    {
      GtkRequisition child_requisition;

      gtk_widget_size_request (child, &child_requisition);

      requisition->width = child_requisition.width;
      requisition->height = child_requisition.height;
    }
}

static void
empathy_search_bar_size_allocate (GtkWidget *widget,
    GtkAllocation *allocation)
{
  GtkBin *bin;
  GtkWidget *child;
  GtkAllocation child_allocation;

  bin = GTK_BIN (widget);
  child = gtk_bin_get_child (bin);

  gtk_widget_set_allocation (widget, allocation);

  if (child && gtk_widget_get_visible (child))
    {
      child_allocation.x = allocation->x;
      child_allocation.y = allocation->y;
      child_allocation.width = MAX (allocation->width, 0);
      child_allocation.height = MAX (allocation->height, 0);

      gtk_widget_size_allocate (child, &child_allocation);
    }
}

static void
empathy_search_bar_finalize (GObject *object)
{
  EmpathySearchBarPriv *priv = GET_PRIV (object);

  g_free (priv->last_search);
}

static void
empathy_search_bar_update_buttons (EmpathySearchBar *self)
{
  gboolean can_go_forward = FALSE;
  gboolean can_go_backward = FALSE;

  EmpathySearchBarPriv* priv = GET_PRIV (self);

  /* update previous / next buttons */
  if (priv->last_search)
    {
      empathy_chat_view_find_abilities (priv->chat_view, priv->last_search,
          &can_go_backward, &can_go_forward);
    }

  gtk_widget_set_sensitive (priv->search_previous,
      can_go_backward && !EMP_STR_EMPTY (priv->last_search));
  gtk_widget_set_sensitive (priv->search_next,
      can_go_forward && !EMP_STR_EMPTY (priv->last_search));
}

void
empathy_search_bar_show (EmpathySearchBar *self)
{
  EmpathySearchBarPriv *priv = GET_PRIV (self);

  if (priv->last_search)
    {
      /* make sure we have an up to date last_search */
      g_free (priv->last_search);
      priv->last_search = gtk_editable_get_chars (
          GTK_EDITABLE (priv->search_entry), 0, -1);
    }
  empathy_chat_view_highlight (priv->chat_view, priv->last_search);
  empathy_search_bar_update_buttons (self);

  /* grab the focus to the search entry */
  gtk_widget_grab_focus (priv->search_entry);

  gtk_widget_show (GTK_WIDGET (self));
}


static void
empathy_search_bar_close_cb (GtkButton *button,
    gpointer user_data)
{
  EmpathySearchBarPriv *priv = GET_PRIV (user_data);

  empathy_chat_view_highlight (priv->chat_view, "");
  gtk_widget_hide (GTK_WIDGET (user_data));

  /* give the focus back to the focus-chain with the chat view */
  gtk_widget_grab_focus (GTK_WIDGET (priv->chat_view));
}

static void
empathy_search_bar_entry_changed (GtkEditable *entry,
    gpointer user_data)
{
  EmpathySearchBarPriv *priv;
  gboolean found;
  gchar *search;

  priv = GET_PRIV (user_data);

  search = gtk_editable_get_chars (entry, 0, -1);

  found = empathy_chat_view_find_previous (priv->chat_view, search, TRUE);
  gtk_widget_set_visible (priv->search_not_found,
      !(found || EMP_STR_EMPTY (search)));

  empathy_chat_view_highlight (priv->chat_view, search);

  g_free (priv->last_search);
  priv->last_search = search;

  empathy_search_bar_update_buttons (EMPATHY_SEARCH_BAR (user_data));
}

static void
empathy_search_bar_next_cb (GtkButton *button,
    gpointer user_data)
{
  EmpathySearchBarPriv *priv = GET_PRIV (user_data);

  empathy_chat_view_find_next (priv->chat_view, priv->last_search, FALSE);
  empathy_search_bar_update_buttons (EMPATHY_SEARCH_BAR (user_data));
}

static void
empathy_search_bar_previous_cb (GtkButton *button,
    gpointer user_data)
{
  EmpathySearchBarPriv *priv = GET_PRIV (user_data);

  empathy_chat_view_find_previous (priv->chat_view, priv->last_search, FALSE);
  empathy_search_bar_update_buttons (EMPATHY_SEARCH_BAR (user_data));
}

static void
empathy_search_bar_init (EmpathySearchBar * self)
{
  gchar *filename;
  GtkBuilder *gui;
  GtkWidget *internal;
  EmpathySearchBarPriv *priv;

  priv = G_TYPE_INSTANCE_GET_PRIVATE (self, EMPATHY_TYPE_SEARCH_BAR,
      EmpathySearchBarPriv);

  self->priv = priv;

  filename = empathy_file_lookup ("empathy-search-bar.ui", "libempathy-gtk");
  gui = empathy_builder_get_file (filename,
      "search_widget", &internal,
      "search_close", &priv->search_close,
      "search_entry", &priv->search_entry,
      "search_previous", &priv->search_previous,
      "search_next", &priv->search_next,
      "search_not_found", &priv->search_not_found,
      NULL);
  g_free (filename);

  /* Add the signals */
  empathy_builder_connect (gui, self,
      "search_close", "clicked", empathy_search_bar_close_cb,
      "search_entry", "changed", empathy_search_bar_entry_changed,
      "search_previous", "clicked", empathy_search_bar_previous_cb,
      "search_next", "clicked", empathy_search_bar_next_cb,
      NULL);

  gtk_container_add (GTK_CONTAINER (self), internal);
  gtk_widget_show_all (internal);
  gtk_widget_hide (priv->search_not_found);
  g_object_unref (gui);
}

static void
empathy_search_bar_class_init (EmpathySearchBarClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

  gobject_class->finalize = empathy_search_bar_finalize;

  g_type_class_add_private (gobject_class, sizeof (EmpathySearchBarPriv));

  /* Neither GtkBin nor GtkContainer seems to do this for us :( */
  widget_class->size_request = empathy_search_bar_size_request;
  widget_class->size_allocate = empathy_search_bar_size_allocate;
}

