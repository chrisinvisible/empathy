/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2008 Collabora Ltd.
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

typedef struct {
	GtkWidget        *window;

	GtkWidget        *alignment_settings;

	GtkWidget        *vbox_details;
	GtkWidget        *frame_no_protocol;

	GtkWidget        *treeview;

	GtkWidget        *button_add;
	GtkWidget        *button_remove;
	GtkWidget        *button_import;

	GtkWidget        *frame_new_account;
	GtkWidget        *combobox_protocol;
	GtkWidget        *hbox_type;
	GtkWidget        *button_create;
	GtkWidget        *button_back;
	GtkWidget        *radiobutton_reuse;
	GtkWidget        *radiobutton_register;

	GtkWidget        *image_type;
	GtkWidget        *label_name;
	GtkWidget        *label_type;
	GtkWidget        *settings_widget;

	gboolean          connecting_show;
	guint             connecting_id;

	gulong            settings_ready_id;
	EmpathyAccountSettings *settings_ready;

	EmpathyAccountManager *account_manager;
} EmpathyAccountsDialog;

enum {
	COL_ENABLED,
	COL_NAME,
	COL_STATUS,
	COL_ACCOUNT_POINTER,
	COL_ACCOUNT_SETTINGS_POINTER,
	COL_COUNT
};

static void       accounts_dialog_update (EmpathyAccountsDialog    *dialog,
							     EmpathyAccountSettings           *settings);
static void       accounts_dialog_model_setup               (EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_model_add_columns         (EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_name_editing_started_cb   (GtkCellRenderer          *renderer,
							     GtkCellEditable          *editable,
							     gchar                    *path,
							     EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_model_select_first        (EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_model_pixbuf_data_func    (GtkTreeViewColumn        *tree_column,
							     GtkCellRenderer          *cell,
							     GtkTreeModel             *model,
							     GtkTreeIter              *iter,
							     EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_model_set_selected        (EmpathyAccountsDialog    *dialog,
							     EmpathyAccountSettings *settings);
static gboolean   accounts_dialog_model_remove_selected     (EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_model_selection_changed   (GtkTreeSelection         *selection,
							     EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_account_added_cb          (EmpathyAccountManager    *manager,
							     EmpathyAccount           *account,
							     EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_account_removed_cb        (EmpathyAccountManager    *manager,
							     EmpathyAccount           *account,
							     EmpathyAccountsDialog    *dialog);
static gboolean   accounts_dialog_row_changed_foreach       (GtkTreeModel             *model,
							     GtkTreePath              *path,
							     GtkTreeIter              *iter,
							     gpointer                  user_data);
static gboolean   accounts_dialog_flash_connecting_cb       (EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_connection_changed_cb     (EmpathyAccountManager    *manager,
							     EmpathyAccount           *account,
							     TpConnectionStatusReason  reason,
							     TpConnectionStatus        current,
							     TpConnectionStatus        previous,
							     EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_button_create_clicked_cb  (GtkWidget                *button,
							     EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_button_back_clicked_cb    (GtkWidget                *button,
							     EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_button_add_clicked_cb     (GtkWidget                *button,
							     EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_button_help_clicked_cb    (GtkWidget                *button,
							     EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_button_remove_clicked_cb  (GtkWidget                *button,
							     EmpathyAccountsDialog    *dialog);
#if 0
/* FIXME MC-5 */
static void       accounts_dialog_button_import_clicked_cb  (GtkWidget                *button,
							     EmpathyAccountsDialog    *dialog);
#endif
static void       accounts_dialog_response_cb               (GtkWidget                *widget,
							     gint                      response,
							     EmpathyAccountsDialog    *dialog);
static void       accounts_dialog_destroy_cb                (GtkWidget                *widget,
							     EmpathyAccountsDialog    *dialog);

static void
accounts_dialog_update_name_label (EmpathyAccountsDialog *dialog,
				   EmpathyAccountSettings *settings)
{
	gchar *text;

	text = g_markup_printf_escaped ("<big><b>%s</b></big>",
			empathy_account_settings_get_display_name (settings));
	gtk_label_set_markup (GTK_LABEL (dialog->label_name), text);

	g_free (text);
}

typedef GtkWidget *CreateWidget (EmpathyAccountSettings *);

static GtkWidget *
get_account_setup_widget (EmpathyAccountSettings *settings)
{
	const gchar *cm = empathy_account_settings_get_cm (settings);
	const gchar *proto = empathy_account_settings_get_protocol (settings);
	struct {
		const gchar *cm;
		const gchar *proto;
		CreateWidget *cb;
	} dialogs[] = {
		{ "gabble", "jabber", empathy_account_widget_jabber_new},
		{ "butterfly", "msn", empathy_account_widget_msn_new},
		{ "salut", "local-xmpp", empathy_account_widget_salut_new},
		{ "idle", "irc", empathy_account_widget_irc_new},
		{ "haze", "icq", empathy_account_widget_icq_new},
		{ "haze", "aim", empathy_account_widget_aim_new},
		{ "haze", "yahoo", empathy_account_widget_yahoo_new},
		{ "haze", "groupwise", empathy_account_widget_groupwise_new},
		{ "sofiasip", "sip", empathy_account_widget_sip_new},
		{ NULL, NULL, NULL }
	};
	int i;

	for (i = 0; dialogs[i].cm != NULL; i++) {
		if (!tp_strdiff (cm, dialogs[i].cm)
			&& !tp_strdiff (proto, dialogs[i].proto))
				return dialogs[i].cb (settings);
	}

	return empathy_account_widget_generic_new (settings);
}


static void
account_dialog_create_settings_widget (EmpathyAccountsDialog *dialog,
	EmpathyAccountSettings *settings)
{
	dialog->settings_widget = get_account_setup_widget (settings);

	gtk_container_add (GTK_CONTAINER (dialog->alignment_settings),
			   dialog->settings_widget);
	gtk_widget_show (dialog->settings_widget);


	gtk_image_set_from_icon_name (GTK_IMAGE (dialog->image_type),
				      empathy_account_settings_get_icon_name (settings),
				      GTK_ICON_SIZE_DIALOG);
	gtk_widget_set_tooltip_text (dialog->image_type,
				     empathy_account_settings_get_protocol (settings));

	accounts_dialog_update_name_label (dialog, settings);
}

static void
account_dialog_settings_ready_cb (EmpathyAccountSettings *settings,
	GParamSpec *spec, EmpathyAccountsDialog *dialog)
{
	if (empathy_account_settings_is_ready (settings))
		account_dialog_create_settings_widget (dialog, settings);
}

static void
accounts_dialog_update_settings (EmpathyAccountsDialog *dialog,
				EmpathyAccountSettings       *settings)
{
	if (dialog->settings_ready != NULL) {
			g_signal_handler_disconnect (dialog->settings_ready,
				dialog->settings_ready_id);
			dialog->settings_ready = NULL;
			dialog->settings_ready_id = 0;
	}

	if (!settings) {
		GtkTreeView  *view;
		GtkTreeModel *model;

		view = GTK_TREE_VIEW (dialog->treeview);
		model = gtk_tree_view_get_model (view);

		if (gtk_tree_model_iter_n_children (model, NULL) > 0) {
			/* We have configured accounts, select the first one */
			accounts_dialog_model_select_first (dialog);
			return;
		}
		if (empathy_protocol_chooser_n_protocols (
			EMPATHY_PROTOCOL_CHOOSER (dialog->combobox_protocol)) > 0) {
			/* We have no account configured but we have some
			 * profiles instsalled. The user obviously wants to add
			 * an account. Click on the Add button for him. */
			accounts_dialog_button_add_clicked_cb (dialog->button_add,
							       dialog);
			return;
		}

		/* No account and no profile, warn the user */
		gtk_widget_hide (dialog->vbox_details);
		gtk_widget_hide (dialog->frame_new_account);
		gtk_widget_show (dialog->frame_no_protocol);
		gtk_widget_set_sensitive (dialog->button_add, FALSE);
		gtk_widget_set_sensitive (dialog->button_remove, FALSE);
		return;
	}

	/* We have an account selected, destroy old settings and create a new
	 * one for the account selected */
	gtk_widget_hide (dialog->frame_new_account);
	gtk_widget_hide (dialog->frame_no_protocol);
	gtk_widget_show (dialog->vbox_details);
	gtk_widget_set_sensitive (dialog->button_add, TRUE);
	gtk_widget_set_sensitive (dialog->button_remove, TRUE);

	if (dialog->settings_widget) {
		gtk_widget_destroy (dialog->settings_widget);
		dialog->settings_widget = NULL;
	}

	if (empathy_account_settings_is_ready (settings))
		{
			account_dialog_create_settings_widget (dialog, settings);
		}
	else
		{
			dialog->settings_ready = settings;
			dialog->settings_ready_id =
				g_signal_connect (settings, "notify::ready",
					G_CALLBACK (account_dialog_settings_ready_cb), dialog);
		}

}

static void
accounts_dialog_model_setup (EmpathyAccountsDialog *dialog)
{
	GtkListStore     *store;
	GtkTreeSelection *selection;

	store = gtk_list_store_new (COL_COUNT,
				    G_TYPE_BOOLEAN,        /* enabled */
				    G_TYPE_STRING,         /* name */
				    G_TYPE_UINT,           /* status */
				    EMPATHY_TYPE_ACCOUNT,   /* account */
				    EMPATHY_TYPE_ACCOUNT_SETTINGS); /* settings */

	gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->treeview),
				 GTK_TREE_MODEL (store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->treeview));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

	g_signal_connect (selection, "changed",
			  G_CALLBACK (accounts_dialog_model_selection_changed),
			  dialog);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
					      COL_NAME, GTK_SORT_ASCENDING);

	accounts_dialog_model_add_columns (dialog);

	g_object_unref (store);
}

static void
accounts_dialog_name_edited_cb (GtkCellRendererText   *renderer,
				gchar                 *path,
				gchar                 *new_text,
				EmpathyAccountsDialog *dialog)
{
	EmpathyAccountSettings    *settings;
	GtkTreeModel *model;
	GtkTreePath  *treepath;
	GtkTreeIter   iter;

	if (empathy_account_manager_get_connecting_accounts (dialog->account_manager) > 0) {
		dialog->connecting_id = g_timeout_add (FLASH_TIMEOUT,
						       (GSourceFunc) accounts_dialog_flash_connecting_cb,
						       dialog);
	}

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->treeview));
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
accounts_dialog_enable_toggled_cb (GtkCellRendererToggle *cell_renderer,
				   gchar                 *path,
				   EmpathyAccountsDialog *dialog)
{
	EmpathyAccount    *account;
	GtkTreeModel *model;
	GtkTreePath  *treepath;
	GtkTreeIter   iter;
	gboolean      enabled;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->treeview));
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

static void
accounts_dialog_name_editing_started_cb (GtkCellRenderer       *renderer,
					 GtkCellEditable       *editable,
					 gchar                 *path,
					 EmpathyAccountsDialog *dialog)
{
	if (dialog->connecting_id) {
		g_source_remove (dialog->connecting_id);
	}
	DEBUG ("Editing account name started; stopping flashing");
}

static void
accounts_dialog_model_add_columns (EmpathyAccountsDialog *dialog)
{
	GtkTreeView       *view;
	GtkTreeViewColumn *column;
	GtkCellRenderer   *cell;

	view = GTK_TREE_VIEW (dialog->treeview);
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

static void
accounts_dialog_model_select_first (EmpathyAccountsDialog *dialog)
{
	GtkTreeView      *view;
	GtkTreeModel     *model;
	GtkTreeSelection *selection;
	GtkTreeIter       iter;

	/* select first */
	view = GTK_TREE_VIEW (dialog->treeview);
	model = gtk_tree_view_get_model (view);

	if (gtk_tree_model_get_iter_first (model, &iter)) {
		selection = gtk_tree_view_get_selection (view);
		gtk_tree_selection_select_iter (selection, &iter);
	} else {
		accounts_dialog_update_settings (dialog, NULL);
	}
}

static void
accounts_dialog_model_pixbuf_data_func (GtkTreeViewColumn    *tree_column,
					GtkCellRenderer      *cell,
					GtkTreeModel         *model,
					GtkTreeIter          *iter,
					EmpathyAccountsDialog *dialog)
{
	EmpathyAccountSettings  *settings;
	const gchar        *icon_name;
	GdkPixbuf          *pixbuf;
	TpConnectionStatus  status;

	gtk_tree_model_get (model, iter,
			    COL_STATUS, &status,
			    COL_ACCOUNT_SETTINGS_POINTER, &settings,
			    -1);

	icon_name = empathy_account_settings_get_icon_name (settings);
	pixbuf = empathy_pixbuf_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON);

	if (pixbuf) {
		if (status == TP_CONNECTION_STATUS_DISCONNECTED ||
		    (status == TP_CONNECTION_STATUS_CONNECTING &&
		     !dialog->connecting_show)) {
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
	if (pixbuf) {
		g_object_unref (pixbuf);
	}
}

static gboolean
accounts_dialog_get_settings_iter (EmpathyAccountsDialog *dialog,
				 EmpathyAccountSettings *settings,
				 GtkTreeIter           *iter)
{
	GtkTreeView      *view;
	GtkTreeSelection *selection;
	GtkTreeModel     *model;
	gboolean          ok;

	/* Update the status in the model */
	view = GTK_TREE_VIEW (dialog->treeview);
	selection = gtk_tree_view_get_selection (view);
	model = gtk_tree_view_get_model (view);

	for (ok = gtk_tree_model_get_iter_first (model, iter);
	     ok;
	     ok = gtk_tree_model_iter_next (model, iter)) {
		EmpathyAccountSettings *this_settings;
		gboolean   equal;

		gtk_tree_model_get (model, iter,
				    COL_ACCOUNT_SETTINGS_POINTER, &this_settings,
				    -1);

		equal = (this_settings == settings);
		g_object_unref (this_settings);

		if (equal) {
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
accounts_dialog_get_account_iter (EmpathyAccountsDialog *dialog,
				 EmpathyAccount *account,
				 GtkTreeIter    *iter)
{
	GtkTreeView      *view;
	GtkTreeSelection *selection;
	GtkTreeModel     *model;
	gboolean          ok;

	/* Update the status in the model */
	view = GTK_TREE_VIEW (dialog->treeview);
	selection = gtk_tree_view_get_selection (view);
	model = gtk_tree_view_get_model (view);

	for (ok = gtk_tree_model_get_iter_first (model, iter);
	     ok;
	     ok = gtk_tree_model_iter_next (model, iter)) {
		EmpathyAccount *this_account;
		gboolean   equal;

		gtk_tree_model_get (model, iter,
				    COL_ACCOUNT_POINTER, &this_account,
				    -1);

		equal = (this_account == account);
		g_object_unref (this_account);

		if (equal) {
			return TRUE;
		}
	}

	return FALSE;
}

static EmpathyAccountSettings *
accounts_dialog_model_get_selected_settings (EmpathyAccountsDialog *dialog)
{
	GtkTreeView      *view;
	GtkTreeModel     *model;
	GtkTreeSelection *selection;
	GtkTreeIter       iter;
	EmpathyAccountSettings   *settings;

	view = GTK_TREE_VIEW (dialog->treeview);
	selection = gtk_tree_view_get_selection (view);

	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		return NULL;
	}

	gtk_tree_model_get (model, &iter,
		COL_ACCOUNT_SETTINGS_POINTER, &settings, -1);

	return settings;
}


static EmpathyAccount *
accounts_dialog_model_get_selected_account (EmpathyAccountsDialog *dialog)
{
	GtkTreeView      *view;
	GtkTreeModel     *model;
	GtkTreeSelection *selection;
	GtkTreeIter       iter;
	EmpathyAccount   *account;

	view = GTK_TREE_VIEW (dialog->treeview);
	selection = gtk_tree_view_get_selection (view);

	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		return NULL;
	}

	gtk_tree_model_get (model, &iter, COL_ACCOUNT_POINTER, &account, -1);

	return account;
}

static void
accounts_dialog_model_set_selected (EmpathyAccountsDialog *dialog,
				    EmpathyAccountSettings        *settings)
{
	GtkTreeSelection *selection;
	GtkTreeIter       iter;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->treeview));
	if (accounts_dialog_get_settings_iter (dialog, settings, &iter)) {
		gtk_tree_selection_select_iter (selection, &iter);
	}
}

static gboolean
accounts_dialog_model_remove_selected (EmpathyAccountsDialog *dialog)
{
	GtkTreeView      *view;
	GtkTreeModel     *model;
	GtkTreeSelection *selection;
	GtkTreeIter       iter;

	view = GTK_TREE_VIEW (dialog->treeview);
	selection = gtk_tree_view_get_selection (view);

	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		return FALSE;
	}

	return gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
}

static void
accounts_dialog_model_selection_changed (GtkTreeSelection     *selection,
					 EmpathyAccountsDialog *dialog)
{
	EmpathyAccountSettings *settings;
	GtkTreeModel *model;
	GtkTreeIter   iter;
	gboolean      is_selection;

	is_selection = gtk_tree_selection_get_selected (selection, &model, &iter);

	settings = accounts_dialog_model_get_selected_settings (dialog);
	accounts_dialog_update_settings (dialog, settings);

	if (settings) {
		g_object_unref (settings);
	}
}

static void
accounts_dialog_add (EmpathyAccountsDialog *dialog,
				       EmpathyAccountSettings        *settings)
{
	GtkTreeModel       *model;
	GtkTreeIter         iter;
	const gchar        *name;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->treeview));
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
accounts_dialog_add_account (EmpathyAccountsDialog *dialog,
				       EmpathyAccount        *account)
{
	EmpathyAccountSettings *settings;
	GtkTreeModel       *model;
	GtkTreeIter         iter;
	TpConnectionStatus  status;
	const gchar        *name;
	gboolean            enabled;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->treeview));
	g_object_get (account, "status", &status, NULL);
	name = empathy_account_get_display_name (account);
	enabled = empathy_account_is_enabled (account);

	gtk_list_store_append (GTK_LIST_STORE (model), &iter);

	settings = empathy_account_settings_new_for_account (account);

	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    COL_ENABLED, enabled,
			    COL_NAME, name,
			    COL_STATUS, status,
			    COL_ACCOUNT_POINTER, account,
			    COL_ACCOUNT_SETTINGS_POINTER, settings,
			    -1);

	accounts_dialog_connection_changed_cb (dialog->account_manager,
					       account,
					       TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED,
					       status,
					       TP_CONNECTION_STATUS_DISCONNECTED,
					       dialog);

	g_object_unref (settings);
}

static void
accounts_dialog_update (EmpathyAccountsDialog *dialog,
				       EmpathyAccountSettings        *settings)
{
	GtkTreeModel       *model;
	GtkTreeIter         iter;
	TpConnectionStatus  status = TP_CONNECTION_STATUS_DISCONNECTED;
	const gchar        *name;
	gboolean            enabled = FALSE;
	EmpathyAccount     *account;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->treeview));
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
				    EmpathyAccount       *account,
				    EmpathyAccountsDialog *dialog)
{
	GtkTreeIter iter;

	if (accounts_dialog_get_account_iter (dialog, account, &iter))
		gtk_list_store_remove (GTK_LIST_STORE (
			gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->treeview))), &iter);
}

static gboolean
accounts_dialog_row_changed_foreach (GtkTreeModel *model,
				     GtkTreePath  *path,
				     GtkTreeIter  *iter,
				     gpointer      user_data)
{
	gtk_tree_model_row_changed (model, path, iter);

	return FALSE;
}

static gboolean
accounts_dialog_flash_connecting_cb (EmpathyAccountsDialog *dialog)
{
	GtkTreeView  *view;
	GtkTreeModel *model;

	dialog->connecting_show = !dialog->connecting_show;

	view = GTK_TREE_VIEW (dialog->treeview);
	model = gtk_tree_view_get_model (view);

	gtk_tree_model_foreach (model, accounts_dialog_row_changed_foreach, NULL);

	return TRUE;
}

static void
accounts_dialog_connection_changed_cb     (EmpathyAccountManager    *manager,
					   EmpathyAccount           *account,
					   TpConnectionStatusReason  reason,
					   TpConnectionStatus        current,
					   TpConnectionStatus        previous,
					   EmpathyAccountsDialog    *dialog)
{
	GtkTreeModel *model;
	GtkTreeIter   iter;
	gboolean      found;

	/* Update the status in the model */
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->treeview));

	if (accounts_dialog_get_account_iter (dialog, account, &iter)) {
		GtkTreePath *path;

		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    COL_STATUS, current,
				    -1);

		path = gtk_tree_model_get_path (model, &iter);
		gtk_tree_model_row_changed (model, path, &iter);
		gtk_tree_path_free (path);
	}

	found = (empathy_account_manager_get_connecting_accounts (manager) > 0);

	if (!found && dialog->connecting_id) {
		g_source_remove (dialog->connecting_id);
		dialog->connecting_id = 0;
	}

	if (found && !dialog->connecting_id) {
		dialog->connecting_id = g_timeout_add (FLASH_TIMEOUT,
						       (GSourceFunc) accounts_dialog_flash_connecting_cb,
						       dialog);
	}
}

static void
enable_or_disable_account (EmpathyAccountsDialog *dialog,
			   EmpathyAccount *account,
			   gboolean enabled)
{
	GtkTreeModel *model;
	GtkTreeIter   iter;

	/* Update the status in the model */
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->treeview));

	DEBUG ("Account %s is now %s",
		empathy_account_get_display_name (account),
		enabled ? "enabled" : "disabled");

	if (accounts_dialog_get_account_iter (dialog, account, &iter)) {
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    COL_ENABLED, enabled,
				    -1);
	}
}

static void
accounts_dialog_account_disabled_cb (EmpathyAccountManager *manager,
				     EmpathyAccount        *account,
				     EmpathyAccountsDialog *dialog)
{
	enable_or_disable_account (dialog, account, FALSE);
}

static void
accounts_dialog_account_enabled_cb (EmpathyAccountManager *manager,
				    EmpathyAccount        *account,
				    EmpathyAccountsDialog *dialog)
{
	enable_or_disable_account (dialog, account, TRUE);
}

static void
accounts_dialog_account_changed_cb (EmpathyAccountManager *manager,
				    EmpathyAccount        *account,
				    EmpathyAccountsDialog  *dialog)
{
	EmpathyAccountSettings *settings, *selected_settings;
	GtkTreeModel *model;
	GtkTreeIter iter;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->treeview));

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
accounts_dialog_button_create_clicked_cb (GtkWidget             *button,
					  EmpathyAccountsDialog  *dialog)
{
	EmpathyAccountSettings *settings;
	gchar     *str;
	TpConnectionManager *cm;
	TpConnectionManagerProtocol *proto;

	cm = empathy_protocol_chooser_dup_selected (
	    EMPATHY_PROTOCOL_CHOOSER (dialog->combobox_protocol), &proto);

	/* Create account */
	/* To translator: %s is the protocol name */
	str = g_strdup_printf (_("New %s account"), proto->name);

	settings = empathy_account_settings_new (cm->name, proto->name, str);

	g_free (str);

	if (tp_connection_manager_protocol_can_register (proto)) {
		gboolean active;

		active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->radiobutton_register));
		if (active) {
			empathy_account_settings_set_boolean (settings, "register", TRUE);
		}
	}

	accounts_dialog_add (dialog, settings);
	accounts_dialog_model_set_selected (dialog, settings);

	g_object_unref (settings);
	g_object_unref (cm);
}

static void
accounts_dialog_button_back_clicked_cb (GtkWidget             *button,
					EmpathyAccountsDialog  *dialog)
{
	EmpathyAccountSettings *settings;

	settings = accounts_dialog_model_get_selected_settings (dialog);
	accounts_dialog_update (dialog, settings);
}

static void
accounts_dialog_protocol_changed_cb (GtkWidget             *widget,
				    EmpathyAccountsDialog *dialog)
{
	TpConnectionManager *cm;
	TpConnectionManagerProtocol *proto;

	cm = empathy_protocol_chooser_dup_selected (
	    EMPATHY_PROTOCOL_CHOOSER (dialog->combobox_protocol), &proto);

	if (tp_connection_manager_protocol_can_register (proto)) {
		gtk_widget_show (dialog->radiobutton_register);
		gtk_widget_show (dialog->radiobutton_reuse);
	} else {
		gtk_widget_hide (dialog->radiobutton_register);
		gtk_widget_hide (dialog->radiobutton_reuse);
	}
	g_object_unref (cm);
}

static void
accounts_dialog_button_add_clicked_cb (GtkWidget             *button,
				       EmpathyAccountsDialog *dialog)
{
	GtkTreeView      *view;
	GtkTreeSelection *selection;
	GtkTreeModel     *model;

	view = GTK_TREE_VIEW (dialog->treeview);
	model = gtk_tree_view_get_model (view);
	selection = gtk_tree_view_get_selection (view);
	gtk_tree_selection_unselect_all (selection);

	gtk_widget_set_sensitive (dialog->button_add, FALSE);
	gtk_widget_set_sensitive (dialog->button_remove, FALSE);
	gtk_widget_hide (dialog->vbox_details);
	gtk_widget_hide (dialog->frame_no_protocol);
	gtk_widget_show (dialog->frame_new_account);

	/* If we have no account, no need of a back button */
	if (gtk_tree_model_iter_n_children (model, NULL) > 0) {
		gtk_widget_show (dialog->button_back);
	} else {
		gtk_widget_hide (dialog->button_back);
	}

	accounts_dialog_protocol_changed_cb (dialog->radiobutton_register, dialog);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->radiobutton_reuse),
				      TRUE);
	gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->combobox_protocol), 0);
	gtk_widget_grab_focus (dialog->combobox_protocol);
}

static void
accounts_dialog_button_help_clicked_cb (GtkWidget             *button,
					EmpathyAccountsDialog *dialog)
{
	empathy_url_show (button, "ghelp:empathy?empathy-create-account");
}

static void
accounts_dialog_button_remove_clicked_cb (GtkWidget            *button,
					  EmpathyAccountsDialog *dialog)
{
	EmpathyAccount *account;
	GtkWidget *message_dialog;
	gint       res;

	account = accounts_dialog_model_get_selected_account (dialog);

	if (account == NULL || !empathy_account_is_valid (account)) {
		accounts_dialog_model_remove_selected (dialog);
		accounts_dialog_model_select_first (dialog);
		return;
	}
	message_dialog = gtk_message_dialog_new
		(GTK_WINDOW (dialog->window),
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

	if (res == GTK_RESPONSE_YES) {
		empathy_account_manager_remove (dialog->account_manager, account);
		accounts_dialog_model_select_first (dialog);
	}
	gtk_widget_destroy (message_dialog);
}

#if 0
/* FIXME MC-5 */
static void
accounts_dialog_button_import_clicked_cb (GtkWidget             *button,
					  EmpathyAccountsDialog *dialog)
{
	empathy_import_dialog_show (GTK_WINDOW (dialog->window), TRUE);
}
#endif

static void
accounts_dialog_response_cb (GtkWidget            *widget,
			     gint                  response,
			     EmpathyAccountsDialog *dialog)
{
	if (response == GTK_RESPONSE_CLOSE) {
		gtk_widget_destroy (widget);
	}
}

static void
accounts_dialog_destroy_cb (GtkWidget            *widget,
			    EmpathyAccountsDialog *dialog)
{
	GList *accounts, *l;

	/* Disconnect signals */
	g_signal_handlers_disconnect_by_func (dialog->account_manager,
					      accounts_dialog_account_added_cb,
					      dialog);
	g_signal_handlers_disconnect_by_func (dialog->account_manager,
					      accounts_dialog_account_removed_cb,
					      dialog);
	g_signal_handlers_disconnect_by_func (dialog->account_manager,
					      accounts_dialog_account_enabled_cb,
					      dialog);
	g_signal_handlers_disconnect_by_func (dialog->account_manager,
					      accounts_dialog_account_disabled_cb,
					      dialog);
	g_signal_handlers_disconnect_by_func (dialog->account_manager,
					      accounts_dialog_account_changed_cb,
					      dialog);
	g_signal_handlers_disconnect_by_func (dialog->account_manager,
					      accounts_dialog_connection_changed_cb,
					      dialog);

	/* Delete incomplete accounts */
	accounts = empathy_account_manager_dup_accounts (dialog->account_manager);
	for (l = accounts; l; l = l->next) {
		EmpathyAccount *account;

		account = l->data;
		if (!empathy_account_is_valid (account)) {
			/* FIXME: Warn the user the account is not complete
			 *        and is going to be removed. */
			empathy_account_manager_remove (dialog->account_manager, account);
		}

		g_object_unref (account);
	}
	g_list_free (accounts);

	if (dialog->connecting_id) {
		g_source_remove (dialog->connecting_id);
	}

	g_object_unref (dialog->account_manager);

	g_free (dialog);
}

GtkWidget *
empathy_accounts_dialog_show (GtkWindow *parent,
			      EmpathyAccount *selected_account)
{
	static EmpathyAccountsDialog *dialog = NULL;
	GtkBuilder                   *gui;
	gchar                        *filename;
	GList                        *accounts, *l;
	gboolean                      import_asked;

	if (dialog) {
		gtk_window_present (GTK_WINDOW (dialog->window));
		return dialog->window;
	}

	dialog = g_new0 (EmpathyAccountsDialog, 1);

	filename = empathy_file_lookup ("empathy-accounts-dialog.ui",
					"src");
	gui = empathy_builder_get_file (filename,
				       "accounts_dialog", &dialog->window,
				       "vbox_details", &dialog->vbox_details,
				       "frame_no_protocol", &dialog->frame_no_protocol,
				       "alignment_settings", &dialog->alignment_settings,
				       "treeview", &dialog->treeview,
				       "frame_new_account", &dialog->frame_new_account,
				       "hbox_type", &dialog->hbox_type,
				       "button_create", &dialog->button_create,
				       "button_back", &dialog->button_back,
				       "radiobutton_reuse", &dialog->radiobutton_reuse,
				       "radiobutton_register", &dialog->radiobutton_register,
				       "image_type", &dialog->image_type,
				       "label_name", &dialog->label_name,
				       "button_add", &dialog->button_add,
				       "button_remove", &dialog->button_remove,
				       "button_import", &dialog->button_import,
				       NULL);
	g_free (filename);

	empathy_builder_connect (gui, dialog,
			      "accounts_dialog", "destroy", accounts_dialog_destroy_cb,
			      "accounts_dialog", "response", accounts_dialog_response_cb,
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

	g_object_add_weak_pointer (G_OBJECT (dialog->window), (gpointer) &dialog);

	g_object_unref (gui);

	/* Create protocol chooser */
	dialog->combobox_protocol = empathy_protocol_chooser_new ();
	gtk_box_pack_end (GTK_BOX (dialog->hbox_type),
			  dialog->combobox_protocol,
			  TRUE, TRUE, 0);
	gtk_widget_show (dialog->combobox_protocol);
	g_signal_connect (dialog->combobox_protocol, "changed",
			  G_CALLBACK (accounts_dialog_protocol_changed_cb),
			  dialog);

	/* Set up signalling */
	dialog->account_manager = empathy_account_manager_dup_singleton ();

	g_signal_connect (dialog->account_manager, "account-created",
			  G_CALLBACK (accounts_dialog_account_added_cb),
			  dialog);
	g_signal_connect (dialog->account_manager, "account-deleted",
			  G_CALLBACK (accounts_dialog_account_removed_cb),
			  dialog);
	g_signal_connect (dialog->account_manager, "account-enabled",
			  G_CALLBACK (accounts_dialog_account_enabled_cb),
			  dialog);
	g_signal_connect (dialog->account_manager, "account-disabled",
			  G_CALLBACK (accounts_dialog_account_disabled_cb),
			  dialog);
	g_signal_connect (dialog->account_manager, "account-changed",
			  G_CALLBACK (accounts_dialog_account_changed_cb),
			  dialog);
	g_signal_connect (dialog->account_manager, "account-connection-changed",
			  G_CALLBACK (accounts_dialog_connection_changed_cb),
			  dialog);

	accounts_dialog_model_setup (dialog);

	/* Add existing accounts */
	accounts = empathy_account_manager_dup_accounts (dialog->account_manager);
	for (l = accounts; l; l = l->next) {
		accounts_dialog_add_account (dialog, l->data);
		g_object_unref (l->data);
	}
	g_list_free (accounts);

	if (selected_account) {
		GtkTreeSelection *selection;
		GtkTreeIter       iter;

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->treeview));
		if (accounts_dialog_get_account_iter (dialog, selected_account, &iter)) {
			gtk_tree_selection_select_iter (selection, &iter);
		}
	} else {
		accounts_dialog_model_select_first (dialog);
	}

	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog->window),
					      GTK_WINDOW (parent));
	}

	gtk_widget_show (dialog->window);

	empathy_conf_get_bool (empathy_conf_get (),
			       EMPATHY_PREFS_IMPORT_ASKED, &import_asked);


#if 0
/* FIXME MC-5 */
	if (empathy_import_dialog_accounts_to_import ()) {

		if (!import_asked) {
			empathy_conf_set_bool (empathy_conf_get (),
					       EMPATHY_PREFS_IMPORT_ASKED, TRUE);
			empathy_import_dialog_show (GTK_WINDOW (dialog->window),
						    FALSE);
		}
	} else {
		gtk_widget_set_sensitive (dialog->button_import, FALSE);
	}
#endif

	return dialog->window;
}

