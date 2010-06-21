/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2003-2007 Imendio AB
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
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 */

#include <config.h>

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>

#include <libempathy/empathy-gsettings.h>
#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-theme-manager.h>
#include <libempathy-gtk/empathy-spell.h>
#include <libempathy-gtk/empathy-contact-list-store.h>
#include <libempathy-gtk/empathy-gtk-enum-types.h>

#ifdef HAVE_WEBKIT
#include <libempathy-gtk/empathy-theme-adium.h>
#endif

#include "empathy-preferences.h"

typedef struct {
	GtkWidget *dialog;

	GtkWidget *notebook;

	GtkWidget *checkbutton_show_smileys;
	GtkWidget *checkbutton_show_contacts_in_rooms;
	GtkWidget *combobox_chat_theme;
	GtkWidget *checkbutton_separate_chat_windows;
	GtkWidget *checkbutton_autoconnect;

	GtkWidget *checkbutton_sounds_enabled;
	GtkWidget *checkbutton_sounds_disabled_away;
	GtkWidget *treeview_sounds;

	GtkWidget *checkbutton_notifications_enabled;
	GtkWidget *checkbutton_notifications_disabled_away;
	GtkWidget *checkbutton_notifications_focus;
	GtkWidget *checkbutton_notifications_contact_signin;
	GtkWidget *checkbutton_notifications_contact_signout;

	GtkWidget *treeview_spell_checker;

	GtkWidget *checkbutton_location_publish;
	GtkWidget *checkbutton_location_reduce_accuracy;
	GtkWidget *checkbutton_location_resource_network;
	GtkWidget *checkbutton_location_resource_cell;
	GtkWidget *checkbutton_location_resource_gps;

	GSettings *gsettings;
	GSettings *gsettings_chat;
	GSettings *gsettings_loc;
	GSettings *gsettings_notify;
	GSettings *gsettings_sound;
	GSettings *gsettings_ui;
} EmpathyPreferences;

static void     preferences_setup_widgets                (EmpathyPreferences      *preferences);
static void     preferences_languages_setup              (EmpathyPreferences      *preferences);
static void     preferences_languages_add                (EmpathyPreferences      *preferences);
static void     preferences_languages_save               (EmpathyPreferences      *preferences);
static gboolean preferences_languages_save_foreach       (GtkTreeModel           *model,
							  GtkTreePath            *path,
							  GtkTreeIter            *iter,
							  gchar                 **languages);
static void     preferences_languages_load               (EmpathyPreferences      *preferences);
static gboolean preferences_languages_load_foreach       (GtkTreeModel           *model,
							  GtkTreePath            *path,
							  GtkTreeIter            *iter,
							  gchar                 **languages);
static void     preferences_languages_cell_toggled_cb    (GtkCellRendererToggle  *cell,
							  gchar                  *path_string,
							  EmpathyPreferences      *preferences);
static void     preferences_destroy_cb                   (GtkWidget              *widget,
							  EmpathyPreferences      *preferences);
static void     preferences_response_cb                  (GtkWidget              *widget,
							  gint                    response,
							  EmpathyPreferences      *preferences);

enum {
	COL_LANG_ENABLED,
	COL_LANG_CODE,
	COL_LANG_NAME,
	COL_LANG_COUNT
};

enum {
	COL_COMBO_IS_ADIUM,
	COL_COMBO_VISIBLE_NAME,
	COL_COMBO_NAME,
	COL_COMBO_PATH,
	COL_COMBO_COUNT
};

enum {
	COL_SOUND_ENABLED,
	COL_SOUND_NAME,
	COL_SOUND_KEY,
	COL_SOUND_COUNT
};

typedef struct {
	const char *name;
	const char *key;
} SoundEventEntry;

/* TODO: add phone related sounds also? */
static SoundEventEntry sound_entries [] = {
	{ N_("Message received"), EMPATHY_PREFS_SOUNDS_INCOMING_MESSAGE },
	{ N_("Message sent"), EMPATHY_PREFS_SOUNDS_OUTGOING_MESSAGE },
	{ N_("New conversation"), EMPATHY_PREFS_SOUNDS_NEW_CONVERSATION },
	{ N_("Contact goes online"), EMPATHY_PREFS_SOUNDS_CONTACT_LOGIN },
	{ N_("Contact goes offline"), EMPATHY_PREFS_SOUNDS_CONTACT_LOGOUT },
	{ N_("Account connected"), EMPATHY_PREFS_SOUNDS_SERVICE_LOGIN },
	{ N_("Account disconnected"), EMPATHY_PREFS_SOUNDS_SERVICE_LOGOUT }
};

static void
preferences_setup_widgets (EmpathyPreferences *preferences)
{
	g_settings_bind (preferences->gsettings_notify,
			 EMPATHY_PREFS_NOTIFICATIONS_ENABLED,
			 preferences->checkbutton_notifications_enabled,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (preferences->gsettings_notify,
			 EMPATHY_PREFS_NOTIFICATIONS_DISABLED_AWAY,
			 preferences->checkbutton_notifications_disabled_away,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (preferences->gsettings_notify,
			 EMPATHY_PREFS_NOTIFICATIONS_FOCUS,
			 preferences->checkbutton_notifications_focus,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (preferences->gsettings_notify,
			 EMPATHY_PREFS_NOTIFICATIONS_CONTACT_SIGNIN,
			 preferences->checkbutton_notifications_contact_signin,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (preferences->gsettings_notify,
			 EMPATHY_PREFS_NOTIFICATIONS_CONTACT_SIGNOUT,
			 preferences->checkbutton_notifications_contact_signout,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);

	g_settings_bind (preferences->gsettings_notify,
			 EMPATHY_PREFS_NOTIFICATIONS_ENABLED,
			 preferences->checkbutton_notifications_disabled_away,
			 "sensitive",
			 G_SETTINGS_BIND_GET);
	g_settings_bind (preferences->gsettings_notify,
			 EMPATHY_PREFS_NOTIFICATIONS_ENABLED,
			 preferences->checkbutton_notifications_focus,
			 "sensitive",
			 G_SETTINGS_BIND_GET);
	g_settings_bind (preferences->gsettings_notify,
			 EMPATHY_PREFS_NOTIFICATIONS_ENABLED,
			 preferences->checkbutton_notifications_contact_signin,
			 "sensitive",
			 G_SETTINGS_BIND_GET);
	g_settings_bind (preferences->gsettings_notify,
			 EMPATHY_PREFS_NOTIFICATIONS_ENABLED,
			 preferences->checkbutton_notifications_contact_signout,
			 "sensitive",
			 G_SETTINGS_BIND_GET);

	g_settings_bind (preferences->gsettings_sound,
			 EMPATHY_PREFS_SOUNDS_ENABLED,
			 preferences->checkbutton_sounds_enabled,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (preferences->gsettings_sound,
			 EMPATHY_PREFS_SOUNDS_DISABLED_AWAY,
			 preferences->checkbutton_sounds_disabled_away,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);

	g_settings_bind (preferences->gsettings_sound,
			 EMPATHY_PREFS_SOUNDS_ENABLED,
			 preferences->checkbutton_sounds_disabled_away,
			 "sensitive",
			 G_SETTINGS_BIND_GET);
	g_settings_bind (preferences->gsettings_sound,
			EMPATHY_PREFS_SOUNDS_ENABLED,
			preferences->treeview_sounds,
			"sensitive",
			G_SETTINGS_BIND_GET);

	g_settings_bind (preferences->gsettings_ui,
			 EMPATHY_PREFS_UI_SEPARATE_CHAT_WINDOWS,
			 preferences->checkbutton_separate_chat_windows,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);

	g_settings_bind (preferences->gsettings_chat,
			 EMPATHY_PREFS_CHAT_SHOW_SMILEYS,
			 preferences->checkbutton_show_smileys,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (preferences->gsettings_chat,
			 EMPATHY_PREFS_CHAT_SHOW_CONTACTS_IN_ROOMS,
			 preferences->checkbutton_show_contacts_in_rooms,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);

	g_settings_bind (preferences->gsettings,
			 EMPATHY_PREFS_AUTOCONNECT,
			 preferences->checkbutton_autoconnect,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);

	g_settings_bind (preferences->gsettings_loc,
			 EMPATHY_PREFS_LOCATION_PUBLISH,
			 preferences->checkbutton_location_publish,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);

	g_settings_bind (preferences->gsettings_loc,
			 EMPATHY_PREFS_LOCATION_RESOURCE_NETWORK,
			 preferences->checkbutton_location_resource_network,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (preferences->gsettings_loc,
			 EMPATHY_PREFS_LOCATION_PUBLISH,
			 preferences->checkbutton_location_resource_network,
			 "sensitive",
			 G_SETTINGS_BIND_GET);

	g_settings_bind (preferences->gsettings_loc,
			 EMPATHY_PREFS_LOCATION_RESOURCE_CELL,
			 preferences->checkbutton_location_resource_cell,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (preferences->gsettings_loc,
			 EMPATHY_PREFS_LOCATION_PUBLISH,
			 preferences->checkbutton_location_resource_cell,
			 "sensitive",
			 G_SETTINGS_BIND_GET);

	g_settings_bind (preferences->gsettings_loc,
			 EMPATHY_PREFS_LOCATION_RESOURCE_GPS,
			 preferences->checkbutton_location_resource_gps,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (preferences->gsettings_loc,
			 EMPATHY_PREFS_LOCATION_PUBLISH,
			 preferences->checkbutton_location_resource_gps,
			 "sensitive",
			 G_SETTINGS_BIND_GET);

	g_settings_bind (preferences->gsettings_loc,
			 EMPATHY_PREFS_LOCATION_REDUCE_ACCURACY,
			 preferences->checkbutton_location_reduce_accuracy,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (preferences->gsettings_loc,
			 EMPATHY_PREFS_LOCATION_PUBLISH,
			 preferences->checkbutton_location_reduce_accuracy,
			 "sensitive",
			 G_SETTINGS_BIND_GET);
}

static void
preferences_sound_cell_toggled_cb (GtkCellRendererToggle *toggle,
				   char *path_string,
				   EmpathyPreferences *preferences)
{
	GtkTreePath *path;
	gboolean toggled, instore;
	GtkTreeIter iter;
	GtkTreeView *view;
	GtkTreeModel *model;
	char *key;

	view = GTK_TREE_VIEW (preferences->treeview_sounds);
	model = gtk_tree_view_get_model (view);

	path = gtk_tree_path_new_from_string (path_string);
	toggled = gtk_cell_renderer_toggle_get_active (toggle);

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, COL_SOUND_KEY, &key,
			    COL_SOUND_ENABLED, &instore, -1);

	instore ^= 1;

	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    COL_SOUND_ENABLED, instore, -1);

	g_settings_set_boolean (preferences->gsettings_sound, key, instore);

	g_free (key);
	gtk_tree_path_free (path);
}

static void
preferences_sound_load (EmpathyPreferences *preferences)
{
	guint i;
	GtkTreeView *view;
	GtkListStore *store;
	GtkTreeIter iter;
	gboolean set;

	view = GTK_TREE_VIEW (preferences->treeview_sounds);
	store = GTK_LIST_STORE (gtk_tree_view_get_model (view));

	for (i = 0; i < G_N_ELEMENTS (sound_entries); i++) {
		set = g_settings_get_boolean (preferences->gsettings_sound,
					      sound_entries[i].key);

		gtk_list_store_insert_with_values (store, &iter, i,
						   COL_SOUND_NAME, gettext (sound_entries[i].name),
						   COL_SOUND_KEY, sound_entries[i].key,
						   COL_SOUND_ENABLED, set, -1);
	}
}

static void
preferences_sound_setup (EmpathyPreferences *preferences)
{
	GtkTreeView *view;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	view = GTK_TREE_VIEW (preferences->treeview_sounds);

	store = gtk_list_store_new (COL_SOUND_COUNT,
				    G_TYPE_BOOLEAN, /* enabled */
				    G_TYPE_STRING,  /* name */
				    G_TYPE_STRING); /* key */

	gtk_tree_view_set_model (view, GTK_TREE_MODEL (store));

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled",
			  G_CALLBACK (preferences_sound_cell_toggled_cb),
			  preferences);

	column = gtk_tree_view_column_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer,
					    "active", COL_SOUND_ENABLED);

	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer,
					    "text", COL_SOUND_NAME);

	gtk_tree_view_append_column (view, column);

	gtk_tree_view_column_set_resizable (column, FALSE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

	g_object_unref (store);
}

static void
preferences_languages_setup (EmpathyPreferences *preferences)
{
	GtkTreeView       *view;
	GtkListStore      *store;
	GtkTreeSelection  *selection;
	GtkTreeModel      *model;
	GtkTreeViewColumn *column;
	GtkCellRenderer   *renderer;
	guint              col_offset;

	view = GTK_TREE_VIEW (preferences->treeview_spell_checker);

	store = gtk_list_store_new (COL_LANG_COUNT,
				    G_TYPE_BOOLEAN,  /* enabled */
				    G_TYPE_STRING,   /* code */
				    G_TYPE_STRING);  /* name */

	gtk_tree_view_set_model (view, GTK_TREE_MODEL (store));

	selection = gtk_tree_view_get_selection (view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

	model = GTK_TREE_MODEL (store);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled",
			  G_CALLBACK (preferences_languages_cell_toggled_cb),
			  preferences);

	column = gtk_tree_view_column_new_with_attributes (NULL, renderer,
							   "active", COL_LANG_ENABLED,
							   NULL);

	gtk_tree_view_append_column (view, column);

	renderer = gtk_cell_renderer_text_new ();
	col_offset = gtk_tree_view_insert_column_with_attributes (view,
								  -1, _("Language"),
								  renderer,
								  "text", COL_LANG_NAME,
								  NULL);

	g_object_set_data (G_OBJECT (renderer),
			   "column", GINT_TO_POINTER (COL_LANG_NAME));

	column = gtk_tree_view_get_column (view, col_offset - 1);
	gtk_tree_view_column_set_sort_column_id (column, COL_LANG_NAME);
	gtk_tree_view_column_set_resizable (column, FALSE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

	g_object_unref (store);
}

static void
preferences_languages_add (EmpathyPreferences *preferences)
{
	GtkTreeView  *view;
	GtkListStore *store;
	GList        *codes, *l;

	view = GTK_TREE_VIEW (preferences->treeview_spell_checker);
	store = GTK_LIST_STORE (gtk_tree_view_get_model (view));

	codes = empathy_spell_get_language_codes ();

	g_settings_set_boolean (preferences->gsettings_chat,
				EMPATHY_PREFS_CHAT_SPELL_CHECKER_ENABLED,
				codes != NULL);
	if (!codes) {
		gtk_widget_set_sensitive (preferences->treeview_spell_checker, FALSE);
	}

	for (l = codes; l; l = l->next) {
		GtkTreeIter  iter;
		const gchar *code;
		const gchar *name;

		code = l->data;
		name = empathy_spell_get_language_name (code);
		if (!name) {
			continue;
		}

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    COL_LANG_CODE, code,
				    COL_LANG_NAME, name,
				    -1);
	}

	empathy_spell_free_language_codes (codes);
}

static void
preferences_languages_save (EmpathyPreferences *preferences)
{
	GtkTreeView       *view;
	GtkTreeModel      *model;

	gchar             *languages = NULL;

	view = GTK_TREE_VIEW (preferences->treeview_spell_checker);
	model = gtk_tree_view_get_model (view);

	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc) preferences_languages_save_foreach,
				&languages);

	/* if user selects no languages, we don't want spell check */
	g_settings_set_boolean (preferences->gsettings_chat,
				EMPATHY_PREFS_CHAT_SPELL_CHECKER_ENABLED,
				languages != NULL);

	g_settings_set_string (preferences->gsettings_chat,
			       EMPATHY_PREFS_CHAT_SPELL_CHECKER_LANGUAGES,
			       languages != NULL ? languages : "");

	g_free (languages);
}

static gboolean
preferences_languages_save_foreach (GtkTreeModel  *model,
				    GtkTreePath   *path,
				    GtkTreeIter   *iter,
				    gchar        **languages)
{
	gboolean  enabled;
	gchar    *code;

	if (!languages) {
		return TRUE;
	}

	gtk_tree_model_get (model, iter, COL_LANG_ENABLED, &enabled, -1);
	if (!enabled) {
		return FALSE;
	}

	gtk_tree_model_get (model, iter, COL_LANG_CODE, &code, -1);
	if (!code) {
		return FALSE;
	}

	if (!(*languages)) {
		*languages = g_strdup (code);
	} else {
		gchar *str = *languages;
		*languages = g_strdup_printf ("%s,%s", str, code);
		g_free (str);
	}

	g_free (code);

	return FALSE;
}

static void
preferences_languages_load (EmpathyPreferences *preferences)
{
	GtkTreeView   *view;
	GtkTreeModel  *model;
	gchar         *value;
	gchar        **vlanguages;

	value = g_settings_get_string (preferences->gsettings_chat,
				       EMPATHY_PREFS_CHAT_SPELL_CHECKER_LANGUAGES);

	if (value == NULL)
		return;

	vlanguages = g_strsplit (value, ",", -1);
	g_free (value);

	view = GTK_TREE_VIEW (preferences->treeview_spell_checker);
	model = gtk_tree_view_get_model (view);

	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc) preferences_languages_load_foreach,
				vlanguages);

	g_strfreev (vlanguages);
}

static gboolean
preferences_languages_load_foreach (GtkTreeModel  *model,
				    GtkTreePath   *path,
				    GtkTreeIter   *iter,
				    gchar        **languages)
{
	gchar    *code;
	gchar    *lang;
	gint      i;
	gboolean  found = FALSE;

	if (!languages) {
		return TRUE;
	}

	gtk_tree_model_get (model, iter, COL_LANG_CODE, &code, -1);
	if (!code) {
		return FALSE;
	}

	for (i = 0, lang = languages[i]; lang; lang = languages[++i]) {
		if (!tp_strdiff (lang, code)) {
			found = TRUE;
		}
	}

	g_free (code);
	gtk_list_store_set (GTK_LIST_STORE (model), iter, COL_LANG_ENABLED, found, -1);
	return FALSE;
}

static void
preferences_languages_cell_toggled_cb (GtkCellRendererToggle *cell,
				       gchar                 *path_string,
				       EmpathyPreferences     *preferences)
{
	GtkTreeView  *view;
	GtkTreeModel *model;
	GtkListStore *store;
	GtkTreePath  *path;
	GtkTreeIter   iter;
	gboolean      enabled;

	view = GTK_TREE_VIEW (preferences->treeview_spell_checker);
	model = gtk_tree_view_get_model (view);
	store = GTK_LIST_STORE (model);

	path = gtk_tree_path_new_from_string (path_string);

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, COL_LANG_ENABLED, &enabled, -1);

	enabled ^= 1;

	gtk_list_store_set (store, &iter, COL_LANG_ENABLED, enabled, -1);
	gtk_tree_path_free (path);

	preferences_languages_save (preferences);
}

static void
preferences_theme_notify_cb (GSettings   *gsettings,
			     const gchar *key,
			     gpointer     user_data)
{
	EmpathyPreferences *preferences = user_data;
	GtkComboBox        *combo;
	gchar              *conf_name;
	gchar              *conf_path;
	GtkTreeModel       *model;
	GtkTreeIter         iter;
	gboolean            found = FALSE;

	conf_name = g_settings_get_string (gsettings, EMPATHY_PREFS_CHAT_THEME);
	conf_path = g_settings_get_string (gsettings, EMPATHY_PREFS_CHAT_ADIUM_PATH);

	combo = GTK_COMBO_BOX (preferences->combobox_chat_theme);
	model = gtk_combo_box_get_model (combo);
	if (gtk_tree_model_get_iter_first (model, &iter)) {
		gboolean is_adium;
		gchar *name;
		gchar *path;

		do {
			gtk_tree_model_get (model, &iter,
					    COL_COMBO_IS_ADIUM, &is_adium,
					    COL_COMBO_NAME, &name,
					    COL_COMBO_PATH, &path,
					    -1);

			if (!tp_strdiff (name, conf_name)) {
				if (tp_strdiff (name, "adium") ||
				    !tp_strdiff (path, conf_path)) {
					found = TRUE;
					gtk_combo_box_set_active_iter (combo, &iter);
					g_free (name);
					g_free (path);
					break;
				}
			}

			g_free (name);
			g_free (path);
		} while (gtk_tree_model_iter_next (model, &iter));
	}

	/* Fallback to the first one. */
	if (!found) {
		if (gtk_tree_model_get_iter_first (model, &iter)) {
			gtk_combo_box_set_active_iter (combo, &iter);
		}
	}

	g_free (conf_name);
	g_free (conf_path);
}

static void
preferences_theme_changed_cb (GtkComboBox        *combo,
			      EmpathyPreferences *preferences)
{
	GtkTreeModel *model;
	GtkTreeIter   iter;
	gboolean      is_adium;
	gchar        *name;
	gchar        *path;

	if (gtk_combo_box_get_active_iter (combo, &iter)) {
		model = gtk_combo_box_get_model (combo);

		gtk_tree_model_get (model, &iter,
				    COL_COMBO_IS_ADIUM, &is_adium,
				    COL_COMBO_NAME, &name,
				    COL_COMBO_PATH, &path,
				    -1);

		g_settings_set_string (preferences->gsettings_chat,
				       EMPATHY_PREFS_CHAT_THEME,
				       name);
		if (is_adium == TRUE)
			g_settings_set_string (preferences->gsettings_chat,
					       EMPATHY_PREFS_CHAT_ADIUM_PATH,
					       path);
		g_free (name);
		g_free (path);
	}
}

static void
preferences_themes_setup (EmpathyPreferences *preferences)
{
	GtkComboBox   *combo;
	GtkCellLayout *cell_layout;
	GtkCellRenderer *renderer;
	GtkListStore  *store;
	const gchar  **themes;
	GList         *adium_themes;
	gint           i;

	combo = GTK_COMBO_BOX (preferences->combobox_chat_theme);
	cell_layout = GTK_CELL_LAYOUT (combo);

	/* Create the model */
	store = gtk_list_store_new (COL_COMBO_COUNT,
				    G_TYPE_BOOLEAN, /* Is an Adium theme */
				    G_TYPE_STRING,  /* Display name */
				    G_TYPE_STRING,  /* Theme name */
				    G_TYPE_STRING); /* Theme path */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
		COL_COMBO_VISIBLE_NAME, GTK_SORT_ASCENDING);

	/* Fill the model */
	themes = empathy_theme_manager_get_themes ();
	for (i = 0; themes[i]; i += 2) {
		gtk_list_store_insert_with_values (store, NULL, -1,
			COL_COMBO_IS_ADIUM, FALSE,
			COL_COMBO_VISIBLE_NAME, _(themes[i + 1]),
			COL_COMBO_NAME, themes[i],
			COL_COMBO_PATH, NULL,
			-1);
	}

	adium_themes = empathy_theme_manager_get_adium_themes ();
	while (adium_themes != NULL) {
		GHashTable *info;
		const gchar *name;
		const gchar *path;

		info = adium_themes->data;
		name = tp_asv_get_string (info, "CFBundleName");
		path = tp_asv_get_string (info, "path");

		if (name != NULL && path != NULL) {
			gtk_list_store_insert_with_values (store, NULL, -1,
				COL_COMBO_IS_ADIUM, TRUE,
				COL_COMBO_VISIBLE_NAME, name,
				COL_COMBO_NAME, "adium",
				COL_COMBO_PATH, path,
				-1);
		}
		g_hash_table_unref (info);
		adium_themes = g_list_delete_link (adium_themes, adium_themes);
	}

	/* Add cell renderer */
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (cell_layout, renderer, TRUE);
	gtk_cell_layout_set_attributes (cell_layout, renderer,
		"text", COL_COMBO_VISIBLE_NAME, NULL);

	gtk_combo_box_set_model (combo, GTK_TREE_MODEL (store));
	g_object_unref (store);

	g_signal_connect (combo, "changed",
			  G_CALLBACK (preferences_theme_changed_cb),
			  preferences);

	/* Select the theme from the GSetting key and track changes */
	preferences_theme_notify_cb (preferences->gsettings_chat,
				     EMPATHY_PREFS_CHAT_THEME,
				     preferences);
	g_signal_connect (preferences->gsettings_chat,
			  "changed::" EMPATHY_PREFS_CHAT_THEME,
			  G_CALLBACK (preferences_theme_notify_cb),
			  preferences);

	g_signal_connect (preferences->gsettings_chat,
			  "changed::" EMPATHY_PREFS_CHAT_ADIUM_PATH,
			  G_CALLBACK (preferences_theme_notify_cb),
			  preferences);
}

static void
preferences_response_cb (GtkWidget         *widget,
			 gint               response,
			 EmpathyPreferences *preferences)
{
	gtk_widget_destroy (widget);
}

static void
preferences_destroy_cb (GtkWidget         *widget,
			EmpathyPreferences *preferences)
{
	g_object_unref (preferences->gsettings);
	g_object_unref (preferences->gsettings_chat);
	g_object_unref (preferences->gsettings_loc);
	g_object_unref (preferences->gsettings_notify);
	g_object_unref (preferences->gsettings_sound);
	g_object_unref (preferences->gsettings_ui);

	g_free (preferences);
}

GtkWidget *
empathy_preferences_show (GtkWindow *parent)
{
	static EmpathyPreferences *preferences;
	GtkBuilder                *gui;
	gchar                     *filename;
	GtkWidget                 *page;

	if (preferences) {
		gtk_window_present (GTK_WINDOW (preferences->dialog));
		return preferences->dialog;
	}

	preferences = g_new0 (EmpathyPreferences, 1);

	filename = empathy_file_lookup ("empathy-preferences.ui", "src");
	gui = empathy_builder_get_file (filename,
		"preferences_dialog", &preferences->dialog,
		"notebook", &preferences->notebook,
		"checkbutton_show_smileys", &preferences->checkbutton_show_smileys,
		"checkbutton_show_contacts_in_rooms", &preferences->checkbutton_show_contacts_in_rooms,
		"combobox_chat_theme", &preferences->combobox_chat_theme,
		"checkbutton_separate_chat_windows", &preferences->checkbutton_separate_chat_windows,
		"checkbutton_autoconnect", &preferences->checkbutton_autoconnect,
		"checkbutton_notifications_enabled", &preferences->checkbutton_notifications_enabled,
		"checkbutton_notifications_disabled_away", &preferences->checkbutton_notifications_disabled_away,
		"checkbutton_notifications_focus", &preferences->checkbutton_notifications_focus,
		"checkbutton_notifications_contact_signin", &preferences->checkbutton_notifications_contact_signin,
		"checkbutton_notifications_contact_signout", &preferences->checkbutton_notifications_contact_signout,
		"checkbutton_sounds_enabled", &preferences->checkbutton_sounds_enabled,
		"checkbutton_sounds_disabled_away", &preferences->checkbutton_sounds_disabled_away,
		"treeview_sounds", &preferences->treeview_sounds,
		"treeview_spell_checker", &preferences->treeview_spell_checker,
		"checkbutton_location_publish", &preferences->checkbutton_location_publish,
		"checkbutton_location_reduce_accuracy", &preferences->checkbutton_location_reduce_accuracy,
		"checkbutton_location_resource_network", &preferences->checkbutton_location_resource_network,
		"checkbutton_location_resource_cell", &preferences->checkbutton_location_resource_cell,
		"checkbutton_location_resource_gps", &preferences->checkbutton_location_resource_gps,
		NULL);
	g_free (filename);

	empathy_builder_connect (gui, preferences,
			      "preferences_dialog", "destroy", preferences_destroy_cb,
			      "preferences_dialog", "response", preferences_response_cb,
			      NULL);

	g_object_unref (gui);

	g_object_add_weak_pointer (G_OBJECT (preferences->dialog), (gpointer) &preferences);

	preferences->gsettings = g_settings_new (EMPATHY_PREFS_SCHEMA);
	preferences->gsettings_chat = g_settings_new (EMPATHY_PREFS_CHAT_SCHEMA);
	preferences->gsettings_loc = g_settings_new (EMPATHY_PREFS_LOCATION_SCHEMA);
	preferences->gsettings_notify = g_settings_new (EMPATHY_PREFS_NOTIFICATIONS_SCHEMA);
	preferences->gsettings_sound = g_settings_new (EMPATHY_PREFS_SOUNDS_SCHEMA);
	preferences->gsettings_ui = g_settings_new (EMPATHY_PREFS_UI_SCHEMA);

	preferences_themes_setup (preferences);

	preferences_setup_widgets (preferences);

	preferences_languages_setup (preferences);
	preferences_languages_add (preferences);
	preferences_languages_load (preferences);

	preferences_sound_setup (preferences);
	preferences_sound_load (preferences);

	if (empathy_spell_supported ()) {
		page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (preferences->notebook), 2);
		gtk_widget_show (page);
	}

	page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (preferences->notebook), 3);
#if HAVE_GEOCLUE
	gtk_widget_show (page);
#else
	gtk_widget_hide (page);
#endif


	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (preferences->dialog),
					      GTK_WINDOW (parent));
	}

	gtk_widget_show (preferences->dialog);

	return preferences->dialog;
}

