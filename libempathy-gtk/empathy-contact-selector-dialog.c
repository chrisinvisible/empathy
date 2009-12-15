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
 * Authors: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <libempathy/empathy-tp-contact-factory.h>
#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-dispatcher.h>
#include <libempathy/empathy-utils.h>

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include <libempathy/empathy-debug.h>

#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-images.h>

#include "empathy-contact-selector-dialog.h"
#include "empathy-account-chooser.h"

G_DEFINE_TYPE(EmpathyContactSelectorDialog, empathy_contact_selector_dialog,
    GTK_TYPE_DIALOG)

typedef struct _EmpathyContactSelectorDialogPriv \
          EmpathyContactSelectorDialogPriv;

struct _EmpathyContactSelectorDialogPriv {
  GtkWidget *table_contact;
  GtkWidget *account_chooser;
  GtkWidget *entry_id;
  EmpathyContactManager *contact_manager;
};

#define GET_PRIV(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG, \
    EmpathyContactSelectorDialogPriv))

enum {
  COMPLETION_COL_TEXT,
  COMPLETION_COL_ID,
  COMPLETION_COL_NAME,
} CompletionCol;

static void
contact_selector_dialog_account_changed_cb (GtkWidget *widget,
    EmpathyContactSelectorDialog *dialog)
{
  EmpathyContactSelectorDialogPriv *priv = GET_PRIV (dialog);
  EmpathyAccountChooser *chooser;
  TpConnection *connection;
  EmpathyTpContactList *contact_list;
  GList *members;
  GtkListStore *store;
  GtkEntryCompletion *completion;
  GtkTreeIter iter;
  gchar *tmpstr;

  /* Remove completions */
  completion = gtk_entry_get_completion (GTK_ENTRY (priv->entry_id));
  store = GTK_LIST_STORE (gtk_entry_completion_get_model (completion));
  gtk_list_store_clear (store);

  /* Get members of the new account */
  chooser = EMPATHY_ACCOUNT_CHOOSER (priv->account_chooser);
  connection = empathy_account_chooser_get_connection (chooser);
  if (!connection)
    return;

  contact_list = empathy_contact_manager_get_list (priv->contact_manager,
               connection);
  members = empathy_contact_list_get_members (
      EMPATHY_CONTACT_LIST (contact_list));

  /* Add members to the completion */
  while (members)
    {
      EmpathyContact *contact = members->data;

      DEBUG ("Adding contact ID %s, Name %s",
             empathy_contact_get_id (contact),
             empathy_contact_get_name (contact));

      tmpstr = g_strdup_printf ("%s (%s)",
        empathy_contact_get_name (contact),
        empathy_contact_get_id (contact));

      gtk_list_store_insert_with_values (store, &iter, -1,
        COMPLETION_COL_TEXT, tmpstr,
        COMPLETION_COL_ID, empathy_contact_get_id (contact),
        COMPLETION_COL_NAME, empathy_contact_get_name (contact),
        -1);

      g_free (tmpstr);

      g_object_unref (contact);
      members = g_list_delete_link (members, members);
  }
}

static gboolean
contact_selector_dialog_match_selected_cb (GtkEntryCompletion *widget,
    GtkTreeModel *model,
    GtkTreeIter *iter,
    EmpathyContactSelectorDialog *dialog)
{
  EmpathyContactSelectorDialogPriv *priv = GET_PRIV (dialog);
  gchar *id;

  if (!iter || !model)
    return FALSE;

  gtk_tree_model_get (model, iter, COMPLETION_COL_ID, &id, -1);
  gtk_entry_set_text (GTK_ENTRY (priv->entry_id), id);

  DEBUG ("Got selected match **%s**", id);

  g_free (id);

  return TRUE;
}

static gboolean
contact_selector_dialog_match_func (GtkEntryCompletion *completion,
    const gchar *key,
    GtkTreeIter *iter,
    gpointer user_data)
{
  GtkTreeModel *model;
  gchar *id;
  gchar *name;

  model = gtk_entry_completion_get_model (completion);
  if (!model || !iter)
    return FALSE;

  gtk_tree_model_get (model, iter, COMPLETION_COL_NAME, &name, -1);
  if (strstr (name, key))
    {
      DEBUG ("Key %s is matching name **%s**", key, name);
      g_free (name);
      return TRUE;
    }
  g_free (name);

  gtk_tree_model_get (model, iter, COMPLETION_COL_ID, &id, -1);
  if (strstr (id, key))
    {
      DEBUG ("Key %s is matching ID **%s**", key, id);
      g_free (id);
      return TRUE;
    }
  g_free (id);

  return FALSE;
}

static void
contact_selector_dialog_response_cb (GtkWidget *widget,
    gint response,
    EmpathyContactSelectorDialog *dialog)
{
  EmpathyContactSelectorDialogPriv *priv = GET_PRIV (dialog);
  TpConnection *connection;
  const gchar *id;
  EmpathyContactSelectorDialogClass *class = \
    EMPATHY_CONTACT_SELECTOR_DIALOG_GET_CLASS (dialog);

  connection = empathy_account_chooser_get_connection (
    EMPATHY_ACCOUNT_CHOOSER (priv->account_chooser));
  id = gtk_entry_get_text (GTK_ENTRY (priv->entry_id));
  if (!connection || EMP_STR_EMPTY (id))
    {
      gtk_widget_destroy (widget);
      return;
    }

  if (response == GTK_RESPONSE_ACCEPT)
    {
      class->got_response (dialog, connection, id);
    }

  gtk_widget_destroy (widget);
}

static void
contact_selector_change_state_button_cb  (GtkEditable *editable,
    EmpathyContactSelectorDialog *dialog)
{
  const gchar *id;
  gboolean sensitive;

  id = gtk_entry_get_text (GTK_ENTRY (editable));
  sensitive = !EMP_STR_EMPTY (id);

  gtk_widget_set_sensitive (dialog->button_action, sensitive);
}

static void
empathy_contact_selector_dialog_init (EmpathyContactSelectorDialog *dialog)
{
  EmpathyContactSelectorDialogPriv *priv = GET_PRIV (dialog);
  GtkBuilder *gui;
  gchar *filename;
  GtkEntryCompletion *completion;
  GtkListStore *model;
  GtkWidget *content_area;

  /* create a contact manager */
  priv->contact_manager = empathy_contact_manager_dup_singleton ();

  filename = empathy_file_lookup ("empathy-new-message-dialog.ui",
          "libempathy-gtk");
  gui = empathy_builder_get_file (filename,
                "table_contact", &priv->table_contact,
                "entry_id", &priv->entry_id,
                NULL);
  g_free (filename);

  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_container_add (GTK_CONTAINER (content_area), priv->table_contact);

  gtk_dialog_add_button (GTK_DIALOG (dialog),
    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

  /* Tweak the dialog */
  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_window_set_type_hint (GTK_WINDOW (dialog), GDK_WINDOW_TYPE_HINT_DIALOG);

  /* text completion */
  completion = gtk_entry_completion_new ();
  model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  gtk_entry_completion_set_text_column (completion, COMPLETION_COL_TEXT);
  gtk_entry_completion_set_match_func (completion,
               contact_selector_dialog_match_func,
               NULL, NULL);
  gtk_entry_completion_set_model (completion, GTK_TREE_MODEL (model));
  gtk_entry_set_completion (GTK_ENTRY (priv->entry_id), completion);
  g_signal_connect (completion, "match-selected",
        G_CALLBACK (contact_selector_dialog_match_selected_cb),
        dialog);
  g_object_unref (completion);
  g_object_unref (model);

  g_signal_connect (dialog, "response",
        G_CALLBACK (contact_selector_dialog_response_cb), dialog);

  empathy_builder_connect (gui, dialog,
             "entry_id", "changed", contact_selector_change_state_button_cb,
             NULL);

  g_object_unref (gui);

  /* Create account chooser */
  priv->account_chooser = empathy_account_chooser_new ();
  gtk_table_attach_defaults (GTK_TABLE (priv->table_contact),
           priv->account_chooser,
           1, 2, 0, 1);
  empathy_account_chooser_set_filter (
      EMPATHY_ACCOUNT_CHOOSER (priv->account_chooser),
      empathy_account_chooser_filter_is_connected,
      NULL);
  gtk_widget_show (priv->account_chooser);

  contact_selector_dialog_account_changed_cb (priv->account_chooser, dialog);
  g_signal_connect (priv->account_chooser, "changed",
        G_CALLBACK (contact_selector_dialog_account_changed_cb),
        dialog);
}

static void
empathy_contact_selector_dialog_dispose (GObject *object)
{
  EmpathyContactSelectorDialogPriv *priv = GET_PRIV (object);

  if (priv->contact_manager != NULL) {
    g_object_unref (priv->contact_manager);
    priv->contact_manager = NULL;
  }

  if (G_OBJECT_CLASS (empathy_contact_selector_dialog_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_contact_selector_dialog_parent_class)->dispose (
        object);
}

static void
empathy_contact_selector_dialog_class_init (
    EmpathyContactSelectorDialogClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  g_type_class_add_private (class, sizeof (EmpathyContactSelectorDialogPriv));

  object_class->dispose = empathy_contact_selector_dialog_dispose;
}
