/*
 * Copyright (C) 2009 Collabora Ltd.
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
 * Authors: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <telepathy-glib/interfaces.h>

#include <libempathy/empathy-tp-contact-factory.h>
#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-call-factory.h>
#include <libempathy/empathy-dispatcher.h>
#include <libempathy/empathy-utils.h>

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include <libempathy/empathy-debug.h>

#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-images.h>

#include "empathy-new-call-dialog.h"
#include "empathy-account-chooser.h"

static EmpathyNewCallDialog *dialog_singleton = NULL;

G_DEFINE_TYPE(EmpathyNewCallDialog, empathy_new_call_dialog,
               EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG)

typedef struct _EmpathyNewCallDialogPriv EmpathyNewCallDialogPriv;

struct _EmpathyNewCallDialogPriv {
  GtkWidget *check_video;
};

#define GET_PRIV(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMPATHY_TYPE_NEW_CALL_DIALOG, \
    EmpathyNewCallDialogPriv))

/**
 * SECTION:empathy-new-call-dialog
 * @title: EmpathyNewCallDialog
 * @short_description: A dialog to show a new call
 * @include: libempathy-gtk/empathy-new-call-dialog.h
 *
 * #EmpathyNewCallDialog is a dialog which allows a call
 * to be started with any contact on any enabled account.
 */

static void
got_contact_cb (EmpathyTpContactFactory *factory,
    EmpathyContact *contact,
    const GError *error,
    gpointer user_data,
    GObject *object)
{
  EmpathyCallFactory *call_factory;
  gboolean video = GPOINTER_TO_UINT (user_data);

  if (error != NULL)
    {
      DEBUG ("Failed: %s", error->message);
      return;
    }

  call_factory = empathy_call_factory_get ();
  empathy_call_factory_new_call_with_streams (call_factory, contact, TRUE,
      video);
}

static void
empathy_new_call_dialog_response (GtkDialog *dialog, int response_id)
{
  EmpathyNewCallDialogPriv *priv = GET_PRIV (dialog);
  EmpathyTpContactFactory *factory;
  gboolean video;
  TpConnection *connection;
  const gchar *contact_id;

  if (response_id != GTK_RESPONSE_ACCEPT) goto out;

  contact_id = empathy_contact_selector_dialog_get_selected (
      EMPATHY_CONTACT_SELECTOR_DIALOG (dialog), &connection);

  if (EMP_STR_EMPTY (contact_id) || connection == NULL) goto out;

  /* check if video is enabled now because the dialog will be destroyed once
   * we return from this function. */
  video = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->check_video));

  factory = empathy_tp_contact_factory_dup_singleton (connection);
  empathy_tp_contact_factory_get_from_id (factory, contact_id,
      got_contact_cb, GUINT_TO_POINTER (video), NULL, NULL);

  g_object_unref (factory);

out:
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static gboolean
empathy_new_call_dialog_account_filter (EmpathyContactSelectorDialog *dialog,
    TpAccount *account)
{
  TpConnection *connection;
  EmpathyDispatcher *dispatcher;
  GList *classes;

  if (tp_account_get_connection_status (account, NULL) !=
      TP_CONNECTION_STATUS_CONNECTED)
    return FALSE;

  /* check if CM supports calls */
  connection = tp_account_get_connection (account);
  if (connection == NULL)
    return FALSE;

  dispatcher = empathy_dispatcher_dup_singleton ();

  classes = empathy_dispatcher_find_requestable_channel_classes
    (dispatcher, connection, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
     TP_HANDLE_TYPE_CONTACT, NULL);

  g_object_unref (dispatcher);

  if (classes == NULL)
    return FALSE;

  g_list_free (classes);
  return TRUE;
}

static GObject *
empathy_new_call_dialog_constructor (GType type,
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
      empathy_new_call_dialog_parent_class)->constructor (type,
        n_props, props);

      dialog_singleton = EMPATHY_NEW_CALL_DIALOG (retval);
      g_object_add_weak_pointer (retval, (gpointer) &dialog_singleton);
    }

  return retval;
}

static void
empathy_new_call_dialog_init (EmpathyNewCallDialog *dialog)
{
  EmpathyContactSelectorDialog *parent = EMPATHY_CONTACT_SELECTOR_DIALOG (
        dialog);
  EmpathyNewCallDialogPriv *priv = GET_PRIV (dialog);
  GtkWidget *image;

  /* add video toggle */
  priv->check_video = gtk_check_button_new_with_mnemonic (_("Send _Video"));

  gtk_box_pack_end (GTK_BOX (parent->vbox), priv->check_video,
      FALSE, TRUE, 0);

  gtk_widget_show (priv->check_video);

  /* add chat button */
  parent->button_action = gtk_button_new_with_mnemonic (_("_Call"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VOIP,
      GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (parent->button_action), image);

  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), parent->button_action,
      GTK_RESPONSE_ACCEPT);
  gtk_widget_show (parent->button_action);

  /* Tweak the dialog */
  gtk_window_set_title (GTK_WINDOW (dialog), _("New Call"));
  gtk_window_set_role (GTK_WINDOW (dialog), "new_call");

  gtk_widget_set_sensitive (parent->button_action, FALSE);
}

static void
empathy_new_call_dialog_class_init (
  EmpathyNewCallDialogClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkDialogClass *dialog_class = GTK_DIALOG_CLASS (class);
  EmpathyContactSelectorDialogClass *selector_dialog_class = \
    EMPATHY_CONTACT_SELECTOR_DIALOG_CLASS (class);

  g_type_class_add_private (class, sizeof (EmpathyNewCallDialogPriv));

  object_class->constructor = empathy_new_call_dialog_constructor;

  dialog_class->response = empathy_new_call_dialog_response;

  selector_dialog_class->account_filter = empathy_new_call_dialog_account_filter;
}

/**
 * empathy_new_call_dialog_new:
 * @parent: parent #GtkWindow of the dialog
 *
 * Create a new #EmpathyNewCallDialog it.
 *
 * Return value: the new #EmpathyNewCallDialog
 */
GtkWidget *
empathy_new_call_dialog_show (GtkWindow *parent)
{
  GtkWidget *dialog;

  dialog = g_object_new (EMPATHY_TYPE_NEW_CALL_DIALOG, NULL);

  if (parent)
    {
      gtk_window_set_transient_for (GTK_WINDOW (dialog),
                  GTK_WINDOW (parent));
    }

  gtk_widget_show (dialog);
  return dialog;
}
