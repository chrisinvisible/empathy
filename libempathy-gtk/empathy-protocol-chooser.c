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
 *          Jonny Lamb <jonny.lamb@collabora.co.uk>
 */

#include <config.h>

#include <string.h>

#include <telepathy-glib/util.h>

#include <gtk/gtk.h>

#include <libempathy/empathy-utils.h>

#include "empathy-protocol-chooser.h"
#include "empathy-ui-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

/**
 * SECTION:empathy-protocol-chooser
 * @title: EmpathyProtocolChooser
 * @short_description: A widget used to choose from a list of protocols
 * @include: libempathy-gtk/empathy-protocol-chooser.h
 *
 * #EmpathyProtocolChooser is a widget which extends #GtkComboBox to provides a
 * chooser of available protocols.
 */

/**
 * EmpathyProtocolChooser:
 * @parent: parent object
 *
 * Widget which extends #GtkComboBox to provide a chooser of available
 * protocols.
 */

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyProtocolChooser)
typedef struct
{
  GtkListStore *store;
  gboolean dispose_run;

} EmpathyProtocolChooserPriv;

enum
{
  COL_ICON,
  COL_LABEL,
  COL_CM,
  COL_PROTOCOL,
  COL_COUNT
};

G_DEFINE_TYPE (EmpathyProtocolChooser, empathy_protocol_chooser,
    GTK_TYPE_COMBO_BOX);

static gint
protocol_chooser_sort_protocol_value (TpConnectionManagerProtocol *protocol)
{
  guint i;
  const gchar *names[] = {
    "jabber",
    "salut",
    "gtalk",
    NULL
  };

  for (i = 0 ; names[i]; i++)
    {
      if (strcmp (protocol->name, names[i]) == 0)
        return i;
    }

  return i;
}

static gint
protocol_chooser_sort_func (GtkTreeModel *model,
    GtkTreeIter  *iter_a,
    GtkTreeIter  *iter_b,
    gpointer      user_data)
{
  TpConnectionManagerProtocol *protocol_a;
  TpConnectionManagerProtocol *protocol_b;
  gint cmp;

  gtk_tree_model_get (model, iter_a,
      COL_PROTOCOL, &protocol_a,
      -1);
  gtk_tree_model_get (model, iter_b,
      COL_PROTOCOL, &protocol_b,
      -1);

  cmp = protocol_chooser_sort_protocol_value (protocol_a);
  cmp -= protocol_chooser_sort_protocol_value (protocol_b);
  if (cmp == 0)
    {
      cmp = strcmp (protocol_a->name, protocol_b->name);
    }

  return cmp;
}

static void
protocol_choosers_add_cm (EmpathyProtocolChooser *chooser,
    TpConnectionManager *cm)
{
  EmpathyProtocolChooserPriv *priv = GET_PRIV (chooser);
  const TpConnectionManagerProtocol * const *iter;

  for (iter = cm->protocols; iter != NULL && *iter != NULL; iter++)
    {
      const TpConnectionManagerProtocol *proto = *iter;
      gchar *icon_name;
      gchar *display_name;


      icon_name = g_strdup_printf ("im-%s", proto->name);

      if (!tp_strdiff (cm->name, "haze") && !tp_strdiff (proto->name, "msn"))
        display_name = g_strdup_printf ("msn (Haze)");
      else
        display_name = g_strdup (proto->name);

      gtk_list_store_insert_with_values (priv->store, NULL, 0,
          COL_ICON, icon_name,
          COL_LABEL, display_name,
          COL_CM, cm,
          COL_PROTOCOL, proto,
          -1);

      g_free (display_name);
      g_free (icon_name);
    }
}


static void
protocol_choosers_cms_listed (TpConnectionManager * const *cms,
    gsize n_cms,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  TpConnectionManager * const *iter;

  if (error !=NULL)
    {
      DEBUG ("Failed to get connection managers: %s", error->message);
      return;
    }

  for (iter = cms ; iter != NULL && *iter != NULL; iter++)
    protocol_choosers_add_cm (EMPATHY_PROTOCOL_CHOOSER (weak_object),
      *iter);

  gtk_combo_box_set_active (GTK_COMBO_BOX (weak_object), 0);
}

static void
protocol_chooser_constructed (GObject *object)
{
  EmpathyProtocolChooser *protocol_chooser;
  EmpathyProtocolChooserPriv *priv;

  GtkCellRenderer *renderer;
  TpDBusDaemon *dbus;

  priv = GET_PRIV (object);
  protocol_chooser = EMPATHY_PROTOCOL_CHOOSER (object);

  /* set up combo box with new store */
  priv->store = gtk_list_store_new (COL_COUNT,
          G_TYPE_STRING,    /* Icon name */
          G_TYPE_STRING,    /* Label     */
          G_TYPE_OBJECT,    /* CM */
          G_TYPE_POINTER);  /* protocol   */

  gtk_combo_box_set_model (GTK_COMBO_BOX (object),
      GTK_TREE_MODEL (priv->store));

  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (object), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (object), renderer,
      "icon-name", COL_ICON,
      NULL);
  g_object_set (renderer, "stock-size", GTK_ICON_SIZE_BUTTON, NULL);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (object), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (object), renderer,
      "text", COL_LABEL,
      NULL);

  dbus = tp_dbus_daemon_dup (NULL);
  tp_list_connection_managers (dbus, protocol_choosers_cms_listed,
    NULL, NULL, object);
  g_object_unref (dbus);

  /* Set the protocol sort function */
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (priv->store),
      COL_PROTOCOL,
      protocol_chooser_sort_func,
      NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->store),
      COL_PROTOCOL,
      GTK_SORT_ASCENDING);

  if (G_OBJECT_CLASS (empathy_protocol_chooser_parent_class)->constructed)
    G_OBJECT_CLASS (empathy_protocol_chooser_parent_class)->constructed (object);
}

static void
empathy_protocol_chooser_init (EmpathyProtocolChooser *protocol_chooser)
{
  EmpathyProtocolChooserPriv *priv =
    G_TYPE_INSTANCE_GET_PRIVATE (protocol_chooser,
        EMPATHY_TYPE_PROTOCOL_CHOOSER, EmpathyProtocolChooserPriv);

  priv->dispose_run = FALSE;

  protocol_chooser->priv = priv;
}

static void
protocol_chooser_dispose (GObject *object)
{
  EmpathyProtocolChooser *protocol_chooser = EMPATHY_PROTOCOL_CHOOSER (object);
  EmpathyProtocolChooserPriv *priv = GET_PRIV (protocol_chooser);

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  if (priv->store)
    {
      g_object_unref (priv->store);
      priv->store = NULL;
    }

  (G_OBJECT_CLASS (empathy_protocol_chooser_parent_class)->dispose) (object);
}

static void
empathy_protocol_chooser_class_init (EmpathyProtocolChooserClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = protocol_chooser_constructed;
  object_class->dispose = protocol_chooser_dispose;

  g_type_class_add_private (object_class, sizeof (EmpathyProtocolChooserPriv));
}

/**
 * empathy_protocol_chooser_get_selected_protocol:
 * @protocol_chooser: an #EmpathyProtocolChooser
 *
 * Returns a pointer to the selected #TpConnectionManagerProtocol in
 * @protocol_chooser.
 *
 * Return value: a pointer to the selected #TpConnectionManagerProtocol
 */
TpConnectionManager *empathy_protocol_chooser_dup_selected (
    EmpathyProtocolChooser *protocol_chooser,
    TpConnectionManagerProtocol **protocol)
{
  EmpathyProtocolChooserPriv *priv = GET_PRIV (protocol_chooser);
  GtkTreeIter iter;
  TpConnectionManager *cm = NULL;

  g_return_val_if_fail (EMPATHY_IS_PROTOCOL_CHOOSER (protocol_chooser), NULL);

  if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (protocol_chooser), &iter))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &iter,
          COL_CM, &cm,
          -1);

      if (protocol != NULL)
        gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &iter,
            COL_PROTOCOL, protocol,
            -1);
    }

  return cm;
}

/**
 * empathy_protocol_chooser_n_protocols:
 * @protocol_chooser: an #EmpathyProtocolChooser
 *
 * Returns the number of protocols in @protocol_chooser.
 *
 * Return value: the number of protocols in @protocol_chooser
 */
gint
empathy_protocol_chooser_n_protocols (EmpathyProtocolChooser *protocol_chooser)
{
  EmpathyProtocolChooserPriv *priv = GET_PRIV (protocol_chooser);

  g_return_val_if_fail (EMPATHY_IS_PROTOCOL_CHOOSER (protocol_chooser), 0);

  return gtk_tree_model_iter_n_children (GTK_TREE_MODEL (priv->store), NULL);
}

/**
 * empathy_protocol_chooser_new:
 *
 * Creates a new #EmpathyProtocolChooser widget.
 *
 * Return value: a new #EmpathyProtocolChooser widget
 */
GtkWidget *
empathy_protocol_chooser_new (void)
{
  return GTK_WIDGET (g_object_new (EMPATHY_TYPE_PROTOCOL_CHOOSER, NULL));
}
