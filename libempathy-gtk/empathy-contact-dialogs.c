/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007 Collabora Ltd.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <glib/gi18n.h>

#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-contact-list.h>

#include "empathy-contact-dialogs.h"
#include "empathy-contact-widget.h"
#include "gossip-ui-utils.h"

static GHashTable *subscription_dialogs = NULL;
static GHashTable *information_dialogs = NULL;

/*
 *  Subscription dialog
 */

static void
subscription_dialog_response_cb (GtkDialog *dialog,
				 gint       response,
				 GtkWidget *contact_widget)
{
	EmpathyContactManager *manager;
	GossipContact         *contact;

	manager = empathy_contact_manager_new ();
	contact = empathy_contact_widget_get_contact (contact_widget);
	empathy_contact_widget_save (contact_widget);

	if (response == GTK_RESPONSE_YES) {
		empathy_contact_list_add (EMPATHY_CONTACT_LIST (manager),
					  contact,
					  _("I would like to add you to my contact list."));
	}
	else if (response == GTK_RESPONSE_NO) {
		empathy_contact_list_remove (EMPATHY_CONTACT_LIST (manager),
					     contact,
					     _("Sorry, I don't want you in my contact list."));
	}

	g_hash_table_remove (subscription_dialogs, contact);
	g_object_unref (manager);
}

void
empathy_subscription_dialog_show (GossipContact *contact,
				  GtkWindow     *parent)
{
	GtkWidget *dialog;
	GtkWidget *hbox_subscription;
	GtkWidget *contact_widget;

	g_return_if_fail (GOSSIP_IS_CONTACT (contact));

	if (!subscription_dialogs) {
		subscription_dialogs = g_hash_table_new_full (gossip_contact_hash,
							      gossip_contact_equal,
							      (GDestroyNotify) g_object_unref,
							      (GDestroyNotify) gtk_widget_destroy);
	}

	dialog = g_hash_table_lookup (subscription_dialogs, contact);
	if (dialog) {
		gtk_window_present (GTK_WINDOW (dialog));
		return;
	}

	gossip_glade_get_file_simple ("empathy-contact-dialogs.glade",
				      "subscription_request_dialog",
				      NULL,
				      "subscription_request_dialog", &dialog,
				      "hbox_subscription", &hbox_subscription,
				      NULL);

	g_hash_table_insert (subscription_dialogs, g_object_ref (contact), dialog);

	contact_widget = empathy_contact_widget_new (contact, TRUE);
	gtk_box_pack_end (GTK_BOX (hbox_subscription),
			  contact_widget,
			  TRUE, TRUE,
			  0);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (subscription_dialog_response_cb),
			  contact_widget);

	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	}

	gtk_widget_show (dialog);
}

/*
 *  Information dialog
 */

static void
contact_information_response_cb (GtkDialog *dialog,
				 gint       response,
				 GtkWidget *contact_widget)
{
	GossipContact *contact;

	contact = empathy_contact_widget_get_contact (contact_widget);

	if (response == GTK_RESPONSE_OK) {
		empathy_contact_widget_save (contact_widget);
	}

	g_hash_table_remove (information_dialogs, contact);
}

void
empathy_contact_information_dialog_show (GossipContact *contact,
					 GtkWindow     *parent,
					 gboolean       edit)
{
	GtkWidget *dialog;
	GtkWidget *button;
	GtkWidget *contact_widget;

	g_return_if_fail (GOSSIP_IS_CONTACT (contact));

	if (!information_dialogs) {
		information_dialogs = g_hash_table_new_full (gossip_contact_hash,
							     gossip_contact_equal,
							     (GDestroyNotify) g_object_unref,
							     (GDestroyNotify) gtk_widget_destroy);
	}

	dialog = g_hash_table_lookup (information_dialogs, contact);
	if (dialog) {
		gtk_window_present (GTK_WINDOW (dialog));
		return;
	}

	dialog = gtk_dialog_new ();
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	if (edit) {
		/* Cancel button */
		button = gtk_button_new_with_label (GTK_STOCK_CANCEL);
		gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
		gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
					      button,
					      GTK_RESPONSE_CANCEL);
		gtk_widget_show (button);

		button = gtk_button_new_with_label (GTK_STOCK_SAVE);
		gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
		gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
					      button,
					      GTK_RESPONSE_OK);
		gtk_widget_show (button);
	} else {
		/* Close button */
		button = gtk_button_new_with_label (GTK_STOCK_CLOSE);
		gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
		gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
					      button,
					      GTK_RESPONSE_CLOSE);
		gtk_widget_show (button);
	}
	
	contact_widget = empathy_contact_widget_new (contact, edit);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    contact_widget,
			    TRUE, TRUE, 0);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (contact_information_response_cb),
			  contact_widget);

	g_hash_table_insert (information_dialogs, g_object_ref (contact), dialog);

	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	}

	gtk_widget_show (dialog);
}

