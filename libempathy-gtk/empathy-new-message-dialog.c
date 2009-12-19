/*
 * Copyright (C) 2007-2008 Collabora Ltd.
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

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <telepathy-glib/interfaces.h>

#include <libempathy/empathy-tp-contact-factory.h>
#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-dispatcher.h>
#include <libempathy/empathy-utils.h>

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include <libempathy/empathy-debug.h>

#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-images.h>

#include "empathy-new-message-dialog.h"
#include "empathy-account-chooser.h"

static EmpathyNewMessageDialog *dialog_singleton = NULL;

G_DEFINE_TYPE(EmpathyNewMessageDialog, empathy_new_message_dialog,
               EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG)

/**
 * SECTION:empathy-new-message-dialog
 * @title: EmpathyNewMessageDialog
 * @short_description: A dialog to show a new message
 * @include: libempathy-gtk/empathy-new-message-dialog.h
 *
 * #EmpathyNewMessageDialog is a dialog which allows a text chat
 * to be started with any contact on any enabled account.
 */

static void
empathy_new_message_dialog_response (GtkDialog *dialog, int response_id)
{
  TpConnection *connection;
  const gchar *contact_id;

  if (response_id != GTK_RESPONSE_ACCEPT) goto out;

  contact_id = empathy_contact_selector_dialog_get_selected (
      EMPATHY_CONTACT_SELECTOR_DIALOG (dialog), &connection);

  if (EMP_STR_EMPTY (contact_id) || connection == NULL) goto out;

  empathy_dispatcher_chat_with_contact_id (connection, contact_id, NULL, NULL);

out:
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static gboolean
empathy_new_message_account_filter (EmpathyContactSelectorDialog *dialog,
    TpAccount *account)
{
  TpConnection *connection;
  EmpathyDispatcher *dispatcher;
  GList *classes;

  if (tp_account_get_connection_status (account, NULL) !=
      TP_CONNECTION_STATUS_CONNECTED)
    return FALSE;

  /* check if CM supports 1-1 text chat */
  connection = tp_account_get_connection (account);
  if (connection == NULL)
    return FALSE;

  dispatcher = empathy_dispatcher_dup_singleton ();

  classes = empathy_dispatcher_find_requestable_channel_classes
    (dispatcher, connection, TP_IFACE_CHANNEL_TYPE_TEXT,
     TP_HANDLE_TYPE_CONTACT, NULL);

  g_object_unref (dispatcher);

  if (classes == NULL)
    return FALSE;

  g_list_free (classes);
  return TRUE;
}

static GObject *
empathy_new_message_dialog_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *retval;

  if (dialog_singleton)
    {
      retval = G_OBJECT (dialog_singleton);
      g_object_ref (retval);
    }
  else
    {
      retval = G_OBJECT_CLASS (
      empathy_new_message_dialog_parent_class)->constructor (type,
        n_props, props);

      dialog_singleton = EMPATHY_NEW_MESSAGE_DIALOG (retval);
      g_object_add_weak_pointer (retval, (gpointer) &dialog_singleton);
    }

  return retval;
}

static void
empathy_new_message_dialog_init (EmpathyNewMessageDialog *dialog)
{
  EmpathyContactSelectorDialog *parent = EMPATHY_CONTACT_SELECTOR_DIALOG (
        dialog);
  GtkWidget *image;

  /* add chat button */
  parent->button_action = gtk_button_new_with_mnemonic (_("C_hat"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_NEW_MESSAGE,
      GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (parent->button_action), image);

  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), parent->button_action,
      GTK_RESPONSE_ACCEPT);
  gtk_widget_show (parent->button_action);

  /* Tweak the dialog */
  gtk_window_set_title (GTK_WINDOW (dialog), _("New Conversation"));
  gtk_window_set_role (GTK_WINDOW (dialog), "new_message");

  gtk_widget_set_sensitive (parent->button_action, FALSE);
}

static void
empathy_new_message_dialog_class_init (
  EmpathyNewMessageDialogClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkDialogClass *dialog_class = GTK_DIALOG_CLASS (class);
  EmpathyContactSelectorDialogClass *selector_dialog_class = \
    EMPATHY_CONTACT_SELECTOR_DIALOG_CLASS (class);

  object_class->constructor = empathy_new_message_dialog_constructor;

  dialog_class->response = empathy_new_message_dialog_response;

  selector_dialog_class->account_filter = empathy_new_message_account_filter;
}

/**
 * empathy_new_message_dialog_new:
 * @parent: parent #GtkWindow of the dialog
 *
 * Create a new #EmpathyNewMessageDialog it.
 *
 * Return value: the new #EmpathyNewMessageDialog
 */
GtkWidget *
empathy_new_message_dialog_show (GtkWindow *parent)
{
  GtkWidget *dialog;

  dialog = g_object_new (EMPATHY_TYPE_NEW_MESSAGE_DIALOG, NULL);

  if (parent)
    {
      gtk_window_set_transient_for (GTK_WINDOW (dialog),
          GTK_WINDOW (parent));
    }

  gtk_widget_show (dialog);
  return dialog;
}
