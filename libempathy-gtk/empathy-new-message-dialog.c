/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#include <libempathy/empathy-call-factory.h>
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
				       GTK_TYPE_DIALOG)

/**
 * SECTION:empathy-new-message-dialog
 * @title: EmpathyNewMessageDialog
 * @short_description: A dialog to show a new message
 * @include: libempathy-gtk/empathy-new-message-dialog.h
 *
 * #EmpathyNewMessageDialog is a dialog which allows a text chat or
 * call to be started with any contact on any enabled account.
 */

typedef struct _EmpathyNewMessageDialogPriv EmpathyNewMessageDialogPriv;

struct _EmpathyNewMessageDialogPriv {
	GtkWidget *dialog;
	GtkWidget *table_contact;
	GtkWidget *account_chooser;
	GtkWidget *entry_id;
	GtkWidget *button_chat;
	GtkWidget *button_call;
	EmpathyContactManager *contact_manager;
};

#define GET_PRIV(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMPATHY_TYPE_NEW_MESSAGE_DIALOG, \
    EmpathyNewMessageDialogPriv))

enum {
	COMPLETION_COL_TEXT,
	COMPLETION_COL_ID,
	COMPLETION_COL_NAME,
} CompletionCol;

static void
new_message_dialog_account_changed_cb (GtkWidget               *widget,
				       EmpathyNewMessageDialog *dialog)
{
	EmpathyNewMessageDialogPriv *priv = GET_PRIV (dialog);
	EmpathyAccountChooser *chooser;
	TpConnection          *connection;
	EmpathyTpContactList *contact_list;
	GList                *members;
	GtkListStore         *store;
	GtkEntryCompletion   *completion;
	GtkTreeIter           iter;
	gchar                *tmpstr;

	/* Remove completions */
	completion = gtk_entry_get_completion (GTK_ENTRY (priv->entry_id));
	store = GTK_LIST_STORE (gtk_entry_completion_get_model (completion));
	gtk_list_store_clear (store);

	/* Get members of the new account */
	chooser = EMPATHY_ACCOUNT_CHOOSER (priv->account_chooser);
	connection = empathy_account_chooser_get_connection (chooser);
	if (!connection) {
		return;
	}
	contact_list = empathy_contact_manager_get_list (priv->contact_manager,
							 connection);
	members = empathy_contact_list_get_members (EMPATHY_CONTACT_LIST (contact_list));

	/* Add members to the completion */
	while (members) {
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
new_message_dialog_match_selected_cb (GtkEntryCompletion *widget,
				      GtkTreeModel       *model,
				      GtkTreeIter        *iter,
				      EmpathyNewMessageDialog *dialog)
{
	EmpathyNewMessageDialogPriv *priv = GET_PRIV (dialog);
	gchar *id;

	if (!iter || !model) {
		return FALSE;
	}

	gtk_tree_model_get (model, iter, COMPLETION_COL_ID, &id, -1);
	gtk_entry_set_text (GTK_ENTRY (priv->entry_id), id);

	DEBUG ("Got selected match **%s**", id);

	g_free (id);

	return TRUE;
}

static gboolean
new_message_dialog_match_func (GtkEntryCompletion *completion,
			       const gchar        *key,
			       GtkTreeIter        *iter,
			       gpointer            user_data)
{
	GtkTreeModel *model;
	gchar        *id;
	gchar        *name;

	model = gtk_entry_completion_get_model (completion);
	if (!model || !iter) {
		return FALSE;
	}

	gtk_tree_model_get (model, iter, COMPLETION_COL_NAME, &name, -1);
	if (strstr (name, key)) {
		DEBUG ("Key %s is matching name **%s**", key, name);
		g_free (name);
		return TRUE;
	}
	g_free (name);

	gtk_tree_model_get (model, iter, COMPLETION_COL_ID, &id, -1);
	if (strstr (id, key)) {
		DEBUG ("Key %s is matching ID **%s**", key, id);
		g_free (id);
		return TRUE;
	}
	g_free (id);

	return FALSE;
}

static void
new_message_dialog_call_got_contact_cb (EmpathyTpContactFactory *factory,
					EmpathyContact          *contact,
					const GError            *error,
					gpointer                 user_data,
					GObject                 *weak_object)
{
	EmpathyCallFactory *call_factory;

	if (error != NULL) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	call_factory = empathy_call_factory_get ();
	empathy_call_factory_new_call (call_factory, contact);
}

static void
new_message_dialog_response_cb (GtkWidget               *widget,
				gint                    response,
				EmpathyNewMessageDialog *dialog)
{
	EmpathyNewMessageDialogPriv *priv = GET_PRIV (dialog);
	TpConnection *connection;
	const gchar *id;

	connection = empathy_account_chooser_get_connection (
		EMPATHY_ACCOUNT_CHOOSER (priv->account_chooser));
	id = gtk_entry_get_text (GTK_ENTRY (priv->entry_id));
	if (!connection || EMP_STR_EMPTY (id)) {
		gtk_widget_destroy (widget);
		return;
	}

	if (response == 1) {
		EmpathyTpContactFactory *factory;

		factory = empathy_tp_contact_factory_dup_singleton (connection);
		empathy_tp_contact_factory_get_from_id (factory, id,
			new_message_dialog_call_got_contact_cb,
			NULL, NULL, NULL);
		g_object_unref (factory);
	} else if (response == 2) {
		empathy_dispatcher_chat_with_contact_id (connection, id, NULL, NULL);
	}

	gtk_widget_destroy (widget);
}

static void
new_message_change_state_button_cb  (GtkEditable             *editable,
				     EmpathyNewMessageDialog *dialog)
{
	EmpathyNewMessageDialogPriv *priv = GET_PRIV (dialog);
	const gchar *id;
	gboolean     sensitive;

	id = gtk_entry_get_text (GTK_ENTRY (editable));
	sensitive = !EMP_STR_EMPTY (id);

	gtk_widget_set_sensitive (priv->button_chat, sensitive);
	gtk_widget_set_sensitive (priv->button_call, sensitive);
}

static GObject *
empathy_new_message_dialog_constructor (GType type,
				        guint n_props,
				        GObjectConstructParam *props)
{
	GObject *retval;

	if (dialog_singleton) {
		retval = G_OBJECT (dialog_singleton);
		g_object_ref (retval);
	}
	else {
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
	EmpathyNewMessageDialogPriv *priv = GET_PRIV (dialog);
	GtkBuilder                     *gui;
	gchar                          *filename;
	GtkEntryCompletion             *completion;
	GtkListStore                   *model;
	GtkWidget                      *content_area;
	GtkWidget                      *image;

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

	/* add buttons */
	gtk_dialog_add_button (GTK_DIALOG (dialog),
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	priv->button_call = gtk_button_new_with_mnemonic (_("C_all"));
	image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VOIP,
		GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image (GTK_BUTTON (priv->button_call), image);

	gtk_dialog_add_action_widget (GTK_DIALOG (dialog), priv->button_call, 1);
	gtk_widget_show (priv->button_call);

	priv->button_chat = gtk_button_new_with_mnemonic (_("C_hat"));
	image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_NEW_MESSAGE,
		GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image (GTK_BUTTON (priv->button_chat), image);

	gtk_dialog_add_action_widget (GTK_DIALOG (dialog), priv->button_chat, 2);
	gtk_widget_show (priv->button_chat);

	/* Tweak the dialog */
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

	gtk_window_set_title (GTK_WINDOW (dialog), _("New Conversation"));
	gtk_window_set_role (GTK_WINDOW (dialog), "new_message");
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_window_set_type_hint (GTK_WINDOW (dialog), GDK_WINDOW_TYPE_HINT_DIALOG);

	/* text completion */
	completion = gtk_entry_completion_new ();
	model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	gtk_entry_completion_set_text_column (completion, COMPLETION_COL_TEXT);
	gtk_entry_completion_set_match_func (completion,
					     new_message_dialog_match_func,
					     NULL, NULL);
	gtk_entry_completion_set_model (completion, GTK_TREE_MODEL (model));
	gtk_entry_set_completion (GTK_ENTRY (priv->entry_id), completion);
	g_signal_connect (completion, "match-selected",
			  G_CALLBACK (new_message_dialog_match_selected_cb),
			  dialog);
	g_object_unref (completion);
	g_object_unref (model);

	g_signal_connect (dialog, "response",
		    G_CALLBACK (new_message_dialog_response_cb), dialog);

	empathy_builder_connect (gui, dialog,
			       "entry_id", "changed", new_message_change_state_button_cb,
			       NULL);

	g_object_unref (gui);

	/* Create account chooser */
	priv->account_chooser = empathy_account_chooser_new ();
	gtk_table_attach_defaults (GTK_TABLE (priv->table_contact),
				   priv->account_chooser,
				   1, 2, 0, 1);
	empathy_account_chooser_set_filter (EMPATHY_ACCOUNT_CHOOSER (priv->account_chooser),
					    empathy_account_chooser_filter_is_connected,
					    NULL);
	gtk_widget_show (priv->account_chooser);

	new_message_dialog_account_changed_cb (priv->account_chooser, dialog);
	g_signal_connect (priv->account_chooser, "changed",
			  G_CALLBACK (new_message_dialog_account_changed_cb),
			  dialog);

	gtk_widget_set_sensitive (priv->button_chat, FALSE);
	gtk_widget_set_sensitive (priv->button_call, FALSE);
}

static void
empathy_new_message_dialog_dispose (GObject *object)
{
	EmpathyNewMessageDialogPriv *priv = GET_PRIV (object);

	if (priv->contact_manager != NULL) {
		g_object_unref (priv->contact_manager);
		priv->contact_manager = NULL;
	}

	if (G_OBJECT_CLASS (empathy_new_message_dialog_parent_class)->dispose)
		G_OBJECT_CLASS (empathy_new_message_dialog_parent_class)->dispose (object);
}

static void
empathy_new_message_dialog_class_init (
  EmpathyNewMessageDialogClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	g_type_class_add_private (class, sizeof (EmpathyNewMessageDialogPriv));

	object_class->constructor = empathy_new_message_dialog_constructor;

	object_class->dispose = empathy_new_message_dialog_dispose;
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

	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog),
					      GTK_WINDOW (parent));
	}

	gtk_widget_show (dialog);
	return dialog;
}
