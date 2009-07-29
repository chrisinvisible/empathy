/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006-2007 Imendio AB
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Martyn Russell <martyn@imendio.com>
 */

#include <config.h>

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-account.h>

#include <telepathy-glib/connection-manager.h>
#include <dbus/dbus-protocol.h>

#include "empathy-account-widget.h"
#include "empathy-ui-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

static gboolean
account_widget_entry_focus_cb (GtkWidget     *widget,
			       GdkEventFocus *event,
			       EmpathyAccountSettings *settings)
{
	const gchar *str;
	const gchar *param_name;

	str = gtk_entry_get_text (GTK_ENTRY (widget));
	param_name = g_object_get_data (G_OBJECT (widget), "param_name");

	if (EMP_STR_EMPTY (str)) {
		const gchar *value = NULL;

		empathy_account_settings_unset (settings, param_name);
		value = empathy_account_settings_get_string (settings, param_name);
		DEBUG ("Unset %s and restore to %s", param_name, value);
		gtk_entry_set_text (GTK_ENTRY (widget), value ? value : "");
	} else {
		DEBUG ("Setting %s to %s", param_name,
			strstr (param_name, "password") ? "***" : str);
		empathy_account_settings_set_string (settings, param_name, str);
	}

	return FALSE;
}

static void
account_widget_int_changed_cb (GtkWidget *widget,
			       EmpathyAccountSettings *settings)
{
	const gchar *param_name;
	gint         value;
	const gchar *signature;

	value = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (widget));
	param_name = g_object_get_data (G_OBJECT (widget), "param_name");

	signature = empathy_settings_get_dbus_signature (settings, param_name);
	g_return_if_fail (signature != NULL);

	DEBUG ("Setting %s to %d", param_name, value);

	switch ((int)*signature)
		{
			case DBUS_TYPE_INT16:
			case DBUS_TYPE_INT32:
				empathy_account_settings_set_int32 (settings, param_name, value);
				break;
			case DBUS_TYPE_INT64:
				empathy_account_settings_set_int64 (settings, param_name, value);
				break;
			case DBUS_TYPE_UINT16:
			case DBUS_TYPE_UINT32:
				empathy_account_settings_set_uint32 (settings, param_name, value);
				break;
			case DBUS_TYPE_UINT64:
				empathy_account_settings_set_uint64 (settings, param_name, value);
				break;
			default:
				g_return_if_reached ();
		}
}

static void
account_widget_checkbutton_toggled_cb (GtkWidget *widget,
				       EmpathyAccountSettings *settings)
{
	gboolean     value;
	gboolean     default_value;
	const gchar *param_name;

	value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	param_name = g_object_get_data (G_OBJECT (widget), "param_name");

	/* FIXME: This is ugly! checkbox don't have a "not-set" value so we
	 * always unset the param and set the value if different from the
	 * default value. */
	empathy_account_settings_unset (settings, param_name);
	default_value = empathy_account_settings_get_boolean (settings, param_name);

	if (default_value == value) {
		DEBUG ("Unset %s and restore to %d", param_name, default_value);
	} else {
		DEBUG ("Setting %s to %d", param_name, value);
		empathy_account_settings_set_boolean (settings, param_name, value);
	}
}

static void
account_widget_forget_clicked_cb (GtkWidget *button,
				  GtkWidget *entry)
{
	EmpathyAccountSettings *settings;
	const gchar *param_name;

	param_name = g_object_get_data (G_OBJECT (entry), "param_name");
	settings = g_object_get_data (G_OBJECT (entry), "settings");

	DEBUG ("Unset %s", param_name);
	empathy_account_settings_unset (settings, param_name);
	gtk_entry_set_text (GTK_ENTRY (entry), "");
}

static void
account_widget_password_changed_cb (GtkWidget *entry,
				    GtkWidget *button)
{
	const gchar *str;

	str = gtk_entry_get_text (GTK_ENTRY (entry));
	gtk_widget_set_sensitive (button, !EMP_STR_EMPTY (str));
}

static void
account_widget_jabber_ssl_toggled_cb (GtkWidget *checkbutton_ssl,
				      GtkWidget *spinbutton_port)
{
	EmpathyAccountSettings *settings;
	gboolean   value;
	gint32       port = 0;

	value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton_ssl));
	settings = g_object_get_data (G_OBJECT (spinbutton_port), "settings");
	port = empathy_account_settings_get_uint32 (settings, "port");

	if (value) {
		if (port == 5222 || port == 0) {
			port = 5223;
		}
	} else {
		if (port == 5223 || port == 0) {
			port = 5222;
		}
	}

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinbutton_port), port);
}

static void
account_widget_setup_widget (GtkWidget   *widget,
			     EmpathyAccountSettings *settings,
			     const gchar *param_name)
{
	g_object_set_data_full (G_OBJECT (widget), "param_name",
				g_strdup (param_name), g_free);
	g_object_set_data_full (G_OBJECT (widget), "settings",
				g_object_ref (settings), g_object_unref);

	if (GTK_IS_SPIN_BUTTON (widget)) {
		gint value = 0;
		const gchar *signature;

		signature = empathy_settings_get_dbus_signature (settings, param_name);
		g_return_if_fail (signature != NULL);

		switch ((int)*signature)
			{
				case DBUS_TYPE_INT16:
				case DBUS_TYPE_INT32:
					value = empathy_account_settings_get_int32 (settings, param_name);
					break;
				case DBUS_TYPE_INT64:
					value = empathy_account_settings_get_int64 (settings, param_name);
					break;
				case DBUS_TYPE_UINT16:
				case DBUS_TYPE_UINT32:
					value = empathy_account_settings_get_uint32 (settings, param_name);
					break;
				case DBUS_TYPE_UINT64:
					value = empathy_account_settings_get_uint64 (settings, param_name);
					break;
				default:
					g_return_if_reached ();
			}

		gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), value);

		g_signal_connect (widget, "value-changed",
				  G_CALLBACK (account_widget_int_changed_cb),
				  settings);
	}
	else if (GTK_IS_ENTRY (widget)) {
		const gchar *str = NULL;

		str = empathy_account_settings_get_string (settings, param_name);
		gtk_entry_set_text (GTK_ENTRY (widget), str ? str : "");

		if (strstr (param_name, "password")) {
			gtk_entry_set_visibility (GTK_ENTRY (widget), FALSE);
		}

		g_signal_connect (widget, "focus-out-event",
				  G_CALLBACK (account_widget_entry_focus_cb),
				  settings);
	}
	else if (GTK_IS_TOGGLE_BUTTON (widget)) {
		gboolean value = FALSE;

		value = empathy_account_settings_get_boolean (settings, param_name);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), value);

		g_signal_connect (widget, "toggled",
				  G_CALLBACK (account_widget_checkbutton_toggled_cb),
				  settings);
	} else {
		DEBUG ("Unknown type of widget for param %s", param_name);
	}
}

static gchar *
account_widget_generic_format_param_name (const gchar *param_name)
{
	gchar *str;
	gchar *p;

	str = g_strdup (param_name);

	if (str && g_ascii_isalpha (str[0])) {
		str[0] = g_ascii_toupper (str[0]);
	}

	while ((p = strchr (str, '-')) != NULL) {
		if (p[1] != '\0' && g_ascii_isalpha (p[1])) {
			p[0] = ' ';
			p[1] = g_ascii_toupper (p[1]);
		}

		p++;
	}

	return str;
}

static void
accounts_widget_generic_setup (EmpathyAccountSettings *settings,
			       GtkWidget *table_common_settings,
			       GtkWidget *table_advanced_settings)
{
	TpConnectionManagerParam *params, *param;

	params = empathy_account_settings_get_tp_params (settings);

	for (param = params; param != NULL && param->name != NULL; param++) {
		GtkWidget       *table_settings;
		guint            n_rows = 0;
		GtkWidget       *widget = NULL;
		gchar           *param_name_formatted;

		if (param->flags & TP_CONN_MGR_PARAM_FLAG_REQUIRED) {
			table_settings = table_common_settings;
		} else {
			table_settings = table_advanced_settings;
		}
		param_name_formatted = account_widget_generic_format_param_name (param->name);
		g_object_get (table_settings, "n-rows", &n_rows, NULL);
		gtk_table_resize (GTK_TABLE (table_settings), ++n_rows, 2);

		if (param->dbus_signature[0] == 's') {
			gchar *str;

			str = g_strdup_printf (_("%s:"), param_name_formatted);
			widget = gtk_label_new (str);
			gtk_misc_set_alignment (GTK_MISC (widget), 0, 0.5);
			g_free (str);

			gtk_table_attach (GTK_TABLE (table_settings),
					  widget,
					  0, 1,
					  n_rows - 1, n_rows,
					  GTK_FILL, 0,
					  0, 0);
			gtk_widget_show (widget);

			widget = gtk_entry_new ();
			if (strcmp (param->name, "account") == 0) {
				g_signal_connect (widget, "realize",
					G_CALLBACK (gtk_widget_grab_focus),
					NULL);
			}
			gtk_table_attach (GTK_TABLE (table_settings),
					  widget,
					  1, 2,
					  n_rows - 1, n_rows,
					  GTK_FILL | GTK_EXPAND, 0,
					  0, 0);
			gtk_widget_show (widget);
		}
		/* int types: ynqiuxt. double type is 'd' */
		else if (param->dbus_signature[0] == 'y' ||
			 param->dbus_signature[0] == 'n' ||
			 param->dbus_signature[0] == 'q' ||
			 param->dbus_signature[0] == 'i' ||
			 param->dbus_signature[0] == 'u' ||
			 param->dbus_signature[0] == 'x' ||
			 param->dbus_signature[0] == 't' ||
			 param->dbus_signature[0] == 'd') {
			gchar   *str = NULL;
			gdouble  minint = 0;
			gdouble  maxint = 0;
			gdouble  step = 1;

			switch (param->dbus_signature[0]) {
			case 'y': minint = G_MININT8;  maxint = G_MAXINT8;   break;
			case 'n': minint = G_MININT16; maxint = G_MAXINT16;  break;
			case 'q': minint = 0;          maxint = G_MAXUINT16; break;
			case 'i': minint = G_MININT32; maxint = G_MAXINT32;  break;
			case 'u': minint = 0;          maxint = G_MAXUINT32; break;
			case 'x': minint = G_MININT64; maxint = G_MAXINT64;  break;
			case 't': minint = 0;          maxint = G_MAXUINT64; break;
			case 'd': minint = G_MININT32; maxint = G_MAXINT32; step = 0.1; break;
			}

			str = g_strdup_printf (_("%s:"), param_name_formatted);
			widget = gtk_label_new (str);
			gtk_misc_set_alignment (GTK_MISC (widget), 0, 0.5);
			g_free (str);

			gtk_table_attach (GTK_TABLE (table_settings),
					  widget,
					  0, 1,
					  n_rows - 1, n_rows,
					  GTK_FILL, 0,
					  0, 0);
			gtk_widget_show (widget);

			widget = gtk_spin_button_new_with_range (minint, maxint, step);
			gtk_table_attach (GTK_TABLE (table_settings),
					  widget,
					  1, 2,
					  n_rows - 1, n_rows,
					  GTK_FILL | GTK_EXPAND, 0,
					  0, 0);
			gtk_widget_show (widget);
		}
		else if (param->dbus_signature[0] == 'b') {
			widget = gtk_check_button_new_with_label (param_name_formatted);
			gtk_table_attach (GTK_TABLE (table_settings),
					  widget,
					  0, 2,
					  n_rows - 1, n_rows,
					  GTK_FILL | GTK_EXPAND, 0,
					  0, 0);
			gtk_widget_show (widget);
		} else {
			DEBUG ("Unknown signature for param %s: %s",
				param_name_formatted, param->dbus_signature);
		}

		if (widget) {
			account_widget_setup_widget (widget, settings, param->name);
		}

		g_free (param_name_formatted);
	}
}

static void
account_widget_handle_params_valist (EmpathyAccountSettings   *settings,
				     GtkBuilder  *gui,
				     const gchar *first_widget,
				     va_list      args)
{
	GObject *object;
	const gchar *name;

	for (name = first_widget; name; name = va_arg (args, const gchar *)) {
		const gchar *param_name;

		param_name = va_arg (args, const gchar *);
		object = gtk_builder_get_object (gui, name);

		if (!object) {
			g_warning ("Builder is missing object '%s'.", name);
			continue;
		}

		account_widget_setup_widget (GTK_WIDGET (object), settings, param_name);
	}
}

void
empathy_account_widget_handle_params (EmpathyAccountSettings   *settings,
				      GtkBuilder  *gui,
				      const gchar *first_widget,
				      ...)
{
	va_list args;

	g_return_if_fail (GTK_IS_BUILDER (gui));

	va_start (args, first_widget);
	account_widget_handle_params_valist (settings, gui, first_widget, args);
	va_end (args);
}

static void
account_widget_apply_clicked_cb (GtkWidget *button,
				 EmpathyAccountSettings *settings)
{
  empathy_account_settings_apply_async (settings, NULL, NULL);
}

void
empathy_account_widget_add_apply_button (EmpathyAccountSettings *settings,
					 GtkWidget *vbox)
{
	GtkWidget *button;

	button = gtk_button_new_from_stock (GTK_STOCK_APPLY);

	gtk_box_pack_end (GTK_BOX (vbox), button, FALSE, FALSE, 3);

	g_signal_connect (button, "clicked",
			  G_CALLBACK (account_widget_apply_clicked_cb),
			  settings);
	gtk_widget_show (button);
}

void
empathy_account_widget_add_forget_button (EmpathyAccountSettings   *settings,
					  GtkBuilder  *gui,
					  const gchar *button,
					  const gchar *entry)
{
	GtkWidget *button_forget;
	GtkWidget *entry_password;
	const gchar   *password = NULL;

	button_forget = GTK_WIDGET (gtk_builder_get_object (gui, button));
	entry_password = GTK_WIDGET (gtk_builder_get_object (gui, entry));

	password = empathy_account_settings_get_string (settings, "password");
	gtk_widget_set_sensitive (button_forget, !EMP_STR_EMPTY (password));

	g_signal_connect (button_forget, "clicked",
			  G_CALLBACK (account_widget_forget_clicked_cb),
			  entry_password);
	g_signal_connect (entry_password, "changed",
			  G_CALLBACK (account_widget_password_changed_cb),
			  button_forget);
}

void
empathy_account_widget_set_default_focus (GtkBuilder  *gui,
					  const gchar *entry)
{
	GObject *default_focus_entry;

	default_focus_entry = gtk_builder_get_object (gui, entry);
	g_signal_connect (default_focus_entry, "realize",
			  G_CALLBACK (gtk_widget_grab_focus),
			  NULL);
}

static void
account_widget_setup_generic (EmpathyAccountSettings *settings,
  GtkBuilder *builder)
{
	GtkWidget *table_common_settings;
	GtkWidget *table_advanced_settings;

	table_common_settings = GTK_WIDGET (gtk_builder_get_object (builder,
		"table_common_settings"));
	table_advanced_settings = GTK_WIDGET (gtk_builder_get_object (builder,
		"table_advanced_settings"));

	accounts_widget_generic_setup (settings, table_common_settings,
		table_advanced_settings);

	g_object_unref (builder);
}

static void
account_widget_settings_ready_cb (EmpathyAccountSettings *settings,
  GParamSpec *pspec, gpointer user_data)
{
	GtkBuilder *builder = GTK_BUILDER (user_data);

	if (empathy_account_settings_is_ready (settings))
		account_widget_setup_generic (settings, builder);
}

GtkWidget *
empathy_account_widget_generic_new (EmpathyAccountSettings *settings)
{
	GtkBuilder *gui;
	GtkWidget *widget;
	gchar     *filename;

	filename = empathy_file_lookup ("empathy-account-widget-generic.ui",
					"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
					"vbox_generic_settings", &widget,
					NULL);

	if (empathy_account_settings_is_ready (settings))
		account_widget_setup_generic (settings, gui);
	else
		g_signal_connect (settings, "notify::ready",
			G_CALLBACK (account_widget_settings_ready_cb), gui);

	empathy_account_widget_add_apply_button (settings, widget);

	g_free (filename);

	g_object_ref (widget);
	g_object_force_floating (G_OBJECT (widget));
	return widget;
}

GtkWidget *
empathy_account_widget_salut_new (EmpathyAccountSettings *settings)
{
	GtkBuilder *gui;
	GtkWidget *widget;
	gchar     *filename;

	filename = empathy_file_lookup ("empathy-account-widget-salut.ui",
					"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
					"vbox_salut_settings", &widget,
					NULL);
	g_free (filename);

	empathy_account_widget_handle_params (settings, gui,
			"entry_published", "published-name",
			"entry_nickname", "nickname",
			"entry_first_name", "first-name",
			"entry_last_name", "last-name",
			"entry_email", "email",
			"entry_jid", "jid",
			NULL);

	empathy_account_widget_set_default_focus (gui, "entry_nickname");
	empathy_account_widget_add_apply_button (settings, widget);

	return empathy_builder_unref_and_keep_widget (gui, widget);
}

GtkWidget *
empathy_account_widget_msn_new (EmpathyAccountSettings *settings)
{
	GtkBuilder *gui;
	GtkWidget *widget;
	gchar     *filename;

	filename = empathy_file_lookup ("empathy-account-widget-msn.ui",
					"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
					"vbox_msn_settings", &widget,
					NULL);
	g_free (filename);

	empathy_account_widget_handle_params (settings, gui,
			"entry_id", "account",
			"entry_password", "password",
			"entry_server", "server",
			"spinbutton_port", "port",
			NULL);

	empathy_account_widget_add_forget_button (settings, gui,
						  "button_forget",
						  "entry_password");

	empathy_account_widget_set_default_focus (gui, "entry_id");

	return empathy_builder_unref_and_keep_widget (gui, widget);
}

GtkWidget *
empathy_account_widget_jabber_new (EmpathyAccountSettings *settings)
{
	GtkBuilder *gui;
	GtkWidget *widget;
	GtkWidget *spinbutton_port;
	GtkWidget *checkbutton_ssl;
	gchar     *filename;

	filename = empathy_file_lookup ("empathy-account-widget-jabber.ui",
					"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
				        "vbox_jabber_settings", &widget,
				        "spinbutton_port", &spinbutton_port,
				        "checkbutton_ssl", &checkbutton_ssl,
				        NULL);
	g_free (filename);

	empathy_account_widget_handle_params (settings, gui,
			"entry_id", "account",
			"entry_password", "password",
			"entry_resource", "resource",
			"entry_server", "server",
			"spinbutton_port", "port",
			"spinbutton_priority", "priority",
			"checkbutton_ssl", "old-ssl",
			"checkbutton_ignore_ssl_errors", "ignore-ssl-errors",
			"checkbutton_encryption", "require-encryption",
			NULL);

	empathy_account_widget_add_forget_button (settings, gui,
						  "button_forget",
						  "entry_password");

	empathy_account_widget_set_default_focus (gui, "entry_id");

	g_signal_connect (checkbutton_ssl, "toggled",
			  G_CALLBACK (account_widget_jabber_ssl_toggled_cb),
			  spinbutton_port);

	empathy_account_widget_add_apply_button (settings, widget);

	return empathy_builder_unref_and_keep_widget (gui, widget);
}

GtkWidget *
empathy_account_widget_icq_new (EmpathyAccountSettings *settings)
{
	GtkBuilder *gui;
	GtkWidget *widget;
	GtkWidget *spinbutton_port;
	gchar     *filename;

	filename = empathy_file_lookup ("empathy-account-widget-icq.ui",
					"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
				        "vbox_icq_settings", &widget,
				        "spinbutton_port", &spinbutton_port,
				        NULL);
	g_free (filename);

	empathy_account_widget_handle_params (settings, gui,
			"entry_uin", "account",
			"entry_password", "password",
			"entry_server", "server",
			"spinbutton_port", "port",
			"entry_charset", "charset",
			NULL);

	empathy_account_widget_add_forget_button (settings, gui,
						  "button_forget",
						  "entry_password");

	empathy_account_widget_set_default_focus (gui, "entry_uin");

	empathy_account_widget_add_apply_button (settings, widget);

	return empathy_builder_unref_and_keep_widget (gui, widget);
}

GtkWidget *
empathy_account_widget_aim_new (EmpathyAccountSettings *settings)
{
	GtkBuilder *gui;
	GtkWidget *widget;
	GtkWidget *spinbutton_port;
	gchar     *filename;

	filename = empathy_file_lookup ("empathy-account-widget-aim.ui",
					"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
				        "vbox_aim_settings", &widget,
				        "spinbutton_port", &spinbutton_port,
				        NULL);
	g_free (filename);

	empathy_account_widget_handle_params (settings, gui,
			"entry_screenname", "account",
			"entry_password", "password",
			"entry_server", "server",
			"spinbutton_port", "port",
			NULL);

	empathy_account_widget_add_forget_button (settings, gui,
						  "button_forget",
						  "entry_password");

	empathy_account_widget_set_default_focus (gui, "entry_screenname");
	empathy_account_widget_add_apply_button (settings, widget);

	return empathy_builder_unref_and_keep_widget (gui, widget);
}

GtkWidget *
empathy_account_widget_yahoo_new (EmpathyAccountSettings *settings)
{
	GtkBuilder *gui;
	GtkWidget *widget;
	gchar     *filename;

	filename = empathy_file_lookup ("empathy-account-widget-yahoo.ui",
					"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
					"vbox_yahoo_settings", &widget,
					NULL);
	g_free (filename);

	empathy_account_widget_handle_params (settings, gui,
			"entry_id", "account",
			"entry_password", "password",
			"entry_server", "server",
			"entry_locale", "room-list-locale",
			"entry_charset", "charset",
			"spinbutton_port", "port",
			"checkbutton_yahoojp", "yahoojp",
			"checkbutton_ignore_invites", "ignore-invites",
			NULL);

	empathy_account_widget_add_forget_button (settings, gui,
						  "button_forget",
						  "entry_password");

	empathy_account_widget_set_default_focus (gui, "entry_id");
	empathy_account_widget_add_apply_button (settings, widget);

	return empathy_builder_unref_and_keep_widget (gui, widget);
}

GtkWidget *
empathy_account_widget_groupwise_new (EmpathyAccountSettings *settings)
{
	GtkBuilder *gui;
	GtkWidget *widget;
	gchar     *filename;

	filename = empathy_file_lookup ("empathy-account-widget-groupwise.ui",
					"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
					"vbox_groupwise_settings", &widget,
					NULL);
	g_free (filename);

	empathy_account_widget_handle_params (settings, gui,
			"entry_id", "account",
			"entry_password", "password",
			"entry_server", "server",
			"spinbutton_port", "port",
			NULL);

	empathy_account_widget_add_forget_button (settings, gui,
						  "button_forget",
						  "entry_password");

	empathy_account_widget_set_default_focus (gui, "entry_id");
	empathy_account_widget_add_apply_button (settings, widget);

	return empathy_builder_unref_and_keep_widget (gui, widget);
}

