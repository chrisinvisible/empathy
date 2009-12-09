/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2009 Collabora Ltd.
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
 * Authors: Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 *          Jonathan Tellier <jonathan.tellier@gmail.com>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-connection-managers.h>
#include <libempathy/empathy-connectivity.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#include <libempathy-gtk/empathy-protocol-chooser.h>
#include <libempathy-gtk/empathy-account-widget.h>
#include <libempathy-gtk/empathy-account-widget-irc.h>
#include <libempathy-gtk/empathy-account-widget-sip.h>
#include <libempathy-gtk/empathy-cell-renderer-activatable.h>
#include <libempathy-gtk/empathy-conf.h>
#include <libempathy-gtk/empathy-images.h>

#include "empathy-accounts-dialog.h"
#include "empathy-import-dialog.h"
#include "empathy-import-utils.h"
#include "ephy-spinner.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

/* Flashing delay for icons (milliseconds). */
#define FLASH_TIMEOUT 500

/* The primary text of the dialog shown to the user when he is about to lose
 * unsaved changes */
#define PENDING_CHANGES_QUESTION_PRIMARY_TEXT \
  _("There are unsaved modification regarding your %s account.")
/* The primary text of the dialog shown to the user when he is about to lose
 * an unsaved new account */
#define UNSAVED_NEW_ACCOUNT_QUESTION_PRIMARY_TEXT \
  _("Your new account has not been saved yet.")

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyAccountsDialog)
G_DEFINE_TYPE (EmpathyAccountsDialog, empathy_accounts_dialog, G_TYPE_OBJECT);

static EmpathyAccountsDialog *dialog_singleton = NULL;

typedef struct {
  GtkWidget *window;

  GtkWidget *alignment_settings;
  GtkWidget *alignment_infobar;

  GtkWidget *vbox_details;
  GtkWidget *infobar;
  GtkWidget *label_status;
  GtkWidget *image_status;
  GtkWidget *throbber;
  GtkWidget *frame_no_protocol;

  GtkWidget *treeview;

  GtkWidget *button_add;
  GtkWidget *button_import;

  GtkWidget *frame_new_account;
  GtkWidget *combobox_protocol;
  GtkWidget *hbox_type;
  GtkWidget *button_create;
  GtkWidget *button_back;
  GtkWidget *radiobutton_reuse;
  GtkWidget *radiobutton_register;

  GtkWidget *image_type;
  GtkWidget *label_name;
  GtkWidget *label_type;
  GtkWidget *settings_widget;

  /* We have to keep a reference on the actual EmpathyAccountWidget, not just
   * his GtkWidget. It is the only reliable source we can query to know if
   * there are any unsaved changes to the currently selected account. We can't
   * look at the account settings because it does not contain everything that
   * can be changed using the EmpathyAccountWidget. For instance, it does not
   * contain the state of the "Enabled" checkbox. */
  EmpathyAccountWidget *setting_widget_object;

  gboolean  connecting_show;
  guint connecting_id;

  gulong  settings_ready_id;
  EmpathyAccountSettings *settings_ready;

  TpAccountManager *account_manager;
  EmpathyConnectionManagers *cms;

  GtkWindow *parent_window;
  TpAccount *initial_selection;

  /* Those are needed when changing the selected row. When a user selects
   * another account and there are unsaved changes on the currently selected
   * one, a confirmation message box is presented to him. Since his answer
   * is retrieved asynchronously, we keep some information as member of the
   * EmpathyAccountsDialog object. */
  gboolean force_change_row;
  GtkTreeRowReference *destination_row;


} EmpathyAccountsDialogPriv;

enum {
  COL_NAME,
  COL_STATUS,
  COL_ACCOUNT_POINTER,
  COL_ACCOUNT_SETTINGS_POINTER,
  COL_COUNT
};

enum {
  PROP_PARENT = 1
};

static EmpathyAccountSettings * accounts_dialog_model_get_selected_settings (
    EmpathyAccountsDialog *dialog);

static gboolean accounts_dialog_get_settings_iter (
    EmpathyAccountsDialog *dialog,
    EmpathyAccountSettings *settings,
    GtkTreeIter *iter);

static void accounts_dialog_model_select_first (EmpathyAccountsDialog *dialog);

static void accounts_dialog_update_settings (EmpathyAccountsDialog *dialog,
    EmpathyAccountSettings *settings);

static void
accounts_dialog_update_name_label (EmpathyAccountsDialog *dialog,
    const gchar *display_name)
{
  gchar *text;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  text = g_markup_printf_escaped ("<big><b>%s</b></big>", display_name);
  gtk_label_set_markup (GTK_LABEL (priv->label_name), text);

  g_free (text);
}

static void
accounts_dialog_update_status_infobar (EmpathyAccountsDialog *dialog,
    TpAccount *account)
{
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);
  const gchar               *message;
  gchar                     *status_message;
  guint                     status;
  guint                     reason;
  guint                     presence;
  EmpathyConnectivity       *connectivity;
  GtkTreeView               *view;
  GtkTreeModel              *model;
  GtkTreeSelection          *selection;
  GtkTreeIter               iter;
  TpAccount                 *selected_account;

  view = GTK_TREE_VIEW (priv->treeview);
  selection = gtk_tree_view_get_selection (view);

  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return;

  gtk_tree_model_get (model, &iter,
      COL_ACCOUNT_POINTER, &selected_account, -1);

  /* do not update the infobar when the account is not selected */
  if (account != selected_account)
    {
      return;
    }

  status = tp_account_get_connection_status (account, &reason);
  presence = tp_account_get_current_presence (account, NULL, &status_message);

  connectivity = empathy_connectivity_dup_singleton ();

  gtk_image_set_from_icon_name (GTK_IMAGE (priv->image_status),
      empathy_icon_name_for_presence (presence), GTK_ICON_SIZE_SMALL_TOOLBAR);

  if (tp_account_is_enabled (account))
    {
      switch (status)
        {
          case TP_CONNECTION_STATUS_CONNECTING:
            message = _("Connecting...");
            gtk_info_bar_set_message_type (GTK_INFO_BAR (priv->infobar),
                GTK_MESSAGE_INFO);

            ephy_spinner_start (EPHY_SPINNER (priv->throbber));
            gtk_widget_show (priv->throbber);
            gtk_widget_hide (priv->image_status);
            break;
          case TP_CONNECTION_STATUS_CONNECTED:
            if (g_strcmp0 (status_message, "") == 0)
              {
                message = g_strdup_printf ("%s",
                    empathy_presence_get_default_message (presence));
              }
            else
              {
                message = g_strdup_printf ("%s - %s",
                    empathy_presence_get_default_message (presence),
                    status_message);
              }
            gtk_info_bar_set_message_type (GTK_INFO_BAR (priv->infobar),
                GTK_MESSAGE_INFO);

            gtk_widget_show (priv->image_status);
            gtk_widget_hide (priv->throbber);
            break;
          case TP_CONNECTION_STATUS_DISCONNECTED:
            message = g_strdup_printf (_("Disconnected - %s"),
                empathy_status_reason_get_default_message (reason));

            if (reason == TP_CONNECTION_STATUS_REASON_REQUESTED)
              {
                message = g_strdup_printf (_("Offline - %s"),
                    empathy_status_reason_get_default_message (reason));
                gtk_info_bar_set_message_type (GTK_INFO_BAR (priv->infobar),
                    GTK_MESSAGE_WARNING);
              }
            else
              {
                gtk_info_bar_set_message_type (GTK_INFO_BAR (priv->infobar),
                    GTK_MESSAGE_ERROR);
              }

            if (!empathy_connectivity_is_online (connectivity))
               message = _("Offline - No Network Connection");

            ephy_spinner_stop (EPHY_SPINNER (priv->throbber));
            gtk_widget_show (priv->image_status);
            gtk_widget_hide (priv->throbber);
            break;
          default:
            message = _("Unknown Status");
            gtk_info_bar_set_message_type (GTK_INFO_BAR (priv->infobar),
                GTK_MESSAGE_WARNING);

            ephy_spinner_stop (EPHY_SPINNER (priv->throbber));
            gtk_widget_hide (priv->image_status);
            gtk_widget_hide (priv->throbber);
        }
    }
  else
    {
      message = _("Offline - Account disabled");
      gtk_info_bar_set_message_type (GTK_INFO_BAR (priv->infobar),
          GTK_MESSAGE_WARNING);
      ephy_spinner_stop (EPHY_SPINNER (priv->throbber));
      gtk_widget_show (priv->image_status);
      gtk_widget_hide (priv->throbber);
    }

  gtk_label_set_text (GTK_LABEL (priv->label_status), message);
  gtk_widget_show (priv->label_status);
  gtk_widget_show (priv->infobar);

  g_object_unref (connectivity);
  g_free (status_message);
}

static void
empathy_account_dialog_widget_cancelled_cb (
    EmpathyAccountWidget *widget_object,
    EmpathyAccountsDialog *dialog)
{
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  EmpathyAccountSettings *settings;
  TpAccount *account;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  view = GTK_TREE_VIEW (priv->treeview);
  selection = gtk_tree_view_get_selection (view);

  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return;

  gtk_tree_model_get (model, &iter,
      COL_ACCOUNT_SETTINGS_POINTER, &settings,
      COL_ACCOUNT_POINTER, &account, -1);

  empathy_account_widget_discard_pending_changes (priv->setting_widget_object);

  if (account == NULL)
    {
      /* We were creating an account. We remove the selected row */
      gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
    }
  else
    {
      /* We were modifying an account. We discard the changes by reloading the
       * settings and the UI. */
      accounts_dialog_update_settings (dialog, settings);
      g_object_unref (account);
    }

  if (settings != NULL)
    g_object_unref (settings);
}

static void
empathy_account_dialog_account_created_cb (EmpathyAccountWidget *widget_object,
    EmpathyAccountsDialog *dialog)
{
  gchar *display_name;
  EmpathyAccountSettings *settings =
      accounts_dialog_model_get_selected_settings (dialog);

  display_name = empathy_account_widget_get_default_display_name (
      widget_object);

  empathy_account_settings_set_display_name_async (settings,
      display_name, NULL, NULL);

  g_free (display_name);

  accounts_dialog_update_settings (dialog, settings);

  if (settings)
    g_object_unref (settings);
}

static void
account_dialog_create_settings_widget (EmpathyAccountsDialog *dialog,
    EmpathyAccountSettings *settings)
{
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);
  gchar                     *icon_name;
  TpAccount                 *account;

  priv->setting_widget_object =
      empathy_account_widget_new_for_protocol (settings, FALSE);

  priv->settings_widget =
      empathy_account_widget_get_widget (priv->setting_widget_object);

  priv->settings_widget =
      empathy_account_widget_get_widget (priv->setting_widget_object);
  g_signal_connect (priv->setting_widget_object, "account-created",
        G_CALLBACK (empathy_account_dialog_account_created_cb), dialog);
  g_signal_connect (priv->setting_widget_object, "cancelled",
          G_CALLBACK (empathy_account_dialog_widget_cancelled_cb), dialog);

  gtk_container_add (GTK_CONTAINER (priv->alignment_settings),
      priv->settings_widget);
  gtk_widget_show (priv->settings_widget);

  icon_name = empathy_account_settings_get_icon_name (settings);

  if (!gtk_icon_theme_has_icon (gtk_icon_theme_get_default (),
          icon_name))
    /* show the default icon; keep this in sync with the default
     * one in empathy-accounts-dialog.ui.
     */
    icon_name = GTK_STOCK_CUT;

  gtk_image_set_from_icon_name (GTK_IMAGE (priv->image_type),
      icon_name, GTK_ICON_SIZE_DIALOG);
  gtk_widget_set_tooltip_text (priv->image_type,
      empathy_protocol_name_to_display_name
      (empathy_account_settings_get_protocol (settings)));
  gtk_widget_show (priv->image_type);

  accounts_dialog_update_name_label (dialog,
      empathy_account_settings_get_display_name (settings));

  account = empathy_account_settings_get_account (settings);
  accounts_dialog_update_status_infobar (dialog, account);
}

static void
account_dialog_settings_ready_cb (EmpathyAccountSettings *settings,
    GParamSpec *spec,
    EmpathyAccountsDialog *dialog)
{
  if (empathy_account_settings_is_ready (settings))
    account_dialog_create_settings_widget (dialog, settings);
}

static void
accounts_dialog_model_select_first (EmpathyAccountsDialog *dialog)
{
  GtkTreeView      *view;
  GtkTreeModel     *model;
  GtkTreeSelection *selection;
  GtkTreeIter       iter;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  /* select first */
  view = GTK_TREE_VIEW (priv->treeview);
  model = gtk_tree_view_get_model (view);

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      selection = gtk_tree_view_get_selection (view);
      gtk_tree_selection_select_iter (selection, &iter);
    }
  else
    {
      accounts_dialog_update_settings (dialog, NULL);
    }
}

static gboolean
accounts_dialog_has_pending_change (EmpathyAccountsDialog *dialog,
    TpAccount **account)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeSelection *selection;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->treeview));

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    gtk_tree_model_get (model, &iter, COL_ACCOUNT_POINTER, account, -1);

  return priv->setting_widget_object != NULL
      && empathy_account_widget_contains_pending_changes (
          priv->setting_widget_object);
}

static void
accounts_dialog_protocol_changed_cb (GtkWidget *widget,
    EmpathyAccountsDialog *dialog)
{
  TpConnectionManager *cm;
  TpConnectionManagerProtocol *proto;
  gboolean is_gtalk;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  cm = empathy_protocol_chooser_dup_selected (
      EMPATHY_PROTOCOL_CHOOSER (priv->combobox_protocol), &proto, &is_gtalk);

  if (cm == NULL)
    return;

  if (proto == NULL)
    {
      g_object_unref (cm);
      return;
    }

#ifndef HAVE_MOBLIN
  if (tp_connection_manager_protocol_can_register (proto) && !is_gtalk)
    {
      gtk_widget_show (priv->radiobutton_register);
      gtk_widget_show (priv->radiobutton_reuse);
    }
  else
    {
      gtk_widget_hide (priv->radiobutton_register);
      gtk_widget_hide (priv->radiobutton_reuse);
    }
#else
  gtk_widget_hide (priv->radiobutton_register);
  gtk_widget_hide (priv->radiobutton_reuse);
#endif

  g_object_unref (cm);
}

static void
accounts_dialog_setup_ui_to_add_account (EmpathyAccountsDialog *dialog)
{
  GtkTreeView *view;
  GtkTreeModel *model;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  view = GTK_TREE_VIEW (priv->treeview);
  model = gtk_tree_view_get_model (view);

  gtk_widget_set_sensitive (priv->button_add, FALSE);
  gtk_widget_hide (priv->vbox_details);
  gtk_widget_hide (priv->frame_no_protocol);
  gtk_widget_show (priv->frame_new_account);

  /* If we have no account, no need of a back button */
  if (gtk_tree_model_iter_n_children (model, NULL) > 0)
    gtk_widget_show (priv->button_back);
  else
    gtk_widget_hide (priv->button_back);

  accounts_dialog_protocol_changed_cb (priv->radiobutton_register, dialog);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->radiobutton_reuse),
      TRUE);
  gtk_combo_box_set_active (GTK_COMBO_BOX (priv->combobox_protocol), 0);
  gtk_widget_grab_focus (priv->combobox_protocol);
}

static void
accounts_dialog_show_question_dialog (EmpathyAccountsDialog *dialog,
    const gchar *primary_text,
    const gchar *secondary_text,
    GCallback response_callback,
    gpointer user_data,
    const gchar *first_button_text,
    ...)
{
  va_list button_args;
  GtkWidget *message_dialog;
  const gchar *button_text;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  message_dialog = gtk_message_dialog_new (GTK_WINDOW (priv->window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_QUESTION,
      GTK_BUTTONS_NONE,
      "%s", primary_text);

  gtk_message_dialog_format_secondary_text (
      GTK_MESSAGE_DIALOG (message_dialog), "%s", secondary_text);

  va_start (button_args, first_button_text);
  for (button_text = first_button_text;
       button_text;
       button_text = va_arg (button_args, const gchar *))
    {
      gint response_id;
      response_id = va_arg (button_args, gint);

      gtk_dialog_add_button (GTK_DIALOG (message_dialog), button_text,
          response_id);
    }
  va_end (button_args);

  g_signal_connect (message_dialog, "response", response_callback, user_data);

  gtk_widget_show (message_dialog);
}

static void
accounts_dialog_add_pending_changes_response_cb (GtkDialog *message_dialog,
  gint response_id,
  gpointer *user_data)
{
  EmpathyAccountsDialog *dialog = EMPATHY_ACCOUNTS_DIALOG (user_data);
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  gtk_widget_destroy (GTK_WIDGET (message_dialog));

  if (response_id == GTK_RESPONSE_YES)
    {
      empathy_account_widget_discard_pending_changes (
          priv->setting_widget_object);
      accounts_dialog_setup_ui_to_add_account (dialog);
    }
}

static gchar *
get_dialog_primary_text (TpAccount *account)
{
  if (account != NULL)
    {
      /* Existing account */
      return g_strdup_printf (PENDING_CHANGES_QUESTION_PRIMARY_TEXT,
          tp_account_get_display_name (account));
    }
  else
    {
      /* Newly created account */
      return g_strdup (UNSAVED_NEW_ACCOUNT_QUESTION_PRIMARY_TEXT);
    }
}

static void
accounts_dialog_button_add_clicked_cb (GtkWidget *button,
    EmpathyAccountsDialog *dialog)
{
  TpAccount *account = NULL;

  if (accounts_dialog_has_pending_change (dialog, &account))
    {
      gchar *question_dialog_primary_text = get_dialog_primary_text (account);

      accounts_dialog_show_question_dialog (dialog,
          question_dialog_primary_text,
          _("You are about to create a new account, which will discard\n"
              "your changes. Are you sure you want to proceed?"),
          G_CALLBACK (accounts_dialog_add_pending_changes_response_cb),
          dialog,
          GTK_STOCK_CANCEL, GTK_RESPONSE_NO,
          GTK_STOCK_DISCARD, GTK_RESPONSE_YES, NULL);

      g_free (question_dialog_primary_text);
    }
  else
    {
      accounts_dialog_setup_ui_to_add_account (dialog);
    }
}

static void
accounts_dialog_update_settings (EmpathyAccountsDialog *dialog,
    EmpathyAccountSettings *settings)
{
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  if (priv->settings_ready != NULL)
    {
      g_signal_handler_disconnect (priv->settings_ready,
          priv->settings_ready_id);
      priv->settings_ready = NULL;
      priv->settings_ready_id = 0;
    }

  if (!settings)
    {
      GtkTreeView  *view;
      GtkTreeModel *model;
      GtkTreeSelection *selection;

      view = GTK_TREE_VIEW (priv->treeview);
      model = gtk_tree_view_get_model (view);
      selection = gtk_tree_view_get_selection (view);

      if (gtk_tree_model_iter_n_children (model, NULL) > 0)
        {
          /* We have configured accounts, select the first one if there
           * is no other account selected already. */
          if (!gtk_tree_selection_get_selected (selection, NULL, NULL))
            accounts_dialog_model_select_first (dialog);

          return;
        }
      if (empathy_connection_managers_get_cms_num (priv->cms) > 0)
        {
          /* We have no account configured but we have some
           * profiles installed. The user obviously wants to add
           * an account. Click on the Add button for him. */
          accounts_dialog_button_add_clicked_cb (priv->button_add,
              dialog);
          return;
        }

      /* No account and no profile, warn the user */
      gtk_widget_hide (priv->vbox_details);
      gtk_widget_hide (priv->frame_new_account);
      gtk_widget_show (priv->frame_no_protocol);
      gtk_widget_set_sensitive (priv->button_add, FALSE);
      return;
    }

  /* We have an account selected, destroy old settings and create a new
   * one for the account selected */
  gtk_widget_hide (priv->frame_new_account);
  gtk_widget_hide (priv->frame_no_protocol);
  gtk_widget_show (priv->vbox_details);
  gtk_widget_set_sensitive (priv->button_add, TRUE);

  if (priv->settings_widget)
    {
      gtk_widget_destroy (priv->settings_widget);
      priv->settings_widget = NULL;
    }

  if (empathy_account_settings_is_ready (settings))
    {
      account_dialog_create_settings_widget (dialog, settings);
    }
  else
    {
      priv->settings_ready = settings;
      priv->settings_ready_id =
        g_signal_connect (settings, "notify::ready",
            G_CALLBACK (account_dialog_settings_ready_cb), dialog);
    }

}

static void
accounts_dialog_name_editing_started_cb (GtkCellRenderer *renderer,
    GtkCellEditable *editable,
    gchar *path,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  if (priv->connecting_id)
    g_source_remove (priv->connecting_id);

  DEBUG ("Editing account name started; stopping flashing");
}

static void
accounts_dialog_model_pixbuf_data_func (GtkTreeViewColumn *tree_column,
    GtkCellRenderer *cell,
    GtkTreeModel *model,
    GtkTreeIter *iter,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccountSettings  *settings;
  gchar              *icon_name;
  GdkPixbuf          *pixbuf;
  TpConnectionStatus  status;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  gtk_tree_model_get (model, iter,
      COL_STATUS, &status,
      COL_ACCOUNT_SETTINGS_POINTER, &settings,
      -1);

  icon_name = empathy_account_settings_get_icon_name (settings);
  pixbuf = empathy_pixbuf_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON);

  if (pixbuf)
    {
      if (status == TP_CONNECTION_STATUS_DISCONNECTED ||
          (status == TP_CONNECTION_STATUS_CONNECTING &&
              !priv->connecting_show))
        {
          GdkPixbuf *modded_pixbuf;

          modded_pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
              TRUE,
              8,
              gdk_pixbuf_get_width (pixbuf),
              gdk_pixbuf_get_height (pixbuf));

          gdk_pixbuf_saturate_and_pixelate (pixbuf,
              modded_pixbuf,
              1.0,
              TRUE);
          g_object_unref (pixbuf);
          pixbuf = modded_pixbuf;
        }
    }

  g_object_set (cell,
      "visible", TRUE,
      "pixbuf", pixbuf,
      NULL);

  g_object_unref (settings);

  if (pixbuf)
    g_object_unref (pixbuf);
}

static gboolean
accounts_dialog_row_changed_foreach (GtkTreeModel *model,
    GtkTreePath *path,
    GtkTreeIter *iter,
    gpointer user_data)
{
  gtk_tree_model_row_changed (model, path, iter);

  return FALSE;
}

static gboolean
accounts_dialog_flash_connecting_cb (EmpathyAccountsDialog *dialog)
{
  GtkTreeView  *view;
  GtkTreeModel *model;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  priv->connecting_show = !priv->connecting_show;

  view = GTK_TREE_VIEW (priv->treeview);
  model = gtk_tree_view_get_model (view);

  gtk_tree_model_foreach (model, accounts_dialog_row_changed_foreach, NULL);

  return TRUE;
}

static void
accounts_dialog_name_edited_cb (GtkCellRendererText *renderer,
    gchar *path,
    gchar *new_text,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccountSettings    *settings;
  GtkTreeModel *model;
  GtkTreePath  *treepath;
  GtkTreeIter   iter;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);
  gboolean connecting;

  empathy_account_manager_get_accounts_connected (&connecting);

  if (connecting)
    {
      priv->connecting_id = g_timeout_add (FLASH_TIMEOUT,
          (GSourceFunc) accounts_dialog_flash_connecting_cb,
          dialog);
    }

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
  treepath = gtk_tree_path_new_from_string (path);
  gtk_tree_model_get_iter (model, &iter, treepath);
  gtk_tree_model_get (model, &iter,
      COL_ACCOUNT_SETTINGS_POINTER, &settings,
      -1);
  gtk_list_store_set (GTK_LIST_STORE (model), &iter,
      COL_NAME, new_text,
      -1);
  gtk_tree_path_free (treepath);

  empathy_account_settings_set_display_name_async (settings, new_text,
      NULL, NULL);
  g_object_set (settings, "display-name-overridden", TRUE, NULL);
  g_object_unref (settings);
}

static void
accounts_dialog_delete_account_response_cb (GtkDialog *message_dialog,
  gint response_id,
  gpointer user_data)
{
  TpAccount *account;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreeSelection *selection;
  EmpathyAccountsDialog *account_dialog = EMPATHY_ACCOUNTS_DIALOG (user_data);
  EmpathyAccountsDialogPriv *priv = GET_PRIV (account_dialog);

  if (response_id == GTK_RESPONSE_YES)
    {
      selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->treeview));

      if (!gtk_tree_selection_get_selected (selection, &model, &iter))
        return;

      gtk_tree_model_get (model, &iter, COL_ACCOUNT_POINTER, &account, -1);

      if (account != NULL)
        {
          tp_account_remove_async (account, NULL, NULL);
          g_object_unref (account);
          account = NULL;
        }

      gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
      accounts_dialog_model_select_first (account_dialog);
    }

  gtk_widget_destroy (GTK_WIDGET (message_dialog));
}

static void
accounts_dialog_view_delete_activated_cb (EmpathyCellRendererActivatable *cell,
    const gchar *path_string,
    EmpathyAccountsDialog *dialog)
{
  TpAccount *account;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *question_dialog_primary_text;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));

  if (!gtk_tree_model_get_iter_from_string (model, &iter, path_string))
    return;

  gtk_tree_model_get (model, &iter, COL_ACCOUNT_POINTER, &account, -1);

  if (account == NULL || !tp_account_is_valid (account))
    {
      gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
      accounts_dialog_model_select_first (dialog);
      return;
    }

  question_dialog_primary_text = g_strdup_printf (
      _("Do you want to remove %s from your computer?"),
      tp_account_get_display_name (account));

  accounts_dialog_show_question_dialog (dialog, question_dialog_primary_text,
      _("This will not remove your account on the server."),
      G_CALLBACK (accounts_dialog_delete_account_response_cb),
      dialog,
      GTK_STOCK_CANCEL, GTK_RESPONSE_NO,
      GTK_STOCK_REMOVE, GTK_RESPONSE_YES, NULL);

  g_free (question_dialog_primary_text);

  if (account != NULL)
    {
      g_object_unref (account);
      account = NULL;
    }
}

static void
accounts_dialog_model_add_columns (EmpathyAccountsDialog *dialog)
{
  GtkTreeView       *view;
  GtkTreeViewColumn *column;
  GtkCellRenderer   *cell;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  view = GTK_TREE_VIEW (priv->treeview);
  gtk_tree_view_set_headers_visible (view, FALSE);

  /* Account column */
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_expand (column, TRUE);
  gtk_tree_view_append_column (view, column);

  /* Icon renderer */
  cell = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (column, cell, FALSE);
  gtk_tree_view_column_set_cell_data_func (column, cell,
      (GtkTreeCellDataFunc)
      accounts_dialog_model_pixbuf_data_func,
      dialog,
      NULL);

  /* Name renderer */
  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell,
      "ellipsize", PANGO_ELLIPSIZE_END,
      "width-chars", 25,
      "editable", TRUE,
      NULL);
  gtk_tree_view_column_pack_start (column, cell, TRUE);
  gtk_tree_view_column_add_attribute (column, cell, "text", COL_NAME);
  g_signal_connect (cell, "edited",
      G_CALLBACK (accounts_dialog_name_edited_cb),
      dialog);
  g_signal_connect (cell, "editing-started",
      G_CALLBACK (accounts_dialog_name_editing_started_cb),
      dialog);
  g_object_set (cell, "ypad", 4, NULL);

  /* Delete column */
  cell = empathy_cell_renderer_activatable_new ();
  gtk_tree_view_column_pack_start (column, cell, FALSE);
  g_object_set (cell,
        "icon-name", GTK_STOCK_DELETE,
        "show-on-select", TRUE,
        NULL);

  g_signal_connect (cell, "path-activated",
      G_CALLBACK (accounts_dialog_view_delete_activated_cb),
      dialog);
}

static EmpathyAccountSettings *
accounts_dialog_model_get_selected_settings (EmpathyAccountsDialog *dialog)
{
  GtkTreeView      *view;
  GtkTreeModel     *model;
  GtkTreeSelection *selection;
  GtkTreeIter       iter;
  EmpathyAccountSettings   *settings;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  view = GTK_TREE_VIEW (priv->treeview);
  selection = gtk_tree_view_get_selection (view);

  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return NULL;

  gtk_tree_model_get (model, &iter,
      COL_ACCOUNT_SETTINGS_POINTER, &settings, -1);

  return settings;
}

static void
accounts_dialog_model_selection_changed (GtkTreeSelection *selection,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccountSettings *settings;
  GtkTreeModel *model;
  GtkTreeIter   iter;
  gboolean      is_selection;

  is_selection = gtk_tree_selection_get_selected (selection, &model, &iter);

  settings = accounts_dialog_model_get_selected_settings (dialog);
  accounts_dialog_update_settings (dialog, settings);

  if (settings != NULL)
    g_object_unref (settings);
}

static void
accounts_dialog_selection_change_response_cb (GtkDialog *message_dialog,
  gint response_id,
  gpointer *user_data)
{
  EmpathyAccountsDialog *dialog = EMPATHY_ACCOUNTS_DIALOG (user_data);
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  gtk_widget_destroy (GTK_WIDGET (message_dialog));

    if (response_id == GTK_RESPONSE_YES && priv->destination_row != NULL)
      {
        /* The user wants to lose unsaved changes to the currently selected
         * account and select another account. We discard the changes and
         * select the other account. */
        GtkTreePath *path;
        GtkTreeSelection *selection;

        priv->force_change_row = TRUE;
        empathy_account_widget_discard_pending_changes (
            priv->setting_widget_object);

        path = gtk_tree_row_reference_get_path (priv->destination_row);
        selection = gtk_tree_view_get_selection (
            GTK_TREE_VIEW (priv->treeview));

        if (path != NULL)
          {
            /* This will trigger a call to
             * accounts_dialog_account_selection_change() */
            gtk_tree_selection_select_path (selection, path);
            gtk_tree_path_free (path);
          }

        gtk_tree_row_reference_free (priv->destination_row);
      }
    else
      {
        priv->force_change_row = FALSE;
      }
}

static gboolean
accounts_dialog_account_selection_change (GtkTreeSelection *selection,
    GtkTreeModel *model,
    GtkTreePath *path,
    gboolean path_currently_selected,
    gpointer data)
{
  TpAccount *account = NULL;
  EmpathyAccountsDialog *dialog = EMPATHY_ACCOUNTS_DIALOG (data);
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  if (priv->force_change_row)
    {
      /* We came back here because the user wants to discard changes to his
       * modified account. The changes have already been discarded so we
       * just change the selected row. */
      priv->force_change_row = FALSE;
      return TRUE;
    }

  if (accounts_dialog_has_pending_change (dialog, &account))
    {
      /* The currently selected account has some unsaved changes. We ask
       * the user if he really wants to lose his changes and select another
       * account */
      gchar *question_dialog_primary_text = get_dialog_primary_text (account);
      priv->destination_row = gtk_tree_row_reference_new (model, path);

      accounts_dialog_show_question_dialog (dialog,
          question_dialog_primary_text,
          _("You are about to select another account, which will discard\n"
              "your changes. Are you sure you want to proceed?"),
          G_CALLBACK (accounts_dialog_selection_change_response_cb),
          dialog,
          GTK_STOCK_CANCEL, GTK_RESPONSE_NO,
          GTK_STOCK_DISCARD, GTK_RESPONSE_YES, NULL);

      g_free (question_dialog_primary_text);
    }
  else
    {
      return TRUE;
    }

  return FALSE;
}

static void
accounts_dialog_model_setup (EmpathyAccountsDialog *dialog)
{
  GtkListStore     *store;
  GtkTreeSelection *selection;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  store = gtk_list_store_new (COL_COUNT,
      G_TYPE_STRING,         /* name */
      G_TYPE_UINT,           /* status */
      TP_TYPE_ACCOUNT,   /* account */
      EMPATHY_TYPE_ACCOUNT_SETTINGS); /* settings */

  gtk_tree_view_set_model (GTK_TREE_VIEW (priv->treeview),
      GTK_TREE_MODEL (store));

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->treeview));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
  gtk_tree_selection_set_select_function (selection,
      accounts_dialog_account_selection_change, dialog, NULL);

  g_signal_connect (selection, "changed",
      G_CALLBACK (accounts_dialog_model_selection_changed),
      dialog);

  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
      COL_NAME, GTK_SORT_ASCENDING);

  accounts_dialog_model_add_columns (dialog);

  g_object_unref (store);
}

static gboolean
accounts_dialog_get_settings_iter (EmpathyAccountsDialog *dialog,
    EmpathyAccountSettings *settings,
    GtkTreeIter *iter)
{
  GtkTreeView      *view;
  GtkTreeSelection *selection;
  GtkTreeModel     *model;
  gboolean          ok;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  /* Update the status in the model */
  view = GTK_TREE_VIEW (priv->treeview);
  selection = gtk_tree_view_get_selection (view);
  model = gtk_tree_view_get_model (view);

  for (ok = gtk_tree_model_get_iter_first (model, iter);
       ok;
       ok = gtk_tree_model_iter_next (model, iter))
    {
      EmpathyAccountSettings *this_settings;
      gboolean   equal;

      gtk_tree_model_get (model, iter,
          COL_ACCOUNT_SETTINGS_POINTER, &this_settings,
          -1);

      equal = (this_settings == settings);
      g_object_unref (this_settings);

      if (equal)
        return TRUE;
    }

  return FALSE;
}

static gboolean
accounts_dialog_get_account_iter (EmpathyAccountsDialog *dialog,
    TpAccount *account,
    GtkTreeIter *iter)
{
  GtkTreeView      *view;
  GtkTreeSelection *selection;
  GtkTreeModel     *model;
  gboolean          ok;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  /* Update the status in the model */
  view = GTK_TREE_VIEW (priv->treeview);
  selection = gtk_tree_view_get_selection (view);
  model = gtk_tree_view_get_model (view);

  for (ok = gtk_tree_model_get_iter_first (model, iter);
       ok;
       ok = gtk_tree_model_iter_next (model, iter))
    {
      EmpathyAccountSettings *settings;
      gboolean   equal;

      gtk_tree_model_get (model, iter,
          COL_ACCOUNT_SETTINGS_POINTER, &settings,
          -1);

      equal = empathy_account_settings_has_account (settings, account);
      g_object_unref (settings);

      if (equal)
        return TRUE;
    }

  return FALSE;
}

static void
accounts_dialog_model_set_selected (EmpathyAccountsDialog *dialog,
    EmpathyAccountSettings *settings)
{
  GtkTreeSelection *selection;
  GtkTreeIter       iter;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->treeview));
  if (accounts_dialog_get_settings_iter (dialog, settings, &iter))
    gtk_tree_selection_select_iter (selection, &iter);
}
static void
accounts_dialog_add (EmpathyAccountsDialog *dialog,
    EmpathyAccountSettings *settings)
{
  GtkTreeModel       *model;
  GtkTreeIter         iter;
  const gchar        *name;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
  name = empathy_account_settings_get_display_name (settings);

  gtk_list_store_append (GTK_LIST_STORE (model), &iter);

  gtk_list_store_set (GTK_LIST_STORE (model), &iter,
      COL_NAME, name,
      COL_STATUS, TP_CONNECTION_STATUS_DISCONNECTED,
      COL_ACCOUNT_SETTINGS_POINTER, settings,
      -1);
}

static void
accounts_dialog_connection_changed_cb (TpAccount *account,
    guint old_status,
    guint current,
    guint reason,
    gchar *dbus_error_name,
    GHashTable *details,
    EmpathyAccountsDialog *dialog)
{
  GtkTreeModel *model;
  GtkTreeIter   iter;
  gboolean      found;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  /* Update the status-infobar in the details view */
  accounts_dialog_update_status_infobar (dialog, account);

  /* Update the status in the model */
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));

  if (accounts_dialog_get_account_iter (dialog, account, &iter))
    {
      GtkTreePath *path;

      gtk_list_store_set (GTK_LIST_STORE (model), &iter,
          COL_STATUS, current,
          -1);

      path = gtk_tree_model_get_path (model, &iter);
      gtk_tree_model_row_changed (model, path, &iter);
      gtk_tree_path_free (path);
    }

  empathy_account_manager_get_accounts_connected (&found);

  if (!found && priv->connecting_id)
    {
      g_source_remove (priv->connecting_id);
      priv->connecting_id = 0;
    }

  if (found && !priv->connecting_id)
    priv->connecting_id = g_timeout_add (FLASH_TIMEOUT,
        (GSourceFunc) accounts_dialog_flash_connecting_cb,
        dialog);
}

static void
accounts_dialog_presence_changed_cb (TpAccount *account,
    guint presence,
    gchar *status,
    gchar *status_message,
    EmpathyAccountsDialog *dialog)
{
  /* Update the status-infobar in the details view*/
  accounts_dialog_update_status_infobar (dialog, account);
}

static void
accounts_dialog_account_display_name_changed_cb (TpAccount *account,
  GParamSpec *pspec,
  gpointer user_data)
{
  const gchar *display_name;
  GtkTreeIter iter;
  GtkTreeModel *model;
  EmpathyAccountSettings *settings;
  TpAccount *selected_account;
  EmpathyAccountsDialog *dialog = EMPATHY_ACCOUNTS_DIALOG (user_data);
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  display_name = tp_account_get_display_name (account);
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
  settings = accounts_dialog_model_get_selected_settings (dialog);
  if (settings == NULL)
    return;

  selected_account = empathy_account_settings_get_account (settings);

  if (accounts_dialog_get_account_iter (dialog, account, &iter))
    {
      gtk_list_store_set (GTK_LIST_STORE (model), &iter,
          COL_NAME, display_name,
          -1);
    }

  if (selected_account == account)
    accounts_dialog_update_name_label (dialog, display_name);

  g_object_unref (settings);
}

static void
accounts_dialog_add_account (EmpathyAccountsDialog *dialog,
    TpAccount *account)
{
  EmpathyAccountSettings *settings;
  GtkTreeModel       *model;
  GtkTreeIter         iter;
  TpConnectionStatus  status;
  const gchar        *name;
  gboolean            enabled;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
  status = tp_account_get_connection_status (account, NULL);
  name = tp_account_get_display_name (account);
  enabled = tp_account_is_enabled (account);

  settings = empathy_account_settings_new_for_account (account);

  if (!accounts_dialog_get_account_iter (dialog, account, &iter))
    gtk_list_store_append (GTK_LIST_STORE (model), &iter);

  gtk_list_store_set (GTK_LIST_STORE (model), &iter,
      COL_NAME, name,
      COL_STATUS, status,
      COL_ACCOUNT_POINTER, account,
      COL_ACCOUNT_SETTINGS_POINTER, settings,
      -1);

  accounts_dialog_connection_changed_cb (account,
      0,
      status,
      TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED,
      NULL,
      NULL,
      dialog);

  empathy_signal_connect_weak (account, "notify::display-name",
      G_CALLBACK (accounts_dialog_account_display_name_changed_cb),
      G_OBJECT (dialog));

  g_object_unref (settings);
}

static void
account_prepare_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyAccountsDialog *dialog = EMPATHY_ACCOUNTS_DIALOG (user_data);
  TpAccount *account = TP_ACCOUNT (source_object);
  GError *error = NULL;

  if (!tp_account_prepare_finish (account, result, &error))
    {
      DEBUG ("Failed to prepare account: %s", error->message);
      g_error_free (error);
      return;
    }

  accounts_dialog_add_account (dialog, account);
}

static void
accounts_dialog_account_validity_changed_cb (TpAccountManager *manager,
    TpAccount *account,
    gboolean valid,
    EmpathyAccountsDialog *dialog)
{
  tp_account_prepare_async (account, NULL, account_prepare_cb, dialog);
}

static void
accounts_dialog_account_removed_cb (TpAccountManager *manager,
    TpAccount *account,
    EmpathyAccountsDialog *dialog)
{
  GtkTreeIter iter;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  if (accounts_dialog_get_account_iter (dialog, account, &iter))
    {
      gtk_list_store_remove (GTK_LIST_STORE (
            gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview))), &iter);
    }
}

static void
enable_or_disable_account (EmpathyAccountsDialog *dialog,
    TpAccount *account,
    gboolean enabled)
{
  GtkTreeModel *model;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  /* Update the status in the model */
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));

  /* Update the status-infobar in the details view when disabling*/
  if (!enabled)
    accounts_dialog_update_status_infobar (dialog, account);

  DEBUG ("Account %s is now %s",
      tp_account_get_display_name (account),
      enabled ? "enabled" : "disabled");
}

static void
accounts_dialog_account_disabled_cb (TpAccountManager *manager,
    TpAccount *account,
    EmpathyAccountsDialog *dialog)
{
  enable_or_disable_account (dialog, account, FALSE);
}

static void
accounts_dialog_account_enabled_cb (TpAccountManager *manager,
    TpAccount *account,
    EmpathyAccountsDialog *dialog)
{
  enable_or_disable_account (dialog, account, TRUE);
}

static void
accounts_dialog_button_create_clicked_cb (GtkWidget *button,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccountSettings *settings;
  gchar *str;
  const gchar *display_name;
  TpConnectionManager *cm;
  TpConnectionManagerProtocol *proto;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);
  gboolean is_gtalk;

  cm = empathy_protocol_chooser_dup_selected (
      EMPATHY_PROTOCOL_CHOOSER (priv->combobox_protocol), &proto, &is_gtalk);

  display_name = empathy_protocol_name_to_display_name (
      is_gtalk ? "gtalk" : proto->name);

  if (display_name == NULL)
    display_name = proto->name;

  /* Create account */
  /* To translator: %s is the name of the protocol, such as "Google Talk" or
   * "Yahoo!"
   */
  str = g_strdup_printf (_("New %s account"), display_name);
  settings = empathy_account_settings_new (cm->name, proto->name, str);

  g_free (str);

#ifndef HAVE_MOBLIN
  if (tp_connection_manager_protocol_can_register (proto))
    {
      gboolean active;

      active = gtk_toggle_button_get_active
        (GTK_TOGGLE_BUTTON (priv->radiobutton_register));
      if (active)
        empathy_account_settings_set_boolean (settings, "register", TRUE);
    }
#endif

  if (is_gtalk)
    empathy_account_settings_set_icon_name_async (settings, "im-google-talk",
        NULL, NULL);

  accounts_dialog_add (dialog, settings);
  accounts_dialog_model_set_selected (dialog, settings);

  g_object_unref (settings);
  g_object_unref (cm);
}

static void
accounts_dialog_button_back_clicked_cb (GtkWidget *button,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccountSettings *settings;

  settings = accounts_dialog_model_get_selected_settings (dialog);
  accounts_dialog_update_settings (dialog, settings);

  if (settings)
    g_object_unref (settings);
}

static void
accounts_dialog_button_help_clicked_cb (GtkWidget *button,
    EmpathyAccountsDialog *dialog)
{
  empathy_url_show (button, "ghelp:empathy?accounts-window");
}

static void
accounts_dialog_button_import_clicked_cb (GtkWidget *button,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);
  GtkWidget *import_dialog;

  import_dialog = empathy_import_dialog_new (GTK_WINDOW (priv->window),
      FALSE);
  gtk_widget_show (import_dialog);
}

static void
accounts_dialog_close_response_cb (GtkDialog *message_dialog,
  gint response_id,
  gpointer user_data)
{
  GtkWidget *account_dialog = GTK_WIDGET (user_data);

  gtk_widget_destroy (GTK_WIDGET (message_dialog));

  if (response_id == GTK_RESPONSE_YES)
    gtk_widget_destroy (account_dialog);
}

static void
accounts_dialog_response_cb (GtkWidget *widget,
    gint response,
    EmpathyAccountsDialog *dialog)
{
  TpAccount *account = NULL;

  if (accounts_dialog_has_pending_change (dialog, &account))
    {
      gchar *question_dialog_primary_text = get_dialog_primary_text (account);

      accounts_dialog_show_question_dialog (dialog,
          question_dialog_primary_text,
          _("You are about to close the window, which will discard\n"
              "your changes. Are you sure you want to proceed?"),
          G_CALLBACK (accounts_dialog_close_response_cb),
          widget,
          GTK_STOCK_CANCEL, GTK_RESPONSE_NO,
          GTK_STOCK_DISCARD, GTK_RESPONSE_YES, NULL);

      g_free (question_dialog_primary_text);
    }
  else if (response == GTK_RESPONSE_CLOSE ||
           response == GTK_RESPONSE_DELETE_EVENT)
    gtk_widget_destroy (widget);
}

static gboolean
accounts_dialog_delete_event_cb (GtkWidget *widget,
    GdkEvent *event,
    EmpathyAccountsDialog *dialog)
{
  /* we maunally handle responses to delete events */
  return TRUE;
}

static void
accounts_dialog_destroy_cb (GtkObject *obj,
    EmpathyAccountsDialog *dialog)
{
  DEBUG ("%p", obj);

  g_object_unref (dialog);
}

static void
accounts_dialog_set_selected_account (EmpathyAccountsDialog *dialog,
    TpAccount *account)
{
  GtkTreeSelection *selection;
  GtkTreeIter       iter;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->treeview));
  if (accounts_dialog_get_account_iter (dialog, account, &iter))
    gtk_tree_selection_select_iter (selection, &iter);
}

static void
accounts_dialog_cms_ready_cb (EmpathyConnectionManagers *cms,
    GParamSpec *pspec,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  if (empathy_connection_managers_is_ready (cms))
    {
      accounts_dialog_update_settings (dialog, NULL);

      if (priv->initial_selection != NULL)
        {
          accounts_dialog_set_selected_account
              (dialog, priv->initial_selection);
          g_object_unref (priv->initial_selection);
          priv->initial_selection = NULL;
        }
    }
}

static void
accounts_dialog_accounts_setup (EmpathyAccountsDialog *dialog)
{
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);
  GList *accounts, *l;

  g_signal_connect (priv->account_manager, "account-validity-changed",
      G_CALLBACK (accounts_dialog_account_validity_changed_cb),
      dialog);
  g_signal_connect (priv->account_manager, "account-removed",
      G_CALLBACK (accounts_dialog_account_removed_cb),
      dialog);
  g_signal_connect (priv->account_manager, "account-enabled",
      G_CALLBACK (accounts_dialog_account_enabled_cb),
      dialog);
  g_signal_connect (priv->account_manager, "account-disabled",
      G_CALLBACK (accounts_dialog_account_disabled_cb),
      dialog);

  /* Add existing accounts */
  accounts = tp_account_manager_get_valid_accounts (priv->account_manager);
  for (l = accounts; l; l = l->next)
    {
      accounts_dialog_add_account (dialog, l->data);

      empathy_signal_connect_weak (l->data, "status-changed",
          G_CALLBACK (accounts_dialog_connection_changed_cb), G_OBJECT (dialog));
      empathy_signal_connect_weak (l->data, "presence-changed",
          G_CALLBACK (accounts_dialog_presence_changed_cb), G_OBJECT (dialog));
    }
  g_list_free (accounts);

  priv->cms = empathy_connection_managers_dup_singleton ();
  if (!empathy_connection_managers_is_ready (priv->cms))
    g_signal_connect (priv->cms, "notify::ready",
        G_CALLBACK (accounts_dialog_cms_ready_cb), dialog);

  accounts_dialog_model_select_first (dialog);
}

static void
accounts_dialog_manager_ready_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (source_object);
  GError *error = NULL;

  if (!tp_account_manager_prepare_finish (manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  accounts_dialog_accounts_setup (user_data);
}

static void
accounts_dialog_build_ui (EmpathyAccountsDialog *dialog)
{
  GtkBuilder                   *gui;
  gchar                        *filename;
  EmpathyAccountsDialogPriv    *priv = GET_PRIV (dialog);
  GtkWidget                    *content_area;
#ifdef HAVE_MOBLIN
  GtkWidget                    *action_area;
#endif

  filename = empathy_file_lookup ("empathy-accounts-dialog.ui", "src");

  gui = empathy_builder_get_file (filename,
      "accounts_dialog", &priv->window,
      "vbox_details", &priv->vbox_details,
      "frame_no_protocol", &priv->frame_no_protocol,
      "alignment_settings", &priv->alignment_settings,
      "alignment_infobar", &priv->alignment_infobar,
      "treeview", &priv->treeview,
      "frame_new_account", &priv->frame_new_account,
      "hbox_type", &priv->hbox_type,
      "button_create", &priv->button_create,
      "button_back", &priv->button_back,
      "radiobutton_reuse", &priv->radiobutton_reuse,
      "radiobutton_register", &priv->radiobutton_register,
      "image_type", &priv->image_type,
      "label_name", &priv->label_name,
      "button_add", &priv->button_add,
      "button_import", &priv->button_import,
      NULL);
  g_free (filename);

  empathy_builder_connect (gui, dialog,
      "accounts_dialog", "response", accounts_dialog_response_cb,
      "accounts_dialog", "destroy", accounts_dialog_destroy_cb,
      "accounts_dialog", "delete-event", accounts_dialog_delete_event_cb,
      "button_create", "clicked", accounts_dialog_button_create_clicked_cb,
      "button_back", "clicked", accounts_dialog_button_back_clicked_cb,
      "button_add", "clicked", accounts_dialog_button_add_clicked_cb,
      "button_help", "clicked", accounts_dialog_button_help_clicked_cb,
      "button_import", "clicked", accounts_dialog_button_import_clicked_cb,
      NULL);

  g_object_unref (gui);

#ifdef HAVE_MOBLIN
  action_area = gtk_dialog_get_action_area (GTK_DIALOG (priv->window));
  gtk_widget_hide (action_area);

  /* Translators: this is used only when built on a moblin platform */
  gtk_button_set_label (GTK_BUTTON (priv->button_create), _("_Next"));
  gtk_button_set_use_underline (GTK_BUTTON (priv->button_create), TRUE);
#endif

  priv->combobox_protocol = empathy_protocol_chooser_new ();
  gtk_box_pack_start (GTK_BOX (priv->hbox_type),
      priv->combobox_protocol,
      TRUE, TRUE, 0);
  gtk_widget_show (priv->combobox_protocol);
  g_signal_connect (priv->combobox_protocol, "changed",
      G_CALLBACK (accounts_dialog_protocol_changed_cb),
      dialog);

  if (priv->parent_window)
    gtk_window_set_transient_for (GTK_WINDOW (priv->window),
        priv->parent_window);

  /* set up spinner */
  priv->throbber = ephy_spinner_new ();
  ephy_spinner_set_size (EPHY_SPINNER (priv->throbber), GTK_ICON_SIZE_SMALL_TOOLBAR);

  priv->infobar = gtk_info_bar_new ();
  gtk_container_add (GTK_CONTAINER (priv->alignment_infobar),
      priv->infobar);
  gtk_widget_show (priv->infobar);

  priv->image_status = gtk_image_new_from_icon_name (
            empathy_icon_name_for_presence (
            TP_CONNECTION_PRESENCE_TYPE_OFFLINE), GTK_ICON_SIZE_SMALL_TOOLBAR);

  priv->label_status = gtk_label_new (NULL);

  content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (priv->infobar));
  gtk_box_pack_start (GTK_BOX (content_area), priv->throbber,
      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (content_area), priv->image_status,
      FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (content_area), priv->label_status);
}

static void
do_dispose (GObject *obj)
{
  EmpathyAccountsDialog *dialog = EMPATHY_ACCOUNTS_DIALOG (obj);
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  /* Disconnect signals */
  g_signal_handlers_disconnect_by_func (priv->account_manager,
      accounts_dialog_account_validity_changed_cb,
      dialog);
  g_signal_handlers_disconnect_by_func (priv->account_manager,
      accounts_dialog_account_removed_cb,
      dialog);
  g_signal_handlers_disconnect_by_func (priv->account_manager,
      accounts_dialog_account_enabled_cb,
      dialog);
  g_signal_handlers_disconnect_by_func (priv->account_manager,
      accounts_dialog_account_disabled_cb,
      dialog);
  g_signal_handlers_disconnect_by_func (priv->account_manager,
      accounts_dialog_manager_ready_cb,
      dialog);

  if (priv->connecting_id)
    g_source_remove (priv->connecting_id);

  if (priv->account_manager != NULL)
    {
      g_object_unref (priv->account_manager);
      priv->account_manager = NULL;
    }

  if (priv->cms != NULL)
    {
      g_object_unref (priv->cms);
      priv->cms = NULL;
    }

  if (priv->initial_selection != NULL)
    g_object_unref (priv->initial_selection);
  priv->initial_selection = NULL;

  G_OBJECT_CLASS (empathy_accounts_dialog_parent_class)->dispose (obj);
}

static GObject *
do_constructor (GType type,
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
      retval =
        G_OBJECT_CLASS (empathy_accounts_dialog_parent_class)->constructor
            (type, n_props, props);

      dialog_singleton = EMPATHY_ACCOUNTS_DIALOG (retval);
      g_object_add_weak_pointer (retval, (gpointer) &dialog_singleton);
      /* We add an extra reference that we'll release when the dialog is
       * destroyed (accounts_dialog_destroy_cb) */
      g_object_ref (retval);
    }

  return retval;
}

static void
do_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccountsDialogPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_PARENT:
      g_value_set_object (value, priv->parent_window);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
do_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccountsDialogPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_PARENT:
      priv->parent_window = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
do_constructed (GObject *object)
{
  EmpathyAccountsDialog *dialog = EMPATHY_ACCOUNTS_DIALOG (object);
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);
  gboolean import_asked;

  accounts_dialog_build_ui (dialog);
  accounts_dialog_model_setup (dialog);

  /* Set up signalling */
  priv->account_manager = tp_account_manager_dup ();

  tp_account_manager_prepare_async (priv->account_manager, NULL,
      accounts_dialog_manager_ready_cb, dialog);

  empathy_conf_get_bool (empathy_conf_get (),
      EMPATHY_PREFS_IMPORT_ASKED, &import_asked);

  if (empathy_import_accounts_to_import ())
    {
      gtk_widget_show (priv->button_import);

      if (!import_asked)
        {
          GtkWidget *import_dialog;

          empathy_conf_set_bool (empathy_conf_get (),
              EMPATHY_PREFS_IMPORT_ASKED, TRUE);
          import_dialog = empathy_import_dialog_new (GTK_WINDOW (priv->window),
              FALSE);
          gtk_widget_show (import_dialog);
        }
    }
}

static void
empathy_accounts_dialog_class_init (EmpathyAccountsDialogClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  oclass->constructor = do_constructor;
  oclass->dispose = do_dispose;
  oclass->constructed = do_constructed;
  oclass->set_property = do_set_property;
  oclass->get_property = do_get_property;

  param_spec = g_param_spec_object ("parent",
      "parent", "The parent window",
      GTK_TYPE_WINDOW,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_PARENT, param_spec);

  g_type_class_add_private (klass, sizeof (EmpathyAccountsDialogPriv));
}

static void
empathy_accounts_dialog_init (EmpathyAccountsDialog *dialog)
{
  EmpathyAccountsDialogPriv *priv;

  priv = G_TYPE_INSTANCE_GET_PRIVATE ((dialog),
      EMPATHY_TYPE_ACCOUNTS_DIALOG,
      EmpathyAccountsDialogPriv);
  dialog->priv = priv;
}

/* public methods */

GtkWidget *
empathy_accounts_dialog_show (GtkWindow *parent,
    TpAccount *selected_account)
{
  EmpathyAccountsDialog *dialog;
  EmpathyAccountsDialogPriv *priv;

  dialog = g_object_new (EMPATHY_TYPE_ACCOUNTS_DIALOG,
      "parent", parent, NULL);

  priv = GET_PRIV (dialog);

  if (selected_account)
    {
      if (priv->cms != NULL && empathy_connection_managers_is_ready (priv->cms))
        accounts_dialog_set_selected_account (dialog, selected_account);
      else
        /* save the selection to set it later when the cms
         * becomes ready.
         */
        priv->initial_selection = g_object_ref (selected_account);
    }

  gtk_window_present (GTK_WINDOW (priv->window));
  /* EmpathyAccountsDialog kepts a ref on itself until the dialog is
   * destroyed so we can release the ref returned by the constructor now. */
  g_object_unref (dialog);

  return priv->window;
}
