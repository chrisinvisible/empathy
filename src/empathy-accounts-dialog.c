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
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include <telepathy-glib/util.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-account-manager.h>
#include <libempathy/empathy-connection-managers.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#include <libempathy-gtk/empathy-protocol-chooser.h>
#include <libempathy-gtk/empathy-account-widget.h>
#include <libempathy-gtk/empathy-account-widget-irc.h>
#include <libempathy-gtk/empathy-account-widget-sip.h>
#include <libempathy-gtk/empathy-conf.h>

#include "empathy-accounts-dialog.h"
#if 0
/* FIXME MC-5 */
#include "empathy-import-dialog.h"
#endif

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

/* Flashing delay for icons (milliseconds). */
#define FLASH_TIMEOUT 500

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyAccountsDialog)
G_DEFINE_TYPE (EmpathyAccountsDialog, empathy_accounts_dialog, G_TYPE_OBJECT);

static EmpathyAccountsDialog *dialog_singleton = NULL;

typedef struct {
  GtkWidget *window;

  GtkWidget *alignment_settings;

  GtkWidget *vbox_details;
  GtkWidget *frame_no_protocol;

  GtkWidget *treeview;

  GtkWidget *button_add;
  GtkWidget *button_remove;
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

  gboolean  connecting_show;
  guint connecting_id;

  gulong  settings_ready_id;
  EmpathyAccountSettings *settings_ready;

  EmpathyAccountManager *account_manager;
  EmpathyConnectionManagers *cms;

  GtkWindow *parent_window;
  EmpathyAccount *initial_selection;
} EmpathyAccountsDialogPriv;

enum {
  COL_ENABLED,
  COL_NAME,
  COL_STATUS,
  COL_ACCOUNT_POINTER,
  COL_ACCOUNT_SETTINGS_POINTER,
  COL_COUNT
};

enum {
  PROP_PARENT = 1
};

static void accounts_dialog_update_settings (EmpathyAccountsDialog *dialog,
    EmpathyAccountSettings *settings);

#if 0
/* FIXME MC-5 */
static void accounts_dialog_button_import_clicked_cb  (GtkWidget *button,
    EmpathyAccountsDialog *dialog);
#endif

static void
accounts_dialog_update_name_label (EmpathyAccountsDialog *dialog,
    EmpathyAccountSettings *settings)
{
  gchar *text;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  text = g_markup_printf_escaped ("<big><b>%s</b></big>",
      empathy_account_settings_get_display_name (settings));
  gtk_label_set_markup (GTK_LABEL (priv->label_name), text);

  g_free (text);
}

static GtkWidget *
get_account_setup_widget (EmpathyAccountSettings *settings)
{
  const gchar *cm = empathy_account_settings_get_cm (settings);
  const gchar *proto = empathy_account_settings_get_protocol (settings);

  struct {
    const gchar *cm;
    const gchar *proto;
  } dialogs[] = {
    { "gabble", "jabber" },
    { "butterfly", "msn" },
    { "salut", "local-xmpp" },
    { "idle", "irc" },
    { "haze", "icq" },
    { "haze", "aim" },
    { "haze", "yahoo" },
    { "haze", "groupwise" },
    { "sofiasip", "sip" },
    { NULL, NULL }
  };
  int i;

  for (i = 0; dialogs[i].cm != NULL; i++)
    {
      if (!tp_strdiff (cm, dialogs[i].cm)
          && !tp_strdiff (proto, dialogs[i].proto))
        return empathy_account_widget_new_for_protocol (dialogs[i].proto, settings);
    }

  return empathy_account_widget_new_for_protocol ("generic", settings);
}

static void
account_dialog_create_settings_widget (EmpathyAccountsDialog *dialog,
    EmpathyAccountSettings *settings)
{
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  priv->settings_widget = get_account_setup_widget (settings);

  gtk_container_add (GTK_CONTAINER (priv->alignment_settings),
      priv->settings_widget);
  gtk_widget_show (priv->settings_widget);


  gtk_image_set_from_icon_name (GTK_IMAGE (priv->image_type),
      empathy_account_settings_get_icon_name (settings),
      GTK_ICON_SIZE_DIALOG);
  gtk_widget_set_tooltip_text (priv->image_type,
      empathy_account_settings_get_protocol (settings));

  accounts_dialog_update_name_label (dialog, settings);
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

static void
accounts_dialog_protocol_changed_cb (GtkWidget *widget,
    EmpathyAccountsDialog *dialog)
{
  TpConnectionManager *cm;
  TpConnectionManagerProtocol *proto;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  cm = empathy_protocol_chooser_dup_selected (
      EMPATHY_PROTOCOL_CHOOSER (priv->combobox_protocol), &proto);

  if (tp_connection_manager_protocol_can_register (proto))
    {
      gtk_widget_show (priv->radiobutton_register);
      gtk_widget_show (priv->radiobutton_reuse);
    }
  else
    {
      gtk_widget_hide (priv->radiobutton_register);
      gtk_widget_hide (priv->radiobutton_reuse);
    }
  g_object_unref (cm);
}

static void
accounts_dialog_button_add_clicked_cb (GtkWidget *button,
    EmpathyAccountsDialog *dialog)
{
  GtkTreeView      *view;
  GtkTreeSelection *selection;
  GtkTreeModel     *model;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  view = GTK_TREE_VIEW (priv->treeview);
  model = gtk_tree_view_get_model (view);
  selection = gtk_tree_view_get_selection (view);
  gtk_tree_selection_unselect_all (selection);

  gtk_widget_set_sensitive (priv->button_add, FALSE);
  gtk_widget_set_sensitive (priv->button_remove, FALSE);
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

      view = GTK_TREE_VIEW (priv->treeview);
      model = gtk_tree_view_get_model (view);

      if (gtk_tree_model_iter_n_children (model, NULL) > 0)
        {
          /* We have configured accounts, select the first one */
          accounts_dialog_model_select_first (dialog);
          return;
        }
      if (empathy_connection_managers_get_cms_num (priv->cms) > 0)
        {
          /* We have no account configured but we have some
           * profiles instsalled. The user obviously wants to add
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
      gtk_widget_set_sensitive (priv->button_remove, FALSE);
      return;
    }

  /* We have an account selected, destroy old settings and create a new
   * one for the account selected */
  gtk_widget_hide (priv->frame_new_account);
  gtk_widget_hide (priv->frame_no_protocol);
  gtk_widget_show (priv->vbox_details);
  gtk_widget_set_sensitive (priv->button_add, TRUE);
  gtk_widget_set_sensitive (priv->button_remove, TRUE);

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
  const gchar        *icon_name;
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

static void
accounts_dialog_enable_toggled_cb (GtkCellRendererToggle *cell_renderer,
    gchar *path,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccount    *account;
  GtkTreeModel *model;
  GtkTreePath  *treepath;
  GtkTreeIter   iter;
  gboolean      enabled;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
  treepath = gtk_tree_path_new_from_string (path);
  gtk_tree_model_get_iter (model, &iter, treepath);
  gtk_tree_model_get (model, &iter,
      COL_ACCOUNT_POINTER, &account,
      -1);
  gtk_tree_path_free (treepath);

  if (account == NULL)
    return;

  enabled = empathy_account_is_enabled (account);
  empathy_account_set_enabled (account, !enabled);

  DEBUG ("%s account %s", enabled ? "Disabled" : "Enable",
      empathy_account_get_display_name (account));

  g_object_unref (account);
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

  if (empathy_account_manager_get_connecting_accounts
      (priv->account_manager) > 0)
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
  g_object_unref (settings);
}

static void
accounts_dialog_model_add_columns (EmpathyAccountsDialog *dialog)
{
  GtkTreeView       *view;
  GtkTreeViewColumn *column;
  GtkCellRenderer   *cell;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  view = GTK_TREE_VIEW (priv->treeview);
  gtk_tree_view_set_headers_visible (view, TRUE);

  /* Enabled column */
  cell = gtk_cell_renderer_toggle_new ();
  gtk_tree_view_insert_column_with_attributes (view, -1,
      _("Enabled"),
      cell,
      "active", COL_ENABLED,
      NULL);
  g_signal_connect (cell, "toggled",
      G_CALLBACK (accounts_dialog_enable_toggled_cb),
      dialog);

  /* Account column */
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, _("Accounts"));
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

  if (settings)
    g_object_unref (settings);
}

static void
accounts_dialog_model_setup (EmpathyAccountsDialog *dialog)
{
  GtkListStore     *store;
  GtkTreeSelection *selection;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  store = gtk_list_store_new (COL_COUNT,
      G_TYPE_BOOLEAN,        /* enabled */
      G_TYPE_STRING,         /* name */
      G_TYPE_UINT,           /* status */
      EMPATHY_TYPE_ACCOUNT,   /* account */
      EMPATHY_TYPE_ACCOUNT_SETTINGS); /* settings */

  gtk_tree_view_set_model (GTK_TREE_VIEW (priv->treeview),
      GTK_TREE_MODEL (store));

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->treeview));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

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
    EmpathyAccount *account,
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

      equal = empathy_account_settings_has_account
        (settings, account);
      g_object_unref (settings);

      if (equal)
        return TRUE;
    }

  return FALSE;
}

static EmpathyAccount *
accounts_dialog_model_get_selected_account (EmpathyAccountsDialog *dialog)
{
  GtkTreeView      *view;
  GtkTreeModel     *model;
  GtkTreeSelection *selection;
  GtkTreeIter       iter;
  EmpathyAccount   *account;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  view = GTK_TREE_VIEW (priv->treeview);
  selection = gtk_tree_view_get_selection (view);

  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return NULL;

  gtk_tree_model_get (model, &iter, COL_ACCOUNT_POINTER, &account, -1);

  return account;
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

static gboolean
accounts_dialog_model_remove_selected (EmpathyAccountsDialog *dialog)
{
  GtkTreeView      *view;
  GtkTreeModel     *model;
  GtkTreeSelection *selection;
  GtkTreeIter       iter;
  EmpathyAccount *account;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  view = GTK_TREE_VIEW (priv->treeview);
  selection = gtk_tree_view_get_selection (view);

  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return FALSE;

  gtk_tree_model_get (model, &iter,
      COL_ACCOUNT_POINTER, &account,
      -1);

  if (account != NULL)
    empathy_account_remove_async (account, NULL, NULL);

  return gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
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
      COL_ENABLED, FALSE,
      COL_NAME, name,
      COL_STATUS, TP_CONNECTION_STATUS_DISCONNECTED,
      COL_ACCOUNT_SETTINGS_POINTER, settings,
      -1);
}

static void
accounts_dialog_connection_changed_cb     (EmpathyAccountManager *manager,
    EmpathyAccount *account,
    TpConnectionStatusReason reason,
    TpConnectionStatus current,
    TpConnectionStatus previous,
    EmpathyAccountsDialog *dialog)
{
  GtkTreeModel *model;
  GtkTreeIter   iter;
  gboolean      found;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

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

  found = (empathy_account_manager_get_connecting_accounts (manager) > 0);

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
accounts_dialog_add_account (EmpathyAccountsDialog *dialog,
    EmpathyAccount *account)
{
  EmpathyAccountSettings *settings;
  GtkTreeModel       *model;
  GtkTreeIter         iter;
  TpConnectionStatus  status;
  const gchar        *name;
  gboolean            enabled;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
  g_object_get (account, "connection-status", &status, NULL);
  name = empathy_account_get_display_name (account);
  enabled = empathy_account_is_enabled (account);

  if (!accounts_dialog_get_account_iter (dialog, account, &iter))
    gtk_list_store_append (GTK_LIST_STORE (model), &iter);

  settings = empathy_account_settings_new_for_account (account);

  gtk_list_store_set (GTK_LIST_STORE (model), &iter,
      COL_ENABLED, enabled,
      COL_NAME, name,
      COL_STATUS, status,
      COL_ACCOUNT_POINTER, account,
      COL_ACCOUNT_SETTINGS_POINTER, settings,
      -1);

  accounts_dialog_connection_changed_cb (priv->account_manager,
      account,
      TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED,
      status,
      TP_CONNECTION_STATUS_DISCONNECTED,
      dialog);

  g_object_unref (settings);
}

static void
accounts_dialog_update (EmpathyAccountsDialog *dialog,
    EmpathyAccountSettings *settings)
{
  GtkTreeModel       *model;
  GtkTreeIter         iter;
  TpConnectionStatus  status = TP_CONNECTION_STATUS_DISCONNECTED;
  const gchar        *name;
  gboolean            enabled = FALSE;
  EmpathyAccount     *account;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
  name = empathy_account_settings_get_display_name (settings);

  account = empathy_account_settings_get_account (settings);
  if (account != NULL)
    {
      enabled = empathy_account_is_enabled (account);
      g_object_get (account, "connection-status", &status, NULL);
    }

  accounts_dialog_get_settings_iter (dialog, settings, &iter);
  gtk_list_store_set (GTK_LIST_STORE (model), &iter,
      COL_ENABLED, enabled,
      COL_NAME, name,
      COL_STATUS, status,
      COL_ACCOUNT_POINTER, account,
      COL_ACCOUNT_SETTINGS_POINTER, settings,
      -1);
}

static void
accounts_dialog_account_added_cb (EmpathyAccountManager *manager,
    EmpathyAccount *account,
    EmpathyAccountsDialog *dialog)
{
  accounts_dialog_add_account (dialog, account);
}


static void
accounts_dialog_account_removed_cb (EmpathyAccountManager *manager,
    EmpathyAccount *account,
    EmpathyAccountsDialog *dialog)
{
  GtkTreeIter iter;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  if (accounts_dialog_get_account_iter (dialog, account, &iter))
    gtk_list_store_remove (GTK_LIST_STORE (
            gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview))), &iter);
}

static void
enable_or_disable_account (EmpathyAccountsDialog *dialog,
    EmpathyAccount *account,
    gboolean enabled)
{
  GtkTreeModel *model;
  GtkTreeIter   iter;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  /* Update the status in the model */
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));

  DEBUG ("Account %s is now %s",
      empathy_account_get_display_name (account),
      enabled ? "enabled" : "disabled");

  if (accounts_dialog_get_account_iter (dialog, account, &iter))
    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
        COL_ENABLED, enabled,
        -1);
}

static void
accounts_dialog_account_disabled_cb (EmpathyAccountManager *manager,
    EmpathyAccount *account,
    EmpathyAccountsDialog *dialog)
{
  enable_or_disable_account (dialog, account, FALSE);
}

static void
accounts_dialog_account_enabled_cb (EmpathyAccountManager *manager,
    EmpathyAccount *account,
    EmpathyAccountsDialog *dialog)
{
  enable_or_disable_account (dialog, account, TRUE);
}

static void
accounts_dialog_account_changed_cb (EmpathyAccountManager *manager,
    EmpathyAccount *account,
    EmpathyAccountsDialog  *dialog)
{
  EmpathyAccountSettings *settings, *selected_settings;
  GtkTreeModel *model;
  GtkTreeIter iter;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));

  if (!accounts_dialog_get_account_iter (dialog, account, &iter))
    return;

  gtk_tree_model_get (model, &iter,
      COL_ACCOUNT_SETTINGS_POINTER, &settings,
      -1);

  accounts_dialog_update (dialog, settings);
  selected_settings = accounts_dialog_model_get_selected_settings (dialog);

  if (settings == selected_settings)
    accounts_dialog_update_name_label (dialog, settings);
}

static void
accounts_dialog_button_create_clicked_cb (GtkWidget *button,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccountSettings *settings;
  gchar     *str;
  TpConnectionManager *cm;
  TpConnectionManagerProtocol *proto;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  cm = empathy_protocol_chooser_dup_selected (
      EMPATHY_PROTOCOL_CHOOSER (priv->combobox_protocol), &proto);

  /* Create account */
  /* To translator: %s is the protocol name */
  str = g_strdup_printf (_("New %s account"), proto->name);

  settings = empathy_account_settings_new (cm->name, proto->name, str);

  g_free (str);

  if (tp_connection_manager_protocol_can_register (proto))
    {
      gboolean active;

      active = gtk_toggle_button_get_active
          (GTK_TOGGLE_BUTTON (priv->radiobutton_register));
      if (active)
        empathy_account_settings_set_boolean (settings, "register", TRUE);
    }

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
  accounts_dialog_update (dialog, settings);
}

static void
accounts_dialog_button_help_clicked_cb (GtkWidget *button,
    EmpathyAccountsDialog *dialog)
{
  empathy_url_show (button, "ghelp:empathy?empathy-create-account");
}

static void
accounts_dialog_button_remove_clicked_cb (GtkWidget *button,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccount *account;
  GtkWidget *message_dialog;
  gint       res;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  account = accounts_dialog_model_get_selected_account (dialog);

  if (account == NULL || !empathy_account_is_valid (account))
    {
      accounts_dialog_model_remove_selected (dialog);
      accounts_dialog_model_select_first (dialog);
      return;
    }
  message_dialog = gtk_message_dialog_new
    (GTK_WINDOW (priv->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_NONE,
        _("You are about to remove your %s account!\n"
            "Are you sure you want to proceed?"),
        empathy_account_get_display_name (account));

  gtk_message_dialog_format_secondary_text
    (GTK_MESSAGE_DIALOG (message_dialog),
        _("Any associated conversations and chat rooms will NOT be "
            "removed if you decide to proceed.\n"
            "\n"
            "Should you decide to add the account back at a later time, "
            "they will still be available."));

  gtk_dialog_add_button (GTK_DIALOG (message_dialog),
      GTK_STOCK_CANCEL,
      GTK_RESPONSE_NO);
  gtk_dialog_add_button (GTK_DIALOG (message_dialog),
      GTK_STOCK_REMOVE,
      GTK_RESPONSE_YES);

  gtk_widget_show (message_dialog);
  res = gtk_dialog_run (GTK_DIALOG (message_dialog));

  if (res == GTK_RESPONSE_YES)
    {
      accounts_dialog_model_remove_selected (dialog);
      accounts_dialog_model_select_first (dialog);
    }
  gtk_widget_destroy (message_dialog);
}

#if 0
/* FIXME MC-5 */
static void
accounts_dialog_button_import_clicked_cb (GtkWidget *button,
    EmpathyAccountsDialog *dialog)
{
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  empathy_import_dialog_show (GTK_WINDOW (priv->window), TRUE);
}
#endif

static void
accounts_dialog_response_cb (GtkWidget *widget,
    gint response,
    EmpathyAccountsDialog *dialog)
{
  GList *accounts, *l;
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  if (response == GTK_RESPONSE_CLOSE)
    {
      /* Delete incomplete accounts */
      accounts = empathy_account_manager_dup_accounts
        (priv->account_manager);
      for (l = accounts; l; l = l->next)
        {
          EmpathyAccount *account;

          account = l->data;
          if (!empathy_account_is_valid (account))
            /* FIXME: Warn the user the account is not
             * complete and is going to be removed.
             */
            empathy_account_manager_remove
              (priv->account_manager, account);

          g_object_unref (account);
        }
      g_list_free (accounts);

      gtk_widget_destroy (widget);
    }
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
    EmpathyAccount *account)
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
          priv->initial_selection = NULL;
        }
    }
}

static void
accounts_dialog_build_ui (EmpathyAccountsDialog *dialog)
{
  GtkBuilder                   *gui;
  gchar                        *filename;
  EmpathyAccountsDialogPriv    *priv = GET_PRIV (dialog);

  filename = empathy_file_lookup ("empathy-accounts-dialog.ui", "src");

  gui = empathy_builder_get_file (filename,
      "accounts_dialog", &priv->window,
      "vbox_details", &priv->vbox_details,
      "frame_no_protocol", &priv->frame_no_protocol,
      "alignment_settings", &priv->alignment_settings,
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
      "button_remove", &priv->button_remove,
      "button_import", &priv->button_import,
      NULL);
  g_free (filename);

  empathy_builder_connect (gui, dialog,
      "accounts_dialog", "response", accounts_dialog_response_cb,
      "accounts_dialog", "destroy", accounts_dialog_destroy_cb,
      "button_create", "clicked", accounts_dialog_button_create_clicked_cb,
      "button_back", "clicked", accounts_dialog_button_back_clicked_cb,
      "button_add", "clicked", accounts_dialog_button_add_clicked_cb,
      "button_remove", "clicked", accounts_dialog_button_remove_clicked_cb,
#if 0
      /* FIXME MC-5  */
      "button_import", "clicked", accounts_dialog_button_import_clicked_cb,
#endif
      "button_help", "clicked", accounts_dialog_button_help_clicked_cb,
      NULL);

  g_object_unref (gui);

  priv->combobox_protocol = empathy_protocol_chooser_new ();
  gtk_box_pack_end (GTK_BOX (priv->hbox_type),
      priv->combobox_protocol,
      TRUE, TRUE, 0);
  gtk_widget_show (priv->combobox_protocol);
  g_signal_connect (priv->combobox_protocol, "changed",
      G_CALLBACK (accounts_dialog_protocol_changed_cb),
      dialog);

  if (priv->parent_window)
    gtk_window_set_transient_for (GTK_WINDOW (priv->window),
        priv->parent_window);
}

static void
do_dispose (GObject *obj)
{
  EmpathyAccountsDialog *dialog = EMPATHY_ACCOUNTS_DIALOG (obj);
  EmpathyAccountsDialogPriv *priv = GET_PRIV (dialog);

  /* Disconnect signals */
  g_signal_handlers_disconnect_by_func (priv->account_manager,
      accounts_dialog_account_added_cb,
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
      accounts_dialog_account_changed_cb,
      dialog);
  g_signal_handlers_disconnect_by_func (priv->account_manager,
      accounts_dialog_connection_changed_cb,
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
    }
  else
    {
      retval =
        G_OBJECT_CLASS (empathy_accounts_dialog_parent_class)->constructor
            (type, n_props, props);

      dialog_singleton = EMPATHY_ACCOUNTS_DIALOG (retval);
      g_object_add_weak_pointer (retval, (gpointer) &dialog_singleton);
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
  GList *accounts, *l;
  gboolean import_asked;

  accounts_dialog_build_ui (dialog);

  /* Set up signalling */
  priv->account_manager = empathy_account_manager_dup_singleton ();

  g_signal_connect (priv->account_manager, "account-created",
      G_CALLBACK (accounts_dialog_account_added_cb),
      dialog);
  g_signal_connect (priv->account_manager, "account-deleted",
      G_CALLBACK (accounts_dialog_account_removed_cb),
      dialog);
  g_signal_connect (priv->account_manager, "account-enabled",
      G_CALLBACK (accounts_dialog_account_enabled_cb),
      dialog);
  g_signal_connect (priv->account_manager, "account-disabled",
      G_CALLBACK (accounts_dialog_account_disabled_cb),
      dialog);
  g_signal_connect (priv->account_manager, "account-changed",
      G_CALLBACK (accounts_dialog_account_changed_cb),
      dialog);
  g_signal_connect (priv->account_manager, "account-connection-changed",
      G_CALLBACK (accounts_dialog_connection_changed_cb),
      dialog);

  accounts_dialog_model_setup (dialog);

  /* Add existing accounts */
  accounts = empathy_account_manager_dup_accounts (priv->account_manager);
  for (l = accounts; l; l = l->next)
    {
      accounts_dialog_add_account (dialog, l->data);
      g_object_unref (l->data);
    }
  g_list_free (accounts);

  priv->cms = empathy_connection_managers_dup_singleton ();
  if (!empathy_connection_managers_is_ready (priv->cms))
    g_signal_connect (priv->cms, "notify::ready",
        G_CALLBACK (accounts_dialog_cms_ready_cb), dialog);

  accounts_dialog_model_select_first (dialog);

  empathy_conf_get_bool (empathy_conf_get (),
      EMPATHY_PREFS_IMPORT_ASKED, &import_asked);


#if 0
  /* FIXME MC-5 */
  if (empathy_import_dialog_accounts_to_import ())
    {

      if (!import_asked)
        {
          empathy_conf_set_bool (empathy_conf_get (),
              EMPATHY_PREFS_IMPORT_ASKED, TRUE);
          empathy_import_dialog_show (GTK_WINDOW (priv->window),
              FALSE);
        }
    }
  else
    {
      gtk_widget_set_sensitive (priv->button_import, FALSE);
    }
#endif
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
    EmpathyAccount *selected_account)
{
  EmpathyAccountsDialog *dialog;
  EmpathyAccountsDialogPriv *priv;

  dialog = g_object_new (EMPATHY_TYPE_ACCOUNTS_DIALOG,
      "parent", parent, NULL);

  priv = GET_PRIV (dialog);

  if (selected_account && empathy_connection_managers_is_ready (priv->cms))
    accounts_dialog_set_selected_account (dialog, selected_account);
  else
    /* save the selection to set it later when the cms
     * becomes ready.
     */
    priv->initial_selection = selected_account;

  gtk_window_present (GTK_WINDOW (priv->window));

  return priv->window;
}
